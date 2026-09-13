#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <sensor_msgs/msg/camera_info.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/string.hpp>
#include <vehicle_interfaces/action/acquire_control_lease.hpp>
#include <vehicle_interfaces/msg/control_lease.hpp>
#include <vehicle_interfaces/msg/episode_state.hpp>
#include <vehicle_interfaces/msg/observation_status.hpp>
#include <vehicle_interfaces/msg/pipeline_trace.hpp>
#include <vehicle_interfaces/msg/policy_status.hpp>
#include <vehicle_interfaces/msg/safety_event.hpp>
#include <vehicle_interfaces/msg/shadow_metrics.hpp>
#include <vehicle_interfaces/msg/system_state.hpp>
#include <vehicle_interfaces/msg/teleop_command.hpp>
#include <vehicle_interfaces/srv/release_control_lease.hpp>
#include <vehicle_interfaces/srv/request_safe_stop.hpp>
#include <vehicle_interfaces/srv/request_control_mode.hpp>
#include <vehicle_interfaces/srv/start_episode.hpp>
#include <vehicle_interfaces/srv/stop_episode.hpp>
#include <vehicle_interfaces/srv/capture_vla_debug.hpp>
#include <vehicle_interfaces/srv/control_component.hpp>
#include <vehicle_interfaces/srv/control_profile.hpp>
#include <vehicle_interfaces/srv/read_component_log.hpp>
#include <vehicle_interfaces/srv/list_components.hpp>
#include <vehicle_interfaces/srv/run_vla_debug.hpp>
#include <vehicle_interfaces/srv/get_storage_status.hpp>
#include <vehicle_interfaces/srv/list_storage_items.hpp>
#include <vehicle_interfaces/srv/cleanup_storage.hpp>
#include <nlohmann/json.hpp>
#include "camera_calibration_manager.hpp"
#include "job_manager.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
using namespace std::chrono_literals;
namespace {
struct HttpRequest {
  std::string method;
  std::string target;
  std::unordered_map<std::string, std::string> headers;
  std::string body;
};
struct HttpResponse {
  int status{200};
  std::string content_type{"application/json; charset=utf-8"};
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;
};
std::string lowercase(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}
std::string trim(const std::string & value)
{
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {return {};}
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}
nlohmann::json read_json_file(const std::filesystem::path & path)
{
  std::ifstream stream(path);
  if (!stream) {throw std::runtime_error("Unable to read JSON file: " + path.string());}
  return nlohmann::json::parse(stream);
}
std::vector<nlohmann::json> load_mobility_plugins(const std::filesystem::path & project_root)
{
  std::vector<nlohmann::json> plugins;
  const auto root = project_root / "config/mobility/plugins";
  if (!std::filesystem::is_directory(root)) {return plugins;}
  for (const auto & entry : std::filesystem::directory_iterator(root)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".json") {continue;}
    try {
      auto plugin = read_json_file(entry.path());
      if (plugin.value("schema_version", "") != "chitu.mobility-plugin.v1") {continue;}
      plugin["manifest_path"] = entry.path().filename().string();
      plugins.push_back(std::move(plugin));
    } catch (const std::exception &) {
    }
  }
  std::sort(plugins.begin(), plugins.end(), [](const auto & left, const auto & right) {
    return left.value("id", "") < right.value("id", "");
  });
  return plugins;
}
bool mobility_plugin_accepts(const nlohmann::json & plugin, const std::string & schema)
{
  if (!plugin.contains("accepts") || !plugin["accepts"].is_array()) {return false;}
  return std::any_of(plugin["accepts"].begin(), plugin["accepts"].end(), [&](const auto & item) {
    return item.is_object() && item.value("schema", "") == schema;
  });
}
std::vector<std::string> recommended_mobility_plugins(
  const std::vector<nlohmann::json> & plugins, const std::string & schema)
{
  std::vector<std::string> result;
  for (const auto & plugin : plugins) {
    if (mobility_plugin_accepts(plugin, schema)) {result.push_back(plugin.value("id", ""));}
  }
  return result;
}
std::vector<uint8_t> decode_base64(const std::string & encoded)
{
  static const std::string alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if (encoded.size() > 900000) {throw std::length_error("Uploaded image is too large");}
  std::vector<uint8_t> output;
  output.reserve(encoded.size() * 3 / 4);
  uint32_t accumulator = 0;
  int bits = 0;
  for (const unsigned char character : encoded) {
    if (std::isspace(character)) {continue;}
    if (character == '=') {break;}
    const auto position = alphabet.find(static_cast<char>(character));
    if (position == std::string::npos) {throw std::invalid_argument("Invalid image encoding");}
    accumulator = (accumulator << 6) | static_cast<uint32_t>(position);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      output.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xff));
      accumulator &= bits == 0 ? 0u : ((1u << bits) - 1u);
    }
  }
  if (output.size() > 700000) {throw std::length_error("Uploaded image is too large");}
  return output;
}
bool is_jpeg(const std::vector<uint8_t> & data)
{
  return data.size() >= 4 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff;
}
std::string json_escape(const std::string & value)
{
  std::ostringstream output;
  for (unsigned char c : value) {
    switch (c) {
      case '"': output << "\\\""; break;
      case '\\': output << "\\\\"; break;
      case '\n': output << "\\n"; break;
      case '\r': output << "\\r"; break;
      case '\t': output << "\\t"; break;
      default: output << c;
    }
  }
  return output.str();
}
std::string json_string(const std::string & value) {return "\"" + json_escape(value) + "\"";}std::string url_decode(std::string value)
{
  std::string output;
  output.reserve(value.size());
  for (std::size_t index = 0; index < value.size(); ++index) {
    if (value[index] == '%' && index + 2 < value.size()) {
      const auto hex = value.substr(index + 1, 2);
      try { output.push_back(static_cast<char>(std::stoi(hex, nullptr, 16))); index += 2; continue; } catch (...) {}
    }
    if (value[index] == '+') {output.push_back(' ');} else {output.push_back(value[index]);}
  }
  return output;
}
std::string utc_timestamp()
{
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm value{};
  gmtime_r(&time, &value);
  std::ostringstream output;
  output << std::put_time(&value, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}
std::string file_time_string(const std::filesystem::path & path)
{
  try { return std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::filesystem::last_write_time(path).time_since_epoch()).count()); }
  catch (...) { return {}; }
}
std::uint64_t directory_bytes(const std::filesystem::path & path, std::uint64_t & files)
{
  std::uint64_t total = 0; files = 0;
  try { for (const auto & entry : std::filesystem::recursive_directory_iterator(path, std::filesystem::directory_options::skip_permission_denied)) { if (entry.is_regular_file()) { total += entry.file_size(); ++files; } } } catch (...) {}
  return total;
}
std::uint64_t count_lines(const std::filesystem::path & path)
{
  std::ifstream input(path); std::uint64_t count = 0; std::string line; while (std::getline(input, line)) { ++count; } return count;
}std::vector<std::string> split_lines(const std::string & body)
{
  std::vector<std::string> values;
  std::istringstream stream(body);
  std::string field;
  while (std::getline(stream, field, static_cast<char>(0x1f))) {values.push_back(trim(field));}
  return values;
}
std::string status_text(int status)
{
  switch (status) {
    case 200: return "OK";
    case 202: return "Accepted";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 408: return "Request Timeout";
    case 413: return "Payload Too Large";
    case 503: return "Service Unavailable";
    default: return "Error";
  }
}
std::string read_file(const std::filesystem::path & path)
{
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {throw std::runtime_error("Unable to read " + path.string());}
  std::ostringstream contents;
  contents << stream.rdbuf();
  return contents.str();
}
std::string content_type_for(const std::filesystem::path & path)
{
  const auto extension = lowercase(path.extension().string());
  if (extension == ".html") {return "text/html; charset=utf-8";}
  if (extension == ".css") {return "text/css; charset=utf-8";}
  if (extension == ".js") {return "application/javascript; charset=utf-8";}
  if (extension == ".svg") {return "image/svg+xml";}
  if (extension == ".webmanifest") {return "application/manifest+json";}
  return "application/octet-stream";
}
std::string mode_name(uint8_t mode)
{
  using M = vehicle_interfaces::msg::SystemState;
  switch (mode) {
    case M::MODE_MANUAL: return "MANUAL";
    case M::MODE_NAV2: return "NAV2";
    case M::MODE_VLA_SHADOW: return "VLA_SHADOW";
    case M::MODE_VLA_ASSISTED: return "VLA_ASSISTED";
    case M::MODE_VLA_AUTONOMOUS: return "VLA_AUTONOMOUS";
    case M::MODE_MOBILE_TELEOP: return "MOBILE_TELEOP";
    case M::MODE_REMOTE_TELEOP: return "REMOTE_TELEOP";
    case M::MODE_SAFE_STOP: return "SAFE_STOP";
    case M::MODE_FAULT: return "FAULT";
    default: return "OTHER";
  }
}
std::string pipeline_stage_status_name(uint8_t status)
{
  using Stage = vehicle_interfaces::msg::PipelineStage;
  switch (status) {
    case Stage::STATUS_LIVE: return "LIVE";
    case Stage::STATUS_READY: return "READY";
    case Stage::STATUS_RUNNING: return "RUNNING";
    case Stage::STATUS_STALE: return "STALE";
    case Stage::STATUS_SKIPPED: return "SKIPPED";
    case Stage::STATUS_REJECTED: return "REJECTED";
    case Stage::STATUS_FAILED: return "FAILED";
    default: return "WAITING";
  }
}
nlohmann::json component_state_json(const vehicle_interfaces::msg::ComponentState & component)
{
  return {{"component_id", component.component_id}, {"display_name", component.display_name},
    {"group_name", component.group_name}, {"unit_name", component.unit_name},
    {"state", component.state}, {"managed", component.managed}, {"healthy", component.healthy},
    {"can_start", component.can_start}, {"can_stop", component.can_stop},
    {"can_restart", component.can_restart}, {"pid", component.pid},
    {"uptime_seconds", component.uptime_seconds}, {"last_exit_code", component.last_exit_code},
    {"message", component.message}, {"dependencies", component.dependencies},
    {"expected_nodes", component.expected_nodes}, {"health_topics", component.health_topics}};
}
nlohmann::json control_lease_json(const vehicle_interfaces::msg::ControlLease & lease)
{
  const auto expires_at_ms = static_cast<std::int64_t>(lease.expires_at.sec) * 1000LL +
    static_cast<std::int64_t>(lease.expires_at.nanosec) / 1000000LL;
  return {{"lease_id", lease.lease_id}, {"controller_id", lease.controller_id},
    {"source", lease.source}, {"expires_at_ms", expires_at_ms},
    {"max_linear_velocity", lease.max_linear_velocity},
    {"max_angular_velocity", lease.max_angular_velocity}, {"remote", lease.remote},
    {"active", lease.active}};
}
nlohmann::json pipeline_trace_json(const vehicle_interfaces::msg::PipelineTrace & trace)
{
  nlohmann::json stages = nlohmann::json::array();
  for (const auto & stage : trace.stages) {
    nlohmann::json detail = nlohmann::json::object();
    try {detail = nlohmann::json::parse(stage.detail_json);}
    catch (const nlohmann::json::exception &) {detail = {{"raw", stage.detail_json}};}
    stages.push_back({
      {"stage_id", stage.stage_id}, {"label", stage.label}, {"component", stage.component},
      {"status", pipeline_stage_status_name(stage.status)}, {"status_code", stage.status},
      {"age_seconds", stage.age_seconds}, {"latency_ms", stage.latency_ms},
      {"input_summary", stage.input_summary}, {"output_summary", stage.output_summary},
      {"message", stage.message}, {"detail", detail},
      {"control_boundary", stage.control_boundary}, {"publishes_control", stage.publishes_control}});
  }
  return {{"trace_id", trace.trace_id}, {"observation_id", trace.observation_id},
    {"mode", trace.mode}, {"provider_id", trace.provider_id}, {"model_id", trace.model_id},
    {"shadow_only", trace.shadow_only}, {"publishes_control", trace.publishes_control},
    {"stages", stages}};
}std::string episode_name(uint8_t state)
{
  using M = vehicle_interfaces::msg::EpisodeState;
  switch (state) {
    case M::STATE_IDLE: return "IDLE";
    case M::STATE_RECORDING: return "RECORDING";
    case M::STATE_FINALIZING: return "FINALIZING";
    case M::STATE_ERROR: return "ERROR";
    default: return "UNKNOWN";
  }
}
std::string policy_name(uint8_t state)
{
  using M = vehicle_interfaces::msg::PolicyStatus;
  switch (state) {
    case M::STATE_DISCONNECTED: return "DISCONNECTED";
    case M::STATE_LOADING: return "LOADING";
    case M::STATE_READY: return "READY";
    case M::STATE_ERROR: return "ERROR";
    default: return "UNKNOWN";
  }
}
double numeric_file(const std::filesystem::path & path)
{
  double value = 0.0;
  std::ifstream(path) >> value;
  return value;
}

double thermal_temperature(const std::string & expected_type)
{
  std::error_code error;
  const std::filesystem::path thermal_root("/sys/class/thermal");
  if (!std::filesystem::is_directory(thermal_root, error)) {return 0.0;}
  for (const auto & entry : std::filesystem::directory_iterator(
    thermal_root, std::filesystem::directory_options::skip_permission_denied, error))
  {
    std::ifstream type_stream(entry.path() / "type");
    std::string type;
    std::getline(type_stream, type);
    if (trim(type) == expected_type) {return numeric_file(entry.path() / "temp") / 1000.0;}
  }
  return 0.0;
}

double first_thermal_temperature(const std::vector<std::string> & expected_types)
{
  for (const auto & expected_type : expected_types) {
    const auto value = thermal_temperature(expected_type);
    if (value > 0.0) {return value;}
  }
  return 0.0;
}

double cpu_usage_percent()
{
  std::ifstream stream("/proc/stat");
  std::string label;
  std::uint64_t user = 0, nice = 0, system = 0, idle = 0, io_wait = 0;
  std::uint64_t irq = 0, soft_irq = 0, steal = 0;
  stream >> label >> user >> nice >> system >> idle >> io_wait >> irq >> soft_irq >> steal;
  const std::uint64_t idle_ticks = idle + io_wait;
  const std::uint64_t total_ticks = user + nice + system + idle + io_wait + irq + soft_irq + steal;
  static std::mutex sample_mutex;
  static std::uint64_t previous_idle = 0;
  static std::uint64_t previous_total = 0;
  std::lock_guard<std::mutex> lock(sample_mutex);
  const auto total_delta = total_ticks - previous_total;
  const auto idle_delta = idle_ticks - previous_idle;
  previous_idle = idle_ticks;
  previous_total = total_ticks;
  if (total_delta == 0) {return 0.0;}
  return 100.0 * static_cast<double>(total_delta - std::min(idle_delta, total_delta)) /
    static_cast<double>(total_delta);
}

