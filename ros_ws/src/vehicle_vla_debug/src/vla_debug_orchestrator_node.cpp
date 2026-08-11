#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <vehicle_interfaces/msg/policy_observation.hpp>
#include <vehicle_interfaces/msg/system_state.hpp>
#include <vehicle_interfaces/srv/capture_vla_debug.hpp>
#include <vehicle_interfaces/srv/run_vla_debug.hpp>
#include <vehicle_policy_transport/mock_policy_transport.hpp>
#include <vehicle_policy_transport/policy_transport.hpp>
#include <vehicle_policy_transport/unix_socket_policy_transport.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

class VlaDebugOrchestrator final : public rclcpp::Node
{
public:
  VlaDebugOrchestrator()
  : Node("vla_debug_orchestrator")
  {
    const auto transport = declare_parameter<std::string>("transport", "mock");
    const auto socket_path = declare_parameter<std::string>(
      "socket_path", "/home/wheeltec/vla_vehicle_platform/run/policy/policy.sock");
    const auto timeout = declare_parameter<double>("transport_timeout", 35.0);
    artifact_root_ = declare_parameter<std::string>(
      "artifact_root", "/home/wheeltec/vla_vehicle_platform/run/ops/debug");
    observation_timeout_ = declare_parameter<double>("observation_timeout", 1.0);
    command_timeout_ = declare_parameter<double>("command_timeout", 0.5);
    stationary_linear_threshold_ = declare_parameter<double>("stationary_linear_threshold", 0.02);
    stationary_angular_threshold_ = declare_parameter<double>("stationary_angular_threshold", 0.05);
    require_shadow_mode_ = declare_parameter<bool>("require_shadow_mode", true);
    provider_busy_retry_count_ = static_cast<std::size_t>(std::max<int64_t>(
      declare_parameter<int64_t>("provider_busy_retry_count", 5), 0));
    provider_busy_retry_delay_ = std::chrono::milliseconds(std::max<int64_t>(
      declare_parameter<int64_t>("provider_busy_retry_delay_ms", 350), 0));
    max_snapshots_ = static_cast<std::size_t>(std::max<int64_t>(
      declare_parameter<int64_t>("max_snapshots", 20), 1));

    if (transport == "mock") {
      transport_ = std::make_unique<vehicle_policy_transport::MockPolicyTransport>(
        8, 100ms, 0.0, 0.0);
    } else if (transport == "unix_socket") {
      transport_ = std::make_unique<vehicle_policy_transport::UnixSocketPolicyTransport>(
        socket_path, std::chrono::milliseconds(
          static_cast<int64_t>(std::max(timeout, 0.1) * 1000.0)));
    } else {
      throw std::invalid_argument("Unknown policy transport: " + transport);
    }

    std::filesystem::create_directories(artifact_root_);
    observation_subscription_ = create_subscription<vehicle_interfaces::msg::PolicyObservation>(
      "/vla/observation", rclcpp::QoS(2).reliable(),
      [this](vehicle_interfaces::msg::PolicyObservation::SharedPtr message) {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_observation_ = std::move(message);
        observation_received_at_ = now();
      });
    const auto state_qos = rclcpp::QoS(1).reliable().transient_local();
    system_subscription_ = create_subscription<vehicle_interfaces::msg::SystemState>(
      "/vehicle/system_state", state_qos,
      [this](vehicle_interfaces::msg::SystemState::SharedPtr message) {
        std::lock_guard<std::mutex> lock(mutex_);
        system_state_ = std::move(message);
      });
    command_subscription_ = create_subscription<geometry_msgs::msg::Twist>(
      "/cmd_vel", 10, [this](geometry_msgs::msg::Twist::SharedPtr message) {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_command_ = *message;
        command_received_at_ = now();
      });
    capture_service_ = create_service<vehicle_interfaces::srv::CaptureVlaDebug>(
      "/vla/debug/capture",
      std::bind(&VlaDebugOrchestrator::capture, this, std::placeholders::_1, std::placeholders::_2));
    run_service_ = create_service<vehicle_interfaces::srv::RunVlaDebug>(
      "/vla/debug/run",
      std::bind(&VlaDebugOrchestrator::run, this, std::placeholders::_1, std::placeholders::_2));
  }

private:
  using Observation = vehicle_interfaces::msg::PolicyObservation;
  using Json = nlohmann::json;

  static int64_t time_ns(const builtin_interfaces::msg::Time & value)
  {
    return static_cast<int64_t>(value.sec) * 1000000000LL + value.nanosec;
  }

  std::string make_run_id()
  {
    return "vla-debug-" + std::to_string(now().nanoseconds()) + "-" +
      std::to_string(++sequence_);
  }

