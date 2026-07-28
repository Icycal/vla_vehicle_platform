#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <vehicle_interfaces/msg/policy_action.hpp>
#include <vehicle_interfaces/msg/policy_status.hpp>

#include "vehicle_runtime/policy_transport.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

using namespace std::chrono_literals;

class VlapolicyGateway final : public rclcpp::Node
{
public:
  VlapolicyGateway()
  : Node("vla_policy_gateway")
  {
    const int horizon = declare_parameter<int>("action_horizon", 8);
    const int control_period_ms = declare_parameter<int>("control_period_ms", 50);
    const double prediction_frequency = declare_parameter<double>("prediction_frequency", 5.0);
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
    task_subscription_ = create_subscription<std_msgs::msg::String>(
      "/vla/task", 10,
      [this](std_msgs::msg::String::SharedPtr message) {task_ = message->data;});

    const auto prediction_period = std::chrono::duration<double>(
      1.0 / std::max(prediction_frequency, 0.1));
    prediction_timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(prediction_period),
      std::bind(&VlapolicyGateway::predict, this));
    status_timer_ = create_wall_timer(1s, std::bind(&VlapolicyGateway::publish_status, this));
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
    message.message = transport_->ready() ? "Mock policy ready" : "Policy unavailable";
    status_publisher_->publish(message);
  }

  void predict()
  {
    if (!transport_->ready()) {
      return;
    }
    const auto started = std::chrono::steady_clock::now();
    const auto prediction = transport_->predict(task_);
    const auto stamp = now();

    vehicle_interfaces::msg::PolicyAction message;
    message.header.stamp = stamp;
    message.header.frame_id = "base_link";
    message.request_id = prediction.request_id;
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

  std::string task_;
  float last_latency_ms_{0.0F};
  std::unique_ptr<vehicle_runtime::PolicyTransport> transport_;
  rclcpp::Publisher<vehicle_interfaces::msg::PolicyStatus>::SharedPtr status_publisher_;
  rclcpp::Publisher<vehicle_interfaces::msg::PolicyAction>::SharedPtr action_publisher_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr task_subscription_;
  rclcpp::TimerBase::SharedPtr prediction_timer_;
  rclcpp::TimerBase::SharedPtr status_timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<VlapolicyGateway>());
  rclcpp::shutdown();
  return 0;
}