struct HostMetrics
{
  double load{0.0};
  double total{0.0};
  double available{0.0};
  double swap_total{0.0};
  double swap_free{0.0};
  double uptime{0.0};
  double cpu_usage{0.0};
  double cpu_temperature{0.0};
  double gpu_usage{0.0};
  double gpu_temperature{0.0};
  double gpu_frequency_mhz{0.0};
  bool gpu_available{false};
  std::string gpu_backend{"unavailable"};
  std::string gpu_memory_mode{"unknown"};
  std::uint32_t cpu_cores{0};
};

HostMetrics host_metrics()
{
  HostMetrics metrics;
  std::ifstream("/proc/loadavg") >> metrics.load;
  std::ifstream("/proc/uptime") >> metrics.uptime;
  std::ifstream stream("/proc/meminfo");
  std::string line;
  while (std::getline(stream, line)) {
    std::istringstream values(line);
    std::string key;
    double value = 0.0;
    values >> key >> value;
    if (key == "MemTotal:") {metrics.total = value / 1024.0;}
    if (key == "MemAvailable:") {metrics.available = value / 1024.0;}
    if (key == "SwapTotal:") {metrics.swap_total = value / 1024.0;}
    if (key == "SwapFree:") {metrics.swap_free = value / 1024.0;}
  }
  metrics.cpu_usage = cpu_usage_percent();
  metrics.cpu_temperature = first_thermal_temperature(
    {"cpu-thermal", "x86_pkg_temp", "Package id 0"});
  const std::filesystem::path jetson_gpu_root(
    "/sys/devices/platform/bus@0/17000000.gpu");
  if (std::filesystem::is_directory(jetson_gpu_root)) {
    metrics.gpu_available = true;
    metrics.gpu_backend = "jetson-sysfs";
    metrics.gpu_memory_mode = "unified";
    metrics.gpu_temperature = thermal_temperature("gpu-thermal");
    metrics.gpu_usage = numeric_file(jetson_gpu_root / "load") / 10.0;
    metrics.gpu_frequency_mhz = numeric_file(
      jetson_gpu_root / "devfreq/17000000.gpu/cur_freq") / 1000000.0;
  }
  metrics.cpu_cores = std::thread::hardware_concurrency();
  return metrics;
}
}  // namespace

class VehicleOpsApi final : public rclcpp::Node
{
public:
  using AcquireLease = vehicle_interfaces::action::AcquireControlLease;
  VehicleOpsApi();
  ~VehicleOpsApi() override;
private:
  template<typename T> struct Timed {
    std::optional<T> message;
    std::chrono::steady_clock::time_point received{};
  };
  template<typename T> void update(Timed<T> & target, const T & message) {
    std::lock_guard<std::mutex> lock(mutex_);
    target.message = message;
    target.received = std::chrono::steady_clock::now();
  }
  template<typename T> double age(const Timed<T> & value, std::chrono::steady_clock::time_point now) const {
    if (!value.message) {return -1.0;}
    return std::chrono::duration<double, std::milli>(now - value.received).count();
  }
  bool fresh(double value) const {return value >= 0.0 && value <= stale_seconds_ * 1000.0;}
  void start_server();
  void server_loop();
  void worker_loop();
  void handle_client(int client);
  HttpRequest read_request(int client) const;
  void send_response(int client, const HttpResponse & response) const;
  static void send_all(int client, const std::string & data);
  HttpResponse route(const HttpRequest & request);
  std::optional<HttpResponse> authorize(const HttpRequest & request) const;
  HttpResponse static_file(const std::string & path) const;
  HttpResponse camera();
  HttpResponse camera_calibration_status();
  HttpResponse camera_calibration_start(const std::string & body);
  HttpResponse camera_calibration_capture();
  HttpResponse camera_calibration_compute();
  HttpResponse camera_calibration_apply(const std::string & body);
  HttpResponse camera_calibration_reset();
  HttpResponse camera_calibration_image(bool undistorted);
  HttpResponse pipeline_live();
  HttpResponse pipeline_history();
  HttpResponse active_debug_create(const std::string & body);
  HttpResponse active_debug_get();
  HttpResponse active_debug_control(const std::string & action);
  HttpResponse components_list();
  HttpResponse component_control(const std::string & body);
  HttpResponse profile_control(const std::string & body);
  HttpResponse component_log(const std::string & component_id);
  HttpResponse storage_status();
  HttpResponse storage_items(const std::string & category_id);
  HttpResponse storage_cleanup(const std::string & body);
  HttpResponse dataset_catalog();
  HttpResponse dataset_detail(const std::string & relative_path);
  HttpResponse dataset_download(const std::string & archive_name);
  HttpResponse dataset_review(const std::string & body);
  HttpResponse models_catalog();
  HttpResponse mobility_plugins();
  HttpResponse task(const std::string & body);
  HttpResponse start_episode(const std::string & body);
  HttpResponse stop_episode(const std::string & body);
  HttpResponse safe_stop(const std::string & body);
  HttpResponse enter_shadow_mode();
  HttpResponse enter_single_step_mode();
  HttpResponse teleop_status();
  HttpResponse teleop_acquire(const std::string & body);
  HttpResponse teleop_command(const std::string & body);
  HttpResponse teleop_release(const std::string & body);
  HttpResponse jobs_create(const std::string & body);
  HttpResponse jobs_list();
  HttpResponse job_get(const std::string & job_id);
  HttpResponse job_log(const std::string & job_id);
  HttpResponse job_cancel(const std::string & job_id);
  HttpResponse debug_capture(const std::string & body);
  HttpResponse debug_run(const std::string & body);
  HttpResponse debug_image(const std::string & run_id, const std::string & image_name);
  std::string status_json();
  void active_debug_tick();
  static HttpResponse success(const std::string & message, const std::string & extra = "");
  static HttpResponse error(int status, const std::string & message);
  std::string bind_, token_, web_root_, project_root_, jobs_root_;
  std::string camera_calibration_path_, camera_calibration_metadata_path_;
  std::string camera_component_id_{"front_camera"};
  int port_{8088}, limit_{1048576}, http_worker_count_{8}, http_queue_limit_{32};
  double http_socket_timeout_seconds_{3.0};
  double service_seconds_{2.0}, stale_seconds_{3.0}, debug_timeout_seconds_{45.0};
  double camera_dark_mean_threshold_{2.0}, command_timeout_seconds_{0.5};
  double odometry_timeout_seconds_{0.5};
  double camera_calibration_verify_timeout_seconds_{8.0};
  double stationary_linear_threshold_{0.02}, stationary_angular_threshold_{0.05};
  std::atomic<bool> running_{false};
  int server_{-1};
  std::thread thread_;
  std::vector<std::thread> worker_threads_;
  std::mutex client_queue_mutex_;
  std::condition_variable client_queue_condition_;
  std::deque<int> client_queue_;
  std::unique_ptr<vehicle_ops::JobManager> jobs_;
  std::unique_ptr<vehicle_ops::CameraCalibrationManager> camera_calibration_;
  std::mutex mutex_;
  Timed<vehicle_interfaces::msg::SystemState> system_;
  Timed<vehicle_interfaces::msg::ObservationStatus> observation_;
  Timed<vehicle_interfaces::msg::EpisodeState> episode_;
  Timed<vehicle_interfaces::msg::PolicyStatus> policy_;
  Timed<vehicle_interfaces::msg::ShadowMetrics> shadow_;
  Timed<vehicle_interfaces::msg::SafetyEvent> safety_;
  Timed<vehicle_interfaces::msg::ControlLease> control_lease_;
  std::optional<vehicle_interfaces::msg::PipelineTrace> pipeline_trace_;
  std::deque<vehicle_interfaces::msg::PipelineTrace> pipeline_history_;
  std::chrono::steady_clock::time_point pipeline_received_{};
  std::vector<uint8_t> camera_data_;
  std::chrono::steady_clock::time_point camera_time_{};
  std::chrono::system_clock::time_point camera_wall_time_{};
  std::uint64_t camera_sequence_{0};
  bool camera_content_analyzed_{false};
  bool camera_content_valid_{false};
  double camera_mean_intensity_{0.0};
  unsigned int camera_max_intensity_{0};
  geometry_msgs::msg::Twist latest_command_;
  std::chrono::steady_clock::time_point command_received_{};
  nav_msgs::msg::Odometry latest_odometry_;
  std::chrono::steady_clock::time_point odometry_received_{};
  struct ActiveDebugSession {
    std::string id;
    std::string task;
    std::string status{"IDLE"};
    std::string stop_reason;
    std::string last_observation_id;
    std::string last_trace_id;
    std::string baseline_observation_id;
    std::chrono::steady_clock::time_point started{};
    std::chrono::steady_clock::time_point last_step{};
    std::chrono::steady_clock::time_point finished{};
    int steps{0};
    int max_steps{100};
    double max_duration_seconds{60.0};
    double frequency_hz{1.0};
    bool shadow_only{true};
    bool waiting_for_task_observation{false};
  } active_debug_;
  rclcpp::TimerBase::SharedPtr active_debug_timer_;
  rclcpp::Subscription<vehicle_interfaces::msg::SystemState>::SharedPtr system_sub_;
  rclcpp::Subscription<vehicle_interfaces::msg::ObservationStatus>::SharedPtr observation_sub_;
  rclcpp::Subscription<vehicle_interfaces::msg::EpisodeState>::SharedPtr episode_sub_;
  rclcpp::Subscription<vehicle_interfaces::msg::PolicyStatus>::SharedPtr policy_sub_;
  rclcpp::Subscription<vehicle_interfaces::msg::ShadowMetrics>::SharedPtr shadow_sub_;
  rclcpp::Subscription<vehicle_interfaces::msg::SafetyEvent>::SharedPtr safety_sub_;
  rclcpp::Subscription<vehicle_interfaces::msg::PipelineTrace>::SharedPtr pipeline_sub_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr raw_camera_sub_;
  rclcpp::Subscription<sensor_msgs::msg::CameraInfo>::SharedPtr camera_info_sub_;
  rclcpp::Subscription<sensor_msgs::msg::CompressedImage>::SharedPtr camera_sub_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr command_sub_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odometry_sub_;
  rclcpp::Subscription<vehicle_interfaces::msg::ControlLease>::SharedPtr control_lease_sub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr task_pub_;
  rclcpp::Publisher<vehicle_interfaces::msg::TeleopCommand>::SharedPtr teleop_command_pub_;
  rclcpp::Client<vehicle_interfaces::srv::StartEpisode>::SharedPtr start_client_;
  rclcpp::Client<vehicle_interfaces::srv::StopEpisode>::SharedPtr stop_client_;
  rclcpp::Client<vehicle_interfaces::srv::RequestSafeStop>::SharedPtr safe_client_;
  rclcpp::Client<vehicle_interfaces::srv::RequestControlMode>::SharedPtr mode_client_;
  rclcpp_action::Client<AcquireLease>::SharedPtr acquire_lease_client_;
  rclcpp::Client<vehicle_interfaces::srv::ReleaseControlLease>::SharedPtr release_lease_client_;
  rclcpp::Client<vehicle_interfaces::srv::CaptureVlaDebug>::SharedPtr debug_capture_client_;
  rclcpp::Client<vehicle_interfaces::srv::RunVlaDebug>::SharedPtr debug_run_client_;
  rclcpp::Client<vehicle_interfaces::srv::ListComponents>::SharedPtr components_client_;
  rclcpp::Client<vehicle_interfaces::srv::ControlComponent>::SharedPtr component_control_client_;
  rclcpp::Client<vehicle_interfaces::srv::ControlProfile>::SharedPtr profile_control_client_;
  rclcpp::Client<vehicle_interfaces::srv::ReadComponentLog>::SharedPtr component_log_client_;
  rclcpp::Client<vehicle_interfaces::srv::GetStorageStatus>::SharedPtr storage_status_client_;
  rclcpp::Client<vehicle_interfaces::srv::ListStorageItems>::SharedPtr storage_items_client_;
  rclcpp::Client<vehicle_interfaces::srv::CleanupStorage>::SharedPtr storage_cleanup_client_;
};
VehicleOpsApi::VehicleOpsApi() : Node("vehicle_ops_api")
{
  bind_ = declare_parameter<std::string>("bind_address", "0.0.0.0");
  port_ = declare_parameter<int>("port", 8088);
  limit_ = declare_parameter<int>("request_limit_bytes", 1048576);
  http_worker_count_ = declare_parameter<int>("http_worker_count", 8);
  http_queue_limit_ = declare_parameter<int>("http_queue_limit", 32);
  http_socket_timeout_seconds_ = declare_parameter<double>("http_socket_timeout_seconds", 3.0);
  service_seconds_ = declare_parameter<double>("service_timeout_seconds", 2.0);
  stale_seconds_ = declare_parameter<double>("stale_after_seconds", 3.0);
  debug_timeout_seconds_ = declare_parameter<double>("debug_timeout_seconds", 45.0);
  camera_dark_mean_threshold_ = declare_parameter<double>("camera_dark_mean_threshold", 2.0);
  command_timeout_seconds_ = declare_parameter<double>("runtime_switch_command_timeout_seconds", 0.5);
  odometry_timeout_seconds_ = declare_parameter<double>("teleop_odometry_timeout_seconds", 0.5);
  stationary_linear_threshold_ = declare_parameter<double>("runtime_switch_stationary_linear_threshold", 0.02);
  stationary_angular_threshold_ = declare_parameter<double>("runtime_switch_stationary_angular_threshold", 0.05);
  token_ = declare_parameter<std::string>("operator_token", "");
  web_root_ = declare_parameter<std::string>("web_root", "");
  project_root_ = declare_parameter<std::string>("project_root", "");
  jobs_root_ = declare_parameter<std::string>("jobs_root", "");
  camera_calibration_path_ = declare_parameter<std::string>("camera_calibration_path", "");
  camera_calibration_metadata_path_ =
    declare_parameter<std::string>("camera_calibration_metadata_path", "");
  camera_component_id_ = declare_parameter<std::string>("camera_component_id", "front_camera");
  camera_calibration_verify_timeout_seconds_ =
    declare_parameter<double>("camera_calibration_verify_timeout_seconds", 8.0);
  if (port_ < 1 || port_ > 65535 || !std::filesystem::is_directory(web_root_) ||
    project_root_.empty() || jobs_root_.empty())
  {
    throw std::invalid_argument("Invalid port, web_root, project_root, or jobs_root");
  }
  http_worker_count_ = std::clamp(http_worker_count_, 1, 32);
  http_queue_limit_ = std::clamp(http_queue_limit_, http_worker_count_, 256);
  http_socket_timeout_seconds_ = std::clamp(http_socket_timeout_seconds_, 0.5, 30.0);
  camera_calibration_verify_timeout_seconds_ =
    std::clamp(camera_calibration_verify_timeout_seconds_, 1.0, 30.0);
  if (camera_calibration_path_.empty()) {
    camera_calibration_path_ = (
      std::filesystem::path(project_root_) /
      "ros_ws/src/vehicle_bringup/config/front_camera_info.yaml").string();
  }
  if (camera_calibration_metadata_path_.empty()) {
    camera_calibration_metadata_path_ = (
      std::filesystem::path(project_root_) /
      "ros_ws/src/vehicle_bringup/config/front_camera_info.meta.json").string();
  }
  camera_calibration_ = std::make_unique<vehicle_ops::CameraCalibrationManager>(
    vehicle_ops::CameraCalibrationManager::Paths{
      camera_calibration_path_, camera_calibration_metadata_path_});
  const auto state_qos = rclcpp::QoS(1).reliable().transient_local();
  system_sub_ = create_subscription<vehicle_interfaces::msg::SystemState>(
    "/vehicle/system_state", state_qos, [this](vehicle_interfaces::msg::SystemState::SharedPtr message) {update(system_, *message);});
  observation_sub_ = create_subscription<vehicle_interfaces::msg::ObservationStatus>(
    "/vehicle/observation_status", state_qos, [this](vehicle_interfaces::msg::ObservationStatus::SharedPtr message) {update(observation_, *message);});
  episode_sub_ = create_subscription<vehicle_interfaces::msg::EpisodeState>(
    "/vehicle/episode_state", state_qos, [this](vehicle_interfaces::msg::EpisodeState::SharedPtr message) {update(episode_, *message);});
  policy_sub_ = create_subscription<vehicle_interfaces::msg::PolicyStatus>(
    "/vla/policy_state", state_qos, [this](vehicle_interfaces::msg::PolicyStatus::SharedPtr message) {update(policy_, *message);});
  shadow_sub_ = create_subscription<vehicle_interfaces::msg::ShadowMetrics>(
    "/vla/shadow_metrics", state_qos, [this](vehicle_interfaces::msg::ShadowMetrics::SharedPtr message) {update(shadow_, *message);});
  safety_sub_ = create_subscription<vehicle_interfaces::msg::SafetyEvent>(
    "/vla/safety_event", 10, [this](vehicle_interfaces::msg::SafetyEvent::SharedPtr message) {update(safety_, *message);});
  pipeline_sub_ = create_subscription<vehicle_interfaces::msg::PipelineTrace>(
    "/vla/pipeline_trace", state_qos,
    [this](vehicle_interfaces::msg::PipelineTrace::SharedPtr message) {
      std::lock_guard<std::mutex> lock(mutex_);
      pipeline_trace_ = *message;
      pipeline_received_ = std::chrono::steady_clock::now();
      if (pipeline_history_.empty() || pipeline_history_.front().trace_id != message->trace_id) {
        pipeline_history_.push_front(*message);
        while (pipeline_history_.size() > 30) {pipeline_history_.pop_back();}
      } else {
        pipeline_history_.front() = *message;
      }
    });
  raw_camera_sub_ = create_subscription<sensor_msgs::msg::Image>(
    "/camera/image_raw", rclcpp::SensorDataQoS(), [this](sensor_msgs::msg::Image::SharedPtr message) {
      camera_calibration_->update_frame(*message);
      const bool analyzable = message->encoding == "rgb8" || message->encoding == "bgr8" ||
        message->encoding == "rgba8" || message->encoding == "bgra8" || message->encoding == "mono8";
      if (!analyzable || message->data.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        camera_content_analyzed_ = false;
        camera_content_valid_ = true;
        return;
      }
      const std::size_t channels = message->encoding == "mono8" ? 1U :
        ((message->encoding == "rgba8" || message->encoding == "bgra8") ? 4U : 3U);
      const std::size_t stride = std::max<std::size_t>(channels, message->data.size() / 8192U);
      std::uint64_t sum = 0;
      std::size_t samples = 0;
      unsigned int maximum = 0;
      for (std::size_t index = 0; index < message->data.size(); index += stride) {
        if (channels == 4U && index % channels == 3U) continue;
        const auto value = static_cast<unsigned int>(message->data[index]);
        sum += value;
        maximum = std::max(maximum, value);
        ++samples;
      }
      const double mean = samples == 0 ? 0.0 : static_cast<double>(sum) / static_cast<double>(samples);
      std::lock_guard<std::mutex> lock(mutex_);
      camera_content_analyzed_ = true;
      camera_mean_intensity_ = mean;
      camera_max_intensity_ = maximum;
      camera_content_valid_ = mean >= camera_dark_mean_threshold_;
    });
  camera_info_sub_ = create_subscription<sensor_msgs::msg::CameraInfo>(
    "/camera/camera_info", rclcpp::SensorDataQoS(),
    [this](sensor_msgs::msg::CameraInfo::SharedPtr message) {
      camera_calibration_->update_camera_info(*message);
    });
  camera_sub_ = create_subscription<sensor_msgs::msg::CompressedImage>(
    "/camera/image_compressed", rclcpp::SensorDataQoS(), [this](sensor_msgs::msg::CompressedImage::SharedPtr message) {
      std::lock_guard<std::mutex> lock(mutex_);
      camera_data_ = message->data;
      camera_time_ = std::chrono::steady_clock::now();
      camera_wall_time_ = std::chrono::system_clock::now();
      ++camera_sequence_;
    });
  command_sub_ = create_subscription<geometry_msgs::msg::Twist>(
    "/cmd_vel", 10, [this](geometry_msgs::msg::Twist::SharedPtr message) {
      std::lock_guard<std::mutex> lock(mutex_);
      latest_command_ = *message;
      command_received_ = std::chrono::steady_clock::now();
    });
  odometry_sub_ = create_subscription<nav_msgs::msg::Odometry>(
    "/odom", rclcpp::SensorDataQoS(), [this](nav_msgs::msg::Odometry::SharedPtr message) {
      std::lock_guard<std::mutex> lock(mutex_);
      latest_odometry_ = *message;
      odometry_received_ = std::chrono::steady_clock::now();
    });
  control_lease_sub_ = create_subscription<vehicle_interfaces::msg::ControlLease>(
    "/vehicle/control_lease", state_qos,
    [this](vehicle_interfaces::msg::ControlLease::SharedPtr message) {
      update(control_lease_, *message);
    });
  task_pub_ = create_publisher<std_msgs::msg::String>(
    "/vla/task", rclcpp::QoS(1).reliable().transient_local());
  teleop_command_pub_ = create_publisher<vehicle_interfaces::msg::TeleopCommand>(
    "/vehicle/teleop_command", 20);
  active_debug_timer_ = create_wall_timer(500ms, std::bind(&VehicleOpsApi::active_debug_tick, this));
  start_client_ = create_client<vehicle_interfaces::srv::StartEpisode>("/vehicle/start_episode");
  stop_client_ = create_client<vehicle_interfaces::srv::StopEpisode>("/vehicle/stop_episode");
  safe_client_ = create_client<vehicle_interfaces::srv::RequestSafeStop>("/vehicle/request_safe_stop");
  mode_client_ = create_client<vehicle_interfaces::srv::RequestControlMode>("/vehicle/request_mode");
  acquire_lease_client_ = rclcpp_action::create_client<AcquireLease>(
    this, "/vehicle/acquire_control_lease");
  release_lease_client_ = create_client<vehicle_interfaces::srv::ReleaseControlLease>(
    "/vehicle/release_control_lease");
  debug_capture_client_ = create_client<vehicle_interfaces::srv::CaptureVlaDebug>("/vla/debug/capture");
  debug_run_client_ = create_client<vehicle_interfaces::srv::RunVlaDebug>("/vla/debug/run");
  components_client_ = create_client<vehicle_interfaces::srv::ListComponents>("/vehicle/operations/list_components");
  component_control_client_ = create_client<vehicle_interfaces::srv::ControlComponent>("/vehicle/operations/control_component");
  profile_control_client_ = create_client<vehicle_interfaces::srv::ControlProfile>("/vehicle/operations/control_profile");
  component_log_client_ = create_client<vehicle_interfaces::srv::ReadComponentLog>("/vehicle/operations/read_component_log");
  storage_status_client_ = create_client<vehicle_interfaces::srv::GetStorageStatus>("/vehicle/storage/get_status");
  storage_items_client_ = create_client<vehicle_interfaces::srv::ListStorageItems>("/vehicle/storage/list_items");
  storage_cleanup_client_ = create_client<vehicle_interfaces::srv::CleanupStorage>("/vehicle/storage/cleanup");
  jobs_ = std::make_unique<vehicle_ops::JobManager>(project_root_, jobs_root_);
  start_server();
  RCLCPP_INFO(get_logger(), "Vehicle Ops listening on http://%s:%d", bind_.c_str(), port_);
}
VehicleOpsApi::~VehicleOpsApi()
{
  running_ = false;
  if (server_ >= 0) {::shutdown(server_, SHUT_RDWR); ::close(server_);}
  client_queue_condition_.notify_all();
  if (thread_.joinable()) {thread_.join();}
  for (auto & worker : worker_threads_) {
    if (worker.joinable()) {worker.join();}
  }
}

