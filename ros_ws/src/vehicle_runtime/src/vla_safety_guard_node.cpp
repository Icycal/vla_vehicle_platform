#include <geometry_msgs/msg/twist.hpp>
#include <geometry_msgs/msg/twist_stamped.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/laser_scan.hpp>
#include <vehicle_interfaces/msg/safety_event.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <string>

class VlaSafetyGuard final : public rclcpp::Node
{
public:
  VlaSafetyGuard()
  : Node("vla_safety_guard")
  {
    max_linear_velocity_ = declare_parameter<double>("max_linear_velocity", 0.4);
    max_reverse_velocity_ = declare_parameter<double>("max_reverse_velocity", 0.2);
    max_angular_velocity_ = declare_parameter<double>("max_angular_velocity", 0.8);
    command_timeout_ = declare_parameter<double>("command_timeout", 0.3);
    scan_timeout_ = declare_parameter<double>("scan_timeout", 0.5);
    stop_distance_ = declare_parameter<double>("stop_distance", 0.45);
    obstacle_half_angle_ = declare_parameter<double>("obstacle_half_angle", 0.52);
    require_scan_ = declare_parameter<bool>("require_scan", false);
    const double publish_frequency = declare_parameter<double>("publish_frequency", 20.0);
    const auto output_topic = declare_parameter<std::string>("output_topic", "/cmd_vel");
    const auto event_topic = declare_parameter<std::string>("event_topic", "/vla/safety_event");
    const auto selected_command_topic = declare_parameter<std::string>(
      "selected_command_topic", "/control/cmd_vel_selected");
    const auto scan_topic = declare_parameter<std::string>("scan_topic", "/scan");

    command_publisher_ = create_publisher<geometry_msgs::msg::Twist>(output_topic, 10);
    event_publisher_ = create_publisher<vehicle_interfaces::msg::SafetyEvent>(
      event_topic, rclcpp::QoS(10).reliable());
    command_subscription_ = create_subscription<geometry_msgs::msg::TwistStamped>(
      selected_command_topic, 10,
      [this](geometry_msgs::msg::TwistStamped::SharedPtr message) {
        selected_command_ = message->twist;
        command_stamp_ = message->header.stamp;
        if (command_stamp_.nanoseconds() == 0) {
          command_stamp_ = now();
        }
      });
    scan_subscription_ = create_subscription<sensor_msgs::msg::LaserScan>(
      scan_topic, rclcpp::SensorDataQoS(),
      std::bind(&VlaSafetyGuard::on_scan, this, std::placeholders::_1));

    const auto period = std::chrono::duration<double>(
      1.0 / std::max(publish_frequency, 1.0));
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      std::bind(&VlaSafetyGuard::publish_safe_command, this));
  }

private:
  void on_scan(const sensor_msgs::msg::LaserScan::SharedPtr message)
  {
    minimum_front_range_ = std::numeric_limits<double>::infinity();
    double angle = message->angle_min;
    for (const float range : message->ranges) {
      if (std::abs(angle) <= obstacle_half_angle_ && std::isfinite(range) &&
        range >= message->range_min && range <= message->range_max)
      {
        minimum_front_range_ = std::min(minimum_front_range_, static_cast<double>(range));
      }
      angle += message->angle_increment;
    }
    scan_stamp_ = now();
  }

  bool is_fresh(const rclcpp::Time & stamp, const double timeout) const
  {
    return stamp.nanoseconds() > 0 && (now() - stamp).seconds() <= timeout;
  }

  void publish_event(const uint8_t severity, const std::string & rule, const std::string & reason)
  {
    if (active_rule_ == rule && active_severity_ == severity) {
      return;
    }
    if (!active_rule_.empty()) {
      vehicle_interfaces::msg::SafetyEvent cleared;
      cleared.header.stamp = now();
      cleared.severity = active_severity_;
      cleared.rule_id = active_rule_;
      cleared.reason = "Safety condition cleared";
      cleared.active = false;
      event_publisher_->publish(cleared);
    }
    active_rule_ = rule;
    active_severity_ = severity;
    if (!rule.empty()) {
      vehicle_interfaces::msg::SafetyEvent event;
      event.header.stamp = now();
      event.severity = severity;
      event.rule_id = rule;
      event.reason = reason;
      event.active = true;
      event_publisher_->publish(event);
    }
  }

  void publish_safe_command()
  {
    geometry_msgs::msg::Twist output;
    if (!is_fresh(command_stamp_, command_timeout_)) {
      publish_event(
        vehicle_interfaces::msg::SafetyEvent::SEVERITY_WARNING,
        "command_timeout", "Selected velocity command is stale");
      command_publisher_->publish(output);
      return;
    }

    const bool scan_fresh = is_fresh(scan_stamp_, scan_timeout_);
    if (require_scan_ && !scan_fresh) {
      publish_event(
        vehicle_interfaces::msg::SafetyEvent::SEVERITY_STOP,
        "scan_timeout", "Required obstacle scan is unavailable");
      command_publisher_->publish(output);
      return;
    }

    output = selected_command_;
    output.linear.x = std::clamp(
      output.linear.x, -std::abs(max_reverse_velocity_), std::abs(max_linear_velocity_));
    output.linear.y = 0.0;
    output.linear.z = 0.0;
    output.angular.x = 0.0;
    output.angular.y = 0.0;
    output.angular.z = std::clamp(
      output.angular.z, -std::abs(max_angular_velocity_), std::abs(max_angular_velocity_));

    if (output.linear.x > 0.0 && scan_fresh && minimum_front_range_ < stop_distance_) {
      publish_event(
        vehicle_interfaces::msg::SafetyEvent::SEVERITY_STOP,
        "front_obstacle", "Obstacle is inside the configured stop distance");
      command_publisher_->publish(geometry_msgs::msg::Twist{});
      return;
    }

    publish_event(vehicle_interfaces::msg::SafetyEvent::SEVERITY_INFO, "", "");
    command_publisher_->publish(output);
  }

  double max_linear_velocity_{0.4};
  double max_reverse_velocity_{0.2};
  double max_angular_velocity_{0.8};
  double command_timeout_{0.3};
  double scan_timeout_{0.5};
  double stop_distance_{0.45};
  double obstacle_half_angle_{0.52};
  bool require_scan_{false};
  double minimum_front_range_{std::numeric_limits<double>::infinity()};
  uint8_t active_severity_{vehicle_interfaces::msg::SafetyEvent::SEVERITY_INFO};
  std::string active_rule_;
  geometry_msgs::msg::Twist selected_command_;
  rclcpp::Time command_stamp_{0, 0, RCL_ROS_TIME};
  rclcpp::Time scan_stamp_{0, 0, RCL_ROS_TIME};
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr command_publisher_;
  rclcpp::Publisher<vehicle_interfaces::msg::SafetyEvent>::SharedPtr event_publisher_;
  rclcpp::Subscription<geometry_msgs::msg::TwistStamped>::SharedPtr command_subscription_;
  rclcpp::Subscription<sensor_msgs::msg::LaserScan>::SharedPtr scan_subscription_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<VlaSafetyGuard>());
  rclcpp::shutdown();
  return 0;
}
