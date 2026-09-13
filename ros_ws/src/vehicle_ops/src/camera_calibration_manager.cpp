#include "camera_calibration_manager.hpp"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace vehicle_ops
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

std::vector<std::uint8_t> encode_jpeg(const cv::Mat & image)
{
  std::vector<std::uint8_t> output;
  if (!image.empty()) {
    cv::imencode(".jpg", image, output, {cv::IMWRITE_JPEG_QUALITY, 88});
  }
  return output;
}

std::vector<double> parse_yaml_data(const std::string & content, const std::string & section)
{
  const auto section_start = content.find(section + ":");
  if (section_start == std::string::npos) {
    return {};
  }
  const auto data_start = content.find("data:", section_start);
  const auto open = content.find('[', data_start);
  const auto close = content.find(']', open);
  if (data_start == std::string::npos || open == std::string::npos || close == std::string::npos) {
    return {};
  }
  std::vector<double> values;
  std::stringstream stream(content.substr(open + 1, close - open - 1));
  std::string token;
  while (std::getline(stream, token, ',')) {
    try {
      values.push_back(std::stod(token));
    } catch (...) {
      return {};
    }
  }
  return values;
}
}  // namespace

CameraCalibrationManager::CameraCalibrationManager(Paths paths)
: paths_(std::move(paths))
{
  if (paths_.calibration.empty() || paths_.metadata.empty()) {
    throw std::invalid_argument("Camera calibration paths must not be empty");
  }
}

cv::Mat CameraCalibrationManager::image_to_bgr(const sensor_msgs::msg::Image & message)
{
  if (message.width == 0 || message.height == 0 || message.data.empty()) {
    return {};
  }
  int channels = 0;
  int conversion = -1;
  if (message.encoding == "bgr8") {
    channels = 3;
  } else if (message.encoding == "rgb8") {
    channels = 3;
    conversion = cv::COLOR_RGB2BGR;
  } else if (message.encoding == "bgra8") {
    channels = 4;
    conversion = cv::COLOR_BGRA2BGR;
  } else if (message.encoding == "rgba8") {
    channels = 4;
    conversion = cv::COLOR_RGBA2BGR;
  } else if (message.encoding == "mono8") {
    channels = 1;
    conversion = cv::COLOR_GRAY2BGR;
  } else {
    return {};
  }
  const std::size_t minimum_step = static_cast<std::size_t>(message.width) * channels;
  if (message.step < minimum_step || message.data.size() < message.step * message.height) {
    return {};
  }
  const int type = channels == 1 ? CV_8UC1 : (channels == 3 ? CV_8UC3 : CV_8UC4);
  const cv::Mat source(
    static_cast<int>(message.height), static_cast<int>(message.width), type,
    const_cast<unsigned char *>(message.data.data()), message.step);
  if (conversion < 0) {
    return source.clone();
  }
  cv::Mat converted;
  cv::cvtColor(source, converted, conversion);
  return converted;
}