void VehicleOpsApi::start_server()
{
  server_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (server_ < 0) {throw std::runtime_error("socket failed");}
  int reuse = 1;
  ::setsockopt(server_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<uint16_t>(port_));
  if (::inet_pton(AF_INET, bind_.c_str(), &address.sin_addr) != 1 ||
    ::bind(server_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
    ::listen(server_, 16) != 0)
  {
    const auto message = std::string(std::strerror(errno));
    ::close(server_);
    server_ = -1;
    throw std::runtime_error("HTTP server setup failed: " + message);
  }
  running_ = true;
  worker_threads_.reserve(static_cast<std::size_t>(http_worker_count_));
  for (int index = 0; index < http_worker_count_; ++index) {
    worker_threads_.emplace_back([this]() {worker_loop();});
  }
  thread_ = std::thread([this]() {server_loop();});
}
void VehicleOpsApi::server_loop()
{
  while (running_) {
    const int client = ::accept(server_, nullptr, nullptr);
    if (client < 0) {
      if (!running_) {break;}
      if (errno == EINTR) {continue;}
      continue;
    }
    bool accepted = false;
    {
      std::lock_guard<std::mutex> lock(client_queue_mutex_);
      if (client_queue_.size() < static_cast<std::size_t>(http_queue_limit_)) {
        client_queue_.push_back(client);
        accepted = true;
      }
    }
    if (accepted) {
      client_queue_condition_.notify_one();
    } else {
      ::shutdown(client, SHUT_RDWR);
      ::close(client);
    }
  }
}
void VehicleOpsApi::worker_loop()
{
  while (true) {
    int client = -1;
    {
      std::unique_lock<std::mutex> lock(client_queue_mutex_);
      client_queue_condition_.wait(lock, [this]() {return !running_ || !client_queue_.empty();});
      if (client_queue_.empty()) {
        if (!running_) {return;}
        continue;
      }
      client = client_queue_.front();
      client_queue_.pop_front();
    }
    handle_client(client);
  }
}
void VehicleOpsApi::handle_client(int client)
{
  const auto timeout_seconds = static_cast<long>(http_socket_timeout_seconds_);
  const auto timeout_microseconds = static_cast<long>(
    (http_socket_timeout_seconds_ - static_cast<double>(timeout_seconds)) * 1000000.0);
  timeval timeout{timeout_seconds, timeout_microseconds};
  ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  ::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  try {send_response(client, route(read_request(client)));}
  catch (const std::length_error & e) {send_response(client, error(413, e.what()));}
  catch (const std::exception & e) {send_response(client, error(400, e.what()));}
  ::shutdown(client, SHUT_RDWR);
  ::close(client);
}
HttpRequest VehicleOpsApi::read_request(int client) const
{
  std::string data;
  char buffer[4096];
  std::size_t header_end = std::string::npos;
  std::size_t delimiter_size = 0;
  const auto find_header_end = [&data, &delimiter_size]() {
      const auto strict_end = data.find("\r\n\r\n");
      if (strict_end != std::string::npos) {
        delimiter_size = 4;
        return strict_end;
      }
      const auto tolerant_end = data.find("\n\n");
      if (tolerant_end != std::string::npos) {
        delimiter_size = 2;
        return tolerant_end;
      }
      return std::string::npos;
    };
  const auto receive = [client, &buffer]() {
      while (true) {
        const auto count = ::recv(client, buffer, sizeof(buffer), 0);
        if (count >= 0) {return count;}
        if (errno == EINTR) {continue;}
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          throw std::runtime_error("Request receive timed out");
        }
        throw std::runtime_error(
          std::string("Request receive failed: ") + std::strerror(errno));
      }
    };
  while ((header_end = find_header_end()) == std::string::npos) {
    const auto count = receive();
    if (count == 0) {throw std::runtime_error("Incomplete request");}
    data.append(buffer, static_cast<std::size_t>(count));
    if (data.size() > static_cast<std::size_t>(limit_)) {throw std::length_error("Request too large");}
  }
  HttpRequest request;
  std::istringstream headers(data.substr(0, header_end));
  std::string line, version;
  std::getline(headers, line);
  std::istringstream first(trim(line));
  first >> request.method >> request.target >> version;
  if (request.method.empty() || request.target.empty() || version.rfind("HTTP/", 0) != 0) {
    throw std::runtime_error("Invalid request line");
  }
  while (std::getline(headers, line)) {
    const auto separator = line.find(':');
    if (separator != std::string::npos) {
      request.headers[lowercase(trim(line.substr(0, separator)))] = trim(line.substr(separator + 1));
    }
  }
  std::size_t length = 0;
  const auto found = request.headers.find("content-length");
  if (found != request.headers.end()) {length = static_cast<std::size_t>(std::stoul(found->second));}
  if (length > static_cast<std::size_t>(limit_)) {throw std::length_error("Body too large");}
  const auto body_offset = header_end + delimiter_size;
  while (data.size() - body_offset < length) {
    const auto count = receive();
    if (count == 0) {throw std::runtime_error("Incomplete body");}
    data.append(buffer, static_cast<std::size_t>(count));
    if (data.size() > static_cast<std::size_t>(limit_)) {throw std::length_error("Request too large");}
  }
  request.body = data.substr(body_offset, length);
  return request;
}
void VehicleOpsApi::send_all(int client, const std::string & data)
{
  std::size_t sent = 0;
  while (sent < data.size()) {
    const auto count = ::send(client, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
    if (count < 0 && errno == EINTR) {continue;}
    if (count <= 0) {return;}
    sent += static_cast<std::size_t>(count);
  }
}
void VehicleOpsApi::send_response(int client, const HttpResponse & response) const
{
  std::ostringstream headers;
  headers << "HTTP/1.1 " << response.status << ' ' << status_text(response.status) << "\r\n";
  headers << "Content-Type: " << response.content_type << "\r\n";
  headers << "Content-Length: " << response.body.size() << "\r\n";
  headers << "Connection: close\r\nX-Content-Type-Options: nosniff\r\nX-Frame-Options: DENY\r\n";
  headers << "Content-Security-Policy: default-src 'self'; img-src 'self' data: blob:; style-src 'self'; script-src 'self'; object-src 'none'; base-uri 'none'\r\n";
  for (const auto & item : response.headers) {headers << item.first << ": " << item.second << "\r\n";}
  headers << "\r\n";
  send_all(client, headers.str());
  send_all(client, response.body);
}

HttpResponse VehicleOpsApi::route(const HttpRequest & request)
{
  const auto query = request.target.find('?');
  const auto path = request.target.substr(0, query);
  if (request.method == "GET" && path == "/api/status") {
    return {200, "application/json; charset=utf-8", status_json(), {{"Cache-Control", "no-store"}}};
  }
  if (request.method == "GET" && path == "/api/camera/front.jpg") {return camera();}
  if (request.method == "GET" && path == "/api/camera-calibration/status") {
    return camera_calibration_status();
  }
  if (request.method == "GET" && path == "/api/camera-calibration/preview.jpg") {
    return camera_calibration_image(false);
  }
  if (request.method == "GET" && path == "/api/camera-calibration/undistorted.jpg") {
    return camera_calibration_image(true);
  }
  if (path.rfind("/api/camera-calibration/", 0) == 0) {
    const auto denied = authorize(request);
    if (denied) {return *denied;}
    if (request.method == "POST" && path == "/api/camera-calibration/start") {
      return camera_calibration_start(request.body);
    }
    if (request.method == "POST" && path == "/api/camera-calibration/capture") {
      return camera_calibration_capture();
    }
    if (request.method == "POST" && path == "/api/camera-calibration/compute") {
      return camera_calibration_compute();
    }
    if (request.method == "POST" && path == "/api/camera-calibration/apply") {
      return camera_calibration_apply(request.body);
    }
    if (request.method == "POST" && path == "/api/camera-calibration/reset") {
      return camera_calibration_reset();
    }
    return error(405, "Unsupported camera calibration endpoint");
  }
  if (request.method == "GET" && path == "/api/pipeline/live") {return pipeline_live();}
  if (request.method == "GET" && path == "/api/teleop/status") {return teleop_status();}
  if (path.rfind("/api/teleop/", 0) == 0) {
    const auto denied = authorize(request);
    if (denied) {return *denied;}
    if (request.method == "POST" && path == "/api/teleop/acquire") {
      return teleop_acquire(request.body);
    }
    if (request.method == "POST" && path == "/api/teleop/command") {
      return teleop_command(request.body);
    }
    if (request.method == "POST" && path == "/api/teleop/release") {
      return teleop_release(request.body);
    }
    return error(405, "Unsupported teleop endpoint");
  }
  if (request.method == "GET" && path == "/api/pipeline/history") {return pipeline_history();}
  if (request.method == "GET" && path == "/api/active-debug/session") {return active_debug_get();}
  if (request.method == "POST" && path == "/api/active-debug/sessions") {
    const auto denied = authorize(request); if (denied) {return *denied;}
    return active_debug_create(request.body);
  }
  if (request.method == "POST" && path.rfind("/api/active-debug/session/", 0) == 0) {
    const auto denied = authorize(request); if (denied) {return *denied;}
    return active_debug_control(path.substr(26));
  }
  if (request.method == "GET" && path == "/api/components") {return components_list();}
  if (request.method == "GET" && path == "/api/storage") {return storage_status();}
  if (request.method == "GET" && path == "/api/datasets") {return dataset_catalog();}
  if (request.method == "GET" && path.rfind("/api/datasets/detail/", 0) == 0) {
    return dataset_detail(url_decode(path.substr(std::string("/api/datasets/detail/").size())));
  }
  if (request.method == "GET" && path.rfind("/api/datasets/download/", 0) == 0) {
    const auto denied = authorize(request); if (denied) {return *denied;}
    return dataset_download(url_decode(path.substr(std::string("/api/datasets/download/").size())));
  }
  if (request.method == "POST" && path == "/api/datasets/review") {
    const auto denied = authorize(request); if (denied) {return *denied;}
    return dataset_review(request.body);
  }
  if (request.method == "GET" && path == "/api/models") {return models_catalog();}
  if (request.method == "GET" && path == "/api/mobility/plugins") {return mobility_plugins();}
  if (request.method == "GET" && path.rfind("/api/storage/items/", 0) == 0) {
    const auto denied = authorize(request);
    if (denied) {return *denied;}
    return storage_items(path.substr(19));
  }
  if (path.rfind("/api/components/", 0) == 0 && path.size() > 20 && path.rfind("/log") == path.size() - 4) {
    const auto denied = authorize(request);
    if (denied) {return *denied;}
    return component_log(path.substr(16, path.size() - 20));
  }
  if (request.method == "POST" && (path == "/api/components/control" || path == "/api/profiles/control")) {
    const auto denied = authorize(request);
    if (denied) {return *denied;}
    return path == "/api/components/control" ? component_control(request.body) : profile_control(request.body);
  }
  if (path.rfind("/api/vla-debug/", 0) == 0) {
    const auto denied = authorize(request);
    if (denied) {return *denied;}
    if (request.method == "POST" && path == "/api/vla-debug/enter-shadow") {
      return enter_shadow_mode();
    }
    if (request.method == "POST" && path == "/api/vla-debug/enter-single-step") {
      return enter_single_step_mode();
    }
    if (request.method == "POST" && path == "/api/vla-debug/capture") {
      return debug_capture(request.body);
    }
    if (request.method == "POST" && path == "/api/vla-debug/run") {
      return debug_run(request.body);
    }
    const std::string prefix = "/api/vla-debug/runs/";
    if (request.method == "GET" && path.rfind(prefix, 0) == 0) {
      const auto remainder = path.substr(prefix.size());
      const auto separator = remainder.find('/');
      if (separator != std::string::npos) {
        return debug_image(remainder.substr(0, separator), remainder.substr(separator + 1));
      }
    }
    return error(405, "Unsupported VLA debug endpoint");
  }
  if (path == "/api/jobs" || path.rfind("/api/jobs/", 0) == 0) {
    const auto denied = authorize(request);
    if (denied) {return *denied;}
    if (request.method == "POST" && path == "/api/jobs") {return jobs_create(request.body);}
    if (request.method == "GET" && path == "/api/jobs") {return jobs_list();}
    const std::string prefix = "/api/jobs/";
    const auto remainder = path.substr(prefix.size());
    const auto separator = remainder.find('/');
    const auto job_id = remainder.substr(0, separator);
    const auto action = separator == std::string::npos ? "" : remainder.substr(separator + 1);
    if (request.method == "GET" && action.empty()) {return job_get(job_id);}
    if (request.method == "GET" && action == "log") {return job_log(job_id);}
    if (request.method == "POST" && action == "cancel") {return job_cancel(job_id);}
    return error(405, "Unsupported job endpoint");
  }
  if (request.method == "POST") {
    const auto denied = authorize(request);
    if (denied) {return *denied;}
    if (path == "/api/task") {return task(request.body);}
    if (path == "/api/episode/start") {return start_episode(request.body);}
    if (path == "/api/episode/stop") {return stop_episode(request.body);}
    if (path == "/api/safe-stop") {return safe_stop(request.body);}
    if (path == "/api/storage/cleanup") {return storage_cleanup(request.body);}
  }
  if (request.method == "GET") {return static_file(path);}
  return error(405, "Unsupported endpoint");
}
std::optional<HttpResponse> VehicleOpsApi::authorize(const HttpRequest & request) const
{
  if (token_.empty()) {return error(503, "Write operations are disabled");}
  const auto found = request.headers.find("x-ops-token");
  if (found == request.headers.end() || found->second != token_) {
    return error(401, "A valid operator token is required");
  }
  return std::nullopt;
}
HttpResponse VehicleOpsApi::static_file(const std::string & path) const
{
  std::filesystem::path relative;
  if (path == "/" || path == "/index.html") {relative = "index.html";}
  else if (path == "/teleop" || path == "/teleop.html") {relative = "teleop.html";}
  else if (path == "/favicon.svg") {relative = "favicon.svg";}
  else if (path == "/manifest.webmanifest") {relative = "manifest.webmanifest";}
  else if (path == "/teleop-sw.js") {relative = "teleop-sw.js";}
  else if (path.rfind("/assets/", 0) == 0 && path.size() <= 180 &&
    path.find("..") == std::string::npos && path.find('\\') == std::string::npos)
  {
    relative = path.substr(1);
    const auto extension = lowercase(relative.extension().string());
    if (extension != ".css" && extension != ".js" && extension != ".svg" &&
      extension != ".png" && extension != ".webp")
    {return error(404, "Resource not found");}
  } else {return error(404, "Resource not found");}
  try {
    const auto file = std::filesystem::path(web_root_) / relative;
    HttpResponse response{200, content_type_for(file), read_file(file), {{"Cache-Control", "no-cache"}}};
    if (path == "/teleop-sw.js") {
      response.headers.emplace_back("Service-Worker-Allowed", "/");
    }
    return response;
  } catch (const std::exception &) {return error(404, "Resource not found");}
}
HttpResponse VehicleOpsApi::camera_calibration_status()
{
  return {
    200, "application/json; charset=utf-8", camera_calibration_->status().dump() + "\n",
    {{"Cache-Control", "no-store"}}};
}

HttpResponse VehicleOpsApi::camera_calibration_start(const std::string & body)
{
  try {
    const auto input = nlohmann::json::parse(body);
    const auto result = camera_calibration_->start(input);
    return success(result.value("message", "Calibration session started"),
      "\"calibration\":" + result.dump());
  } catch (const nlohmann::json::exception &) {
    return error(400, "Request body must be valid JSON");
  } catch (const std::invalid_argument & exception) {
    return error(400, exception.what());
  } catch (const std::exception & exception) {
    return error(409, exception.what());
  }
}

HttpResponse VehicleOpsApi::camera_calibration_capture()
{
  try {
    const auto result = camera_calibration_->capture();
    return success(result.value("message", "Calibration sample captured"),
      "\"calibration\":" + result.dump());
  } catch (const std::exception & exception) {
    return error(409, exception.what());
  }
}

HttpResponse VehicleOpsApi::camera_calibration_compute()
{
  try {
    const auto result = camera_calibration_->compute();
    return success(result.value("message", "Calibration computed"),
      "\"calibration\":" + result.dump());
  } catch (const std::exception & exception) {
    return error(409, exception.what());
  }
}

HttpResponse VehicleOpsApi::camera_calibration_apply(const std::string & body)
{
  nlohmann::json input;
  try {
    input = nlohmann::json::parse(body);
  } catch (const nlohmann::json::exception &) {
    return error(400, "Request body must be valid JSON");
  }
  nlohmann::json result;
  try {
    result = camera_calibration_->apply(input);
  } catch (const std::invalid_argument & exception) {
    return error(400, exception.what());
  } catch (const std::exception & exception) {
    return error(409, exception.what());
  }

  nlohmann::json restart{{"requested", true}, {"accepted", false}};
  if (!component_control_client_->wait_for_service(500ms)) {
    restart["message"] = "Operation Orchestrator is unavailable";
  } else {
    auto request = std::make_shared<vehicle_interfaces::srv::ControlComponent::Request>();
    request->component_id = camera_component_id_;
    request->action = "restart";
    request->force = false;
    auto future = component_control_client_->async_send_request(request);
    if (future.wait_for(std::chrono::milliseconds(
        static_cast<int>(service_seconds_ * 4000))) != std::future_status::ready)
    {
      restart["message"] = "Camera restart timed out";
    } else {
      const auto response = future.get();
      restart["accepted"] = response->accepted;
      restart["message"] = response->message;
    }
  }
  const bool verified = restart.value("accepted", false) &&
    camera_calibration_->wait_for_verification(std::chrono::milliseconds(
      static_cast<int>(camera_calibration_verify_timeout_seconds_ * 1000.0)));
  result["restart"] = restart;
  result["verified"] = verified;
  const std::string message = verified ?
    "标定已保存、相机已重启，CameraInfo 验证通过" :
    "标定文件已保存，但相机重启或 CameraInfo 验证未通过";
  return success(message, "\"calibration\":" + result.dump());
}

HttpResponse VehicleOpsApi::camera_calibration_reset()
{
  const auto result = camera_calibration_->reset();
  return success(result.value("message", "Calibration session reset"),
    "\"calibration\":" + result.dump());
}

HttpResponse VehicleOpsApi::camera_calibration_image(bool undistorted)
{
  const auto data = undistorted ? camera_calibration_->undistorted_jpeg() :
    camera_calibration_->preview_jpeg();
  if (data.empty()) {
    return error(404, undistorted ? "Undistorted preview is not ready" : "Camera frame is unavailable");
  }
  return {
    200, "image/jpeg", std::string(data.begin(), data.end()),
    {{"Cache-Control", "no-store"}}};
}
HttpResponse VehicleOpsApi::camera()
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (camera_data_.empty()) {return error(404, "No camera frame received");}
  return {200, "image/jpeg", std::string(camera_data_.begin(), camera_data_.end()), {{"Cache-Control", "no-store"}}};
}

