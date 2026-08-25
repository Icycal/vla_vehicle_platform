#include <geometry_msgs/msg/twist.hpp>
#include <geometry_msgs/msg/twist_stamped.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rosbag2_cpp/writer.hpp>
#include <sensor_msgs/msg/camera_info.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <std_msgs/msg/string.hpp>
#include <vehicle_interfaces/msg/episode_state.hpp>
#include <vehicle_interfaces/msg/observation_status.hpp>
#include <vehicle_interfaces/msg/policy_action.hpp>
#include <vehicle_interfaces/msg/policy_observation.hpp>
#include <vehicle_interfaces/msg/safety_event.hpp>
#include <vehicle_interfaces/msg/shadow_comparison.hpp>
#include <vehicle_interfaces/msg/shadow_metrics.hpp>
#include <vehicle_interfaces/msg/system_state.hpp>
#include <vehicle_interfaces/srv/start_episode.hpp>
#include <vehicle_interfaces/srv/stop_episode.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace std::chrono_literals;

class EpisodeRecorder final : public rclcpp::Node
{
public:
  EpisodeRecorder()
  : Node("episode_recorder")
  {
    storage_root_ = declare_parameter<std::string>("storage_root", "");
    if (storage_root_.empty()) {
      throw std::invalid_argument("storage_root must be configured");
    }
    minimum_free_space_gb_ = declare_parameter<double>("minimum_free_space_gb", 10.0);
    max_episode_duration_ = declare_parameter<double>("max_episode_duration", 900.0);
    image_record_frequency_ = declare_parameter<double>("image_record_frequency", 5.0);
    require_observation_ready_ = declare_parameter<bool>("require_observation_ready", true);

    state_publisher_ = create_publisher<vehicle_interfaces::msg::EpisodeState>(
      "/vehicle/episode_state", rclcpp::QoS(1).reliable().transient_local());
    start_service_ = create_service<vehicle_interfaces::srv::StartEpisode>(
      "/vehicle/start_episode",
      std::bind(
        &EpisodeRecorder::on_start, this, std::placeholders::_1, std::placeholders::_2));
    stop_service_ = create_service<vehicle_interfaces::srv::StopEpisode>(
      "/vehicle/stop_episode",
      std::bind(
        &EpisodeRecorder::on_stop, this, std::placeholders::_1, std::placeholders::_2));

    observation_subscription_ = create_subscription<vehicle_interfaces::msg::ObservationStatus>(
      "/vehicle/observation_status", rclcpp::QoS(1).reliable().transient_local(),
      [this](vehicle_interfaces::msg::ObservationStatus::SharedPtr message) {
        observation_ready_ = message->ready;
      });
    image_subscription_ = create_subscription<sensor_msgs::msg::CompressedImage>(
      "/camera/image_compressed", rclcpp::SensorDataQoS(),
      std::bind(&EpisodeRecorder::on_image, this, std::placeholders::_1));
    camera_info_subscription_ = create_subscription<sensor_msgs::msg::CameraInfo>(
      "/camera/camera_info", rclcpp::SensorDataQoS(),
      [this](sensor_msgs::msg::CameraInfo::SharedPtr message) {
        record_header_message(*message, "/camera/camera_info");
      });
    odometry_subscription_ = create_subscription<nav_msgs::msg::Odometry>(
      "/odom", rclcpp::SensorDataQoS(),
      [this](nav_msgs::msg::Odometry::SharedPtr message) {
        record_header_message(*message, "/odom");
      });
    imu_subscription_ = create_subscription<sensor_msgs::msg::Imu>(
      "/imu/data_raw", rclcpp::SensorDataQoS(),
      [this](sensor_msgs::msg::Imu::SharedPtr message) {
        record_header_message(*message, "/imu/data_raw");
      });
    system_state_subscription_ = create_subscription<vehicle_interfaces::msg::SystemState>(
      "/vehicle/system_state", rclcpp::QoS(1).reliable().transient_local(),
      [this](vehicle_interfaces::msg::SystemState::SharedPtr message) {
        record_header_message(*message, "/vehicle/system_state");
      });
    observation_state_subscription_ =
      create_subscription<vehicle_interfaces::msg::ObservationStatus>(
      "/vehicle/observation_status", rclcpp::QoS(1).reliable().transient_local(),
      [this](vehicle_interfaces::msg::ObservationStatus::SharedPtr message) {
        record_header_message(*message, "/vehicle/observation_status");
      });
    policy_action_subscription_ = create_subscription<vehicle_interfaces::msg::PolicyAction>(
      "/vla/policy_action", 10,
      [this](vehicle_interfaces::msg::PolicyAction::SharedPtr message) {
        record_header_message(*message, "/vla/policy_action");
      });
    policy_observation_subscription_ =
      create_subscription<vehicle_interfaces::msg::PolicyObservation>(
      "/vla/observation", rclcpp::QoS(2).reliable(),
      [this](vehicle_interfaces::msg::PolicyObservation::SharedPtr message) {
        record_header_message(*message, "/vla/observation");
      });
    shadow_comparison_subscription_ =
      create_subscription<vehicle_interfaces::msg::ShadowComparison>(
      "/vla/shadow_comparison", 10,
      [this](vehicle_interfaces::msg::ShadowComparison::SharedPtr message) {
        record_header_message(*message, "/vla/shadow_comparison");
      });
    shadow_metrics_subscription_ = create_subscription<vehicle_interfaces::msg::ShadowMetrics>(
      "/vla/shadow_metrics", rclcpp::QoS(1).reliable().transient_local(),
      [this](vehicle_interfaces::msg::ShadowMetrics::SharedPtr message) {
        record_header_message(*message, "/vla/shadow_metrics");
      });
    safety_event_subscription_ = create_subscription<vehicle_interfaces::msg::SafetyEvent>(
      "/vla/safety_event", 10,
      [this](vehicle_interfaces::msg::SafetyEvent::SharedPtr message) {
        record_header_message(*message, "/vla/safety_event");
      });
    vla_command_subscription_ = create_subscription<geometry_msgs::msg::TwistStamped>(
      "/vla/cmd_vel_raw", 10,
      [this](geometry_msgs::msg::TwistStamped::SharedPtr message) {
        record_header_message(*message, "/vla/cmd_vel_raw");
      });
    selected_command_subscription_ = create_subscription<geometry_msgs::msg::TwistStamped>(
      "/control/cmd_vel_selected", 10,
      [this](geometry_msgs::msg::TwistStamped::SharedPtr message) {
        record_header_message(*message, "/control/cmd_vel_selected");
      });
    final_command_subscription_ = create_subscription<geometry_msgs::msg::Twist>(
      "/cmd_vel", 10,
      [this](geometry_msgs::msg::Twist::SharedPtr message) {
        record_message(*message, "/cmd_vel", now());
      });
    task_subscription_ = create_subscription<std_msgs::msg::String>(
      "/vla/task", rclcpp::QoS(1).reliable().transient_local(),
      [this](std_msgs::msg::String::SharedPtr message) {
        record_message(*message, "/vla/task", now());
      });

    timer_ = create_wall_timer(500ms, std::bind(&EpisodeRecorder::on_timer, this));
    publish_state("Episode recorder ready");
  }