void CameraCalibrationManager::update_frame(const sensor_msgs::msg::Image & message)
{
  cv::Mat frame = image_to_bgr(message);
  if (frame.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  latest_frame_ = std::move(frame);
  latest_frame_time_ = std::chrono::steady_clock::now();
  ++latest_frame_sequence_;

}

void CameraCalibrationManager::update_camera_info(const sensor_msgs::msg::CameraInfo & message)
{
  std::lock_guard<std::mutex> lock(mutex_);
  camera_info_width_ = static_cast<int>(message.width);
  camera_info_height_ = static_cast<int>(message.height);
  camera_info_fx_ = message.k[0];
  camera_info_fy_ = message.k[4];
  camera_info_calibrated_ = camera_info_fx_ > 0.0 && camera_info_fy_ > 0.0;
  ++camera_info_sequence_;
  if (verification_pending_ && camera_info_sequence_ > verification_start_sequence_ &&
    camera_info_calibrated_ && camera_info_width_ == session_.image_width &&
    camera_info_height_ == session_.image_height)
  {
    verified_ = true;
    verification_pending_ = false;
    session_.message = "标定文件已应用，CameraInfo 验证通过";
    verification_condition_.notify_all();
  }
}

nlohmann::json CameraCalibrationManager::status() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  const auto now = std::chrono::steady_clock::now();
  const double frame_age_ms = latest_frame_time_.time_since_epoch().count() == 0 ? -1.0 :
    std::chrono::duration<double, std::milli>(now - latest_frame_time_).count();
  return {
    {"schema_version", "chitu.camera-calibration.v1"},
    {"session", {
      {"active", session_.active}, {"camera_name", session_.camera_name},
      {"camera_id", session_.camera_id}, {"board_columns", session_.board_columns},
      {"board_rows", session_.board_rows}, {"square_size_m", session_.square_size_m},
      {"minimum_samples", session_.minimum_samples}, {"sample_count", session_.samples.size()},
      {"image_width", session_.image_width}, {"image_height", session_.image_height},
      {"result_ready", session_.result_ready}, {"message", session_.message}
    }},
    {"camera", {
      {"frame_available", !latest_frame_.empty()}, {"frame_sequence", latest_frame_sequence_},
      {"frame_age_ms", frame_age_ms}, {"width", latest_frame_.cols}, {"height", latest_frame_.rows},
      {"camera_info_received", camera_info_sequence_ > 0},
      {"camera_info_calibrated", camera_info_calibrated_},
      {"camera_info_width", camera_info_width_}, {"camera_info_height", camera_info_height_},
      {"fx", camera_info_fx_}, {"fy", camera_info_fy_}
    }},
    {"storage", {
      {"calibration_path", paths_.calibration.string()},
      {"metadata_path", paths_.metadata.string()},
      {"calibration_file_valid", existing_calibration_valid()}
    }},
    {"coverage", coverage_json()}, {"result", result_json()},
    {"verification", {{"pending", verification_pending_}, {"verified", verified_}}}
  };
}