HttpResponse VehicleOpsApi::pipeline_live()
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!pipeline_trace_) {return error(404, "No pipeline trace received");}
  auto value = pipeline_trace_json(*pipeline_trace_);
  value["received_age_ms"] = std::chrono::duration<double, std::milli>(
    std::chrono::steady_clock::now() - pipeline_received_).count();
  return {200, "application/json; charset=utf-8", value.dump() + "\n", {{"Cache-Control", "no-store"}}};
}
HttpResponse VehicleOpsApi::pipeline_history()
{
  std::lock_guard<std::mutex> lock(mutex_);
  nlohmann::json values = nlohmann::json::array();
  for (const auto & trace : pipeline_history_) {values.push_back(pipeline_trace_json(trace));}
  return {200, "application/json; charset=utf-8", nlohmann::json({{"traces", values}}).dump() + "\n",
    {{"Cache-Control", "no-store"}}};
}HttpResponse VehicleOpsApi::active_debug_create(const std::string & body)
{
  nlohmann::json input;
  try {input = nlohmann::json::parse(body);}
  catch (const nlohmann::json::exception &) {return error(400, "Request body must be valid JSON");}
  const auto task_value = input.value("task", "");
  if (task_value.empty()) {return error(400, "Active debug task is required");}
  std::lock_guard<std::mutex> lock(mutex_);
  if (active_debug_.status == "RUNNING" || active_debug_.status == "PAUSED") {
    return error(409, "An active debug session is already running");
  }
  const auto now = std::chrono::steady_clock::now();
  active_debug_ = {};
  active_debug_.id = "active-debug-" + std::to_string(
    std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
  active_debug_.task = task_value;
  active_debug_.status = "RUNNING";
  active_debug_.started = now;
  active_debug_.baseline_observation_id = pipeline_trace_ ? pipeline_trace_->observation_id : "";
  active_debug_.waiting_for_task_observation = true;
  active_debug_.max_steps = std::clamp(input.value("max_steps", 100), 1, 10000);
  active_debug_.max_duration_seconds = std::clamp(input.value("max_duration_seconds", 60.0), 1.0, 3600.0);
  active_debug_.frequency_hz = std::clamp(input.value("hz", 1.0), 0.1, 5.0);
  active_debug_.shadow_only = input.value("mode", "shadow") != "controlled_real";
  std_msgs::msg::String message; message.data = task_value; task_pub_->publish(message);
  return {202, "application/json; charset=utf-8", nlohmann::json({
    {"success", true}, {"message", "Active debug session started in Shadow mode"},
    {"session_id", active_debug_.id}, {"status", active_debug_.status}}).dump() + "\n", {}};
}
HttpResponse VehicleOpsApi::active_debug_get()
{
  std::lock_guard<std::mutex> lock(mutex_);
  const auto elapsed_end = active_debug_.finished.time_since_epoch().count() == 0 ?
    std::chrono::steady_clock::now() : active_debug_.finished;
  const auto elapsed = active_debug_.started.time_since_epoch().count() == 0 ? 0.0 :
    std::chrono::duration<double>(elapsed_end - active_debug_.started).count();
  return {200, "application/json; charset=utf-8", nlohmann::json({
    {"session_id", active_debug_.id}, {"task", active_debug_.task}, {"status", active_debug_.status},
    {"stop_reason", active_debug_.stop_reason}, {"steps", active_debug_.steps},
    {"elapsed_seconds", elapsed}, {"max_steps", active_debug_.max_steps},
    {"max_duration_seconds", active_debug_.max_duration_seconds}, {"frequency_hz", active_debug_.frequency_hz},
    {"shadow_only", active_debug_.shadow_only}, {"last_observation_id", active_debug_.last_observation_id},
    {"last_trace_id", active_debug_.last_trace_id},
    {"waiting_for_task_observation", active_debug_.waiting_for_task_observation}}).dump() + "\n",
    {{"Cache-Control", "no-store"}}};
}
HttpResponse VehicleOpsApi::active_debug_control(const std::string & action)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (active_debug_.status == "IDLE") {return error(404, "No active debug session");}
  if (action == "pause" && active_debug_.status == "RUNNING") active_debug_.status = "PAUSED";
  else if (action == "resume" && active_debug_.status == "PAUSED") active_debug_.status = "RUNNING";
  else if (action == "stop" && (active_debug_.status == "RUNNING" || active_debug_.status == "PAUSED")) {
    active_debug_.status = "STOPPED"; active_debug_.stop_reason = "Stopped by operator";
    active_debug_.finished = std::chrono::steady_clock::now();
  } else {return error(409, "Invalid active debug operation");}
  return success("Active debug session updated");
}
void VehicleOpsApi::active_debug_tick()
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (active_debug_.status != "RUNNING") return;
  const auto now = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::duration<double>(now - active_debug_.started).count();
  if (elapsed >= active_debug_.max_duration_seconds) {
    active_debug_.status = "TIMED_OUT"; active_debug_.stop_reason = "Maximum duration reached";
    active_debug_.finished = now; return;
  }
  if (active_debug_.steps >= active_debug_.max_steps) {
    active_debug_.status = "STOPPED"; active_debug_.stop_reason = "Maximum steps reached";
    active_debug_.finished = now; return;
  }
  constexpr double task_observation_timeout_seconds = 5.0;
  if (!pipeline_trace_) {
    if (active_debug_.waiting_for_task_observation && elapsed >= task_observation_timeout_seconds) {
      active_debug_.status = "FAILED";
      active_debug_.stop_reason = "Timed out waiting for an Observation containing the active task";
      active_debug_.finished = now;
    }
    return;
  }
  const auto & trace = *pipeline_trace_;
  if (active_debug_.waiting_for_task_observation) {
    std::string observation_task;
    for (const auto & stage : trace.stages) {
      if (stage.stage_id != "observation_assembly") continue;
      try {
        const auto detail = nlohmann::json::parse(stage.detail_json);
        if (detail.contains("task") && detail.at("task").is_string()) {
          observation_task = detail.at("task").get<std::string>();
        }
      } catch (const nlohmann::json::exception &) {
      }
      break;
    }
    const bool is_new_observation = !trace.observation_id.empty() &&
      trace.observation_id != active_debug_.baseline_observation_id;
    if (!is_new_observation || observation_task != active_debug_.task) {
      if (elapsed >= task_observation_timeout_seconds) {
        active_debug_.status = "FAILED";
        active_debug_.stop_reason = "Timed out waiting for an Observation containing the active task";
        active_debug_.finished = now;
      }
      return;
    }
    active_debug_.waiting_for_task_observation = false;
  }
  const auto step_period = std::chrono::duration<double>(1.0 / active_debug_.frequency_hz);
  if (active_debug_.last_step.time_since_epoch().count() != 0 &&
    std::chrono::duration<double>(now - active_debug_.last_step).count() < step_period.count()) return;
  if (trace.observation_id.empty() || trace.observation_id == active_debug_.last_observation_id) return;
  active_debug_.last_observation_id = trace.observation_id;
  active_debug_.last_trace_id = trace.trace_id;
  active_debug_.last_step = now;
  ++active_debug_.steps;
  for (const auto & stage : trace.stages) {
    const auto message = lowercase(stage.message + " " + stage.output_summary);
    if (message.find("success") != std::string::npos ||
      message.find("completed") != std::string::npos || message.find("terminate") != std::string::npos) {
      active_debug_.status = "SUCCEEDED"; active_debug_.stop_reason = stage.label + ": task completion reported";
      active_debug_.finished = now; return;
    }
    if (stage.status == vehicle_interfaces::msg::PipelineStage::STATUS_FAILED ||
      stage.status == vehicle_interfaces::msg::PipelineStage::STATUS_REJECTED) {
      active_debug_.status = "FAILED"; active_debug_.stop_reason = stage.label + ": " + stage.message;
      active_debug_.finished = now; return;
    }
  }
}HttpResponse VehicleOpsApi::components_list()
{
  if (!components_client_->wait_for_service(250ms)) {return error(503, "Operation Orchestrator is unavailable");}
  auto future = components_client_->async_send_request(std::make_shared<vehicle_interfaces::srv::ListComponents::Request>());
  if (future.wait_for(std::chrono::milliseconds(static_cast<int>(service_seconds_ * 1000))) != std::future_status::ready) {
    return error(408, "Component status request timed out");
  }
  const auto response = future.get();
  nlohmann::json components = nlohmann::json::array();
  for (const auto & component : response->components) {components.push_back(component_state_json(component));}
  return {200, "application/json; charset=utf-8",
    nlohmann::json({{"components", components}, {"profiles", response->profiles}}).dump() + "\n",
    {{"Cache-Control", "no-store"}}};
}
HttpResponse VehicleOpsApi::component_control(const std::string & body)
{
  if (!component_control_client_->wait_for_service(250ms)) {return error(503, "Operation Orchestrator is unavailable");}
  nlohmann::json input;
  try {input = nlohmann::json::parse(body);}
  catch (const nlohmann::json::exception &) {return error(400, "Request body must be valid JSON");}
  if (!input.contains("component_id") || !input.at("component_id").is_string() ||
    !input.contains("action") || !input.at("action").is_string())
  {return error(400, "component_id and action are required");}
  auto request = std::make_shared<vehicle_interfaces::srv::ControlComponent::Request>();
  request->component_id = input.at("component_id").get<std::string>();
  request->action = input.at("action").get<std::string>();
  request->force = input.value("force", false);
  auto future = component_control_client_->async_send_request(request);
  if (future.wait_for(std::chrono::milliseconds(static_cast<int>(service_seconds_ * 4000))) != std::future_status::ready) {
    return error(408, "Component operation timed out");
  }
  const auto response = future.get();
  if (!response->accepted) {return error(409, response->message);}
  return success(response->message, "\"component\":" + component_state_json(response->component).dump());
}
HttpResponse VehicleOpsApi::profile_control(const std::string & body)
{
  if (!profile_control_client_->wait_for_service(250ms)) {return error(503, "Operation Orchestrator is unavailable");}
  nlohmann::json input;
  try {input = nlohmann::json::parse(body);}
  catch (const nlohmann::json::exception &) {return error(400, "Request body must be valid JSON");}
  if (!input.contains("profile_id") || !input.at("profile_id").is_string() ||
    !input.contains("action") || !input.at("action").is_string())
  {return error(400, "profile_id and action are required");}
  auto request = std::make_shared<vehicle_interfaces::srv::ControlProfile::Request>();
  request->profile_id = input.at("profile_id").get<std::string>();
  request->action = input.at("action").get<std::string>();
  auto future = profile_control_client_->async_send_request(request);
  if (future.wait_for(std::chrono::milliseconds(static_cast<int>(service_seconds_ * 8000))) != std::future_status::ready) {
    return error(408, "Profile operation timed out");
  }
  const auto response = future.get();
  if (!response->accepted) {return error(409, response->message);}
  return success(response->message);
}
HttpResponse VehicleOpsApi::component_log(const std::string & component_id)
{
  if (!component_log_client_->wait_for_service(250ms)) {return error(503, "Operation Orchestrator is unavailable");}
  auto request = std::make_shared<vehicle_interfaces::srv::ReadComponentLog::Request>();
  request->component_id = component_id;
  auto future = component_log_client_->async_send_request(request);
  if (future.wait_for(std::chrono::milliseconds(static_cast<int>(service_seconds_ * 2000))) != std::future_status::ready) {
    return error(408, "Component log request timed out");
  }
  const auto response = future.get();
  if (!response->accepted) {return error(404, response->message);}
  return {200, "text/plain; charset=utf-8", response->content, {{"Cache-Control", "no-store"}}};
}

