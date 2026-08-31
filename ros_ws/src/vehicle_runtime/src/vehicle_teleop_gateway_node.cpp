#include <geometry_msgs/msg/twist_stamped.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <vehicle_interfaces/action/acquire_control_lease.hpp>
#include <vehicle_interfaces/msg/control_lease.hpp>
#include <vehicle_interfaces/msg/system_state.hpp>
#include <vehicle_interfaces/msg/teleop_command.hpp>
#include <vehicle_interfaces/srv/release_control_lease.hpp>
#include <vehicle_interfaces/srv/request_control_mode.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <future>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

using namespace std::chrono_literals;

class VehicleTeleopGateway final : public rclcpp::Node
{
public:
  using AcquireLease = vehicle_interfaces::action::AcquireControlLease;
  using GoalHandle = rclcpp_action::ServerGoalHandle<AcquireLease>;

  VehicleTeleopGateway()
  : Node("vehicle_teleop_gateway")
  {
    maximum_lease_seconds_ = declare_parameter<double>("maximum_lease_seconds", 30.0);
    minimum_lease_seconds_ = declare_parameter<double>("minimum_lease_seconds", 3.0);
    command_timeout_seconds_ = declare_parameter<double>("command_timeout_seconds", 0.3);
    maximum_linear_velocity_ = declare_parameter<double>("maximum_linear_velocity", 0.4);
    maximum_angular_velocity_ = declare_parameter<double>("maximum_angular_velocity", 0.8);
    publish_frequency_ = declare_parameter<double>("publish_frequency", 20.0);
    output_frame_ = declare_parameter<std::string>("output_frame", "base_link");
    output_topic_ = declare_parameter<std::string>("output_topic", "/mobile_teleop/cmd_vel");
    if (minimum_lease_seconds_ <= 0.0 || maximum_lease_seconds_ < minimum_lease_seconds_ ||
      command_timeout_seconds_ <= 0.0 || publish_frequency_ <= 0.0 ||
      maximum_linear_velocity_ <= 0.0 || maximum_angular_velocity_ <= 0.0)
    {
      throw std::invalid_argument("Invalid teleop gateway parameters");
    }

    const auto lease_qos = rclcpp::QoS(1).reliable().transient_local();
    lease_publisher_ = create_publisher<vehicle_interfaces::msg::ControlLease>(
      "/vehicle/control_lease", lease_qos);
    command_publisher_ = create_publisher<geometry_msgs::msg::TwistStamped>(output_topic_, 10);
    command_subscription_ = create_subscription<vehicle_interfaces::msg::TeleopCommand>(
      "/vehicle/teleop_command", 20,
      std::bind(&VehicleTeleopGateway::on_command, this, std::placeholders::_1));
    release_service_ = create_service<vehicle_interfaces::srv::ReleaseControlLease>(
      "/vehicle/release_control_lease",
      std::bind(
        &VehicleTeleopGateway::on_release, this,
        std::placeholders::_1, std::placeholders::_2));
    mode_client_ = create_client<vehicle_interfaces::srv::RequestControlMode>(
      "/vehicle/request_mode");
    action_server_ = rclcpp_action::create_server<AcquireLease>(
      this, "/vehicle/acquire_control_lease",
      std::bind(&VehicleTeleopGateway::handle_goal, this, std::placeholders::_1, std::placeholders::_2),
      std::bind(&VehicleTeleopGateway::handle_cancel, this, std::placeholders::_1),
      std::bind(&VehicleTeleopGateway::handle_accepted, this, std::placeholders::_1));
    const auto timer_period = std::chrono::duration<double>(1.0 / publish_frequency_);
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::nanoseconds>(timer_period),
      std::bind(&VehicleTeleopGateway::on_timer, this));
    publish_lease();
  }