nlohmann::json CameraCalibrationManager::start(const nlohmann::json & input)
{
  Session next;
  next.camera_name = input.value("camera_name", "front_camera");
  next.camera_id = input.value("camera_id", "");
  next.board_columns = input.value("board_columns", 6);
  next.board_rows = input.value("board_rows", 8);
  next.square_size_m = input.value("square_size_m", 0.0);
  next.minimum_samples = input.value("minimum_samples", 12);
  if (next.camera_name.empty() || next.camera_name.size() > 64) {
    throw std::invalid_argument("相机名称不能为空且不能超过 64 个字符");
  }
  if (next.board_columns < 3 || next.board_columns > 20 ||
    next.board_rows < 3 || next.board_rows > 20)
  {
    throw std::invalid_argument("内部角点行列数必须在 3 到 20 之间");
  }
  if (!std::isfinite(next.square_size_m) || next.square_size_m < 0.001 ||
    next.square_size_m > 1.0)
  {
    throw std::invalid_argument("请填写实测单格边长，单位米，范围 0.001 到 1.0");
  }
  if (next.minimum_samples < 8 || next.minimum_samples > 60) {
    throw std::invalid_argument("最小样本数必须在 8 到 60 之间");
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (latest_frame_.empty()) {
    throw std::runtime_error("尚未收到 /camera/image_raw，请先启动前视相机组件");
  }
  next.active = true;
  next.image_width = latest_frame_.cols;
  next.image_height = latest_frame_.rows;
  next.message = "标定会话已创建，请从不同位置、距离和倾角采集棋盘格";
  session_ = std::move(next);
  preview_frame_.release();
  preview_frame_time_ = {};
  undistorted_frame_.release();
  verification_pending_ = false;
  verified_ = false;
  return {{"message", session_.message}};
}

bool CameraCalibrationManager::is_duplicate(const Sample & candidate) const
{
  return std::any_of(session_.samples.begin(), session_.samples.end(), [&](const Sample & sample) {
    return std::hypot(sample.center_x - candidate.center_x, sample.center_y - candidate.center_y) < 0.075 &&
      std::abs(sample.area_ratio - candidate.area_ratio) < 0.025 &&
      std::abs(sample.angle_degrees - candidate.angle_degrees) < 8.0;
  });
}

nlohmann::json CameraCalibrationManager::capture()
{
  cv::Mat frame;
  cv::Size board_size;
  int board_columns = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!session_.active) {
      throw std::runtime_error("请先创建标定会话");
    }
    if (latest_frame_.empty()) {
      throw std::runtime_error("当前没有可用相机图像");
    }
    if (latest_frame_.cols != session_.image_width || latest_frame_.rows != session_.image_height) {
      throw std::runtime_error("相机分辨率在标定过程中发生变化，请重置会话后重新采集");
    }
    frame = latest_frame_.clone();
    board_size = cv::Size(session_.board_columns, session_.board_rows);
    board_columns = session_.board_columns;
  }

  cv::Mat gray;
  cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
  std::vector<cv::Point2f> corners;
  bool found = cv::findChessboardCornersSB(
    gray, board_size, corners,
    cv::CALIB_CB_NORMALIZE_IMAGE | cv::CALIB_CB_EXHAUSTIVE | cv::CALIB_CB_ACCURACY);
  if (!found) {
    found = cv::findChessboardCorners(
      gray, board_size, corners,
      cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE | cv::CALIB_CB_FAST_CHECK);
    if (found) {
      cv::cornerSubPix(
        gray, corners, cv::Size(11, 11), cv::Size(-1, -1),
        cv::TermCriteria(cv::TermCriteria::EPS | cv::TermCriteria::COUNT, 30, 0.001));
    }
  }
  cv::drawChessboardCorners(frame, board_size, corners, found);
  if (!found || corners.size() != static_cast<std::size_t>(board_size.area())) {
    std::lock_guard<std::mutex> lock(mutex_);
    preview_frame_ = std::move(frame);
    preview_frame_time_ = std::chrono::steady_clock::now();
    session_.message = "未检测到完整棋盘格，请检查角点行列、光照和棋盘是否完整入镜";
    throw std::runtime_error(session_.message);
  }

  const auto bounds = cv::boundingRect(corners);
  const auto center = std::accumulate(
    corners.begin(), corners.end(), cv::Point2f{},
    [](const cv::Point2f & sum, const cv::Point2f & point) {return sum + point;}) /
    static_cast<float>(corners.size());
  const cv::Point2f direction = corners[board_columns - 1] - corners[0];
  Sample candidate;
  candidate.corners = std::move(corners);
  candidate.center_x = center.x / static_cast<double>(frame.cols);
  candidate.center_y = center.y / static_cast<double>(frame.rows);
  candidate.area_ratio = static_cast<double>(bounds.area()) /
    static_cast<double>(frame.cols * frame.rows);
  candidate.angle_degrees = std::atan2(direction.y, direction.x) * 180.0 / kPi;

  std::lock_guard<std::mutex> lock(mutex_);
  preview_frame_ = std::move(frame);
  preview_frame_time_ = std::chrono::steady_clock::now();
  if (is_duplicate(candidate)) {
    session_.message = "该姿态与已有样本过于接近，请移动棋盘位置、改变距离或增加倾角";
    throw std::runtime_error(session_.message);
  }
  session_.samples.push_back(std::move(candidate));
  clear_result();
  session_.message = "样本采集成功";
  return {
    {"message", session_.message}, {"sample_count", session_.samples.size()},
    {"coverage", coverage_json()}, {"sample", sample_json(session_.samples.back())}
  };
}