  ~EpisodeRecorder() override
  {
    if (recording_) {
      stop_recording(false, "Recorder shutdown");
    }
  }

private:
  template<typename MessageT>
  void record_message(
    const MessageT & message, const std::string & topic, const rclcpp::Time & stamp)
  {
    if (!recording_ || !writer_) {
      return;
    }
    try {
      writer_->write(message, topic, stamp);
      ++message_count_;
    } catch (const std::exception & error) {
      fail_recording(std::string("Failed to write ") + topic + ": " + error.what());
    }
  }

  template<typename MessageT>
  void record_header_message(const MessageT & message, const std::string & topic)
  {
    const rclcpp::Time header_stamp(message.header.stamp);
    record_message(message, topic, header_stamp.nanoseconds() > 0 ? header_stamp : now());
  }

  void on_image(const sensor_msgs::msg::CompressedImage::SharedPtr message)
  {
    if (!recording_) {
      return;
    }
    const double minimum_period = 1.0 / std::max(image_record_frequency_, 0.1);
    const auto current_time = now();
    if (last_image_recorded_.nanoseconds() > 0 &&
      (current_time - last_image_recorded_).seconds() < minimum_period)
    {
      return;
    }
    const rclcpp::Time header_stamp(message->header.stamp);
    record_message(
      *message, "/episode/camera/image_compressed",
      header_stamp.nanoseconds() > 0 ? header_stamp : current_time);
    last_image_recorded_ = current_time;
    ++image_count_;
  }