HttpResponse VehicleOpsApi::dataset_catalog()
{
  namespace fs = std::filesystem;
  nlohmann::json result{{"schema_version", "vehicle.ops.dataset-catalog.v1"}, {"episodes", nlohmann::json::array()}, {"exports", nlohmann::json::array()}, {"lerobot", nlohmann::json::array()}, {"archives", nlohmann::json::array()}};
  std::string active_episode_id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (episode_.message && episode_.message->state == vehicle_interfaces::msg::EpisodeState::STATE_RECORDING) {active_episode_id = episode_.message->episode_id;}
  }
  const auto scan = [&](const std::string & category, nlohmann::json & output) {
    const fs::path root = fs::path(project_root_) / "datasets" / category;
    if (!fs::exists(root)) {return;}
    for (const auto & entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied)) {
      if (!entry.is_directory()) {continue;}
      std::uint64_t files = 0;
      const auto bytes = directory_bytes(entry.path(), files);
      nlohmann::json item{{"id", entry.path().filename().string()}, {"path", "datasets/" + category + "/" + entry.path().filename().string()}, {"name", entry.path().filename().string()}, {"bytes", bytes}, {"file_count", files}, {"modified_at", file_time_string(entry.path())}, {"protected", category == "episodes" && entry.path().filename().string() == active_episode_id}};
      const auto manifest = entry.path() / (category == "episodes" ? "episode_manifest.json" : "dataset_manifest.json");
      if (fs::exists(manifest)) {
        try { item["manifest"] = nlohmann::json::parse(std::ifstream(manifest)); } catch (...) { item["manifest_error"] = true; }
      }
      if (category == "episodes") {
        item["frame_count"] = count_lines(entry.path() / "frames.jsonl");
        item["format"] = "vehicle.episode.v1 / rosbag2";
      } else if (category == "exports") {
        item["frame_count"] = count_lines(entry.path() / "frames.jsonl");
        item["format"] = "vehicle.dataset.v1";
      } else {
        item["format"] = "LeRobot";
        item["frame_count"] = fs::exists(entry.path() / "data") ? files : 0;
        item["training_ready"] = fs::exists(entry.path() / "meta" / "info.json");
      }
      output.push_back(item);
    }
  };
  scan("episodes", result["episodes"]);
  scan("exports", result["exports"]);
  scan("lerobot", result["lerobot"]);
  const auto archive_root = fs::path(project_root_) / "run/ops/exports";
  if (fs::exists(archive_root)) {
    for (const auto & entry : fs::directory_iterator(archive_root, fs::directory_options::skip_permission_denied)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".zip") {continue;}
      result["archives"].push_back({{"name", entry.path().filename().string()}, {"path", "run/ops/exports/" + entry.path().filename().string()}, {"bytes", entry.file_size()}, {"modified_at", file_time_string(entry.path())}});
    }
  }
  return {200, "application/json; charset=utf-8", result.dump() + "\n", {{"Cache-Control", "no-store"}}};
}
HttpResponse VehicleOpsApi::dataset_detail(const std::string & relative_path)
{
  namespace fs = std::filesystem;
  if (relative_path.empty() || relative_path.find("..") != std::string::npos || relative_path.find('\\') != std::string::npos) {return error(400, "Invalid dataset path");}
  const auto root = fs::weakly_canonical(fs::path(project_root_) / "datasets");
  const auto path = fs::weakly_canonical(fs::path(project_root_) / relative_path);
  if (!fs::is_directory(path) || (path != root && path.string().rfind((root / "").string(), 0) != 0)) {return error(404, "Dataset not found under datasets/");}
  std::uint64_t files = 0;
  nlohmann::json result{{"path", relative_path}, {"name", path.filename().string()}, {"bytes", directory_bytes(path, files)}, {"file_count", files}, {"modified_at", file_time_string(path)}, {"files", nlohmann::json::array()}};
  for (const auto & entry : fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied)) {
    if (entry.is_regular_file()) {result["files"].push_back({{"path", fs::relative(entry.path(), path).generic_string()}, {"bytes", entry.file_size()}});}
  }
  std::string review_key = relative_path;
  std::replace(review_key.begin(), review_key.end(), '/', '_');
  const auto review_path = fs::path(project_root_) / "run/ops/dataset-reviews" / (review_key + ".json");
  if (fs::exists(review_path)) {
    try {result["review"] = nlohmann::json::parse(std::ifstream(review_path));}
    catch (...) {result["review_error"] = true;}
  }
  const auto quality_root = fs::path(project_root_) / "run/test/dataset-quality";
  fs::path latest_report;
  fs::file_time_type latest_time{};
  if (fs::is_directory(quality_root)) {
    for (const auto & entry : fs::recursive_directory_iterator(quality_root, fs::directory_options::skip_permission_denied)) {
      if (!entry.is_regular_file() || entry.path().filename() != "vehicle_ops_source.json") {continue;}
      try {
        const auto source = nlohmann::json::parse(std::ifstream(entry.path()));
        std::error_code source_error;
        const auto source_path = fs::weakly_canonical(source.value("dataset_path", ""), source_error);
        const auto report_path = entry.path().parent_path() / "dataset_quality_report.json";
        if (!source_error && source_path == path && fs::is_regular_file(report_path)) {
          const auto modified = fs::last_write_time(report_path);
          if (latest_report.empty() || modified > latest_time) {latest_report = report_path; latest_time = modified;}
        }
      } catch (...) {}
    }
  }
  if (!latest_report.empty()) {
    try {result["quality_report"] = nlohmann::json::parse(std::ifstream(latest_report));}
    catch (...) {result["quality_report_error"] = true;}
  }
  return {200, "application/json; charset=utf-8", result.dump() + "\n", {{"Cache-Control", "no-store"}}};
}
HttpResponse VehicleOpsApi::dataset_download(const std::string & archive_name)
{
  namespace fs = std::filesystem;
  if (archive_name.empty() || archive_name.find("..") != std::string::npos || archive_name.find('/') != std::string::npos || archive_name.find('\\') != std::string::npos || archive_name.size() > 160) {return error(400, "Invalid archive name");}
  const auto root = fs::weakly_canonical(fs::path(project_root_) / "run/ops/exports");
  const auto archive = fs::weakly_canonical(root / archive_name);
  const auto archive_string = archive.string();
  const auto root_string = (root / "").string();
  if (!fs::is_regular_file(archive) || archive.extension() != ".zip" || archive_string.rfind(root_string, 0) != 0) {return error(404, "Export archive not found");}
  if (fs::file_size(archive) > 512ULL * 1024ULL * 1024ULL) {return error(413, "Export archive is too large for the current download endpoint");}
  return {200, "application/zip", read_file(archive), {{"Content-Disposition", "attachment; filename=\"" + archive.filename().string() + "\""}, {"Cache-Control", "no-store"}}};
}
HttpResponse VehicleOpsApi::dataset_review(const std::string & body)
{
  namespace fs = std::filesystem;
  const auto input = nlohmann::json::parse(body, nullptr, false);
  if (input.is_discarded() || !input.is_object()) {return error(400, "Request body must be valid JSON");}
  const auto relative_path = input.value("path", std::string{});
  const auto status = input.value("status", std::string{});
  const auto note = input.value("note", std::string{});
  if (relative_path.empty() || relative_path.find("..") != std::string::npos ||
    relative_path.find('\\') != std::string::npos)
  {return error(400, "Invalid dataset path");}
  if (status != "pending" && status != "accepted" && status != "rejected") {
    return error(400, "Review status must be pending, accepted or rejected");
  }
  if (note.size() > 2000) {return error(400, "Review note is too long");}
  const auto dataset_root = fs::weakly_canonical(fs::path(project_root_) / "datasets");
  const auto dataset_path = fs::weakly_canonical(fs::path(project_root_) / relative_path);
  if (!fs::is_directory(dataset_path) ||
    dataset_path.string().rfind((dataset_root / "").string(), 0) != 0)
  {return error(404, "Dataset not found under datasets/");}
  nlohmann::json excluded_frames = input.value("excluded_frames", nlohmann::json::array());
  if (!excluded_frames.is_array() || excluded_frames.size() > 10000) {
    return error(400, "excluded_frames must be an array with at most 10000 items");
  }
  for (const auto & frame : excluded_frames) {
    if (!frame.is_number_unsigned() && !(frame.is_number_integer() && frame.get<long long>() >= 0)) {
      return error(400, "excluded_frames must contain non-negative frame indexes");
    }
  }
  std::string key = relative_path;
  std::replace(key.begin(), key.end(), '/', '_');
  const auto review_root = fs::path(project_root_) / "run/ops/dataset-reviews";
  fs::create_directories(review_root);
  const auto review_path = review_root / (key + ".json");
  const nlohmann::json review{{"schema_version", "vehicle.dataset.review.v1"},
    {"path", relative_path}, {"status", status}, {"note", note},
    {"excluded_frames", excluded_frames}, {"updated_at", utc_timestamp()}};
  const auto temporary = review_path.string() + ".partial";
  {std::ofstream stream(temporary); stream << review.dump(2) << '\n';}
  fs::rename(temporary, review_path);
  return {200, "application/json; charset=utf-8", review.dump() + "\n", {{"Cache-Control", "no-store"}}};
}