nlohmann::json CameraCalibrationManager::compute()
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!session_.active) {
    throw std::runtime_error("请先创建标定会话");
  }
  if (session_.samples.size() < static_cast<std::size_t>(session_.minimum_samples)) {
    throw std::runtime_error(
            "有效样本不足，至少需要 " + std::to_string(session_.minimum_samples) + " 组");
  }
  std::vector<cv::Point3f> board_points;
  board_points.reserve(session_.board_columns * session_.board_rows);
  for (int row = 0; row < session_.board_rows; ++row) {
    for (int column = 0; column < session_.board_columns; ++column) {
      board_points.emplace_back(
        static_cast<float>(column * session_.square_size_m),
        static_cast<float>(row * session_.square_size_m), 0.0F);
    }
  }
  std::vector<std::vector<cv::Point3f>> object_points(session_.samples.size(), board_points);
  std::vector<std::vector<cv::Point2f>> image_points;
  image_points.reserve(session_.samples.size());
  for (const auto & sample : session_.samples) {
    image_points.push_back(sample.corners);
  }
  std::vector<cv::Mat> rotation_vectors;
  std::vector<cv::Mat> translation_vectors;
  session_.camera_matrix = cv::Mat::eye(3, 3, CV_64F);
  session_.distortion_coefficients = cv::Mat::zeros(8, 1, CV_64F);
  session_.rms_error = cv::calibrateCamera(
    object_points, image_points, cv::Size(session_.image_width, session_.image_height),
    session_.camera_matrix, session_.distortion_coefficients,
    rotation_vectors, translation_vectors, 0,
    cv::TermCriteria(cv::TermCriteria::EPS | cv::TermCriteria::COUNT, 100, 1e-9));
  double total_squared_error = 0.0;
  std::size_t total_points = 0;
  for (std::size_t index = 0; index < session_.samples.size(); ++index) {
    std::vector<cv::Point2f> projected;
    cv::projectPoints(
      object_points[index], rotation_vectors[index], translation_vectors[index],
      session_.camera_matrix, session_.distortion_coefficients, projected);
    const double error = cv::norm(image_points[index], projected, cv::NORM_L2);
    session_.samples[index].reprojection_error =
      std::sqrt((error * error) / static_cast<double>(projected.size()));
    total_squared_error += error * error;
    total_points += projected.size();
  }
  session_.mean_reprojection_error = std::sqrt(total_squared_error / total_points);
  session_.result_ready = std::isfinite(session_.rms_error) &&
    session_.camera_matrix.at<double>(0, 0) > 0.0 &&
    session_.camera_matrix.at<double>(1, 1) > 0.0;
  if (!session_.result_ready) {
    throw std::runtime_error("OpenCV 未能得到有效内参，请重置后重新采集覆盖更充分的样本");
  }
  if (!latest_frame_.empty()) {
    cv::undistort(
      latest_frame_, undistorted_frame_, session_.camera_matrix,
      session_.distortion_coefficients);
  }
  session_.message = session_.mean_reprojection_error <= 1.0 ?
    "标定计算完成，请检查误差和去畸变预览后再应用" :
    "标定计算完成，但重投影误差偏高，建议补充样本或重新标定";
  return {{"message", session_.message}, {"result", result_json()}};
}

nlohmann::json CameraCalibrationManager::apply(const nlohmann::json & input)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!session_.result_ready) {
    throw std::runtime_error("请先完成标定计算");
  }
  if (!input.value("confirmed", false)) {
    throw std::invalid_argument("应用标定前必须确认已检查误差和去畸变预览");
  }
  if (session_.camera_name != "front_camera") {
    throw std::invalid_argument("当前前视相机配置要求 camera_name 为 front_camera");
  }
  std::filesystem::create_directories(paths_.calibration.parent_path());
  std::filesystem::create_directories(paths_.metadata.parent_path());
  std::string backup_path;
  if (std::filesystem::exists(paths_.calibration)) {
    const auto backup = paths_.calibration.string() + ".backup-" + timestamp_token();
    std::filesystem::copy_file(
      paths_.calibration, backup, std::filesystem::copy_options::overwrite_existing);
    backup_path = backup;
  }
  atomic_write(paths_.calibration, calibration_yaml());
  atomic_write(paths_.metadata, metadata_json().dump(2) + "\n");
  verification_pending_ = true;
  verified_ = false;
  verification_start_sequence_ = camera_info_sequence_;
  session_.message = "标定文件已保存，等待重启相机并验证 CameraInfo";
  return {
    {"message", session_.message}, {"calibration_path", paths_.calibration.string()},
    {"metadata_path", paths_.metadata.string()}, {"backup_path", backup_path},
    {"verification_pending", true}
  };
}

