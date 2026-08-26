#include <geometry_msgs/msg/twist.hpp>
#include <geometry_msgs/msg/twist_stamped.hpp>
#include <rclcpp/rclcpp.hpp>
#include <vehicle_interfaces/msg/policy_action.hpp>
#include <vehicle_interfaces/msg/training_action.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

struct ActionSample
{
  std::string observation_id;
  std::string schema;
  std::string schema_hash;
  std::vector<std::string> feature_names;
  std::vector<std::string> feature_units;
  std::vector<float> values;
};

std::size_t find_feature(
  const std::vector<std::string> & feature_names,
  const std::vector<std::string> & accepted_names)
{
  for (const auto & accepted : accepted_names) {
    const auto iterator = std::find(feature_names.begin(), feature_names.end(), accepted);
    if (iterator != feature_names.end()) {
      return static_cast<std::size_t>(std::distance(feature_names.begin(), iterator));
    }
  }
  throw std::invalid_argument("required action feature is missing");
}

float finite_value(const ActionSample & sample, const std::size_t index)
{
  if (index >= sample.values.size()) {
    throw std::invalid_argument("action feature index exceeds vector dimension");
  }
  const float value = sample.values[index];
  if (!std::isfinite(value)) {
    throw std::invalid_argument("action contains a non-finite value");
  }
  return value;
}

class MobilityAdapter
{
public:
  virtual ~MobilityAdapter() = default;
  virtual std::string id() const = 0;
  virtual bool accepts(const ActionSample & sample) const = 0;
  virtual geometry_msgs::msg::Twist convert(const ActionSample & sample) const = 0;
};

class TwistMobilityAdapter final : public MobilityAdapter
{
public:
  std::string id() const override {return "chitu.mobility.twist.v1";}

  bool accepts(const ActionSample & sample) const override
  {
    return sample.schema == "vehicle.twist_chunk.v1" ||
           sample.schema == "chitu.action.differential.v1" ||
           sample.schema == "chitu.action.mecanum.v1";
  }

  geometry_msgs::msg::Twist convert(const ActionSample & sample) const override
  {
    geometry_msgs::msg::Twist output;
    output.linear.x = finite_value(
      sample, find_feature(sample.feature_names, {"linear_x", "linear_x_mps"}));
    const auto lateral = std::find_if(
      sample.feature_names.begin(), sample.feature_names.end(),
      [](const std::string & name) {return name == "linear_y" || name == "linear_y_mps";});
    if (lateral != sample.feature_names.end()) {
      output.linear.y = finite_value(
        sample, static_cast<std::size_t>(std::distance(sample.feature_names.begin(), lateral)));
    }
    output.angular.z = finite_value(
      sample, find_feature(sample.feature_names, {"angular_z", "angular_z_radps"}));
    return output;
  }
};

class AckermannMobilityAdapter final : public MobilityAdapter
{
public:
  AckermannMobilityAdapter(const double wheelbase, std::string command_semantics)
  : wheelbase_(wheelbase), command_semantics_(std::move(command_semantics))
  {
    if (!std::isfinite(wheelbase_) || wheelbase_ <= 0.0) {
      throw std::invalid_argument("ackermann wheelbase must be positive");
    }
    if (command_semantics_ != "yaw_rate" && command_semantics_ != "steering_angle") {
      throw std::invalid_argument("ackermann command_semantics must be yaw_rate or steering_angle");
    }
  }

  std::string id() const override {return "chitu.mobility.ackermann.v1";}

  bool accepts(const ActionSample & sample) const override
  {
    return sample.schema == "chitu.action.ackermann.v1";
  }

  geometry_msgs::msg::Twist convert(const ActionSample & sample) const override
  {
    const double speed = finite_value(
      sample, find_feature(sample.feature_names, {"speed_mps"}));
    const double steering_angle = finite_value(
      sample, find_feature(sample.feature_names, {"steering_angle_rad"}));
    geometry_msgs::msg::Twist output;
    output.linear.x = speed;
    output.angular.z = command_semantics_ == "steering_angle" ?
      steering_angle : speed * std::tan(steering_angle) / wheelbase_;
    if (!std::isfinite(output.angular.z)) {
      throw std::invalid_argument("ackermann conversion produced a non-finite angular command");
    }
    return output;
  }

private:
  double wheelbase_;
  std::string command_semantics_;
};

std::unique_ptr<MobilityAdapter> create_adapter(
  const std::string & adapter, const double wheelbase, const std::string & command_semantics)
{
  if (adapter == "twist") {
    return std::make_unique<TwistMobilityAdapter>();
  }
  if (adapter == "ackermann") {
    return std::make_unique<AckermannMobilityAdapter>(wheelbase, command_semantics);
  }
  throw std::invalid_argument("unsupported mobility adapter: " + adapter);
}

}  // namespace

