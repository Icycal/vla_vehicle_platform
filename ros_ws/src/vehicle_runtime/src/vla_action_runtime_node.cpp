#include <geometry_msgs/msg/twist_stamped.hpp>
#include <rclcpp/rclcpp.hpp>
#include <vehicle_interfaces/msg/policy_action.hpp>

#include <algorithm>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

class VlaActionRuntime final : public rclcpp::Node
{
public:
  VlaActionRuntime()
  : Node("vla_action_runtime")
  {
    const double execution_frequency = declare_parameter<double>("execution_frequency", 20.0);
    publisher_ = create_publisher<geometry_msgs::msg::TwistStamped>("/vla/cmd_vel_raw", 10);
    subscription_ = create_subscription<vehicle_interfaces::msg::PolicyAction>(
      "/vla/policy_action", 10,
      std::bind(&VlaActionRuntime::on_action, this, std::placeholders::_1));
    const auto period = std::chrono::duration<double>(
      1.0 / std::max(execution_frequency, 1.0));
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      std::bind(&VlaActionRuntime::execute, this));
  }

private:
  void on_action(const vehicle_interfaces::msg::PolicyAction::SharedPtr message)
  {
    if (message->schema_version != "vehicle.twist_chunk.v1") {
      RCLCPP_WARN(get_logger(), "Rejected action schema: %s", message->schema_version.c_str());
      return;
    }
    const rclcpp::Time message_stamp(message->generated_at);
    std::lock_guard<std::mutex> lock(mutex_);
    if (message_stamp < last_generated_at_) {
      RCLCPP_WARN(get_logger(), "Rejected out-of-order policy action");
      return;
    }
    actions_.assign(message->actions.begin(), message->actions.end());
    valid_until_ = rclcpp::Time(message->valid_until);
    last_generated_at_ = message_stamp;
    request_id_ = message->request_id;
  }

  void execute()
  {
    geometry_msgs::msg::TwistStamped output;
    const auto current_time = now();
    output.header.stamp = current_time;
    output.header.frame_id = "base_link";
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (current_time <= valid_until_ && !actions_.empty()) {
        output.twist = actions_.front();
        actions_.pop_front();
      } else {
        actions_.clear();
      }
    }
    publisher_->publish(output);
  }

  std::mutex mutex_;
  std::deque<geometry_msgs::msg::Twist> actions_;
  rclcpp::Time valid_until_{0, 0, RCL_ROS_TIME};
  rclcpp::Time last_generated_at_{0, 0, RCL_ROS_TIME};
  std::string request_id_;
  rclcpp::Publisher<geometry_msgs::msg::TwistStamped>::SharedPtr publisher_;
  rclcpp::Subscription<vehicle_interfaces::msg::PolicyAction>::SharedPtr subscription_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<VlaActionRuntime>());
  rclcpp::shutdown();
  return 0;
}