nlohmann::json CameraCalibrationManager::reset()
{
  std::lock_guard<std::mutex> lock(mutex_);
  session_ = Session{};
  preview_frame_.release();
  preview_frame_time_ = {};
  undistorted_frame_.release();
  verification_pending_ = false;
  verified_ = false;
  return {{"message", "标定会话已重置"}};
}

std::vector<std::uint8_t> CameraCalibrationManager::preview_jpeg() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  const bool overlay_fresh = !preview_frame_.empty() &&
    preview_frame_time_.time_since_epoch().count() != 0 &&
    std::chrono::steady_clock::now() - preview_frame_time_ < std::chrono::milliseconds(1500);
  return encode_jpeg(overlay_fresh ? preview_frame_ : latest_frame_);
}

std::vector<std::uint8_t> CameraCalibrationManager::undistorted_jpeg() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return encode_jpeg(undistorted_frame_);
}

bool CameraCalibrationManager::wait_for_verification(std::chrono::milliseconds timeout)
{
  std::unique_lock<std::mutex> lock(mutex_);
  verification_condition_.wait_for(lock, timeout, [this]() {return verified_;});
  return verified_;
}

bool CameraCalibrationManager::existing_calibration_valid() const
{
  if (!std::filesystem::is_regular_file(paths_.calibration)) {
    return false;
  }
  std::ifstream stream(paths_.calibration);
  const std::string content(
    (std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  const auto values = parse_yaml_data(content, "camera_matrix");
  return values.size() == 9 && values[0] > 0.0 && values[4] > 0.0;
}

nlohmann::json CameraCalibrationManager::coverage_json() const
{
  std::array<bool, 3> horizontal{false, false, false};
  std::array<bool, 3> vertical{false, false, false};
  bool near = false;
  bool far = false;
  bool tilted = false;
  for (const auto & sample : session_.samples) {
    horizontal[std::min(2, std::max(0, static_cast<int>(sample.center_x * 3.0)))] = true;
    vertical[std::min(2, std::max(0, static_cast<int>(sample.center_y * 3.0)))] = true;
    near = near || sample.area_ratio >= 0.22;
    far = far || sample.area_ratio <= 0.10;
    tilted = tilted || std::abs(sample.angle_degrees) >= 12.0;
  }
  const auto count_true = [](const auto & values) {
      return std::count(values.begin(), values.end(), true);
    };
  const int score = count_true(horizontal) + count_true(vertical) + (near ? 1 : 0) +
    (far ? 1 : 0) + (tilted ? 1 : 0);
  return {
    {"horizontal", horizontal}, {"vertical", vertical},
    {"near", near}, {"far", far}, {"tilted", tilted},
    {"score", score}, {"maximum_score", 9}, {"sufficient", score >= 7}
  };
}

nlohmann::json CameraCalibrationManager::result_json() const
{
  nlohmann::json samples = nlohmann::json::array();
  for (std::size_t index = 0; index < session_.samples.size(); ++index) {
    auto item = sample_json(session_.samples[index]);
    item["index"] = index + 1;
    samples.push_back(std::move(item));
  }
  if (!session_.result_ready) {
    return {{"ready", false}, {"samples", samples}};
  }
  return {
    {"ready", true}, {"rms_error", session_.rms_error},
    {"mean_reprojection_error", session_.mean_reprojection_error},
    {"camera_matrix", matrix_values(session_.camera_matrix)},
    {"distortion_coefficients", matrix_values(session_.distortion_coefficients)},
    {"samples", samples}
  };
}

nlohmann::json CameraCalibrationManager::sample_json(const Sample & sample)
{
  return {
    {"center_x", sample.center_x}, {"center_y", sample.center_y},
    {"area_ratio", sample.area_ratio}, {"angle_degrees", sample.angle_degrees},
    {"reprojection_error", sample.reprojection_error}
  };
}

nlohmann::json CameraCalibrationManager::matrix_values(const cv::Mat & matrix)
{
  nlohmann::json values = nlohmann::json::array();
  const cv::Mat flattened = matrix.reshape(1, 1);
  for (int column = 0; column < flattened.cols; ++column) {
    values.push_back(flattened.at<double>(0, column));
  }
  return values;
}

std::string CameraCalibrationManager::calibration_yaml() const
{
  const auto value = [&](int row, int column) {
      return session_.camera_matrix.at<double>(row, column);
    };
  const cv::Mat distortion = session_.distortion_coefficients.reshape(1, 1);
  std::ostringstream output;
  output << std::setprecision(15);
  output << "image_width: " << session_.image_width << "\n";
  output << "image_height: " << session_.image_height << "\n";
  output << "camera_name: " << session_.camera_name << "\n";
  output << "camera_matrix:\n  rows: 3\n  cols: 3\n  data: [";
  output << value(0, 0) << ", 0.0, " << value(0, 2) << ", 0.0, " << value(1, 1) <<
    ", " << value(1, 2) << ", 0.0, 0.0, 1.0]\n";
  output << "distortion_model: plumb_bob\n";
  output << "distortion_coefficients:\n  rows: 1\n  cols: 5\n  data: [";
  for (int index = 0; index < 5; ++index) {
    if (index > 0) {
      output << ", ";
    }
    output << distortion.at<double>(0, index);
  }
  output << "]\n";
  output << "rectification_matrix:\n  rows: 3\n  cols: 3\n";
  output << "  data: [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0]\n";
  output << "projection_matrix:\n  rows: 3\n  cols: 4\n  data: [";
  output << value(0, 0) << ", 0.0, " << value(0, 2) << ", 0.0, 0.0, " <<
    value(1, 1) << ", " << value(1, 2) << ", 0.0, 0.0, 0.0, 1.0, 0.0]\n";
  return output.str();
}

nlohmann::json CameraCalibrationManager::metadata_json() const
{
  return {
    {"schema_version", "chitu.camera-calibration-metadata.v1"},
    {"calibrated_at", iso_timestamp()}, {"camera_name", session_.camera_name},
    {"camera_id", session_.camera_id}, {"image_width", session_.image_width},
    {"image_height", session_.image_height}, {"board_columns", session_.board_columns},
    {"board_rows", session_.board_rows}, {"square_size_m", session_.square_size_m},
    {"sample_count", session_.samples.size()}, {"rms_error", session_.rms_error},
    {"mean_reprojection_error", session_.mean_reprojection_error},
    {"distortion_model", "plumb_bob"}
  };
}

void CameraCalibrationManager::clear_result()
{
  session_.result_ready = false;
  session_.camera_matrix.release();
  session_.distortion_coefficients.release();
  session_.rms_error = 0.0;
  session_.mean_reprojection_error = 0.0;
  undistorted_frame_.release();
}

std::string CameraCalibrationManager::timestamp_token()
{
  const auto now = std::chrono::system_clock::now();
  const std::time_t value = std::chrono::system_clock::to_time_t(now);
  std::tm local{};
  localtime_r(&value, &local);
  std::ostringstream output;
  output << std::put_time(&local, "%Y%m%d-%H%M%S");
  return output.str();
}

std::string CameraCalibrationManager::iso_timestamp()
{
  const auto now = std::chrono::system_clock::now();
  const std::time_t value = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
  gmtime_r(&value, &utc);
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

void CameraCalibrationManager::atomic_write(
  const std::filesystem::path & path, const std::string & content)
{
  const auto temporary = path.string() + ".tmp-" + timestamp_token();
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
      throw std::runtime_error("无法创建临时标定文件: " + temporary);
    }
    stream << content;
    stream.flush();
    if (!stream) {
      throw std::runtime_error("写入临时标定文件失败: " + temporary);
    }
  }
  std::filesystem::rename(temporary, path);
}

}  // namespace vehicle_ops
