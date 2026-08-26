#include <rclcpp/rclcpp.hpp>
#include <vehicle_interfaces/msg/action_vector.hpp>
#include <vehicle_interfaces/msg/policy_action.hpp>
#include <vehicle_interfaces/msg/policy_observation.hpp>
#include <vehicle_interfaces/msg/policy_status.hpp>

#include <vehicle_policy_transport/mock_policy_transport.hpp>
#include <vehicle_policy_transport/policy_transport.hpp>
#include <vehicle_policy_transport/unix_socket_policy_transport.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

using namespace std::chrono_literals;

class VlaPolicyGateway final : public rclcpp::Node
{
public:
  VlaPolicyGateway()
  : Node("vla_policy_gateway")
  {
    const std::string transport = declare_parameter<std::string>("transport", "mock");
    const std::string socket_path = declare_parameter<std::string>(
      "socket_path", "/run/vla-policy/policy.sock");
    const double transport_timeout = declare_parameter<double>("transport_timeout", 1.0);
    const std::string observation_topic = declare_parameter<std::string>(
      "observation_topic", "/vla/observation");
    const int horizon = declare_parameter<int>("action_horizon", 8);
    const int control_period_ms = declare_parameter<int>("control_period_ms", 50);
    const double prediction_frequency = declare_parameter<double>("prediction_frequency", 5.0);
    observation_timeout_ = declare_parameter<double>("observation_timeout", 0.75);
    const double linear_velocity = declare_parameter<double>("mock_linear_velocity", 0.0);
    const double angular_velocity = declare_parameter<double>("mock_angular_velocity", 0.0);

    if (transport == "mock") {
      transport_ = std::make_unique<vehicle_policy_transport::MockPolicyTransport>(
        static_cast<std::size_t>(std::max(horizon, 1)),
        std::chrono::milliseconds(std::max(control_period_ms, 1)),
        linear_velocity,
        angular_velocity);
    } else if (transport == "unix_socket") {
      transport_ = std::make_unique<vehicle_policy_transport::UnixSocketPolicyTransport>(
        socket_path,
        std::chrono::milliseconds(
          static_cast<int64_t>(std::max(transport_timeout, 0.01) * 1000.0)));
    } else {
      throw std::invalid_argument("Unknown policy transport: " + transport);
    }

    observation_callback_group_ = create_callback_group(rclcpp::CallbackGroupType::Reentrant);
    prediction_callback_group_ = create_callback_group(
      rclcpp::CallbackGroupType::MutuallyExclusive);
    status_callback_group_ = create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);

    const auto state_qos = rclcpp::QoS(1).reliable().transient_local();
    status_publisher_ = create_publisher<vehicle_interfaces::msg::PolicyStatus>(
      "/vla/policy_state", state_qos);
    action_publisher_ = create_publisher<vehicle_interfaces::msg::PolicyAction>(
      "/vla/policy_action", 10);

    rclcpp::SubscriptionOptions subscription_options;
    subscription_options.callback_group = observation_callback_group_;
    observation_subscription_ =
      create_subscription<vehicle_interfaces::msg::PolicyObservation>(
      observation_topic, rclcpp::QoS(2).reliable(),
      [this](vehicle_interfaces::msg::PolicyObservation::SharedPtr message) {
        std::lock_guard<std::mutex> lock(observation_mutex_);
        observation_ = std::move(message);
        observation_received_at_ = now();
      }, subscription_options);

    const auto prediction_period = std::chrono::duration<double>(
      1.0 / std::max(prediction_frequency, 0.1));
    prediction_timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(prediction_period),
      std::bind(&VlaPolicyGateway::predict, this), prediction_callback_group_);
    status_timer_ = create_wall_timer(
      1s, std::bind(&VlaPolicyGateway::publish_status, this), status_callback_group_);
    publish_status();
  }