private:
  rclcpp_action::GoalResponse handle_goal(
    const rclcpp_action::GoalUUID &,
    std::shared_ptr<const AcquireLease::Goal> goal)
  {
    const double duration = duration_seconds(goal->requested_duration);
    if (goal->controller_id.empty() || goal->source.empty() ||
      duration < minimum_lease_seconds_ || duration > maximum_lease_seconds_ ||
      !std::isfinite(goal->requested_max_linear_velocity) ||
      !std::isfinite(goal->requested_max_angular_velocity) ||
      goal->requested_max_linear_velocity <= 0.0F ||
      goal->requested_max_angular_velocity <= 0.0F)
    {
      return rclcpp_action::GoalResponse::REJECT;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if ((lease_.active && !lease_expired_locked() && lease_.controller_id != goal->controller_id) ||
      (!pending_controller_id_.empty() && pending_controller_id_ != goal->controller_id))
    {
      return rclcpp_action::GoalResponse::REJECT;
    }
    pending_controller_id_ = goal->controller_id;
    return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
  }

  rclcpp_action::CancelResponse handle_cancel(const std::shared_ptr<GoalHandle>)
  {
    return rclcpp_action::CancelResponse::REJECT;
  }

  void handle_accepted(const std::shared_ptr<GoalHandle> goal_handle)
  {
    std::thread([this, goal_handle]() {execute(goal_handle);}).detach();
  }

  void execute(const std::shared_ptr<GoalHandle> goal_handle)
  {
    const auto goal = goal_handle->get_goal();
    auto result = std::make_shared<AcquireLease::Result>();
    const double requested_duration = std::clamp(
      duration_seconds(goal->requested_duration), minimum_lease_seconds_, maximum_lease_seconds_);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const bool renewal = lease_.active && !lease_expired_locked() &&
        lease_.controller_id == goal->controller_id;
      if (!renewal) {
        lease_.lease_id = make_lease_id();
        last_sequence_ = 0;
      }
      lease_.header.stamp = now();
      lease_.controller_id = goal->controller_id;
      lease_.source = goal->source;
      lease_.remote = goal->remote;
      lease_.active = true;
      lease_.max_linear_velocity = std::min(
        goal->requested_max_linear_velocity, static_cast<float>(maximum_linear_velocity_));
      lease_.max_angular_velocity = std::min(
        goal->requested_max_angular_velocity, static_cast<float>(maximum_angular_velocity_));
      lease_deadline_ = std::chrono::steady_clock::now() +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(requested_duration));
      lease_.expires_at = now() + rclcpp::Duration::from_seconds(requested_duration);
      command_deadline_ = std::chrono::steady_clock::time_point{};
      pending_controller_id_.clear();
      deadman_ = false;
      linear_normalized_ = 0.0;
      angular_normalized_ = 0.0;
    }
    publish_lease();

    std::string mode_message;
    const uint8_t requested_mode = goal->remote ?
      vehicle_interfaces::msg::SystemState::MODE_REMOTE_TELEOP :
      vehicle_interfaces::msg::SystemState::MODE_MOBILE_TELEOP;
    if (!request_mode(requested_mode, "Teleop lease acquired", mode_message)) {
      revoke_lease("Control mode rejected");
      result->granted = false;
      result->message = mode_message;
      goal_handle->abort(result);
      return;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      result->lease = lease_;
    }
    result->granted = true;
    result->message = "Control lease granted";
    goal_handle->succeed(result);
  }

  void on_command(const vehicle_interfaces::msg::TeleopCommand::SharedPtr message)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!lease_.active || lease_expired_locked() || message->lease_id != lease_.lease_id ||
      message->controller_id != lease_.controller_id || message->sequence <= last_sequence_)
    {
      return;
    }
    const double valid_for = std::clamp(
      duration_seconds(message->valid_for), 0.01, command_timeout_seconds_);
    last_sequence_ = message->sequence;
    deadman_ = message->deadman;
    linear_normalized_ = std::clamp(static_cast<double>(message->linear_normalized), -1.0, 1.0);
    angular_normalized_ = std::clamp(static_cast<double>(message->angular_normalized), -1.0, 1.0);
    command_deadline_ = std::chrono::steady_clock::now() +
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(
      std::chrono::duration<double>(valid_for));
  }

  void on_release(
    const std::shared_ptr<vehicle_interfaces::srv::ReleaseControlLease::Request> request,
    std::shared_ptr<vehicle_interfaces::srv::ReleaseControlLease::Response> response)
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!lease_.active || request->lease_id != lease_.lease_id ||
        request->controller_id != lease_.controller_id)
      {
        response->released = false;
        response->message = "Active lease does not match";
        return;
      }
    }
    revoke_lease("Released by controller");
    response->released = true;
    response->message = "Control lease released";
  }

  void on_timer()
  {
    geometry_msgs::msg::TwistStamped output;
    output.header.stamp = now();
    output.header.frame_id = output_frame_;
    bool expired = false;
    bool lease_active = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      lease_active = lease_.active;
      expired = lease_.active && lease_expired_locked();
      const bool command_valid = lease_.active && !expired && deadman_ &&
        command_deadline_.time_since_epoch().count() != 0 &&
        std::chrono::steady_clock::now() <= command_deadline_;
      if (command_valid) {
        output.twist.linear.x = linear_normalized_ * lease_.max_linear_velocity;
        output.twist.angular.z = angular_normalized_ * lease_.max_angular_velocity;
      }
    }
    if (lease_active) {
      command_publisher_->publish(output);
    }
    if (expired) {
      revoke_lease("Lease expired");
    }
  }

  void revoke_lease(const std::string & reason)
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!lease_.active) {
        return;
      }
      lease_.active = false;
      lease_.header.stamp = now();
      lease_.expires_at = now();
      deadman_ = false;
      linear_normalized_ = 0.0;
      angular_normalized_ = 0.0;
      command_deadline_ = std::chrono::steady_clock::time_point{};
    }
    publish_lease();
    request_mode_async(vehicle_interfaces::msg::SystemState::MODE_MANUAL, reason);
  }

  bool request_mode(uint8_t mode, const std::string & reason, std::string & message)
  {
    if (!mode_client_->wait_for_service(1s)) {
      message = "Vehicle supervisor is unavailable";
      return false;
    }
    auto request = std::make_shared<vehicle_interfaces::srv::RequestControlMode::Request>();
    request->requested_mode = mode;
    request->requester = "vehicle_teleop_gateway";
    request->reason = reason;
    auto future = mode_client_->async_send_request(request);
    if (future.wait_for(2s) != std::future_status::ready) {
      message = "Control mode request timed out";
      return false;
    }
    const auto response = future.get();
    message = response->message;
    return response->accepted;
  }

  void request_mode_async(uint8_t mode, const std::string & reason)
  {
    if (!mode_client_->service_is_ready()) {
      return;
    }
    auto request = std::make_shared<vehicle_interfaces::srv::RequestControlMode::Request>();
    request->requested_mode = mode;
    request->requester = "vehicle_teleop_gateway";
    request->reason = reason;
    mode_client_->async_send_request(request);
  }

  void publish_lease()
  {
    vehicle_interfaces::msg::ControlLease lease;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      lease = lease_;
    }
    lease_publisher_->publish(lease);
  }

  bool lease_expired_locked() const
  {
    return lease_deadline_.time_since_epoch().count() == 0 ||
           std::chrono::steady_clock::now() >= lease_deadline_;
  }

  static double duration_seconds(const builtin_interfaces::msg::Duration & duration)
  {
    return static_cast<double>(duration.sec) + static_cast<double>(duration.nanosec) / 1.0e9;
  }

  std::string make_lease_id()
  {
    const auto count = ++lease_counter_;
    std::ostringstream output;
    output << "teleop-" << std::hex << now().nanoseconds() << '-' << count;
    return output.str();
  }

  double maximum_lease_seconds_{30.0};
  double minimum_lease_seconds_{3.0};
  double command_timeout_seconds_{0.3};
  double maximum_linear_velocity_{0.4};
  double maximum_angular_velocity_{0.8};
  double publish_frequency_{20.0};
  std::string output_frame_{"base_link"};
  std::string output_topic_{"/mobile_teleop/cmd_vel"};
  std::mutex mutex_;
  vehicle_interfaces::msg::ControlLease lease_;
  std::chrono::steady_clock::time_point lease_deadline_{};
  std::chrono::steady_clock::time_point command_deadline_{};
  std::uint64_t last_sequence_{0};
  double linear_normalized_{0.0};
  double angular_normalized_{0.0};
  bool deadman_{false};
  std::string pending_controller_id_;
  std::atomic<std::uint64_t> lease_counter_{0};
  rclcpp_action::Server<AcquireLease>::SharedPtr action_server_;
  rclcpp::Service<vehicle_interfaces::srv::ReleaseControlLease>::SharedPtr release_service_;
  rclcpp::Client<vehicle_interfaces::srv::RequestControlMode>::SharedPtr mode_client_;
  rclcpp::Subscription<vehicle_interfaces::msg::TeleopCommand>::SharedPtr command_subscription_;
  rclcpp::Publisher<vehicle_interfaces::msg::ControlLease>::SharedPtr lease_publisher_;
  rclcpp::Publisher<geometry_msgs::msg::TwistStamped>::SharedPtr command_publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<VehicleTeleopGateway>());
  rclcpp::shutdown();
  return 0;
}