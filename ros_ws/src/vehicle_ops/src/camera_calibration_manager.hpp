#pragma once

#include <sensor_msgs/msg/camera_info.hpp>
#include <sensor_msgs/msg/image.hpp>

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace vehicle_ops
{

class CameraCalibrationManager
{
public:
  struct Paths
  {
    std::filesystem::path calibration;
    std::filesystem::path metadata;
  };

  explicit CameraCalibrationManager(Paths paths);

  void update_frame(const sensor_msgs::msg::Image & message);
  void update_camera_info(const sensor_msgs::msg::CameraInfo & message);

  nlohmann::json status() const;
  nlohmann::json start(const nlohmann::json & input);
  nlohmann::json capture();
  nlohmann::json compute();
  nlohmann::json apply(const nlohmann::json & input);
  nlohmann::json reset();

  std::vector<std::uint8_t> preview_jpeg() const;
  std::vector<std::uint8_t> undistorted_jpeg() const;
  bool wait_for_verification(std::chrono::milliseconds timeout);

private:
  struct Sample
  {
    std::vector<cv::Point2f> corners;
    double center_x{0.0};
    double center_y{0.0};
    double area_ratio{0.0};
    double angle_degrees{0.0};
    double reprojection_error{0.0};
  };

  struct Session
  {
    bool active{false};
    std::string camera_name{"front_camera"};
    std::string camera_id;
    int board_columns{6};
    int board_rows{8};
    double square_size_m{0.025};
    int minimum_samples{12};
    int image_width{0};
    int image_height{0};
    std::vector<Sample> samples;
    bool result_ready{false};
    cv::Mat camera_matrix;
    cv::Mat distortion_coefficients;
    double rms_error{0.0};
    double mean_reprojection_error{0.0};
    std::string message{"尚未创建标定会话"};
  };

  static cv::Mat image_to_bgr(const sensor_msgs::msg::Image & message);
  static std::string timestamp_token();
  static std::string iso_timestamp();
  static void atomic_write(const std::filesystem::path & path, const std::string & content);
  static nlohmann::json sample_json(const Sample & sample);
  static nlohmann::json matrix_values(const cv::Mat & matrix);

  bool existing_calibration_valid() const;
  bool is_duplicate(const Sample & candidate) const;
  nlohmann::json coverage_json() const;
  nlohmann::json result_json() const;
  std::string calibration_yaml() const;
  nlohmann::json metadata_json() const;
  void clear_result();

  Paths paths_;
  mutable std::mutex mutex_;
  std::condition_variable verification_condition_;
  Session session_;
  cv::Mat latest_frame_;
  cv::Mat preview_frame_;
  cv::Mat undistorted_frame_;
  std::chrono::steady_clock::time_point latest_frame_time_{};
  std::chrono::steady_clock::time_point preview_frame_time_{};
  std::uint64_t latest_frame_sequence_{0};
  std::uint64_t camera_info_sequence_{0};
  std::uint64_t verification_start_sequence_{0};
  int camera_info_width_{0};
  int camera_info_height_{0};
  double camera_info_fx_{0.0};
  double camera_info_fy_{0.0};
  bool camera_info_calibrated_{false};
  bool verification_pending_{false};
  bool verified_{false};
};

}  // namespace vehicle_ops