  Json observation_json(const Observation & observation, const std::string & run_id) const
  {
    Json states = Json::array();
    const auto count = std::min({
      observation.state_keys.size(), observation.state.size(), observation.state_valid.size()});
    for (std::size_t index = 0; index < count; ++index) {
      states.push_back({{"key", observation.state_keys[index]}, {"value", observation.state[index]},
        {"valid", observation.state_valid[index]}});
    }
    Json images = Json::array();
    for (std::size_t index = 0; index < observation.images.size(); ++index) {
      images.push_back({
        {"key", index < observation.image_keys.size() ? observation.image_keys[index] : "unknown"},
        {"format", observation.images[index].format}, {"bytes", observation.images[index].data.size()}});
    }
    return {{"schema_version", "vehicle.vla.debug.observation.v1"}, {"run_id", run_id},
      {"observation_id", observation.observation_id}, {"task", observation.task},
      {"generated_at_ns", time_ns(observation.generated_at)}, {"images", images}, {"state", states}};
  }

  vehicle_policy_transport::PolicyObservationInput transport_input(
    const Observation & observation, const std::string & run_id) const
  {
    vehicle_policy_transport::PolicyObservationInput input;
    input.observation_id = run_id;
    input.schema_version = observation.schema_version;
    input.task = observation.task;
    input.generated_at_ns = time_ns(observation.generated_at);
    input.valid_until_ns = now().nanoseconds() + 60000000000LL;
    input.state_keys = observation.state_keys;
    input.state = observation.state;
    input.state_valid.assign(observation.state_valid.begin(), observation.state_valid.end());
    for (std::size_t index = 0; index < observation.images.size(); ++index) {
      vehicle_policy_transport::PolicyImage image;
      image.key = index < observation.image_keys.size() ? observation.image_keys[index] : "unknown";
      image.format = observation.images[index].format;
      image.data = observation.images[index].data;
      input.images.push_back(std::move(image));
    }
    return input;
  }

  bool safe_to_debug(std::string & reason) const
  {
    if (require_shadow_mode_) {
      if (!system_state_) {reason = "Vehicle system state is unavailable"; return false;}
      if (system_state_->mode != vehicle_interfaces::msg::SystemState::MODE_VLA_SHADOW) {
        reason = "VLA debug requires VLA_SHADOW mode";
        return false;
      }
    }
    if (command_received_at_.nanoseconds() > 0 &&
      (now() - command_received_at_).seconds() <= command_timeout_ &&
      (std::abs(latest_command_.linear.x) > stationary_linear_threshold_ ||
      std::abs(latest_command_.angular.z) > stationary_angular_threshold_))
    {
      reason = "Vehicle command is not stationary";
      return false;
    }
    return true;
  }

  void capture(
    const std::shared_ptr<vehicle_interfaces::srv::CaptureVlaDebug::Request> request,
    std::shared_ptr<vehicle_interfaces::srv::CaptureVlaDebug::Response> response)
  {
    Observation snapshot;
    std::string run_id;
    std::string input_source{"camera"};
    std::string image_source_name;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      std::string reason;
      if (!safe_to_debug(reason)) {response->message = reason; return;}
      if (!latest_observation_ || (now() - observation_received_at_).seconds() > observation_timeout_) {
        response->message = "A fresh PolicyObservation is required";
        return;
      }
      snapshot = *latest_observation_;
      if (request->use_image_override) {
        const auto & format = request->image_override.format;
        const bool jpeg_format = format == "jpeg" || format == "jpg" ||
          format.find("jpeg") != std::string::npos;
        if (!jpeg_format || request->image_override.data.empty()) {
          response->message = "Uploaded debug image must be a non-empty JPEG";
          return;
        }
        if (snapshot.images.empty()) {
          snapshot.images.push_back(request->image_override);
          snapshot.image_keys.push_back("observation.images.front");
        } else {
          snapshot.images.front() = request->image_override;
        }
        snapshot.images.front().header.stamp = now();
        input_source = "upload";
        image_source_name = request->image_source_name.substr(0, 128);
      }
      if (!request->task_override.empty()) {snapshot.task = request->task_override;}
      if (snapshot.task.empty()) {response->message = "Task text is required"; return;}
      run_id = make_run_id();
      snapshots_[run_id] = snapshot;
      snapshot_order_.push_back(run_id);
      while (snapshot_order_.size() > max_snapshots_) {
        snapshots_.erase(snapshot_order_.front());
        snapshot_order_.erase(snapshot_order_.begin());
      }
    }

