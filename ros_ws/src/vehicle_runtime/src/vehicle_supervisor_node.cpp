#include <rclcpp/rclcpp.hpp>
#include <vehicle_interfaces/msg/policy_status.hpp>
#include <vehicle_interfaces/msg/safety_event.hpp>
#include <vehicle_interfaces/msg/system_state.hpp>
#include <vehicle_interfaces/srv/request_control_mode.hpp>
#include <vehicle_interfaces/srv/request_safe_stop.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <string>

using namespace std::chrono_literals;

class VehicleSupervisor final : public rclcpp::Node
{
public:
  VehicleSupervisor();

private:
  void on_policy_status(vehicle_interfaces::msg::PolicyStatus::SharedPtr message);
  void on_safety_event(vehicle_interfaces::msg::SafetyEvent::SharedPtr message);
  void on_mode_request(
    std::shared_ptr<vehicle_interfaces::srv::RequestControlMode::Request> request,
    std::shared_ptr<vehicle_interfaces::srv::RequestControlMode::Response> response);
  void on_safe_stop(
    std::shared_ptr<vehicle_interfaces::srv::RequestSafeStop::Request> request,
    std::shared_ptr<vehicle_interfaces::srv::RequestSafeStop::Response> response);
  static bool is_vla_mode(uint8_t mode);
  static std::string control_source_for(uint8_t mode);
  void set_mode(uint8_t mode, const std::string & reason);
  void publish_state();

  bool policy_ready_{false};
  bool safety_stop_active_{false};
  vehicle_interfaces::msg::SystemState state_;
  rclcpp::Publisher<vehicle_interfaces::msg::SystemState>::SharedPtr state_publisher_;
  rclcpp::Subscription<vehicle_interfaces::msg::PolicyStatus>::SharedPtr policy_subscription_;
  rclcpp::Subscription<vehicle_interfaces::msg::SafetyEvent>::SharedPtr safety_subscription_;
  rclcpp::Service<vehicle_interfaces::srv::RequestControlMode>::SharedPtr mode_service_;
  rclcpp::Service<vehicle_interfaces::srv::RequestSafeStop>::SharedPtr safe_stop_service_;
  rclcpp::TimerBase::SharedPtr timer_;
};

VehicleSupervisor::VehicleSupervisor()
: Node("vehicle_supervisor")
{
  const auto state_qos = rclcpp::QoS(1).reliable().transient_local();
  state_publisher_ = create_publisher<vehicle_interfaces::msg::SystemState>(
    "/vehicle/system_state", state_qos);
  policy_subscription_ = create_subscription<vehicle_interfaces::msg::PolicyStatus>(
    "/vla/policy_state", state_qos,
    std::bind(&VehicleSupervisor::on_policy_status, this, std::placeholders::_1));
  safety_subscription_ = create_subscription<vehicle_interfaces::msg::SafetyEvent>(
    "/vla/safety_event", 10,
    std::bind(&VehicleSupervisor::on_safety_event, this, std::placeholders::_1));
  mode_service_ = create_service<vehicle_interfaces::srv::RequestControlMode>(
    "/vehicle/request_mode",
    std::bind(
      &VehicleSupervisor::on_mode_request, this,
      std::placeholders::_1, std::placeholders::_2));
  safe_stop_service_ = create_service<vehicle_interfaces::srv::RequestSafeStop>(
    "/vehicle/request_safe_stop",
    std::bind(
      &VehicleSupervisor::on_safe_stop, this,
      std::placeholders::_1, std::placeholders::_2));
  state_.mode = vehicle_interfaces::msg::SystemState::MODE_MANUAL;
  state_.control_source = "manual";
  state_.safe_to_move = true;
  state_.status_message = "Phase-0 supervisor ready";
  timer_ = create_wall_timer(500ms, std::bind(&VehicleSupervisor::publish_state, this));
  publish_state();
}

void VehicleSupervisor::on_policy_status(
  vehicle_interfaces::msg::PolicyStatus::SharedPtr message)
{
  policy_ready_ = message->state == vehicle_interfaces::msg::PolicyStatus::STATE_READY;
  state_.active_policy_id = message->model_id;
}