  void on_start(
    const std::shared_ptr<vehicle_interfaces::srv::StartEpisode::Request> request,
    std::shared_ptr<vehicle_interfaces::srv::StartEpisode::Response> response)
  {
    response->accepted = false;
    if (recording_) {
      response->message = "An episode is already recording";
      return;
    }
    if (require_observation_ready_ && !observation_ready_) {
      response->message = "Observation inputs are not ready";
      return;
    }

    try {
      std::filesystem::create_directories(storage_root_);
      const auto space = std::filesystem::space(storage_root_);
      const double free_gb = static_cast<double>(space.available) / 1000000000.0;
      if (free_gb < minimum_free_space_gb_) {
        std::ostringstream stream;
        stream << "Insufficient free space: " << std::fixed << std::setprecision(1) << free_gb
               << " GB available";
        response->message = stream.str();
        return;
      }

      episode_id_ = unique_episode_id(request->episode_id);
      task_ = request->task;
      operator_id_ = request->operator_id;
      directory_ = (std::filesystem::path(storage_root_) / episode_id_).string();
      writer_ = std::make_unique<rosbag2_cpp::Writer>();
      writer_->open(directory_);
      recording_ = true;
      state_ = vehicle_interfaces::msg::EpisodeState::STATE_RECORDING;
      message_count_ = 0;
      image_count_ = 0;
      start_time_ = now();
      last_image_recorded_ = rclcpp::Time(0, 0, RCL_ROS_TIME);
      last_message_ = "Recording";
      write_manifest(false, false, "Recording");
      publish_state(last_message_);

      response->accepted = true;
      response->resolved_episode_id = episode_id_;
      response->directory = directory_;
      response->message = "Episode recording started";
    } catch (const std::exception & error) {
      writer_.reset();
      recording_ = false;
      state_ = vehicle_interfaces::msg::EpisodeState::STATE_ERROR;
      last_message_ = error.what();
      response->message = std::string("Failed to start episode: ") + error.what();
      publish_state(last_message_);
    }
  }

  void on_stop(
    const std::shared_ptr<vehicle_interfaces::srv::StopEpisode::Request> request,
    std::shared_ptr<vehicle_interfaces::srv::StopEpisode::Response> response)
  {
    response->accepted = false;
    if (!recording_) {
      response->message = "No episode is recording";
      return;
    }
    const std::string completed_directory = directory_;
    const uint64_t completed_messages = message_count_;
    stop_recording(request->success, request->reason);
    response->accepted = true;
    response->directory = completed_directory;
    response->message_count = completed_messages;
    response->message = "Episode recording stopped";
  }

  void on_timer()
  {
    if (recording_ && max_episode_duration_ > 0.0 &&
      (now() - start_time_).seconds() >= max_episode_duration_)
    {
      stop_recording(false, "Maximum episode duration reached");
      return;
    }
    publish_state(last_message_);
  }

  void stop_recording(const bool success, const std::string & reason)
  {
    if (!recording_) {
      return;
    }
    state_ = vehicle_interfaces::msg::EpisodeState::STATE_FINALIZING;
    last_message_ = reason.empty() ? "Finalizing" : reason;
    publish_state(last_message_);
    try {
      writer_->close();
      writer_.reset();
      recording_ = false;
      write_manifest(true, success, reason);
      state_ = vehicle_interfaces::msg::EpisodeState::STATE_IDLE;
      last_message_ = success ? "Episode completed" : "Episode stopped";
    } catch (const std::exception & error) {
      writer_.reset();
      recording_ = false;
      state_ = vehicle_interfaces::msg::EpisodeState::STATE_ERROR;
      last_message_ = error.what();
    }
    publish_state(last_message_);
  }

  void fail_recording(const std::string & reason)
  {
    RCLCPP_ERROR(get_logger(), "%s", reason.c_str());
    stop_recording(false, reason);
  }

  void publish_state(const std::string & status_message)
  {
    vehicle_interfaces::msg::EpisodeState message;
    message.header.stamp = now();
    message.state = state_;
    message.episode_id = episode_id_;
    message.task = task_;
    message.directory = directory_;
    message.message_count = message_count_;
    message.image_count = image_count_;
    message.elapsed_seconds = recording_ ?
      static_cast<float>((now() - start_time_).seconds()) : 0.0F;
    message.message = status_message;
    state_publisher_->publish(message);
  }

  std::string unique_episode_id(const std::string & requested) const
  {
    std::string base = sanitize_id(requested.empty() ? timestamp_id() : requested);
    if (base.empty()) {
      base = timestamp_id();
    }
    std::string candidate = base;
    int suffix = 1;
    while (std::filesystem::exists(std::filesystem::path(storage_root_) / candidate)) {
      candidate = base + "-" + std::to_string(suffix++);
    }
    return candidate;
  }