class VlaActionRuntime final : public rclcpp::Node
{
public:
  VlaActionRuntime()
  : Node("vla_action_runtime")
  {
    const double execution_frequency = declare_parameter<double>("execution_frequency", 20.0);
    const std::string adapter = declare_parameter<std::string>("mobility_adapter", "twist");
    const double wheelbase = declare_parameter<double>("ackermann.wheelbase_m", 0.32);
    const std::string command_semantics = declare_parameter<std::string>(
      "ackermann.command_semantics", "yaw_rate");
    adapter_ = create_adapter(adapter, wheelbase, command_semantics);
    publisher_ = create_publisher<geometry_msgs::msg::TwistStamped>("/vla/cmd_vel_raw", 10);
    policy_target_publisher_ = create_publisher<vehicle_interfaces::msg::TrainingAction>(
      "/chitu/action/policy_target", 10);
    subscription_ = create_subscription<vehicle_interfaces::msg::PolicyAction>(
      "/vla/policy_action", 10,
      std::bind(&VlaActionRuntime::on_action, this, std::placeholders::_1));
    const auto period = std::chrono::duration<double>(
      1.0 / std::max(execution_frequency, 1.0));
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      std::bind(&VlaActionRuntime::execute, this));
    RCLCPP_INFO(get_logger(), "Loaded mobility adapter %s", adapter_->id().c_str());
  }

private:
  std::vector<ActionSample> samples_from(
    const vehicle_interfaces::msg::PolicyAction & message) const
  {
    std::vector<ActionSample> samples;
    if (!message.action_vectors.empty()) {
      samples.reserve(message.action_vectors.size());
      for (const auto & action : message.action_vectors) {
        samples.push_back(ActionSample{
          message.observation_id, message.schema_version, message.schema_hash,
          message.feature_names, message.feature_units, action.values});
      }
      return samples;
    }

    if (message.schema_version != "vehicle.twist_chunk.v1") {
      return samples;
    }
    samples.reserve(message.actions.size());
    for (const auto & action : message.actions) {
      samples.push_back(ActionSample{
        message.observation_id,
        message.schema_version,
        message.schema_hash,
        {"linear_x", "angular_z"},
        {"m/s", "rad/s"},
        {static_cast<float>(action.linear.x), static_cast<float>(action.angular.z)}});
    }
    return samples;
  }

  void on_action(const vehicle_interfaces::msg::PolicyAction::SharedPtr message)
  {
    const rclcpp::Time message_stamp(message->generated_at);
    std::vector<ActionSample> samples;
    try {
      samples = samples_from(*message);
      if (samples.empty()) {
        throw std::invalid_argument("policy action contains no generic action vectors");
      }
      for (const auto & sample : samples) {
        if (!adapter_->accepts(sample)) {
          throw std::invalid_argument(
                  "mobility adapter " + adapter_->id() + " rejects schema " + sample.schema);
        }
        adapter_->convert(sample);
      }
    } catch (const std::exception & error) {
      RCLCPP_WARN(get_logger(), "Rejected policy action: %s", error.what());
      return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (message_stamp < last_generated_at_) {
      RCLCPP_WARN(get_logger(), "Rejected out-of-order policy action");
      return;
    }
    actions_.assign(samples.begin(), samples.end());
    valid_until_ = rclcpp::Time(message->valid_until);
    last_generated_at_ = message_stamp;
    request_id_ = message->request_id;
  }

  void publish_policy_target(const ActionSample & sample, const rclcpp::Time & stamp)
  {
    vehicle_interfaces::msg::TrainingAction target;
    target.header.stamp = stamp;
    target.header.frame_id = "base_link";
    target.observation_id = sample.observation_id;
    target.action_schema = sample.schema;
    target.schema_hash = sample.schema_hash;
    target.feature_names = sample.feature_names;
    target.feature_units = sample.feature_units;
    target.values = sample.values;
    target.source = "policy.output";
    target.safety_intervened = false;
    policy_target_publisher_->publish(target);
  }

  void execute()
  {
    geometry_msgs::msg::TwistStamped output;
    const auto current_time = now();
    output.header.stamp = current_time;
    output.header.frame_id = "base_link";
    std::optional<ActionSample> sample;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (current_time <= valid_until_ && !actions_.empty()) {
        sample = actions_.front();
        actions_.pop_front();
      } else {
        actions_.clear();
      }
    }
    if (sample) {
      try {
        output.twist = adapter_->convert(*sample);
        publish_policy_target(*sample, current_time);
      } catch (const std::exception & error) {
        RCLCPP_ERROR(get_logger(), "Mobility conversion failed: %s", error.what());
      }
    }
    publisher_->publish(output);
  }

  std::unique_ptr<MobilityAdapter> adapter_;
  std::mutex mutex_;
  std::deque<ActionSample> actions_;
  rclcpp::Time valid_until_{0, 0, RCL_ROS_TIME};
  rclcpp::Time last_generated_at_{0, 0, RCL_ROS_TIME};
  std::string request_id_;
  rclcpp::Publisher<geometry_msgs::msg::TwistStamped>::SharedPtr publisher_;
  rclcpp::Publisher<vehicle_interfaces::msg::TrainingAction>::SharedPtr policy_target_publisher_;
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