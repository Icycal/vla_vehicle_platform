#include <geometry_msgs/msg/twist.hpp>
#include <geometry_msgs/msg/twist_stamped.hpp>
#include <rclcpp/rclcpp.hpp>
#include <vehicle_interfaces/msg/policy_observation.hpp>
#include <vehicle_interfaces/msg/training_action.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

class VehicleActionCapture final : public rclcpp::Node
{
public:
  VehicleActionCapture()
  : Node("vehicle_action_capture")
  {
    adapter_ = declare_parameter<std::string>("mobility_adapter", "twist");
    wheelbase_m_ = declare_parameter<double>("ackermann.wheelbase_m", 0.32);
    command_semantics_ = declare_parameter<std::string>(
      "ackermann.command_semantics", "yaw_rate");
    comparison_tolerance_ = declare_parameter<double>("comparison_tolerance", 1.0e-4);
    if (adapter_ != "twist" && adapter_ != "ackermann") {
      throw std::invalid_argument("capture mobility_adapter must be twist or ackermann");
    }
    if (adapter_ == "ackermann" && (!std::isfinite(wheelbase_m_) || wheelbase_m_ <= 0.0)) {
      throw std::invalid_argument("ackermann wheelbase must be positive");
    }
    if (command_semantics_ != "yaw_rate" && command_semantics_ != "steering_angle") {
      throw std::invalid_argument("ackermann command_semantics must be yaw_rate or steering_angle");
    }

    target_publisher_ = create_publisher<vehicle_interfaces::msg::TrainingAction>(
      "/chitu/action/target", 10);
    executed_publisher_ = create_publisher<vehicle_interfaces::msg::TrainingAction>(
      "/chitu/action/executed", 10);
    observation_subscription_ = create_subscription<vehicle_interfaces::msg::PolicyObservation>(
      "/vla/observation", rclcpp::QoS(2).reliable(),
      [this](vehicle_interfaces::msg::PolicyObservation::SharedPtr message) {
        std::lock_guard<std::mutex> lock(mutex_);
        observation_id_ = message->observation_id;
      });
    selected_subscription_ = create_subscription<geometry_msgs::msg::TwistStamped>(
      "/control/cmd_vel_selected", 10,
      std::bind(&VehicleActionCapture::on_target, this, std::placeholders::_1));
    executed_subscription_ = create_subscription<geometry_msgs::msg::Twist>(
      "/cmd_vel", 10,
      std::bind(&VehicleActionCapture::on_executed, this, std::placeholders::_1));
  }

private:
  vehicle_interfaces::msg::TrainingAction encode(
    const geometry_msgs::msg::Twist & command,
    const rclcpp::Time & stamp,
    const std::string & source) const
  {
    vehicle_interfaces::msg::TrainingAction action;
    action.header.stamp = stamp;
    action.header.frame_id = "base_link";
    {
      std::lock_guard<std::mutex> lock(mutex_);
      action.observation_id = observation_id_;
    }
    action.source = source;
    if (adapter_ == "twist") {
      action.action_schema = "vehicle.twist_chunk.v1";
      action.feature_names = {"linear_x", "angular_z"};
      action.feature_units = {"m/s", "rad/s"};
      action.values = {
        static_cast<float>(command.linear.x), static_cast<float>(command.angular.z)};
      return action;
    }

    action.action_schema = "chitu.action.ackermann.v1";
    action.feature_names = {"speed_mps", "steering_angle_rad"};
    action.feature_units = {"m/s", "rad"};
    const double steering_angle = command_semantics_ == "steering_angle" ?
      command.angular.z : inverse_ackermann(command.linear.x, command.angular.z);
    action.values = {
      static_cast<float>(command.linear.x), static_cast<float>(steering_angle)};
    return action;
  }

  double inverse_ackermann(const double speed, const double yaw_rate) const
  {
    if (std::abs(speed) <= 1.0e-4) {
      return 0.0;
    }
    return std::atan(wheelbase_m_ * yaw_rate / speed);
  }

  void on_target(const geometry_msgs::msg::TwistStamped::SharedPtr message)
  {
    auto action = encode(message->twist, rclcpp::Time(message->header.stamp), "control.selected");
    {
      std::lock_guard<std::mutex> lock(mutex_);
      latest_target_values_ = action.values;
    }
    target_publisher_->publish(action);
  }

  void on_executed(const geometry_msgs::msg::Twist::SharedPtr message)
  {
    auto action = encode(*message, now(), "control.executed");
    std::vector<float> target_values;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      target_values = latest_target_values_;
    }
    action.safety_intervened = target_values.size() == action.values.size() &&
      !std::equal(
      target_values.begin(), target_values.end(), action.values.begin(),
      [this](const float left, const float right) {
        return std::abs(left - right) <= comparison_tolerance_;
      });
    if (action.safety_intervened) {
      action.safety_reasons = {"safety_or_control_adjustment"};
    }
    executed_publisher_->publish(action);
  }

  std::string adapter_;
  double wheelbase_m_{0.32};
  std::string command_semantics_{"yaw_rate"};
  double comparison_tolerance_{1.0e-4};
  mutable std::mutex mutex_;
  std::string observation_id_;
  std::vector<float> latest_target_values_;
  rclcpp::Publisher<vehicle_interfaces::msg::TrainingAction>::SharedPtr target_publisher_;
  rclcpp::Publisher<vehicle_interfaces::msg::TrainingAction>::SharedPtr executed_publisher_;
  rclcpp::Subscription<vehicle_interfaces::msg::PolicyObservation>::SharedPtr observation_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::TwistStamped>::SharedPtr selected_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr executed_subscription_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<VehicleActionCapture>());
  rclcpp::shutdown();
  return 0;
}