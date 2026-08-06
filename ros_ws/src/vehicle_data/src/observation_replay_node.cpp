#include <rclcpp/rclcpp.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <rosbag2_cpp/reader.hpp>
#include <vehicle_interfaces/msg/policy_observation.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace
{

struct ReplayRecord
{
  int64_t bag_timestamp_ns{0};
  vehicle_interfaces::msg::PolicyObservation observation;
};

template<typename MessageT>
MessageT deserialize_message(
  const std::shared_ptr<rosbag2_storage::SerializedBagMessage> & bag_message)
{
  rclcpp::SerializedMessage serialized_message(*bag_message->serialized_data);
  rclcpp::Serialization<MessageT> serialization;
  MessageT message;
  serialization.deserialize_message(&serialized_message, &message);
  return message;
}

}  // namespace

class ObservationReplay final : public rclcpp::Node
{
public:
  ObservationReplay()
  : Node("observation_replay")
  {
    bag_path_ = declare_parameter<std::string>("bag_path", "");
    output_topic_ = declare_parameter<std::string>(
      "output_topic", "/vla/replay/observation");
    playback_rate_ = std::max(declare_parameter<double>("playback_rate", 1.0), 0.01);
    validity_seconds_ = std::max(declare_parameter<double>("validity_seconds", 5.0), 0.1);
    start_delay_seconds_ = std::max(declare_parameter<double>("start_delay_seconds", 1.0), 0.0);
    max_messages_ = std::max<int64_t>(
      declare_parameter<int64_t>("max_messages", 0), 0);
    allow_live_topic_ = declare_parameter<bool>("allow_live_topic", false);

    if (bag_path_.empty()) {
      throw std::invalid_argument("bag_path parameter is required");
    }
    if (!allow_live_topic_ && output_topic_.rfind("/vla/replay/", 0) != 0) {
      throw std::invalid_argument(
              "output_topic must stay under /vla/replay/ unless allow_live_topic=true");
    }

    load_observations();
    publisher_ = create_publisher<vehicle_interfaces::msg::PolicyObservation>(
      output_topic_, rclcpp::QoS(2).reliable());
    start_timer_ = create_wall_timer(100ms, std::bind(&ObservationReplay::start, this));
  }

  ~ObservationReplay() override
  {
    stop_requested_.store(true);
    if (worker_.joinable()) {
      worker_.join();
    }
  }

private:
  void load_observations()
  {
    rosbag2_cpp::Reader reader;
    reader.open(bag_path_);
    while (reader.has_next()) {
      const auto bag_message = reader.read_next();
      if (bag_message->topic_name != "/vla/observation") {
        continue;
      }
      records_.push_back({
        bag_message->time_stamp,
        deserialize_message<vehicle_interfaces::msg::PolicyObservation>(bag_message)});
      if (max_messages_ > 0 && static_cast<int64_t>(records_.size()) >= max_messages_) {
        break;
      }
    }
    if (records_.empty()) {
      throw std::runtime_error("bag contains no /vla/observation messages");
    }
  }

  void start()
  {
    start_timer_->cancel();
    worker_ = std::thread(&ObservationReplay::replay, this);
  }

  bool wait_until(const std::chrono::steady_clock::time_point target_time)
  {
    while (!stop_requested_.load() && rclcpp::ok()) {
      const auto current_time = std::chrono::steady_clock::now();
      if (current_time >= target_time) {
        return true;
      }
      const auto remaining = target_time - current_time;
      const auto maximum_sleep = std::chrono::duration_cast<std::chrono::steady_clock::duration>(50ms);
      std::this_thread::sleep_for(std::min(remaining, maximum_sleep));
    }
    return false;
  }

  void replay()
  {
    const auto playback_started = std::chrono::steady_clock::now() +
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(
      std::chrono::duration<double>(start_delay_seconds_));
    if (!wait_until(playback_started)) {
      return;
    }
    const int64_t first_timestamp = records_.front().bag_timestamp_ns;
    std::size_t published_count = 0;

    for (const auto & record : records_) {
      if (stop_requested_.load() || !rclcpp::ok()) {
        break;
      }
      const auto relative_nanoseconds = static_cast<int64_t>(
        static_cast<double>(record.bag_timestamp_ns - first_timestamp) / playback_rate_);
      if (!wait_until(playback_started + std::chrono::nanoseconds(relative_nanoseconds))) {
        break;
      }

      auto message = record.observation;
      const auto current_time = now();
      message.header.stamp = current_time;
      for (auto & image : message.images) {
        image.header.stamp = current_time;
      }
      message.generated_at = current_time;
      message.valid_until = current_time + rclcpp::Duration::from_seconds(validity_seconds_);
      message.observation_id = "replay-" + std::to_string(run_id_) + "-" +
        record.observation.observation_id;
      publisher_->publish(message);
      ++published_count;
    }

    RCLCPP_INFO(
      get_logger(), "Replay completed: published=%zu topic=%s rate=%.3f",
      published_count, output_topic_.c_str(), playback_rate_);
    rclcpp::shutdown();
  }

  std::string bag_path_;
  std::string output_topic_;
  double playback_rate_{1.0};
  double validity_seconds_{5.0};
  double start_delay_seconds_{1.0};
  int64_t max_messages_{0};
  bool allow_live_topic_{false};
  const int64_t run_id_{
    std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count()};
  std::vector<ReplayRecord> records_;
  std::atomic<bool> stop_requested_{false};
  std::thread worker_;
  rclcpp::Publisher<vehicle_interfaces::msg::PolicyObservation>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr start_timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  try {
    rclcpp::spin(std::make_shared<ObservationReplay>());
  } catch (const std::exception & error) {
    RCLCPP_ERROR(rclcpp::get_logger("observation_replay"), "%s", error.what());
    rclcpp::shutdown();
    return 1;
  }
  return 0;
}
