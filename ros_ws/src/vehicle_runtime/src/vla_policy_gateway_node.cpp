#include <rclcpp/rclcpp.hpp>
#include <vehicle_interfaces/msg/policy_action.hpp>
#include <vehicle_interfaces/msg/policy_observation.hpp>
#include <vehicle_interfaces/msg/policy_status.hpp>

#include "vehicle_runtime/policy_transport.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

using namespace std::chrono_literals;

class VlaPolicyGateway final : public rclcpp::Node
{
public:
  VlaPolicyGateway()
  : Node("vla_policy_gateway")
  {
    const int horizon = declare_parameter<int>("action_horizon", 8);
    const int control_period_ms = declare_parameter<int>("control_period_ms", 50);
    const double prediction_frequency = declare_parameter<double>("prediction_frequency", 5.0);
    observation_timeout_ = declare_parameter<double>("observation_timeout", 0.75);
    const double linear_velocity = declare_parameter<double>("mock_linear_velocity", 0.0);
    const double angular_velocity = declare_parameter<double>("mock_angular_velocity", 0.0);

    transport_ = std::make_unique<vehicle_runtime::MockPolicyTransport>(
      static_cast<std::size_t>(std::max(horizon, 1)),
      std::chrono::milliseconds(std::max(control_period_ms, 1)),
      linear_velocity,
      angular_velocity);

    const auto state_qos = rclcpp::QoS(1).reliable().transient_local();
    status_publisher_ = create_publisher<vehicle_interfaces::msg::PolicyStatus>(
      "/vla/policy_state", state_qos);
    action_publisher_ = create_publisher<vehicle_interfaces::msg::PolicyAction>(
      "/vla/policy_action", 10);
    observation_subscription_ =
      create_subscription<vehicle_interfaces::msg::PolicyObservation>(
      "/vla/observation", rclcpp::QoS(2).reliable(),
      [this](vehicle_interfaces::msg::PolicyObservation::SharedPtr message) {
        observation_ = std::move(message);
        observation_received_at_ = now();
      });

    const auto prediction_period = std::chrono::duration<double>(
      1.0 / std::max(prediction_frequency, 0.1));
    prediction_timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(prediction_period),
      std::bind(&VlaPolicyGateway::predict, this));
    status_timer_ = create_wall_timer(1s, std::bind(&VlaPolicyGateway::publish_status, this));
    publish_status();
  }

private:
  void publish_status()
  {
    vehicle_interfaces::msg::PolicyStatus message;
    message.header.stamp = now();
    message.state = transport_->ready() ?
      vehicle_interfaces::msg::PolicyStatus::STATE_READY :
      vehicle_interfaces::msg::PolicyStatus::STATE_ERROR;
    message.provider_id = transport_->provider_id();
    message.model_id = transport_->model_id();
    message.protocol_version = "1";
    message.observation_schema = "vehicle.observation.v1";
    message.action_schema = "vehicle.twist_chunk.v1";
    message.inference_latency_ms = last_latency_ms_;
    message.message = transport_->ready() ? "Mock policy ready" : "Policy unavailable";
    status_publisher_->publish(message);
  }

  void predict()
  {
    if (!transport_->ready() || !observation_) {
      return;
    }
    const auto current_time = now();
    if ((current_time - observation_received_at_).seconds() > observation_timeout_ ||
      current_time > rclcpp::Time(observation_->valid_until) ||
      observation_->observation_id == last_observation_id_)
    {
      return;
    }
    const auto started = std::chrono::steady_clock::now();
    vehicle_runtime::PolicyObservationInput input;
    input.observation_id = observation_->observation_id;
    input.schema_version = observation_->schema_version;
    input.task = observation_->task;
    input.state_keys = observation_->state_keys;
    input.state = observation_->state;
    input.state_valid.assign(
      observation_->state_valid.begin(), observation_->state_valid.end());
    input.images.reserve(observation_->images.size());
    for (std::size_t index = 0; index < observation_->images.size(); ++index) {
      vehicle_runtime::PolicyImage image;
      image.key = index < observation_->image_keys.size() ?
        observation_->image_keys[index] : "observation.images.unknown";
      image.format = observation_->images[index].format;
      image.data = observation_->images[index].data;
      input.images.push_back(std::move(image));
    }
    const auto prediction = transport_->predict(input);
    const auto stamp = now();
    last_observation_id_ = observation_->observation_id;

    vehicle_interfaces::msg::PolicyAction message;
    message.header.stamp = stamp;
    message.header.frame_id = "base_link";
    message.request_id = prediction.request_id;
    message.observation_id = input.observation_id;
    message.model_id = prediction.model_id;
    message.schema_version = "vehicle.twist_chunk.v1";
    message.generated_at = stamp;
    message.control_period = rclcpp::Duration(prediction.control_period);
    const auto validity = prediction.control_period *
      static_cast<int64_t>(std::max<std::size_t>(prediction.actions.size(), 1));
    message.valid_until = stamp + rclcpp::Duration(validity + 500ms);
    message.actions = prediction.actions;
    action_publisher_->publish(message);

    const auto elapsed = std::chrono::steady_clock::now() - started;
    last_latency_ms_ = std::chrono::duration<float, std::milli>(elapsed).count();
  }

  double observation_timeout_{0.75};
  float last_latency_ms_{0.0F};
  std::string last_observation_id_;
  std::unique_ptr<vehicle_runtime::PolicyTransport> transport_;
  rclcpp::Publisher<vehicle_interfaces::msg::PolicyStatus>::SharedPtr status_publisher_;
  rclcpp::Publisher<vehicle_interfaces::msg::PolicyAction>::SharedPtr action_publisher_;
  vehicle_interfaces::msg::PolicyObservation::SharedPtr observation_;
  rclcpp::Time observation_received_at_{0, 0, RCL_ROS_TIME};
  rclcpp::Subscription<vehicle_interfaces::msg::PolicyObservation>::SharedPtr
    observation_subscription_;
  rclcpp::TimerBase::SharedPtr prediction_timer_;
  rclcpp::TimerBase::SharedPtr status_timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<VlaPolicyGateway>());
  rclcpp::shutdown();
  return 0;
}
