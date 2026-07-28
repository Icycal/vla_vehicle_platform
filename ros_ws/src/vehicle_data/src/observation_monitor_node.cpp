#include <nav_msgs/msg/odometry.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/camera_info.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <vehicle_interfaces/msg/observation_status.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

class ObservationMonitor final : public rclcpp::Node
{
public:
  ObservationMonitor()
  : Node("observation_monitor")
  {
    camera_timeout_ = declare_parameter<double>("camera_timeout", 0.5);
    odometry_timeout_ = declare_parameter<double>("odometry_timeout", 0.5);
    imu_timeout_ = declare_parameter<double>("imu_timeout", 0.5);
    require_camera_ = declare_parameter<bool>("require_camera", true);
    require_calibration_ = declare_parameter<bool>("require_calibration", false);
    require_odometry_ = declare_parameter<bool>("require_odometry", false);
    require_imu_ = declare_parameter<bool>("require_imu", false);
    const double publish_frequency = declare_parameter<double>("publish_frequency", 2.0);

    publisher_ = create_publisher<vehicle_interfaces::msg::ObservationStatus>(
      "/vehicle/observation_status", rclcpp::QoS(1).reliable().transient_local());
    image_subscription_ = create_subscription<sensor_msgs::msg::Image>(
      "/camera/image_raw", rclcpp::SensorDataQoS(),
      [this](sensor_msgs::msg::Image::SharedPtr message) {
        camera_stamp_ = now();
        image_width_ = message->width;
        image_height_ = message->height;
      });
    camera_info_subscription_ = create_subscription<sensor_msgs::msg::CameraInfo>(
      "/camera/camera_info", rclcpp::SensorDataQoS(),
      [this](sensor_msgs::msg::CameraInfo::SharedPtr message) {
        camera_calibrated_ = message->k[0] > 0.0 && message->k[4] > 0.0;
      });
    odometry_subscription_ = create_subscription<nav_msgs::msg::Odometry>(
      "/odom", rclcpp::SensorDataQoS(),
      [this](nav_msgs::msg::Odometry::SharedPtr) {odometry_stamp_ = now();});
    imu_subscription_ = create_subscription<sensor_msgs::msg::Imu>(
      "/imu/data_raw", rclcpp::SensorDataQoS(),
      [this](sensor_msgs::msg::Imu::SharedPtr) {imu_stamp_ = now();});

    const auto period = std::chrono::duration<double>(
      1.0 / std::max(publish_frequency, 0.1));
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      std::bind(&ObservationMonitor::publish_status, this));
    publish_status();
  }

private:
  double age_seconds(const rclcpp::Time & stamp) const
  {
    if (stamp.nanoseconds() == 0) {
      return std::numeric_limits<double>::infinity();
    }
    return std::max(0.0, (now() - stamp).seconds());
  }

  static float finite_age(const double age)
  {
    return std::isfinite(age) ? static_cast<float>(age) : -1.0F;
  }

  void publish_status()
  {
    const double camera_age = age_seconds(camera_stamp_);
    const double odometry_age = age_seconds(odometry_stamp_);
    const double imu_age = age_seconds(imu_stamp_);
    const bool camera_ready = camera_age <= camera_timeout_;
    const bool odometry_ready = odometry_age <= odometry_timeout_;
    const bool imu_ready = imu_age <= imu_timeout_;

    std::vector<std::string> missing;
    if (require_camera_ && !camera_ready) {
      missing.emplace_back("camera");
    }
    if (require_calibration_ && !camera_calibrated_) {
      missing.emplace_back("camera_calibration");
    }
    if (require_odometry_ && !odometry_ready) {
      missing.emplace_back("odometry");
    }
    if (require_imu_ && !imu_ready) {
      missing.emplace_back("imu");
    }

    vehicle_interfaces::msg::ObservationStatus message;
    message.header.stamp = now();
    message.header.frame_id = "base_link";
    message.ready = missing.empty();
    message.camera_ready = camera_ready;
    message.camera_calibrated = camera_calibrated_;
    message.odometry_ready = odometry_ready;
    message.imu_ready = imu_ready;
    message.camera_age_seconds = finite_age(camera_age);
    message.odometry_age_seconds = finite_age(odometry_age);
    message.imu_age_seconds = finite_age(imu_age);
    message.image_width = image_width_;
    message.image_height = image_height_;
    if (missing.empty()) {
      message.message = "Observation inputs ready";
    } else {
      std::ostringstream stream;
      stream << "Waiting for";
      for (const auto & item : missing) {
        stream << ' ' << item;
      }
      message.message = stream.str();
    }
    publisher_->publish(message);
  }

  double camera_timeout_{0.5};
  double odometry_timeout_{0.5};
  double imu_timeout_{0.5};
  bool require_camera_{true};
  bool require_calibration_{false};
  bool require_odometry_{false};
  bool require_imu_{false};
  bool camera_calibrated_{false};
  uint32_t image_width_{0};
  uint32_t image_height_{0};
  rclcpp::Time camera_stamp_{0, 0, RCL_ROS_TIME};
  rclcpp::Time odometry_stamp_{0, 0, RCL_ROS_TIME};
  rclcpp::Time imu_stamp_{0, 0, RCL_ROS_TIME};
  rclcpp::Publisher<vehicle_interfaces::msg::ObservationStatus>::SharedPtr publisher_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr image_subscription_;
  rclcpp::Subscription<sensor_msgs::msg::CameraInfo>::SharedPtr camera_info_subscription_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odometry_subscription_;
  rclcpp::Subscription<sensor_msgs::msg::Imu>::SharedPtr imu_subscription_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<ObservationMonitor>());
  rclcpp::shutdown();
  return 0;
}