HttpResponse VehicleOpsApi::mobility_plugins()
{
  const nlohmann::json result = load_mobility_plugins(std::filesystem::path(project_root_));
  return {200, "application/json; charset=utf-8", result.dump(), {{"Cache-Control", "no-store"}}};
}
HttpResponse VehicleOpsApi::models_catalog()
{
  namespace fs = std::filesystem;
  const auto project_root = fs::path(project_root_);
  const auto mobility_plugins = load_mobility_plugins(project_root);
  nlohmann::json active_mobility = nlohmann::json::object();
  const auto active_mobility_path = project_root / "run/config/mobility.json";
  if (fs::is_regular_file(active_mobility_path)) {
    try {active_mobility = read_json_file(active_mobility_path);}
    catch (const std::exception &) {active_mobility["manifest_error"] = true;}
  }
  const std::string active_plugin_id = active_mobility.value("plugin_id", "");
  nlohmann::json result{{"schema_version", "vehicle.ops.model-catalog.v1"},
    {"providers", nlohmann::json::array({{{"provider_id", "smolvla"},
      {"display_name", "SmolVLA"}, {"install_supported", true}, {"activate_supported", true}}})},
    {"models", nlohmann::json::array()},
    {"mobility", {{"active_plugin_id", active_plugin_id}, {"active", active_mobility},
      {"plugins", mobility_plugins}}}};
  fs::path active_root;
  std::string active_model_id;
  std::ifstream environment(fs::path(project_root_) / "run/config/smolvla-runtime.env");
  for (std::string line; std::getline(environment, line);) {
    if (line.rfind("SMOLVLA_MODEL_ROOT=", 0) == 0) {active_root = line.substr(19);}
    if (line.rfind("SMOLVLA_MODEL_ID=", 0) == 0) {active_model_id = line.substr(17);}
  }
  if (active_root.empty()) {active_root = fs::path(project_root_) / "run/models/smolvla_base";}
  std::error_code active_error;
  active_root = fs::weakly_canonical(active_root, active_error);
  const auto append_model = [&](const fs::path & model_path, const std::string & provider,
    const std::string & version, bool activatable) {
    if (!fs::is_directory(model_path)) {return;}
    std::uint64_t files = 0;
    nlohmann::json item{{"provider", provider}, {"version", version}, {"path", model_path.string()},
      {"bytes", directory_bytes(model_path, files)}, {"file_count", files},
      {"modified_at", file_time_string(model_path)}, {"activatable", activatable},
      {"valid", fs::exists(model_path / "config.json") && fs::exists(model_path / "model.safetensors") &&
        fs::exists(model_path / "policy_preprocessor.json") && fs::exists(model_path / "policy_postprocessor.json")}};
    std::error_code model_error;
    item["active"] = fs::weakly_canonical(model_path, model_error) == active_root;
    nlohmann::json compatibility{{"status", "missing_action_descriptor"},
      {"action_schema", ""}, {"recommended_plugin_ids", nlohmann::json::array()},
      {"active_plugin_id", active_plugin_id}, {"active_plugin_compatible", true},
      {"activation_allowed", true}, {"uses_zero_adapter", true}};
    nlohmann::json runtime_compatibility{{"status", "compatible"},
      {"backend", "pytorch"}, {"weight_precision", "mixed"},
      {"activation_precision", "bfloat16"}, {"activation_allowed", true},
      {"message", "Legacy model uses the PyTorch mixed-precision runtime"}};
    const auto manifest_path = model_path / "vehicle_model_manifest.json";
    if (fs::exists(manifest_path)) {
      try {
        item["manifest"] = read_json_file(manifest_path);
        const auto & manifest = item["manifest"];
        const auto inference = manifest.contains("inference") && manifest["inference"].is_object() ?
          manifest["inference"] : nlohmann::json::object();
        const std::string backend = inference.value("backend", "pytorch");
        const std::string weight_precision = inference.value(
          "weight_precision", inference.value("precision", "mixed"));
        runtime_compatibility["backend"] = backend;
        runtime_compatibility["weight_precision"] = weight_precision;
        runtime_compatibility["activation_precision"] = inference.value(
          "activation_precision", "bfloat16");
        if (backend != "pytorch") {
          runtime_compatibility["status"] = "unsupported_backend";
          runtime_compatibility["activation_allowed"] = false;
          runtime_compatibility["message"] =
            "Current platform build does not provide this inference backend";
        } else if (weight_precision == "int8") {
          const auto quantization = inference.contains("quantization") &&
            inference["quantization"].is_object() ?
            inference["quantization"] : nlohmann::json::object();
          const std::string engine = quantization.value("engine", "pytorch-native");
          if (engine != "torchao" && engine != "pytorch-native") {
            runtime_compatibility["status"] = "unsupported_quantization";
            runtime_compatibility["activation_allowed"] = false;
            runtime_compatibility["message"] =
              "INT8 requires the native PyTorch or TorchAO weight-only backend";
          } else if (engine == "torchao") {
            runtime_compatibility["status"] = "requires_runtime_check";
            runtime_compatibility["message"] =
              "Activation will verify TorchAO in the selected runtime image";
          } else {
            runtime_compatibility["message"] =
              "Portable PyTorch INT8 reference backend is available";
          }
        } else {
          runtime_compatibility["message"] =
            "Model precision is supported by the PyTorch backend";
        }
        if (manifest.contains("action") && !manifest["action"].is_null() &&
          !manifest["action"].is_object())
        {
          compatibility["status"] = "invalid_action_descriptor";
          compatibility["active_plugin_compatible"] = false;
          compatibility["activation_allowed"] = false;
        } else if (manifest.contains("action") && manifest["action"].is_object()) {
          const std::string action_schema = manifest["action"].value("schema", "");
          compatibility["action_schema"] = action_schema;
          compatibility["uses_zero_adapter"] = false;
          if (action_schema.empty()) {
            compatibility["status"] = "invalid_action_descriptor";
            compatibility["active_plugin_compatible"] = false;
            compatibility["activation_allowed"] = false;
          } else {
            const auto recommended = recommended_mobility_plugins(mobility_plugins, action_schema);
            compatibility["recommended_plugin_ids"] = recommended;
            const bool active_compatible = !active_plugin_id.empty() &&
              mobility_plugin_accepts(active_mobility, action_schema);
            compatibility["active_plugin_compatible"] = active_compatible;
            compatibility["activation_allowed"] = active_compatible;
            if (recommended.empty()) {
              compatibility["status"] = "unsupported_schema";
            } else if (active_plugin_id.empty()) {
              compatibility["status"] = "no_active_plugin";
            } else {
              compatibility["status"] = active_compatible ? "compatible" : "incompatible";
            }
          }
        }
      } catch (const std::exception &) {
        item["manifest_error"] = true;
        compatibility["status"] = "manifest_error";
        compatibility["active_plugin_compatible"] = false;
        compatibility["activation_allowed"] = false;
        runtime_compatibility["status"] = "manifest_error";
        runtime_compatibility["activation_allowed"] = false;
        runtime_compatibility["message"] = "Model runtime manifest cannot be parsed";
      }
    }
    compatibility["activation_allowed"] = compatibility.value("activation_allowed", true) &&
      runtime_compatibility.value("activation_allowed", true);
    item["compatibility"] = std::move(compatibility);
    item["runtime_compatibility"] = std::move(runtime_compatibility);
    result["models"].push_back(item);
  };
  append_model(fs::path(project_root_) / "run/models/smolvla_base", "smolvla", "legacy-base", false);
  const auto registry = fs::path(project_root_) / "run/models/providers/smolvla";
  if (fs::is_directory(registry)) {
    for (const auto & entry : fs::directory_iterator(registry, fs::directory_options::skip_permission_denied)) {
      if (entry.is_directory() && entry.path().extension() != ".partial") {
        append_model(entry.path(), "smolvla", entry.path().filename().string(), true);
      }
    }
  }
  result["active_model_id"] = active_model_id;
  return {200, "application/json; charset=utf-8", result.dump() + "\n", {{"Cache-Control", "no-store"}}};
}

HttpResponse VehicleOpsApi::storage_status()
{
  if (!storage_status_client_->wait_for_service(250ms)) {return error(503, "Storage Manager is unavailable");}
  auto future = storage_status_client_->async_send_request(
    std::make_shared<vehicle_interfaces::srv::GetStorageStatus::Request>());
  if (future.wait_for(std::chrono::milliseconds(static_cast<int>(service_seconds_ * 3000))) != std::future_status::ready) {
    return error(408, "Storage status request timed out");
  }
  const auto response = future.get();
  nlohmann::json categories = nlohmann::json::array();
  for (const auto & category : response->categories) {
    categories.push_back({
      {"category_id", category.category_id}, {"display_name", category.display_name},
      {"path", category.path}, {"bytes", category.bytes}, {"item_count", category.item_count},
      {"cleanup_allowed", category.cleanup_allowed}, {"automatic_cleanup", category.automatic_cleanup},
      {"message", category.message}});
  }
  nlohmann::json value = {
    {"total_bytes", response->total_bytes}, {"used_bytes", response->used_bytes},
    {"available_bytes", response->available_bytes}, {"used_percent", response->used_percent},
    {"level", response->level}, {"message", response->message}, {"categories", categories}};
  return {200, "application/json; charset=utf-8", value.dump() + "\n", {{"Cache-Control", "no-store"}}};
}

HttpResponse VehicleOpsApi::storage_items(const std::string & category_id)
{
  if (category_id.empty()) {return error(400, "Storage category is required");}
  if (!storage_items_client_->wait_for_service(250ms)) {return error(503, "Storage Manager is unavailable");}
  auto request = std::make_shared<vehicle_interfaces::srv::ListStorageItems::Request>();
  request->category_id = category_id;
  auto future = storage_items_client_->async_send_request(request);
  if (future.wait_for(std::chrono::milliseconds(static_cast<int>(service_seconds_ * 3000))) != std::future_status::ready) {
    return error(408, "Storage item request timed out");
  }
  const auto response = future.get();
  if (!response->accepted) {return error(404, response->message);}
  nlohmann::json items = nlohmann::json::array();
  for (const auto & item : response->items) {
    items.push_back({
      {"item_id", item.item_id}, {"display_name", item.display_name}, {"path", item.path},
      {"bytes", item.bytes}, {"modified_at", {{"sec", item.modified_at.sec}, {"nanosec", item.modified_at.nanosec}}},
      {"protected", item.is_protected}, {"message", item.message}});
  }
  return {200, "application/json; charset=utf-8",
    nlohmann::json({{"category_id", category_id}, {"items", items}}).dump() + "\n",
    {{"Cache-Control", "no-store"}}};
}

HttpResponse VehicleOpsApi::storage_cleanup(const std::string & body)
{
  nlohmann::json input;
  try {input = nlohmann::json::parse(body);}
  catch (const nlohmann::json::exception &) {return error(400, "Request body must be valid JSON");}
  if (!input.contains("category_id") || !input.at("category_id").is_string() ||
    !input.contains("item_ids") || !input.at("item_ids").is_array())
  {return error(400, "category_id and item_ids are required");}
  auto request = std::make_shared<vehicle_interfaces::srv::CleanupStorage::Request>();
  request->category_id = input.at("category_id").get<std::string>();
  request->item_ids = input.at("item_ids").get<std::vector<std::string>>();
  request->dry_run = input.value("dry_run", true);
  if (!storage_cleanup_client_->wait_for_service(250ms)) {return error(503, "Storage Manager is unavailable");}
  auto future = storage_cleanup_client_->async_send_request(request);
  if (future.wait_for(std::chrono::milliseconds(static_cast<int>(service_seconds_ * 5000))) != std::future_status::ready) {
    return error(408, "Storage cleanup request timed out");
  }
  const auto response = future.get();
  if (!response->accepted) {return error(409, response->message);}
  return {200, "application/json; charset=utf-8", nlohmann::json({
    {"success", true}, {"message", response->message}, {"dry_run", request->dry_run},
    {"bytes", response->bytes}, {"item_count", response->item_count}}).dump() + "\n",
    {{"Cache-Control", "no-store"}}};
}

