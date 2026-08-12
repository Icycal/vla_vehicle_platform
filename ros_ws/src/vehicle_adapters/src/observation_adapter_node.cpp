#include <geometry_msgs/msg/twist.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <std_msgs/msg/float32.hpp>
#include <std_msgs/msg/string.hpp>
#include <vehicle_interfaces/msg/observation_status.hpp>
#include <vehicle_interfaces/msg/policy_observation.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class ObservationAdapter final : public rclcpp::Node
{
public:
  ObservationAdapter()
  : Node("observation_adapter")
  {
    publish_frequency_ = declare_parameter<double>("publish_frequency", 5.0);
    image_timeout_ = declare_parameter<double>("image_timeout", 0.5);
    observation_validity_ = declare_parameter<double>("observation_validity", 0.5);
    require_status_ready_ = declare_parameter<bool>("require_status_ready", true);

    publisher_ = create_publisher<vehicle_interfaces::msg::PolicyObservation>(
      "/vla/observation", rclcpp::QoS(2).reliable());
    image_subscription_ = create_subscription<sensor_msgs::msg::CompressedImage>(
      "/camera/image_compressed", rclcpp::QoS(2).best_effort(),
      [this](sensor_msgs::msg::CompressedImage::SharedPtr message) {
        image_ = std::move(message);
        image_received_at_ = now();
      });
    status_subscription_ = create_subscription<vehicle_interfaces::msg::ObservationStatus>(
      "/vehicle/observation_status", rclcpp::QoS(1).reliable().transient_local(),
      [this](vehicle_interfaces::msg::ObservationStatus::SharedPtr message) {
        observation_ready_ = message->ready;
      });
    odometry_subscription_ = create_subscription<nav_msgs::msg::Odometry>(
      "/odom", rclcpp::SensorDataQoS(),
      [this](nav_msgs::msg::Odometry::SharedPtr message) {
        odometry_ = *message;
        odometry_valid_ = true;
      });
    imu_subscription_ = create_subscription<sensor_msgs::msg::Imu>(
      "/imu/data_raw", rclcpp::SensorDataQoS(),
      [this](sensor_msgs::msg::Imu::SharedPtr message) {
        imu_ = *message;
        imu_valid_ = true;
      });
    command_subscription_ = create_subscription<geometry_msgs::msg::Twist>(
      "/cmd_vel", 10,
      [this](geometry_msgs::msg::Twist::SharedPtr message) {
        command_ = *message;
        command_valid_ = true;
      });
    voltage_subscription_ = create_subscription<std_msgs::msg::Float32>(
      "/PowerVoltage", 10,
      [this](std_msgs::msg::Float32::SharedPtr message) {
        voltage_ = message->data;
        voltage_valid_ = true;
      });
    task_subscription_ = create_subscription<std_msgs::msg::String>(
      "/vla/task", rclcpp::QoS(1).reliable().transient_local(),
      [this](std_msgs::msg::String::SharedPtr message) {task_ = message->data;});

    const auto period = std::chrono::duration<double>(
      1.0 / std::max(publish_frequency_, 0.1));
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      std::bind(&ObservationAdapter::publish_observation, this));
  }

private:
  void append_state(
    vehicle_interfaces::msg::PolicyObservation & message,
    const std::string & key, const double value, const bool valid) const
  {
    message.state_keys.push_back(key);
    message.state.push_back(valid ? static_cast<float>(value) : 0.0F);
    message.state_valid.push_back(valid);
  }

  void publish_observation()
  {
    if (!image_ || (now() - image_received_at_).seconds() > image_timeout_) {
      return;
    }
    if (require_status_ready_ && !observation_ready_) {
      return;
    }

    const auto stamp = now();
    vehicle_interfaces::msg::PolicyObservation message;
    message.header.stamp = stamp;
    message.header.frame_id = "base_link";
    message.observation_id = "obs-" + std::to_string(++sequence_);
    message.schema_version = "vehicle.observation.v1";
    message.task = task_;
    message.image_keys.emplace_back("observation.images.front");
    message.images.push_back(*image_);
    append_state(
      message, "observation.state.linear_velocity",
      odometry_.twist.twist.linear.x, odometry_valid_);
    append_state(
      message, "observation.state.angular_velocity",
      odometry_.twist.twist.angular.z, odometry_valid_);
    append_state(
      message, "observation.state.acceleration_x",
      imu_.linear_acceleration.x, imu_valid_);
    append_state(
      message, "observation.state.acceleration_y",
      imu_.linear_acceleration.y, imu_valid_);
    append_state(
      message, "observation.state.yaw_rate",
      imu_.angular_velocity.z, imu_valid_);
    append_state(
      message, "observation.state.executed_linear_command",
      command_.linear.x, command_valid_);
    append_state(
      message, "observation.state.executed_angular_command",
      command_.angular.z, command_valid_);
    append_state(
      message, "observation.state.battery_voltage", voltage_, voltage_valid_);
    message.generated_at = stamp;
    message.valid_until = stamp + rclcpp::Duration::from_seconds(observation_validity_);
    publisher_->publish(message);
  }

  double publish_frequency_{5.0};
  double image_timeout_{0.5};
  double observation_validity_{0.5};
  bool require_status_ready_{true};
  bool observation_ready_{false};
  bool odometry_valid_{false};
  bool imu_valid_{false};
  bool command_valid_{false};
  bool voltage_valid_{false};
  float voltage_{0.0F};
  uint64_t sequence_{0};
  std::string task_;
  geometry_msgs::msg::Twist command_;
  nav_msgs::msg::Odometry odometry_;
  sensor_msgs::msg::Imu imu_;
  sensor_msgs::msg::CompressedImage::SharedPtr image_;
  rclcpp::Time image_received_at_{0, 0, RCL_ROS_TIME};
  rclcpp::Publisher<vehicle_interfaces::msg::PolicyObservation>::SharedPtr publisher_;
  rclcpp::Subscription<sensor_msgs::msg::CompressedImage>::SharedPtr image_subscription_;
  rclcpp::Subscription<vehicle_interfaces::msg::ObservationStatus>::SharedPtr status_subscription_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odometry_subscription_;
  rclcpp::Subscription<sensor_msgs::msg::Imu>::SharedPtr imu_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr command_subscription_;
  rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr voltage_subscription_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr task_subscription_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<ObservationAdapter>());
  rclcpp::shutdown();
  return 0;
}