void VehicleSupervisor::on_safety_event(
  vehicle_interfaces::msg::SafetyEvent::SharedPtr message)
{
  safety_stop_active_ = message->active &&
    message->severity >= vehicle_interfaces::msg::SafetyEvent::SEVERITY_STOP;
  if (safety_stop_active_) {
    set_mode(vehicle_interfaces::msg::SystemState::MODE_SAFE_STOP, message->reason);
  }
}

void VehicleSupervisor::on_mode_request(
  std::shared_ptr<vehicle_interfaces::srv::RequestControlMode::Request> request,
  std::shared_ptr<vehicle_interfaces::srv::RequestControlMode::Response> response)
{
  response->accepted = false;
  response->current_mode = state_.mode;
  if (request->requested_mode > vehicle_interfaces::msg::SystemState::MODE_FAULT) {
    response->message = "Unknown control mode";
    return;
  }
  if (safety_stop_active_ &&
    request->requested_mode != vehicle_interfaces::msg::SystemState::MODE_SAFE_STOP &&
    request->requested_mode != vehicle_interfaces::msg::SystemState::MODE_MANUAL)
  {
    response->message = "Safety stop is active";
    return;
  }
  const bool requires_policy =
    request->requested_mode == vehicle_interfaces::msg::SystemState::MODE_VLA_ASSISTED ||
    request->requested_mode == vehicle_interfaces::msg::SystemState::MODE_VLA_AUTONOMOUS;
  if (requires_policy && !policy_ready_) {
    response->message = "Policy is not ready for vehicle control mode";
    return;
  }
  set_mode(request->requested_mode, request->reason);
  response->accepted = true;
  response->current_mode = state_.mode;
  response->message = "Control mode updated for " + request->requester;
}

void VehicleSupervisor::on_safe_stop(
  std::shared_ptr<vehicle_interfaces::srv::RequestSafeStop::Request> request,
  std::shared_ptr<vehicle_interfaces::srv::RequestSafeStop::Response> response)
{
  set_mode(
    vehicle_interfaces::msg::SystemState::MODE_SAFE_STOP,
    request->reason.empty() ? "Safe stop requested" : request->reason);
  response->accepted = true;
  response->message = "Safe stop accepted for " + request->requester;
}

bool VehicleSupervisor::is_vla_mode(const uint8_t mode)
{
  return mode == vehicle_interfaces::msg::SystemState::MODE_VLA_SHADOW ||
         mode == vehicle_interfaces::msg::SystemState::MODE_VLA_ASSISTED ||
         mode == vehicle_interfaces::msg::SystemState::MODE_VLA_AUTONOMOUS;
}

std::string VehicleSupervisor::control_source_for(const uint8_t mode)
{
  switch (mode) {
    case vehicle_interfaces::msg::SystemState::MODE_NAV2: return "nav2";
    case vehicle_interfaces::msg::SystemState::MODE_VLA_SHADOW: return "vla_shadow";
    case vehicle_interfaces::msg::SystemState::MODE_VLA_ASSISTED:
    case vehicle_interfaces::msg::SystemState::MODE_VLA_AUTONOMOUS: return "vla";
    case vehicle_interfaces::msg::SystemState::MODE_MOBILE_TELEOP: return "mobile_teleop";
    case vehicle_interfaces::msg::SystemState::MODE_REMOTE_TELEOP: return "remote_teleop";
    case vehicle_interfaces::msg::SystemState::MODE_SAFE_STOP: return "safe_stop";
    case vehicle_interfaces::msg::SystemState::MODE_FAULT: return "fault";
    default: return "manual";
  }
}

void VehicleSupervisor::set_mode(const uint8_t mode, const std::string & reason)
{
  state_.mode = mode;
  state_.control_source = control_source_for(mode);
  state_.safe_to_move = mode != vehicle_interfaces::msg::SystemState::MODE_SAFE_STOP &&
    mode != vehicle_interfaces::msg::SystemState::MODE_FAULT;
  state_.status_message = reason.empty() ? "Mode changed" : reason;
  publish_state();
}

void VehicleSupervisor::publish_state()
{
  state_.header.stamp = now();
  state_.header.frame_id = "base_link";
  state_publisher_->publish(state_);
}

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<VehicleSupervisor>());
  rclcpp::shutdown();
  return 0;
}