  static std::string sanitize_id(const std::string & value)
  {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value) {
      if (std::isalnum(character) || character == '-' || character == '_') {
        result.push_back(static_cast<char>(character));
      } else {
        result.push_back('_');
      }
    }
    return result.substr(0, 96);
  }

  static std::string timestamp_id()
  {
    const auto current = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(current);
    std::tm local_time{};
    localtime_r(&time, &local_time);
    std::ostringstream stream;
    stream << "episode-" << std::put_time(&local_time, "%Y%m%d-%H%M%S");
    return stream.str();
  }

  static std::string json_escape(const std::string & value)
  {
    std::string result;
    for (const char character : value) {
      switch (character) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += character; break;
      }
    }
    return result;
  }

  void write_manifest(const bool finished, const bool success, const std::string & reason) const
  {
    if (directory_.empty() || !std::filesystem::exists(directory_)) {
      return;
    }
    std::ofstream output(std::filesystem::path(directory_) / "episode_manifest.json");
    output << "{\n"
           << "  \"schema_version\": \"vehicle.episode.v1\",\n"
           << "  \"episode_id\": \"" << json_escape(episode_id_) << "\",\n"
           << "  \"task\": \"" << json_escape(task_) << "\",\n"
           << "  \"operator_id\": \"" << json_escape(operator_id_) << "\",\n"
           << "  \"finished\": " << (finished ? "true" : "false") << ",\n"
           << "  \"success\": " << (success ? "true" : "false") << ",\n"
           << "  \"message_count\": " << message_count_ << ",\n"
           << "  \"image_count\": " << image_count_ << ",\n"
           << "  \"reason\": \"" << json_escape(reason) << "\"\n"
           << "}\n";
  }

  std::string storage_root_;
  double minimum_free_space_gb_{10.0};
  double max_episode_duration_{900.0};
  double image_record_frequency_{5.0};
  bool require_observation_ready_{true};
  bool observation_ready_{false};
  bool recording_{false};
  uint8_t state_{vehicle_interfaces::msg::EpisodeState::STATE_IDLE};
  uint64_t message_count_{0};
  uint64_t image_count_{0};
  std::string episode_id_;
  std::string task_;
  std::string operator_id_;
  std::string directory_;
  std::string last_message_{"Idle"};
  rclcpp::Time start_time_{0, 0, RCL_ROS_TIME};
  rclcpp::Time last_image_recorded_{0, 0, RCL_ROS_TIME};
  std::unique_ptr<rosbag2_cpp::Writer> writer_;
  rclcpp::Publisher<vehicle_interfaces::msg::EpisodeState>::SharedPtr state_publisher_;
  rclcpp::Service<vehicle_interfaces::srv::StartEpisode>::SharedPtr start_service_;
  rclcpp::Service<vehicle_interfaces::srv::StopEpisode>::SharedPtr stop_service_;
  rclcpp::Subscription<vehicle_interfaces::msg::ObservationStatus>::SharedPtr
    observation_subscription_;
  rclcpp::Subscription<sensor_msgs::msg::CompressedImage>::SharedPtr image_subscription_;
  rclcpp::Subscription<sensor_msgs::msg::CameraInfo>::SharedPtr camera_info_subscription_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odometry_subscription_;
  rclcpp::Subscription<sensor_msgs::msg::Imu>::SharedPtr imu_subscription_;
  rclcpp::Subscription<vehicle_interfaces::msg::SystemState>::SharedPtr
    system_state_subscription_;
  rclcpp::Subscription<vehicle_interfaces::msg::ObservationStatus>::SharedPtr
    observation_state_subscription_;
  rclcpp::Subscription<vehicle_interfaces::msg::PolicyAction>::SharedPtr
    policy_action_subscription_;
  rclcpp::Subscription<vehicle_interfaces::msg::PolicyObservation>::SharedPtr
    policy_observation_subscription_;
  rclcpp::Subscription<vehicle_interfaces::msg::ShadowComparison>::SharedPtr
    shadow_comparison_subscription_;
  rclcpp::Subscription<vehicle_interfaces::msg::ShadowMetrics>::SharedPtr
    shadow_metrics_subscription_;
  rclcpp::Subscription<vehicle_interfaces::msg::SafetyEvent>::SharedPtr
    safety_event_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::TwistStamped>::SharedPtr vla_command_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::TwistStamped>::SharedPtr
    selected_command_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr final_command_subscription_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr task_subscription_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<EpisodeRecorder>());
  rclcpp::shutdown();
  return 0;
}
