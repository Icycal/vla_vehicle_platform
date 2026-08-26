#include <geometry_msgs/msg/twist.hpp>
#include <geometry_msgs/msg/twist_stamped.hpp>
#include <rclcpp/rclcpp.hpp>
#include <vehicle_interfaces/msg/policy_action.hpp>
#include <vehicle_interfaces/msg/shadow_comparison.hpp>
#include <vehicle_interfaces/msg/shadow_metrics.hpp>
#include <vehicle_interfaces/msg/system_state.hpp>

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class ShadowEvaluator final : public rclcpp::Node
{
public:
  ShadowEvaluator()
  : Node("shadow_evaluator")
  {
    prediction_timeout_ = declare_parameter<double>("prediction_timeout", 0.75);
    linear_error_threshold_ = declare_parameter<double>("linear_error_threshold", 0.10);
    angular_error_threshold_ = declare_parameter<double>("angular_error_threshold", 0.20);

    comparison_publisher_ = create_publisher<vehicle_interfaces::msg::ShadowComparison>(
      "/vla/shadow_comparison", 10);
    metrics_publisher_ = create_publisher<vehicle_interfaces::msg::ShadowMetrics>(
      "/vla/shadow_metrics", rclcpp::QoS(1).reliable().transient_local());
    state_subscription_ = create_subscription<vehicle_interfaces::msg::SystemState>(
      "/vehicle/system_state", rclcpp::QoS(1).reliable().transient_local(),
      [this](vehicle_interfaces::msg::SystemState::SharedPtr message) {
        shadow_mode_ = message->mode == vehicle_interfaces::msg::SystemState::MODE_VLA_SHADOW;
        if (!shadow_mode_) {
          metadata_pending_ = false;
          prediction_pending_ = false;
        }
      });
    action_subscription_ = create_subscription<vehicle_interfaces::msg::PolicyAction>(
      "/vla/policy_action", 10,
      std::bind(&ShadowEvaluator::on_policy_action, this, std::placeholders::_1));
    raw_command_subscription_ = create_subscription<geometry_msgs::msg::TwistStamped>(
      "/vla/cmd_vel_raw", 10,
      std::bind(&ShadowEvaluator::on_raw_command, this, std::placeholders::_1));
    command_subscription_ = create_subscription<geometry_msgs::msg::Twist>(
      "/cmd_vel", 10,
      std::bind(&ShadowEvaluator::on_executed_command, this, std::placeholders::_1));
    publish_metrics("Shadow evaluator ready", false, 0.0);
  }

private:
  void on_policy_action(const vehicle_interfaces::msg::PolicyAction::SharedPtr message)
  {
    if (!shadow_mode_ || (message->action_vectors.empty() && message->actions.empty())) {
      return;
    }
    pending_request_id_ = message->request_id;
    pending_observation_id_ = message->observation_id;
    pending_model_id_ = message->model_id;
    pending_generated_at_ = rclcpp::Time(message->generated_at);
    metadata_pending_ = true;
    prediction_pending_ = false;
  }

  void on_raw_command(const geometry_msgs::msg::TwistStamped::SharedPtr message)
  {
    if (!shadow_mode_ || !metadata_pending_) {
      return;
    }
    pending_action_ = message->twist;
    metadata_pending_ = false;
    prediction_pending_ = true;
  }

  void on_executed_command(const geometry_msgs::msg::Twist::SharedPtr message)
  {
    if (!shadow_mode_ || !prediction_pending_) {
      return;
    }
    prediction_pending_ = false;
    const auto evaluated_at = now();
    const double prediction_age = pending_generated_at_.nanoseconds() > 0 ?
      (evaluated_at - pending_generated_at_).seconds() : prediction_timeout_ + 1.0;
    if (prediction_age < 0.0 || prediction_age > prediction_timeout_) {
      publish_metrics("Prediction expired before evaluation", false, prediction_age);
      return;
    }

    const double linear_error = std::abs(pending_action_.linear.x - message->linear.x);
    const double angular_error = std::abs(pending_action_.angular.z - message->angular.z);
    ++sample_count_;
    linear_error_sum_ += linear_error;
    angular_error_sum_ += angular_error;
    latest_linear_error_ = linear_error;
    latest_angular_error_ = angular_error;

    vehicle_interfaces::msg::ShadowComparison comparison;
    comparison.header.stamp = evaluated_at;
    comparison.header.frame_id = "base_link";
    comparison.request_id = pending_request_id_;
    comparison.observation_id = pending_observation_id_;
    comparison.model_id = pending_model_id_;
    comparison.prediction_generated_at = pending_generated_at_;
    comparison.evaluated_at = evaluated_at;
    comparison.predicted = pending_action_;
    comparison.executed = *message;
    comparison.linear_absolute_error = static_cast<float>(linear_error);
    comparison.angular_absolute_error = static_cast<float>(angular_error);
    comparison.within_threshold = linear_error <= linear_error_threshold_ &&
      angular_error <= angular_error_threshold_;
    comparison.message = comparison.within_threshold ?
      "Prediction is within shadow thresholds" : "Prediction exceeds shadow thresholds";
    comparison_publisher_->publish(comparison);
    publish_metrics("Shadow sample evaluated", true, prediction_age);
  }

  void publish_metrics(
    const std::string & status_message, const bool prediction_fresh,
    const double prediction_age)
  {
    vehicle_interfaces::msg::ShadowMetrics metrics;
    metrics.header.stamp = now();
    metrics.header.frame_id = "base_link";
    metrics.model_id = pending_model_id_;
    metrics.sample_count = sample_count_;
    metrics.linear_mean_absolute_error = sample_count_ > 0 ?
      static_cast<float>(linear_error_sum_ / static_cast<double>(sample_count_)) : 0.0F;
    metrics.angular_mean_absolute_error = sample_count_ > 0 ?
      static_cast<float>(angular_error_sum_ / static_cast<double>(sample_count_)) : 0.0F;
    metrics.latest_linear_absolute_error = static_cast<float>(latest_linear_error_);
    metrics.latest_angular_absolute_error = static_cast<float>(latest_angular_error_);
    metrics.latest_prediction_age_seconds = static_cast<float>(prediction_age);
    metrics.prediction_fresh = prediction_fresh;
    metrics.message = status_message;
    metrics_publisher_->publish(metrics);
  }

  double prediction_timeout_{0.75};
  double linear_error_threshold_{0.10};
  double angular_error_threshold_{0.20};
  bool shadow_mode_{false};
  bool metadata_pending_{false};
  bool prediction_pending_{false};
  uint64_t sample_count_{0};
  double linear_error_sum_{0.0};
  double angular_error_sum_{0.0};
  double latest_linear_error_{0.0};
  double latest_angular_error_{0.0};
  std::string pending_request_id_;
  std::string pending_observation_id_;
  std::string pending_model_id_;
  rclcpp::Time pending_generated_at_{0, 0, RCL_ROS_TIME};
  geometry_msgs::msg::Twist pending_action_;
  rclcpp::Publisher<vehicle_interfaces::msg::ShadowComparison>::SharedPtr comparison_publisher_;
  rclcpp::Publisher<vehicle_interfaces::msg::ShadowMetrics>::SharedPtr metrics_publisher_;
  rclcpp::Subscription<vehicle_interfaces::msg::SystemState>::SharedPtr state_subscription_;
  rclcpp::Subscription<vehicle_interfaces::msg::PolicyAction>::SharedPtr action_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::TwistStamped>::SharedPtr raw_command_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr command_subscription_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<ShadowEvaluator>());
  rclcpp::shutdown();
  return 0;
}