HttpResponse VehicleOpsApi::teleop_status()
{
  nlohmann::json response;
  const auto server_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
  std::lock_guard<std::mutex> lock(mutex_);
  response["server_time_ms"] = server_time_ms;
  response["lease_received"] = control_lease_.message.has_value();
  response["lease"] = control_lease_.message ?
    control_lease_json(*control_lease_.message) : nlohmann::json::object();
  response["system"] = nlohmann::json::object();
  if (system_.message) {
    response["system"] = {{"mode", system_.message->mode},
      {"mode_name", mode_name(system_.message->mode)},
      {"control_source", system_.message->control_source},
      {"safe_to_move", system_.message->safe_to_move},
      {"message", system_.message->status_message}};
  }
  response["episode"] = nlohmann::json::object();
  if (episode_.message) {
    response["episode"] = {{"state", episode_.message->state},
      {"state_name", episode_name(episode_.message->state)},
      {"episode_id", episode_.message->episode_id}, {"task", episode_.message->task},
      {"elapsed_seconds", episode_.message->elapsed_seconds},
      {"image_count", episode_.message->image_count},
      {"message_count", episode_.message->message_count}};
  }
  response["executed_command"] = {{"linear_x", latest_command_.linear.x},
    {"angular_z", latest_command_.angular.z}};
  const bool odometry_available = odometry_received_.time_since_epoch().count() != 0 &&
    std::chrono::duration<double>(std::chrono::steady_clock::now() - odometry_received_).count() <=
    odometry_timeout_seconds_;
  response["measured_motion"] = {{"available", odometry_available},
    {"linear_x", latest_odometry_.twist.twist.linear.x},
    {"angular_z", latest_odometry_.twist.twist.angular.z}};
  return {200, "application/json; charset=utf-8", response.dump(),
    {{"Cache-Control", "no-store"}}};
}

HttpResponse VehicleOpsApi::teleop_acquire(const std::string & body)
{
  nlohmann::json input;
  try {input = nlohmann::json::parse(body);}
  catch (const nlohmann::json::exception &) {return error(400, "Request body must be valid JSON");}
  const std::string controller_id = input.value("controller_id", "");
  const bool valid_controller = !controller_id.empty() && controller_id.size() <= 64 &&
    std::all_of(controller_id.begin(), controller_id.end(), [](unsigned char value) {
      return std::isalnum(value) || value == '-' || value == '_';
    });
  if (!valid_controller) {return error(400, "Invalid controller_id");}
  if (!acquire_lease_client_->wait_for_action_server(
      std::chrono::duration<double>(service_seconds_)))
  {
    return error(503, "Teleop lease manager is unavailable");
  }
  AcquireLease::Goal goal;
  goal.controller_id = controller_id;
  goal.source = "mobile";
  const int duration_seconds = std::clamp(input.value("duration_seconds", 30), 3, 30);
  goal.requested_duration.sec = duration_seconds;
  goal.requested_duration.nanosec = 0;
  goal.requested_max_linear_velocity = std::clamp(
    input.value("max_linear_velocity", 0.25F), 0.02F, 0.4F);
  goal.requested_max_angular_velocity = std::clamp(
    input.value("max_angular_velocity", 0.5F), 0.05F, 0.8F);
  goal.remote = false;
  auto goal_future = acquire_lease_client_->async_send_goal(goal);
  if (goal_future.wait_for(std::chrono::duration<double>(service_seconds_)) !=
    std::future_status::ready)
  {
    return error(408, "Teleop lease request timed out");
  }
  const auto goal_handle = goal_future.get();
  if (!goal_handle) {return error(409, "Another controller owns the active teleop lease");}
  auto result_future = acquire_lease_client_->async_get_result(goal_handle);
  if (result_future.wait_for(std::chrono::duration<double>(service_seconds_ + 1.0)) !=
    std::future_status::ready)
  {
    return error(408, "Teleop lease result timed out");
  }
  const auto wrapped = result_future.get();
  if (wrapped.code != rclcpp_action::ResultCode::SUCCEEDED || !wrapped.result ||
    !wrapped.result->granted)
  {
    return error(409, wrapped.result ? wrapped.result->message : "Teleop lease was rejected");
  }
  return success(wrapped.result->message,
    "\"lease\":" + control_lease_json(wrapped.result->lease).dump());
}

HttpResponse VehicleOpsApi::teleop_command(const std::string & body)
{
  nlohmann::json input;
  try {input = nlohmann::json::parse(body);}
  catch (const nlohmann::json::exception &) {return error(400, "Request body must be valid JSON");}
  const std::string lease_id = input.value("lease_id", "");
  const std::string controller_id = input.value("controller_id", "");
  const std::uint64_t sequence = input.value("sequence", std::uint64_t{0});
  const bool deadman = input.value("deadman", false);
  const bool manual_obstacle_override = input.value("manual_obstacle_override", false);
  const double linear = input.value("linear", 0.0);
  const double angular = input.value("angular", 0.0);
  const int valid_for_ms = std::clamp(input.value("valid_for_ms", 250), 50, 300);
  if (lease_id.empty() || controller_id.empty() || sequence == 0 ||
    !std::isfinite(linear) || !std::isfinite(angular) ||
    std::abs(linear) > 1.0 || std::abs(angular) > 1.0)
  {
    return error(400, "Invalid teleop command");
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!control_lease_.message || !control_lease_.message->active ||
      control_lease_.message->lease_id != lease_id ||
      control_lease_.message->controller_id != controller_id)
    {
      return error(409, "Teleop lease is not active for this controller");
    }
  }
  vehicle_interfaces::msg::TeleopCommand command;
  command.header.stamp = now();
  command.header.frame_id = "base_link";
  command.lease_id = lease_id;
  command.controller_id = controller_id;
  command.sequence = sequence;
  command.valid_for.sec = valid_for_ms / 1000;
  command.valid_for.nanosec = static_cast<std::uint32_t>(valid_for_ms % 1000) * 1000000U;
  command.deadman = deadman;
  command.manual_obstacle_override = deadman && manual_obstacle_override;
  command.linear_normalized = static_cast<float>(linear);
  command.angular_normalized = static_cast<float>(angular);
  teleop_command_pub_->publish(command);
  return success(deadman ? "Teleop command accepted" : "Teleop zero command accepted");
}

HttpResponse VehicleOpsApi::teleop_release(const std::string & body)
{
  nlohmann::json input;
  try {input = nlohmann::json::parse(body);}
  catch (const nlohmann::json::exception &) {return error(400, "Request body must be valid JSON");}
  const std::string lease_id = input.value("lease_id", "");
  const std::string controller_id = input.value("controller_id", "");
  if (lease_id.empty() || controller_id.empty()) {return error(400, "Lease identity is required");}
  if (!release_lease_client_->wait_for_service(std::chrono::duration<double>(service_seconds_))) {
    return error(503, "Teleop lease manager is unavailable");
  }
  auto request = std::make_shared<vehicle_interfaces::srv::ReleaseControlLease::Request>();
  request->lease_id = lease_id;
  request->controller_id = controller_id;
  auto future = release_lease_client_->async_send_request(request);
  if (future.wait_for(std::chrono::duration<double>(service_seconds_)) !=
    std::future_status::ready)
  {
    return error(408, "Teleop release timed out");
  }
  const auto response = future.get();
  if (!response->released) {return error(409, response->message);}
  return success(response->message);
}
HttpResponse VehicleOpsApi::task(const std::string & body)
{
  const auto value = trim(body);
  if (value.empty()) {return error(400, "Task text is required");}
  std_msgs::msg::String message;
  message.data = value;
  task_pub_->publish(message);
  return success("Task published", "\"task\":" + json_string(value));
}
HttpResponse VehicleOpsApi::start_episode(const std::string & body)
{
  if (!start_client_->wait_for_service(250ms)) {return error(503, "Episode Recorder is unavailable");}
  const auto fields = split_lines(body);
  auto request = std::make_shared<vehicle_interfaces::srv::StartEpisode::Request>();
  request->episode_id = fields.size() > 0 ? fields[0] : "";
  request->task = fields.size() > 1 ? fields[1] : "";
  request->operator_id = fields.size() > 2 && !fields[2].empty() ? fields[2] : "vehicle_ops_console";
  auto future = start_client_->async_send_request(request);
  if (future.wait_for(std::chrono::milliseconds(static_cast<int>(service_seconds_ * 1000))) != std::future_status::ready) {
    return error(408, "Episode start timed out");
  }
  const auto response = future.get();
  if (!response->accepted) {return error(400, response->message);}
  const auto extra = "\"episode_id\":" + json_string(response->resolved_episode_id) +
    ",\"directory\":" + json_string(response->directory);
  return success(response->message, extra);
}
HttpResponse VehicleOpsApi::stop_episode(const std::string & body)
{
  if (!stop_client_->wait_for_service(250ms)) {return error(503, "Episode Recorder is unavailable");}
  auto request = std::make_shared<vehicle_interfaces::srv::StopEpisode::Request>();
  request->success = true;
  request->reason = "Episode completed from Vehicle Ops";
  if (!trim(body).empty()) {
    const auto input = nlohmann::json::parse(body, nullptr, false);
    if (input.is_discarded() || !input.is_object()) {
      return error(400, "Episode stop body must be valid JSON");
    }
    request->success = input.value("success", true);
    request->reason = input.value("reason", request->reason);
  }
  auto future = stop_client_->async_send_request(request);
  if (future.wait_for(std::chrono::milliseconds(static_cast<int>(service_seconds_ * 1000))) != std::future_status::ready) {
    return error(408, "Episode stop timed out");
  }
  const auto response = future.get();
  if (!response->accepted) {return error(400, response->message);}
  return success(response->message, "\"directory\":" + json_string(response->directory));
}

HttpResponse VehicleOpsApi::safe_stop(const std::string & body)
{
  if (!safe_client_->wait_for_service(250ms)) {return error(503, "Vehicle Supervisor is unavailable");}
  const auto fields = split_lines(body);
  auto request = std::make_shared<vehicle_interfaces::srv::RequestSafeStop::Request>();
  request->requester = fields.size() > 0 && !fields[0].empty() ? fields[0] : "vehicle_ops_console";
  request->reason = fields.size() > 1 && !fields[1].empty() ? fields[1] : "Ops Console safe stop";
  auto future = safe_client_->async_send_request(request);
  if (future.wait_for(std::chrono::milliseconds(static_cast<int>(service_seconds_ * 1000))) != std::future_status::ready) {
    return error(408, "Safe stop timed out");
  }
  const auto response = future.get();
  return response->accepted ? success(response->message) : error(400, response->message);
}
HttpResponse VehicleOpsApi::enter_shadow_mode()
{
  if (!mode_client_->wait_for_service(250ms)) {
    return error(503, "Vehicle Supervisor is unavailable");
  }
  auto request = std::make_shared<vehicle_interfaces::srv::RequestControlMode::Request>();
  request->requested_mode = vehicle_interfaces::msg::SystemState::MODE_VLA_SHADOW;
  request->requester = "vehicle_ops_console";
  request->reason = "Operator entered VLA shadow debug mode";
  auto future = mode_client_->async_send_request(request);
  if (future.wait_for(std::chrono::milliseconds(static_cast<int>(service_seconds_ * 1000))) !=
    std::future_status::ready)
  {
    return error(408, "Control mode request timed out");
  }
  const auto response = future.get();
  if (!response->accepted) {return error(409, response->message);}
  return success(response->message,
    "\"mode\":\"VLA_SHADOW\",\"current_mode\":" +
    std::to_string(response->current_mode));
}
HttpResponse VehicleOpsApi::enter_single_step_mode()
{
  if (!mode_client_->wait_for_service(250ms)) {
    return error(503, "Vehicle Supervisor is unavailable");
  }
  auto request = std::make_shared<vehicle_interfaces::srv::RequestControlMode::Request>();
  request->requested_mode = vehicle_interfaces::msg::SystemState::MODE_VLA_ASSISTED;
  request->requester = "vehicle_ops_console";
  request->reason = "Operator entered VLA single-step control mode";
  auto future = mode_client_->async_send_request(request);
  if (future.wait_for(std::chrono::milliseconds(static_cast<int>(service_seconds_ * 1000))) !=
    std::future_status::ready)
  {
    return error(408, "Control mode request timed out");
  }
  const auto response = future.get();
  if (!response->accepted) {return error(409, response->message);}
  return success(response->message,
    "\"mode\":\"VLA_ASSISTED\",\"current_mode\":" +
    std::to_string(response->current_mode));
}

