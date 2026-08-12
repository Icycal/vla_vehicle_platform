#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <geometry_msgs/msg/twist_stamped.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <vehicle_interfaces/msg/episode_state.hpp>
#include <vehicle_interfaces/msg/observation_status.hpp>
#include <vehicle_interfaces/msg/pipeline_stage.hpp>
#include <vehicle_interfaces/msg/pipeline_trace.hpp>
#include <vehicle_interfaces/msg/policy_action.hpp>
#include <vehicle_interfaces/msg/policy_observation.hpp>
#include <vehicle_interfaces/msg/policy_status.hpp>
#include <vehicle_interfaces/msg/safety_event.hpp>
#include <vehicle_interfaces/msg/shadow_comparison.hpp>
#include <vehicle_interfaces/msg/system_state.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

using namespace std::chrono_literals;

class PipelineTraceAggregator final : public rclcpp::Node
{
public:
  PipelineTraceAggregator()
  : Node("pipeline_trace_aggregator")
  {
    stale_seconds_ = declare_parameter<double>("stale_seconds", 3.0);
    camera_stale_seconds_ = declare_parameter<double>("camera_stale_seconds", 1.0);
    command_stale_seconds_ = declare_parameter<double>("command_stale_seconds", 0.75);
    policy_action_stale_seconds_ = declare_parameter<double>("policy_action_stale_seconds", 5.0);
    shadow_stale_seconds_ = declare_parameter<double>("shadow_stale_seconds", 5.0);
    const auto publish_frequency = declare_parameter<double>("publish_frequency", 2.0);

    const auto state_qos = rclcpp::QoS(1).reliable().transient_local();
    publisher_ = create_publisher<vehicle_interfaces::msg::PipelineTrace>(
      "/vla/pipeline_trace", state_qos);
    camera_subscription_ = create_subscription<sensor_msgs::msg::CompressedImage>(
      "/camera/image_compressed", rclcpp::SensorDataQoS(),
      [this](sensor_msgs::msg::CompressedImage::SharedPtr message) {update(camera_, *message);});
    observation_status_subscription_ = create_subscription<vehicle_interfaces::msg::ObservationStatus>(
      "/vehicle/observation_status", state_qos,
      [this](vehicle_interfaces::msg::ObservationStatus::SharedPtr message) {
        update(observation_status_, *message);
      });
    observation_subscription_ = create_subscription<vehicle_interfaces::msg::PolicyObservation>(
      "/vla/observation", rclcpp::QoS(2).reliable(),
      [this](vehicle_interfaces::msg::PolicyObservation::SharedPtr message) {
        update(observation_, *message);
      });
    policy_status_subscription_ = create_subscription<vehicle_interfaces::msg::PolicyStatus>(
      "/vla/policy_state", state_qos,
      [this](vehicle_interfaces::msg::PolicyStatus::SharedPtr message) {update(policy_status_, *message);});
    policy_action_subscription_ = create_subscription<vehicle_interfaces::msg::PolicyAction>(
      "/vla/policy_action", 10,
      [this](vehicle_interfaces::msg::PolicyAction::SharedPtr message) {update(policy_action_, *message);});
    raw_command_subscription_ = create_subscription<geometry_msgs::msg::TwistStamped>(
      "/vla/cmd_vel_raw", 10,
      [this](geometry_msgs::msg::TwistStamped::SharedPtr message) {update(raw_command_, *message);});
    selected_command_subscription_ = create_subscription<geometry_msgs::msg::TwistStamped>(
      "/control/cmd_vel_selected", 10,
      [this](geometry_msgs::msg::TwistStamped::SharedPtr message) {update(selected_command_, *message);});
    final_command_subscription_ = create_subscription<geometry_msgs::msg::Twist>(
      "/cmd_vel", 10,
      [this](geometry_msgs::msg::Twist::SharedPtr message) {update(final_command_, *message);});
    safety_subscription_ = create_subscription<vehicle_interfaces::msg::SafetyEvent>(
      "/vla/safety_event", 10,
      [this](vehicle_interfaces::msg::SafetyEvent::SharedPtr message) {update(safety_, *message);});
    shadow_subscription_ = create_subscription<vehicle_interfaces::msg::ShadowComparison>(
      "/vla/shadow_comparison", 10,
      [this](vehicle_interfaces::msg::ShadowComparison::SharedPtr message) {update(shadow_, *message);});
    episode_subscription_ = create_subscription<vehicle_interfaces::msg::EpisodeState>(
      "/vehicle/episode_state", state_qos,
      [this](vehicle_interfaces::msg::EpisodeState::SharedPtr message) {update(episode_, *message);});
    system_subscription_ = create_subscription<vehicle_interfaces::msg::SystemState>(
      "/vehicle/system_state", state_qos,
      [this](vehicle_interfaces::msg::SystemState::SharedPtr message) {update(system_, *message);});

    const auto period = std::chrono::duration<double>(1.0 / std::max(publish_frequency, 0.2));
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      std::bind(&PipelineTraceAggregator::publish, this));
  }