    const auto directory = artifact_root_ / run_id;
    std::filesystem::create_directories(directory);
    auto metadata = observation_json(snapshot, run_id);
    metadata["input_source"] = input_source;
    metadata["image_source_name"] = image_source_name;
    std::ofstream(directory / "observation.json") << std::setw(2) << metadata << '\n';
    if (!snapshot.images.empty()) {
      std::ofstream image(directory / "camera-original.jpg", std::ios::binary);
      const auto & data = snapshot.images.front().data;
      image.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    response->accepted = true;
    response->run_id = run_id;
    response->directory = directory.string();
    response->observation_json = metadata.dump();
    response->message = input_source == "upload" ?
      "Uploaded image snapshot captured; no control command was published" :
      "Observation snapshot captured; no control command was published";
  }

  void run(
    const std::shared_ptr<vehicle_interfaces::srv::RunVlaDebug::Request> request,
    std::shared_ptr<vehicle_interfaces::srv::RunVlaDebug::Response> response)
  {
    const std::string stage = request->stage == "preprocess" ? "preprocess" :
      (request->stage == "inference" ? "inference" : "");
    if (stage.empty()) {response->message = "Stage must be preprocess or inference"; return;}
    Observation snapshot;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      std::string reason;
      if (!safe_to_debug(reason)) {response->message = reason; return;}
      const auto found = snapshots_.find(request->run_id);
      if (found == snapshots_.end()) {response->message = "Debug snapshot not found"; return;}
      snapshot = found->second;
    }
    try {
      const auto input = transport_input(snapshot, request->run_id);
      vehicle_policy_transport::PolicyDebugResult result;
      for (std::size_t attempt = 0;; ++attempt) {
        try {
          result = transport_->debug(input, request->run_id, stage);
          break;
        } catch (const std::exception & error) {
          const std::string message = error.what();
          const bool provider_busy = message.find("provider is busy") != std::string::npos;
          if (!provider_busy || attempt >= provider_busy_retry_count_) {
            throw;
          }
          RCLCPP_WARN(
            get_logger(), "VLA debug provider busy; retrying %zu/%zu after %ld ms",
            attempt + 1, provider_busy_retry_count_, provider_busy_retry_delay_.count());
          std::this_thread::sleep_for(provider_busy_retry_delay_);
        }
      }
      auto result_json = Json::parse(result.result_json);
      const auto directory = artifact_root_ / request->run_id;
      result_json["artifacts"] = {
        {"directory", directory.string()}, {"original_image", "camera-original.jpg"},
        {"processed_image", result.processed_image_jpeg.empty() ? "" : "camera-processed.jpg"},
        {"result", stage + ".json"}};
      std::ofstream(directory / (stage + ".json")) << std::setw(2) << result_json << '\n';
      std::string processed_path;
      if (!result.processed_image_jpeg.empty()) {
        const auto path = directory / "camera-processed.jpg";
        std::ofstream image(path, std::ios::binary);
        image.write(reinterpret_cast<const char *>(result.processed_image_jpeg.data()),
          static_cast<std::streamsize>(result.processed_image_jpeg.size()));
        processed_path = path.string();
      }
      response->accepted = true;
      response->result_json = result_json.dump();
      response->processed_image_path = processed_path;
      response->message = stage + " completed in Shadow-only debug mode";
    } catch (const std::exception & error) {
      response->message = error.what();
    }
  }

  std::filesystem::path artifact_root_;
  double observation_timeout_{1.0};
  double command_timeout_{0.5};
  double stationary_linear_threshold_{0.02};
  double stationary_angular_threshold_{0.05};
  bool require_shadow_mode_{true};
  std::size_t provider_busy_retry_count_{5};
  std::chrono::milliseconds provider_busy_retry_delay_{350};
  std::size_t max_snapshots_{20};
  uint64_t sequence_{0};
  mutable std::mutex mutex_;
  Observation::SharedPtr latest_observation_;
  vehicle_interfaces::msg::SystemState::SharedPtr system_state_;
  geometry_msgs::msg::Twist latest_command_;
  rclcpp::Time observation_received_at_{0, 0, RCL_ROS_TIME};
  rclcpp::Time command_received_at_{0, 0, RCL_ROS_TIME};
  std::map<std::string, Observation> snapshots_;
  std::vector<std::string> snapshot_order_;
  std::unique_ptr<vehicle_policy_transport::PolicyTransport> transport_;
  rclcpp::Subscription<Observation>::SharedPtr observation_subscription_;
  rclcpp::Subscription<vehicle_interfaces::msg::SystemState>::SharedPtr system_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr command_subscription_;
  rclcpp::Service<vehicle_interfaces::srv::CaptureVlaDebug>::SharedPtr capture_service_;
  rclcpp::Service<vehicle_interfaces::srv::RunVlaDebug>::SharedPtr run_service_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<VlaDebugOrchestrator>();
  rclcpp::executors::MultiThreadedExecutor executor(rclcpp::ExecutorOptions(), 2U);
  executor.add_node(node);
  executor.spin();
  rclcpp::shutdown();
  return 0;
}
