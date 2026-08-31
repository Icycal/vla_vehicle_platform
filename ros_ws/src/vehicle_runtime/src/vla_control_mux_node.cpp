#include <geometry_msgs/msg/twist.hpp>
#include <geometry_msgs/msg/twist_stamped.hpp>
#include <rclcpp/rclcpp.hpp>
#include <vehicle_interfaces/msg/control_lease.hpp>
#include <vehicle_interfaces/msg/system_state.hpp>

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>

class VlaControlMux final : public rclcpp::Node
{
public:
  VlaControlMux()
  : Node("vla_control_mux")
  {
    timeout_seconds_ = declare_parameter<double>("source_timeout", 0.3);
    const double publish_frequency = declare_parameter<double>("publish_frequency", 20.0);
    output_frame_ = declare_parameter<std::string>("output_frame", "base_link");
    const auto output_topic = declare_parameter<std::string>(
      "output_topic", "/control/cmd_vel_selected");
    const auto state_topic = declare_parameter<std::string>("state_topic", "/vehicle/system_state");
    const auto lease_topic = declare_parameter<std::string>(
      "lease_topic", "/vehicle/control_lease");
    const auto nav_topic = declare_parameter<std::string>("nav_topic", "/nav2/cmd_vel");
    const auto vla_topic = declare_parameter<std::string>("vla_topic", "/vla/cmd_vel_raw");
    const auto mobile_topic = declare_parameter<std::string>(
      "mobile_topic", "/mobile_teleop/cmd_vel");
    publisher_ = create_publisher<geometry_msgs::msg::TwistStamped>(output_topic, 10);
    state_subscription_ = create_subscription<vehicle_interfaces::msg::SystemState>(
      state_topic, rclcpp::QoS(1).reliable().transient_local(),
      [this](vehicle_interfaces::msg::SystemState::SharedPtr message) {mode_ = message->mode;});
    lease_subscription_ = create_subscription<vehicle_interfaces::msg::ControlLease>(
      lease_topic, rclcpp::QoS(1).reliable().transient_local(),
      [this](vehicle_interfaces::msg::ControlLease::SharedPtr message) {
        teleop_lease_active_ = message->active;
        teleop_lease_remote_ = message->remote;
        if (!message->active) {
          mobile_command_ = geometry_msgs::msg::Twist{};
          mobile_stamp_ = rclcpp::Time(0, 0, RCL_ROS_TIME);
        }
      });
    nav_subscription_ = create_subscription<geometry_msgs::msg::Twist>(
      nav_topic, 10,
      [this](geometry_msgs::msg::Twist::SharedPtr message) {
        nav_command_ = *message;
        nav_stamp_ = now();
      });
    vla_subscription_ = create_subscription<geometry_msgs::msg::TwistStamped>(
      vla_topic, 10,
      [this](geometry_msgs::msg::TwistStamped::SharedPtr message) {
        vla_command_ = message->twist;
        vla_stamp_ = message->header.stamp;
      });
    mobile_subscription_ = create_subscription<geometry_msgs::msg::TwistStamped>(
      mobile_topic, 10,
      [this](geometry_msgs::msg::TwistStamped::SharedPtr message) {
        mobile_command_ = message->twist;
        mobile_stamp_ = message->header.stamp;
      });
    const auto period = std::chrono::duration<double>(
      1.0 / std::max(publish_frequency, 1.0));
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      std::bind(&VlaControlMux::publish_selected, this));
  }

private:
  bool fresh(const rclcpp::Time & stamp) const
  {
    return stamp.nanoseconds() > 0 && (now() - stamp).seconds() <= timeout_seconds_;
  }

  void publish_selected()
  {
    geometry_msgs::msg::TwistStamped output;
    output.header.stamp = now();
    output.header.frame_id = output_frame_;
    if (mode_ == vehicle_interfaces::msg::SystemState::MODE_NAV2 && fresh(nav_stamp_)) {
      output.twist = nav_command_;
    } else if (
      (mode_ == vehicle_interfaces::msg::SystemState::MODE_VLA_ASSISTED ||
      mode_ == vehicle_interfaces::msg::SystemState::MODE_VLA_AUTONOMOUS) &&
      fresh(vla_stamp_))
    {
      output.twist = vla_command_;
    } else if (
      teleop_lease_active_ &&
      ((mode_ == vehicle_interfaces::msg::SystemState::MODE_MOBILE_TELEOP &&
      !teleop_lease_remote_) ||
      (mode_ == vehicle_interfaces::msg::SystemState::MODE_REMOTE_TELEOP &&
      teleop_lease_remote_)) && fresh(mobile_stamp_))
    {
      output.twist = mobile_command_;
    }
    publisher_->publish(output);
  }

  uint8_t mode_{vehicle_interfaces::msg::SystemState::MODE_MANUAL};
  double timeout_seconds_{0.3};
  std::string output_frame_{"base_link"};
  bool teleop_lease_active_{false};
  bool teleop_lease_remote_{false};
  geometry_msgs::msg::Twist nav_command_;
  geometry_msgs::msg::Twist vla_command_;
  geometry_msgs::msg::Twist mobile_command_;
  rclcpp::Time nav_stamp_{0, 0, RCL_ROS_TIME};
  rclcpp::Time vla_stamp_{0, 0, RCL_ROS_TIME};
  rclcpp::Time mobile_stamp_{0, 0, RCL_ROS_TIME};
  rclcpp::Publisher<geometry_msgs::msg::TwistStamped>::SharedPtr publisher_;
  rclcpp::Subscription<vehicle_interfaces::msg::SystemState>::SharedPtr state_subscription_;
  rclcpp::Subscription<vehicle_interfaces::msg::ControlLease>::SharedPtr lease_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr nav_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::TwistStamped>::SharedPtr vla_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::TwistStamped>::SharedPtr mobile_subscription_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<VlaControlMux>());
  rclcpp::shutdown();
  return 0;
}