private:
  using Stage = vehicle_interfaces::msg::PipelineStage;
  using Trace = vehicle_interfaces::msg::PipelineTrace;
  using Json = nlohmann::json;

  template<typename T>
  struct Timed
  {
    std::optional<T> message;
    std::chrono::steady_clock::time_point received{};
  };

  template<typename T>
  void update(Timed<T> & target, const T & message)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    target.message = message;
    target.received = std::chrono::steady_clock::now();
  }

  template<typename T>
  double age_seconds(const Timed<T> & value, const std::chrono::steady_clock::time_point current) const
  {
    if (!value.message) {
      return -1.0;
    }
    return std::chrono::duration<double>(current - value.received).count();
  }

  static std::string twist_json(const geometry_msgs::msg::Twist & value)
  {
    return Json({{"linear_x", value.linear.x}, {"angular_z", value.angular.z}}).dump();
  }

  Stage stage(
    const std::string & id, const std::string & label, const std::string & component,
    const uint8_t status, const double age, const double latency,
    const std::string & input, const std::string & output, const std::string & message,
    const Json & detail = Json::object(), const bool control_boundary = false,
    const bool publishes_control = false) const
  {
    Stage result;
    result.stage_id = id;
    result.label = label;
    result.component = component;
    result.status = status;
    result.updated_at = now();
    result.age_seconds = static_cast<float>(age);
    result.latency_ms = static_cast<float>(latency);
    result.input_summary = input;
    result.output_summary = output;
    result.message = message;
    result.detail_json = detail.dump();
    result.control_boundary = control_boundary;
    result.publishes_control = publishes_control;
    return result;
  }

  uint8_t freshness_status(const double age, const double threshold, const uint8_t fresh_status) const
  {
    if (age < 0.0) {
      return Stage::STATUS_WAITING;
    }
    return age <= threshold ? fresh_status : Stage::STATUS_STALE;
  }

  void publish()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto current = std::chrono::steady_clock::now();
    Trace trace;
    trace.header.stamp = now();
    trace.mode = "live";
    trace.shadow_only = system_.message &&
      system_.message->mode == vehicle_interfaces::msg::SystemState::MODE_VLA_SHADOW;
    trace.publishes_control = false;
    trace.observation_id = observation_.message ? observation_.message->observation_id : "";
    trace.trace_id = trace.observation_id.empty() ? "live-waiting" : "live-" + trace.observation_id;
    trace.provider_id = policy_status_.message ? policy_status_.message->provider_id : "";
    trace.model_id = policy_status_.message ? policy_status_.message->model_id : "";

    const double camera_age = age_seconds(camera_, current);
    trace.stages.push_back(stage(
      "sensor_capture", "Sensor Capture", "front_camera + image_transport",
      freshness_status(camera_age, camera_stale_seconds_, Stage::STATUS_LIVE), camera_age, -1.0,
      "/dev/video0", camera_.message ?
      std::to_string(camera_.message->data.size()) + " encoded bytes" : "No frame",
      camera_age < 0.0 ? "Waiting for compressed camera frame" : "Passive live camera observation",
      camera_.message ? Json({{"format", camera_.message->format}, {"frame_id", camera_.message->header.frame_id},
        {"encoded_bytes", camera_.message->data.size()}}) : Json::object()));

    const double health_age = age_seconds(observation_status_, current);
    uint8_t health_status = freshness_status(health_age, stale_seconds_, Stage::STATUS_READY);
    if (observation_status_.message && health_age <= stale_seconds_ && !observation_status_.message->ready) {
      health_status = Stage::STATUS_FAILED;
    }
    trace.stages.push_back(stage(
      "observation_health", "Observation Health", "observation_monitor",
      health_status, health_age, -1.0, "camera / odom / imu", observation_status_.message ?
      (observation_status_.message->ready ? "READY" : "NOT READY") : "No status",
      observation_status_.message ? observation_status_.message->message : "Waiting for health status",
      observation_status_.message ? Json({
        {"camera_ready", observation_status_.message->camera_ready},
        {"camera_calibrated", observation_status_.message->camera_calibrated},
        {"odometry_ready", observation_status_.message->odometry_ready},
        {"imu_ready", observation_status_.message->imu_ready},
        {"image_width", observation_status_.message->image_width},
        {"image_height", observation_status_.message->image_height}}) : Json::object()));

    const double observation_age = age_seconds(observation_, current);
    trace.stages.push_back(stage(
      "observation_assembly", "Observation Assembly", "observation_adapter",
      freshness_status(observation_age, stale_seconds_, Stage::STATUS_READY), observation_age, -1.0,
      "compressed image + vehicle state + task", trace.observation_id.empty() ? "No Observation" : trace.observation_id,
      observation_.message ? "Model-independent Observation assembled" : "Waiting for /vla/observation",
      observation_.message ? Json({{"schema_version", observation_.message->schema_version},
        {"task", observation_.message->task}, {"image_count", observation_.message->images.size()},
        {"state_count", observation_.message->state.size()}}) : Json::object()));

    uint8_t contract_status = freshness_status(observation_age, stale_seconds_, Stage::STATUS_READY);
    std::string contract_message = "Waiting for Observation contract";
    Json contract_detail = Json::object();
    if (observation_.message && observation_age <= stale_seconds_) {
      const bool schema_valid = observation_.message->schema_version == "vehicle.observation.v1";
      const bool image_valid = !observation_.message->images.empty() &&
        !observation_.message->images.front().data.empty();
      const bool task_valid = !observation_.message->task.empty();
      const auto state_valid_count = std::count(
        observation_.message->state_valid.begin(), observation_.message->state_valid.end(), true);
      Json blocking_issues = Json::array();
      if (!schema_valid) {blocking_issues.push_back("schema_version");}
      if (!image_valid) {blocking_issues.push_back("front_image");}
      if (!task_valid) {blocking_issues.push_back("task_text");}
      Json diagnostic_issues = Json::array();
      if (state_valid_count == 0) {diagnostic_issues.push_back("vehicle_state_unavailable");}
      contract_detail = {{"schema_valid", schema_valid}, {"image_valid", image_valid},
        {"task_valid", task_valid}, {"state_valid_count", state_valid_count},
        {"state_total_count", observation_.message->state_valid.size()},
        {"blocking_issues", blocking_issues}, {"diagnostic_issues", diagnostic_issues}};
      contract_status = blocking_issues.empty() ? Stage::STATUS_READY : Stage::STATUS_REJECTED;
      if (blocking_issues.empty()) {
        contract_message = "Observation contract accepted";
      } else if (!task_valid && schema_valid && image_valid) {
        contract_message = "Task text is missing";
      } else if (!image_valid && schema_valid && task_valid) {
        contract_message = "Front image is missing";
      } else if (!schema_valid && image_valid && task_valid) {
        contract_message = "Observation schema version is invalid";
      } else {
        contract_message = "Observation contract has multiple blocking issues";
      }
    }
    trace.stages.push_back(stage(
      "contract_validation", "Contract Validation", "vehicle.observation.v1",
      contract_status, observation_age, -1.0, "PolicyObservation", contract_status == Stage::STATUS_READY ?
      "ACCEPTED" : "BLOCKED", contract_message, contract_detail));

    const double policy_age = age_seconds(policy_status_, current);
    uint8_t runtime_status = freshness_status(policy_age, stale_seconds_, Stage::STATUS_READY);
    if (policy_status_.message && policy_age <= stale_seconds_) {
      using PolicyStatus = vehicle_interfaces::msg::PolicyStatus;
      runtime_status = policy_status_.message->state == PolicyStatus::STATE_READY ? Stage::STATUS_READY :
        (policy_status_.message->state == PolicyStatus::STATE_LOADING ? Stage::STATUS_RUNNING : Stage::STATUS_FAILED);
    }
    trace.stages.push_back(stage(
      "model_runtime", "Model Runtime", trace.provider_id.empty() ? "policy provider" : trace.provider_id,
      runtime_status, policy_age, policy_status_.message ? policy_status_.message->inference_latency_ms : -1.0,
      policy_status_.message ? policy_status_.message->observation_schema : "--",
      policy_status_.message ? policy_status_.message->action_schema : "--",
      policy_status_.message ? policy_status_.message->message : "Waiting for Provider status",
      policy_status_.message ? Json({{"provider_id", policy_status_.message->provider_id},
        {"model_id", policy_status_.message->model_id},
        {"protocol_version", policy_status_.message->protocol_version},
        {"inference_latency_ms", policy_status_.message->inference_latency_ms}}) : Json::object()));

    const double action_age = age_seconds(policy_action_, current);
    bool action_matches = policy_action_.message && observation_.message &&
      policy_action_.message->observation_id == observation_.message->observation_id;
    const uint8_t action_status = freshness_status(
      action_age, policy_action_stale_seconds_, Stage::STATUS_READY);
    trace.stages.push_back(stage(
      "policy_output", "Policy Output", "vla_policy_gateway",
      action_status, action_age, policy_status_.message ? policy_status_.message->inference_latency_ms : -1.0,
      policy_action_.message ? policy_action_.message->observation_id : "No request",
      policy_action_.message ? std::to_string(policy_action_.message->actions.size()) + " Twist actions" : "No action",
      !policy_action_.message ? "Waiting for PolicyAction" :
      (action_age > policy_action_stale_seconds_ ? "Latest PolicyAction exceeded the freshness threshold" :
      (action_matches ? "Policy output matches live Observation head" :
      "Policy output is fresh; Observation advanced during inference")),
      policy_action_.message ? Json({{"request_id", policy_action_.message->request_id},
        {"observation_id", policy_action_.message->observation_id},
        {"action_count", policy_action_.message->actions.size()},
        {"first_action", policy_action_.message->actions.empty() ? "" : twist_json(policy_action_.message->actions.front())}}) : Json::object()));

    const double raw_age = age_seconds(raw_command_, current);
    trace.stages.push_back(stage(
      "action_runtime", "Action Runtime", "vla_action_runtime",
      freshness_status(raw_age, command_stale_seconds_, Stage::STATUS_READY), raw_age, -1.0,
      "PolicyAction chunk", raw_command_.message ? twist_json(raw_command_.message->twist) : "No candidate",
      raw_command_.message ? "First valid action converted to candidate Twist" : "Waiting for action candidate",
      raw_command_.message ? Json::parse(twist_json(raw_command_.message->twist)) : Json::object()));

    const double selected_age = age_seconds(selected_command_, current);
    trace.stages.push_back(stage(
      "control_mux", "Control Arbitration", "vla_control_mux",
      freshness_status(selected_age, command_stale_seconds_, Stage::STATUS_READY), selected_age, -1.0,
      system_.message ? system_.message->control_source : "unknown source",
      selected_command_.message ? twist_json(selected_command_.message->twist) : "No selected command",
      trace.shadow_only ? "Shadow mode keeps VLA outside the final control source" : "Candidate selected by control mode",
      selected_command_.message ? Json({{"selected", Json::parse(twist_json(selected_command_.message->twist))},
        {"control_source", system_.message ? system_.message->control_source : ""},
        {"mode", system_.message ? system_.message->mode : 0}}) : Json::object(), true, false));

    const double final_age = age_seconds(final_command_, current);
    uint8_t safety_status = freshness_status(final_age, command_stale_seconds_, Stage::STATUS_READY);
    std::string safety_message = "Waiting for safety output";
    if (safety_.message && safety_.message->active) {
      safety_status = safety_.message->severity >= vehicle_interfaces::msg::SafetyEvent::SEVERITY_FAULT ?
        Stage::STATUS_FAILED : Stage::STATUS_REJECTED;
      safety_message = safety_.message->reason;
    } else if (final_command_.message) {
      safety_message = "Safety output is fresh";
    }
    trace.stages.push_back(stage(
      "safety_guard", "Safety Guard", "vla_safety_guard",
      safety_status, final_age, -1.0,
      selected_command_.message ? twist_json(selected_command_.message->twist) : "No selected command",
      final_command_.message ? twist_json(*final_command_.message) : "No final command",
      safety_message,
      Json({{"active_rule", safety_.message ? safety_.message->rule_id : ""},
        {"active", safety_.message ? safety_.message->active : false},
        {"severity", safety_.message ? safety_.message->severity : 0},
        {"final_command", final_command_.message ? Json::parse(twist_json(*final_command_.message)) : Json::object()}}),
      true, true));

    const double shadow_age = age_seconds(shadow_, current);
    uint8_t shadow_status = freshness_status(
      shadow_age, shadow_stale_seconds_, Stage::STATUS_READY);
    if (shadow_.message && shadow_age <= stale_seconds_ && !shadow_.message->within_threshold) {
      shadow_status = Stage::STATUS_REJECTED;
    }
    trace.stages.push_back(stage(
      "shadow_evaluation", "Shadow Evaluation", "shadow_evaluator",
      shadow_status, shadow_age, -1.0,
      "predicted vs executed Twist", shadow_.message ?
      (shadow_.message->within_threshold ? "WITHIN THRESHOLD" : "OUTSIDE THRESHOLD") : "No comparison",
      shadow_.message ? shadow_.message->message : "Waiting for Shadow comparison",
      shadow_.message ? Json({{"linear_error", shadow_.message->linear_absolute_error},
        {"angular_error", shadow_.message->angular_absolute_error},
        {"within_threshold", shadow_.message->within_threshold}}) : Json::object()));

    const double episode_age = age_seconds(episode_, current);
    uint8_t episode_status = freshness_status(episode_age, stale_seconds_, Stage::STATUS_READY);
    if (episode_.message && episode_age <= stale_seconds_) {
      episode_status = episode_.message->state == vehicle_interfaces::msg::EpisodeState::STATE_RECORDING ?
        Stage::STATUS_LIVE : (episode_.message->state == vehicle_interfaces::msg::EpisodeState::STATE_ERROR ?
        Stage::STATUS_FAILED : Stage::STATUS_READY);
    }
    trace.stages.push_back(stage(
      "trace_recording", "Episode / Trace", "episode_recorder",
      episode_status, episode_age, -1.0,
      trace.observation_id, episode_.message ? episode_.message->episode_id : "No Episode",
      episode_.message ? episode_.message->message : "Waiting for Episode state",
      episode_.message ? Json({{"state", episode_.message->state}, {"episode_id", episode_.message->episode_id},
        {"message_count", episode_.message->message_count}, {"image_count", episode_.message->image_count}}) : Json::object()));

    publisher_->publish(trace);
  }

  double stale_seconds_{3.0};
  double camera_stale_seconds_{1.0};
  double command_stale_seconds_{0.75};
  double policy_action_stale_seconds_{5.0};
  double shadow_stale_seconds_{5.0};
  std::mutex mutex_;
  Timed<sensor_msgs::msg::CompressedImage> camera_;
  Timed<vehicle_interfaces::msg::ObservationStatus> observation_status_;
  Timed<vehicle_interfaces::msg::PolicyObservation> observation_;
  Timed<vehicle_interfaces::msg::PolicyStatus> policy_status_;
  Timed<vehicle_interfaces::msg::PolicyAction> policy_action_;
  Timed<geometry_msgs::msg::TwistStamped> raw_command_;
  Timed<geometry_msgs::msg::TwistStamped> selected_command_;
  Timed<geometry_msgs::msg::Twist> final_command_;
  Timed<vehicle_interfaces::msg::SafetyEvent> safety_;
  Timed<vehicle_interfaces::msg::ShadowComparison> shadow_;
  Timed<vehicle_interfaces::msg::EpisodeState> episode_;
  Timed<vehicle_interfaces::msg::SystemState> system_;
  rclcpp::Publisher<Trace>::SharedPtr publisher_;
  rclcpp::Subscription<sensor_msgs::msg::CompressedImage>::SharedPtr camera_subscription_;
  rclcpp::Subscription<vehicle_interfaces::msg::ObservationStatus>::SharedPtr observation_status_subscription_;
  rclcpp::Subscription<vehicle_interfaces::msg::PolicyObservation>::SharedPtr observation_subscription_;
  rclcpp::Subscription<vehicle_interfaces::msg::PolicyStatus>::SharedPtr policy_status_subscription_;
  rclcpp::Subscription<vehicle_interfaces::msg::PolicyAction>::SharedPtr policy_action_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::TwistStamped>::SharedPtr raw_command_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::TwistStamped>::SharedPtr selected_command_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr final_command_subscription_;
  rclcpp::Subscription<vehicle_interfaces::msg::SafetyEvent>::SharedPtr safety_subscription_;
  rclcpp::Subscription<vehicle_interfaces::msg::ShadowComparison>::SharedPtr shadow_subscription_;
  rclcpp::Subscription<vehicle_interfaces::msg::EpisodeState>::SharedPtr episode_subscription_;
  rclcpp::Subscription<vehicle_interfaces::msg::SystemState>::SharedPtr system_subscription_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<PipelineTraceAggregator>());
  rclcpp::shutdown();
  return 0;
}