private:
  void publish_status()
  {
    transport_->refresh();
    vehicle_interfaces::msg::PolicyStatus message;
    message.header.stamp = now();
    message.state = transport_->ready() ?
      vehicle_interfaces::msg::PolicyStatus::STATE_READY :
      vehicle_interfaces::msg::PolicyStatus::STATE_ERROR;
    message.provider_id = transport_->provider_id();
    message.model_id = transport_->model_id();
    message.protocol_version = "1";
    message.observation_schema = "vehicle.observation.v1";
    message.action_schema = transport_->action_schema();
    message.inference_latency_ms = last_latency_ms_.load();
    message.message = transport_->status_message();
    status_publisher_->publish(message);
  }

  void predict()
  {
    if (prediction_in_progress_.exchange(true)) {
      return;
    }
    struct PredictionGuard
    {
      explicit PredictionGuard(std::atomic<bool> & flag) : flag_(flag) {}
      ~PredictionGuard() {flag_.store(false);}
      std::atomic<bool> & flag_;
    } prediction_guard(prediction_in_progress_);

    if (!transport_->ready()) {
      return;
    }

    vehicle_interfaces::msg::PolicyObservation::SharedPtr observation;
    rclcpp::Time observation_received_at{0, 0, RCL_ROS_TIME};
    {
      std::lock_guard<std::mutex> lock(observation_mutex_);
      observation = observation_;
      observation_received_at = observation_received_at_;
    }
    if (!observation) {
      return;
    }

    const auto current_time = now();
    if ((current_time - observation_received_at).seconds() > observation_timeout_ ||
      current_time > rclcpp::Time(observation->valid_until) ||
      observation->observation_id == last_observation_id_)
    {
      return;
    }

    const auto started = std::chrono::steady_clock::now();
    vehicle_policy_transport::PolicyObservationInput input;
    input.observation_id = observation->observation_id;
    input.schema_version = observation->schema_version;
    input.task = observation->task;
    input.generated_at_ns = rclcpp::Time(observation->generated_at).nanoseconds();
    input.valid_until_ns = rclcpp::Time(observation->valid_until).nanoseconds();
    input.state_keys = observation->state_keys;
    input.state = observation->state;
    input.state_valid.assign(
      observation->state_valid.begin(), observation->state_valid.end());
    input.images.reserve(observation->images.size());
    for (std::size_t index = 0; index < observation->images.size(); ++index) {
      vehicle_policy_transport::PolicyImage image;
      image.key = index < observation->image_keys.size() ?
        observation->image_keys[index] : "observation.images.unknown";
      image.format = observation->images[index].format;
      image.data = observation->images[index].data;
      input.images.push_back(std::move(image));
    }

    vehicle_policy_transport::PolicyPrediction prediction;
    try {
      prediction = transport_->predict(input);
    } catch (const std::exception & error) {
      RCLCPP_ERROR_THROTTLE(
        get_logger(), *get_clock(), 2000, "Policy prediction failed: %s", error.what());
      return;
    }

    const auto stamp = now();
    last_observation_id_ = observation->observation_id;
    vehicle_interfaces::msg::PolicyAction message;
    message.header.stamp = stamp;
    message.header.frame_id = "base_link";
    message.request_id = prediction.request_id;
    message.observation_id = prediction.observation_id;
    message.model_id = prediction.model_id;
    message.schema_version = prediction.action_schema;
    message.schema_hash = prediction.action_schema_hash;
    message.feature_names = prediction.action_features;
    message.feature_units = prediction.action_units;
    message.generated_at = stamp;
    message.control_period = rclcpp::Duration(prediction.control_period);
    const auto validity = prediction.control_period *
      static_cast<int64_t>(std::max<std::size_t>(prediction.action_vectors.size(), 1));
    message.valid_until = stamp + rclcpp::Duration(validity + 500ms);
    message.action_vectors.reserve(prediction.action_vectors.size());
    for (const auto & values : prediction.action_vectors) {
      vehicle_interfaces::msg::ActionVector action;
      action.values = values;
      message.action_vectors.push_back(std::move(action));
    }
    message.actions = prediction.actions;
    action_publisher_->publish(message);

    const auto elapsed = std::chrono::steady_clock::now() - started;
    last_latency_ms_.store(std::chrono::duration<float, std::milli>(elapsed).count());
  }

  double observation_timeout_{0.75};
  std::atomic<float> last_latency_ms_{0.0F};
  std::atomic<bool> prediction_in_progress_{false};
  std::string last_observation_id_;
  std::mutex observation_mutex_;
  std::unique_ptr<vehicle_policy_transport::PolicyTransport> transport_;
  rclcpp::Publisher<vehicle_interfaces::msg::PolicyStatus>::SharedPtr status_publisher_;
  rclcpp::Publisher<vehicle_interfaces::msg::PolicyAction>::SharedPtr action_publisher_;
  vehicle_interfaces::msg::PolicyObservation::SharedPtr observation_;
  rclcpp::Time observation_received_at_{0, 0, RCL_ROS_TIME};
  rclcpp::Subscription<vehicle_interfaces::msg::PolicyObservation>::SharedPtr
    observation_subscription_;
  rclcpp::TimerBase::SharedPtr prediction_timer_;
  rclcpp::TimerBase::SharedPtr status_timer_;
  rclcpp::CallbackGroup::SharedPtr observation_callback_group_;
  rclcpp::CallbackGroup::SharedPtr prediction_callback_group_;
  rclcpp::CallbackGroup::SharedPtr status_callback_group_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<VlaPolicyGateway>();
  rclcpp::executors::MultiThreadedExecutor executor(rclcpp::ExecutorOptions(), 3U);
  executor.add_node(node);
  executor.spin();
  rclcpp::shutdown();
  return 0;
}