HttpResponse VehicleOpsApi::debug_capture(const std::string & body)
{
  if (!debug_capture_client_->wait_for_service(250ms)) {
    return error(503, "VLA Debug Orchestrator is unavailable");
  }
  auto request = std::make_shared<vehicle_interfaces::srv::CaptureVlaDebug::Request>();
  const auto content = trim(body);
  if (!content.empty() && content.front() == '{') {
    nlohmann::json input;
    try {input = nlohmann::json::parse(content);}
    catch (const nlohmann::json::exception &) {return error(400, "Request body must be valid JSON");}
    if (!input.contains("task") || !input.at("task").is_string()) {
      return error(400, "task is required");
    }
    request->task_override = input.at("task").get<std::string>();
    if (input.contains("image_base64")) {
      if (!input.at("image_base64").is_string()) {return error(400, "image_base64 must be a string");}
      try {request->image_override.data = decode_base64(input.at("image_base64").get<std::string>());}
      catch (const std::length_error & error_value) {return error(413, error_value.what());}
      catch (const std::exception & error_value) {return error(400, error_value.what());}
      if (!is_jpeg(request->image_override.data)) {return error(400, "Uploaded image must be JPEG");}
      request->use_image_override = true;
      request->image_override.format = "jpeg";
      request->image_source_name = input.value("image_name", "uploaded-image.jpg");
      if (request->image_source_name.size() > 128) {request->image_source_name.resize(128);}
    }
  } else {
    request->task_override = content;
  }
  if (!request->use_image_override) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (camera_content_analyzed_ && !camera_content_valid_) {
      return error(409, "Camera is publishing near-black frames; check the lens, USB connection, or restart the front camera");
    }
  }
  auto future = debug_capture_client_->async_send_request(request);
  if (future.wait_for(std::chrono::milliseconds(static_cast<int>(service_seconds_ * 1000))) !=
    std::future_status::ready)
  {
    return error(408, "VLA debug capture timed out");
  }
  const auto response = future.get();
  if (!response->accepted) {return error(400, response->message);}
  return success(response->message,
    "\"run_id\":" + json_string(response->run_id) +
    ",\"directory\":" + json_string(response->directory) +
    ",\"observation\":" + response->observation_json);
}
HttpResponse VehicleOpsApi::debug_run(const std::string & body)
{
  if (!debug_run_client_->wait_for_service(250ms)) {
    return error(503, "VLA Debug Orchestrator is unavailable");
  }
  nlohmann::json input;
  try {input = nlohmann::json::parse(body);}
  catch (const nlohmann::json::exception &) {return error(400, "Request body must be valid JSON");}
  if (!input.contains("run_id") || !input.at("run_id").is_string() ||
    !input.contains("stage") || !input.at("stage").is_string())
  {
    return error(400, "run_id and stage are required");
  }
  auto request = std::make_shared<vehicle_interfaces::srv::RunVlaDebug::Request>();
  request->run_id = input.at("run_id").get<std::string>();
  request->stage = input.at("stage").get<std::string>();
  request->mobility_plugin_id = input.value("mobility_plugin_id", "");
  request->execute_command = input.value("execute_command", false);
  request->operator_confirmation = input.value("operator_confirmation", "");
  request->execution_duration_ms = input.value("execution_duration_ms", 150U);
  auto future = debug_run_client_->async_send_request(request);
  if (future.wait_for(std::chrono::milliseconds(static_cast<int>(debug_timeout_seconds_ * 1000))) !=
    std::future_status::ready)
  {
    return error(408, "VLA debug stage timed out");
  }
  const auto response = future.get();
  if (!response->accepted) {return error(400, response->message);}
  return success(response->message, "\"result\":" + response->result_json);
}
HttpResponse VehicleOpsApi::debug_image(
  const std::string & run_id, const std::string & image_name)
{
  const bool valid_id = run_id.rfind("vla-debug-", 0) == 0 &&
    std::all_of(run_id.begin(), run_id.end(), [](unsigned char value) {
      return std::isalnum(value) || value == '-';
    });
  const std::string filename = image_name == "original.jpg" ? "camera-original.jpg" :
    (image_name == "processed.jpg" ? "camera-processed.jpg" : "");
  if (!valid_id || filename.empty()) {return error(404, "Debug image not found");}
  const auto path = std::filesystem::path(project_root_) / "run/ops/debug" / run_id / filename;
  try {
    return {200, "image/jpeg", read_file(path), {{"Cache-Control", "no-store"}}};
  } catch (const std::exception &) {return error(404, "Debug image not found");}
}
HttpResponse VehicleOpsApi::jobs_create(const std::string & body)
{
  try {
    const auto input = nlohmann::json::parse(body, nullptr, false);
    if (input.is_discarded()) {throw std::invalid_argument("Request body must be valid JSON");}
    const auto job_type = input.value("job_type", "");
    if (job_type == "policy.runtime_switch" || job_type == "policy.model_activate") {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto now = std::chrono::steady_clock::now();
      const auto system_age = age(system_, now);
      if (!system_.message || !fresh(system_age) ||
        system_.message->mode != vehicle_interfaces::msg::SystemState::MODE_VLA_SHADOW)
      {
        return error(409, "Policy Runtime changes require VLA_SHADOW mode");
      }
      if (active_debug_.status == "RUNNING" || active_debug_.status == "PAUSED") {
        return error(409, "Stop the active debug session before changing Policy Runtime");
      }
      if (episode_.message &&
        episode_.message->state == vehicle_interfaces::msg::EpisodeState::STATE_RECORDING)
      {
        return error(409, "Stop episode recording before changing Policy Runtime");
      }
      const bool command_recent = command_received_.time_since_epoch().count() != 0 &&
        std::chrono::duration<double>(now - command_received_).count() <= command_timeout_seconds_;
      if (command_recent &&
        (std::abs(latest_command_.linear.x) > stationary_linear_threshold_ ||
        std::abs(latest_command_.angular.z) > stationary_angular_threshold_))
      {
        return error(409, "Vehicle command is not stationary");
      }
    }
    if (job_type == "policy.model_activate") {
      const auto parameters = input.value("parameters", nlohmann::json::object());
      const std::string provider = parameters.value("provider", "");
      const std::string version = parameters.value("version", "");
      if (provider != "smolvla" || version.empty() ||
        version.find_first_of("/\\") != std::string::npos)
      {
        return error(400, "Invalid model activation request");
      }
      const auto project_root = std::filesystem::path(project_root_);
      const auto model_manifest_path = project_root / "run/models/providers/smolvla" / version /
        "vehicle_model_manifest.json";
      if (std::filesystem::is_regular_file(model_manifest_path)) {
        nlohmann::json manifest;
        try {manifest = read_json_file(model_manifest_path);}
        catch (const std::exception &) {return error(409, "Model manifest is invalid");}
        if (manifest.contains("action") && !manifest["action"].is_null() &&
          !manifest["action"].is_object())
        {
          return error(409, "Model action descriptor must be an object");
        }
        if (manifest.contains("action") && manifest["action"].is_object()) {
          const std::string action_schema = manifest["action"].value("schema", "");
          if (action_schema.empty()) {return error(409, "Model action descriptor has no schema");}
          const auto mobility_path = project_root / "run/config/mobility.json";
          if (!std::filesystem::is_regular_file(mobility_path)) {
            return error(409, "Activate a compatible mobility plugin before activating this model");
          }
          nlohmann::json active_mobility;
          try {active_mobility = read_json_file(mobility_path);}
          catch (const std::exception &) {return error(409, "Active mobility plugin metadata is invalid");}
          if (!mobility_plugin_accepts(active_mobility, action_schema)) {
            const auto recommended = recommended_mobility_plugins(
              load_mobility_plugins(project_root), action_schema);
            std::ostringstream message;
            message << "Model action schema " << action_schema << " is incompatible with " <<
              active_mobility.value("plugin_id", "the active mobility plugin");
            if (!recommended.empty()) {
              message << "; recommended plugin: " << recommended.front();
            }
            return error(409, message.str());
          }
        }
      }
    }
    return {202, "application/json; charset=utf-8", jobs_->create(body), {{"Cache-Control", "no-store"}}};
  } catch (const std::invalid_argument & error_value) {
    return error(400, error_value.what());
  } catch (const std::runtime_error & error_value) {
    return error(409, error_value.what());
  }
}
HttpResponse VehicleOpsApi::jobs_list()
{
  return {200, "application/json; charset=utf-8", jobs_->list(), {{"Cache-Control", "no-store"}}};
}
HttpResponse VehicleOpsApi::job_get(const std::string & job_id)
{
  const auto job = jobs_->get(job_id);
  return job ? HttpResponse{200, "application/json; charset=utf-8", *job, {{"Cache-Control", "no-store"}}} :
    error(404, "Job not found");
}
HttpResponse VehicleOpsApi::job_log(const std::string & job_id)
{
  const auto log_value = jobs_->log(job_id);
  return log_value ? HttpResponse{200, "text/plain; charset=utf-8", *log_value, {{"Cache-Control", "no-store"}}} :
    error(404, "Job not found");
}
HttpResponse VehicleOpsApi::job_cancel(const std::string & job_id)
{
  try {
    const auto job = jobs_->cancel(job_id);
    return job ? HttpResponse{200, "application/json; charset=utf-8", *job, {{"Cache-Control", "no-store"}}} :
      error(404, "Job not found");
  } catch (const std::runtime_error & error_value) {
    return error(409, error_value.what());
  }
}
HttpResponse VehicleOpsApi::success(const std::string & message, const std::string & extra)
{
  std::string body = "{\"success\":true,\"message\":" + json_string(message);
  if (!extra.empty()) {body += "," + extra;}
  body += "}\n";
  return {200, "application/json; charset=utf-8", body, {{"Cache-Control", "no-store"}}};
}
HttpResponse VehicleOpsApi::error(int status, const std::string & message)
{
  return {status, "application/json; charset=utf-8",
    "{\"success\":false,\"message\":" + json_string(message) + "}\n",
    {{"Cache-Control", "no-store"}}};
}
std::string VehicleOpsApi::status_json()
{
  const auto host = host_metrics();
  const auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(mutex_);
  const auto system_age = age(system_, now);
  const auto observation_age = age(observation_, now);
  const auto episode_age = age(episode_, now);
  const auto policy_age = age(policy_, now);
  const auto shadow_age = age(shadow_, now);
  const auto safety_age = age(safety_, now);
  const auto camera_age = camera_data_.empty() ? -1.0 :
    std::chrono::duration<double, std::milli>(now - camera_time_).count();
  const auto camera_received_at_ms = camera_data_.empty() ? 0LL :
    std::chrono::duration_cast<std::chrono::milliseconds>(
    camera_wall_time_.time_since_epoch()).count();
  std::ostringstream out;
  out << std::fixed << std::setprecision(2);
  out << "{\"schema_version\":\"vehicle.ops.status.v1\",";
  out << "\"server\":{\"write_enabled\":" << (!token_.empty() ? "true" : "false") << "},";
  out << "\"host\":{\"load_one\":" << host.load <<
    ",\"cpu_usage_percent\":" << host.cpu_usage <<
    ",\"cpu_temperature_c\":" << host.cpu_temperature <<
    ",\"cpu_cores\":" << host.cpu_cores <<
    ",\"memory_total_mb\":" << host.total <<
    ",\"memory_available_mb\":" << host.available <<
    ",\"swap_total_mb\":" << host.swap_total <<
    ",\"swap_free_mb\":" << host.swap_free <<
    ",\"gpu_usage_percent\":" << host.gpu_usage <<
    ",\"gpu_temperature_c\":" << host.gpu_temperature <<
    ",\"gpu_frequency_mhz\":" << host.gpu_frequency_mhz <<
    ",\"gpu_available\":" << (host.gpu_available ? "true" : "false") <<
    ",\"gpu_backend\":\"" << host.gpu_backend << "\"" <<
    ",\"gpu_memory_mode\":\"" << host.gpu_memory_mode << "\""
    ",\"uptime_seconds\":" << host.uptime << "},";  out << "\"system\":{\"received\":" << (system_.message ? "true" : "false") <<
    ",\"fresh\":" << (fresh(system_age) ? "true" : "false") << ",\"age_ms\":" << system_age;
  if (system_.message) {
    const auto & m = *system_.message;
    out << ",\"mode_name\":" << json_string(mode_name(m.mode)) <<
      ",\"control_source\":" << json_string(m.control_source) <<
      ",\"active_policy_id\":" << json_string(m.active_policy_id) <<
      ",\"safe_to_move\":" << (m.safe_to_move ? "true" : "false") <<
      ",\"message\":" << json_string(m.status_message);
  }
  out << "},\"observation\":{\"received\":" << (observation_.message ? "true" : "false") <<
    ",\"fresh\":" << (fresh(observation_age) ? "true" : "false") << ",\"age_ms\":" << observation_age;
  if (observation_.message) {
    const auto & m = *observation_.message;
    out << ",\"ready\":" << (m.ready ? "true" : "false") <<
      ",\"camera_ready\":" << (m.camera_ready ? "true" : "false") <<
      ",\"camera_calibrated\":" << (m.camera_calibrated ? "true" : "false") <<
      ",\"odometry_ready\":" << (m.odometry_ready ? "true" : "false") <<
      ",\"imu_ready\":" << (m.imu_ready ? "true" : "false") <<
      ",\"image_width\":" << m.image_width << ",\"image_height\":" << m.image_height <<
      ",\"message\":" << json_string(m.message);
  }
  out << "},";  out << "\"policy\":{\"received\":" << (policy_.message ? "true" : "false") <<
    ",\"fresh\":" << (fresh(policy_age) ? "true" : "false") << ",\"age_ms\":" << policy_age;
  if (policy_.message) {
    const auto & m = *policy_.message;
    out << ",\"state_name\":" << json_string(policy_name(m.state)) <<
      ",\"provider_id\":" << json_string(m.provider_id) <<
      ",\"model_id\":" << json_string(m.model_id) <<
      ",\"inference_latency_ms\":" << m.inference_latency_ms <<
      ",\"message\":" << json_string(m.message);
  }
  out << "},\"episode\":{\"received\":" << (episode_.message ? "true" : "false") <<
    ",\"fresh\":" << (fresh(episode_age) ? "true" : "false") << ",\"age_ms\":" << episode_age;
  if (episode_.message) {
    const auto & m = *episode_.message;
    out << ",\"state_name\":" << json_string(episode_name(m.state)) <<
      ",\"episode_id\":" << json_string(m.episode_id) << ",\"task\":" << json_string(m.task) <<
      ",\"directory\":" << json_string(m.directory) << ",\"message_count\":" << m.message_count <<
      ",\"image_count\":" << m.image_count << ",\"elapsed_seconds\":" << m.elapsed_seconds <<
      ",\"message\":" << json_string(m.message);
  }
  out << "},";  out << "\"shadow\":{\"received\":" << (shadow_.message ? "true" : "false") <<
    ",\"fresh\":" << (fresh(shadow_age) ? "true" : "false") << ",\"age_ms\":" << shadow_age;
  if (shadow_.message) {
    const auto & m = *shadow_.message;
    out << ",\"sample_count\":" << m.sample_count <<
      ",\"linear_mae\":" << m.linear_mean_absolute_error <<
      ",\"angular_mae\":" << m.angular_mean_absolute_error <<
      ",\"prediction_fresh\":" << (m.prediction_fresh ? "true" : "false");
  }
  out << "},\"safety\":{\"received\":" << (safety_.message ? "true" : "false") <<
    ",\"fresh\":" << (fresh(safety_age) ? "true" : "false") << ",\"age_ms\":" << safety_age;
  if (safety_.message) {
    const auto & m = *safety_.message;
    out << ",\"severity\":" << static_cast<int>(m.severity) <<
      ",\"rule_id\":" << json_string(m.rule_id) << ",\"reason\":" << json_string(m.reason) <<
      ",\"active\":" << (m.active ? "true" : "false");
  }
  out << "},\"camera\":{\"received\":" << (!camera_data_.empty() ? "true" : "false") <<
    ",\"fresh\":" << (fresh(camera_age) ? "true" : "false") <<
    ",\"age_ms\":" << camera_age << ",\"bytes\":" << camera_data_.size() <<
    ",\"frame_received_at_ms\":" << camera_received_at_ms <<
    ",\"frame_sequence\":" << camera_sequence_ <<
    ",\"content_analyzed\":" << (camera_content_analyzed_ ? "true" : "false") <<
    ",\"content_valid\":" << (camera_content_valid_ ? "true" : "false") <<
    ",\"mean_intensity\":" << camera_mean_intensity_ <<
    ",\"max_intensity\":" << camera_max_intensity_ << "}}\n";
  return out.str();
}
int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  try {
    auto node = std::make_shared<VehicleOpsApi>();
    rclcpp::executors::MultiThreadedExecutor executor(rclcpp::ExecutorOptions(), 3);
    executor.add_node(node);
    executor.spin();
  } catch (const std::exception & error) {
    std::cerr << "vehicle_ops_api failed: " << error.what() << std::endl;
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
