#pragma once

#include <geometry_msgs/msg/twist.hpp>

#include <chrono>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace vehicle_runtime
{

struct PolicyPrediction
{
  std::string request_id;
  std::string model_id;
  std::chrono::milliseconds control_period{50};
  std::vector<geometry_msgs::msg::Twist> actions;
};

class PolicyTransport
{
public:
  virtual ~PolicyTransport() = default;
  virtual bool ready() const = 0;
  virtual std::string provider_id() const = 0;
  virtual std::string model_id() const = 0;
  virtual PolicyPrediction predict(const std::string & task) = 0;
};

class MockPolicyTransport final : public PolicyTransport
{
public:
  MockPolicyTransport(
    const std::size_t horizon,
    const std::chrono::milliseconds control_period,
    const double linear_velocity,
    const double angular_velocity)
  : horizon_(horizon),
    control_period_(control_period),
    linear_velocity_(linear_velocity),
    angular_velocity_(angular_velocity)
  {
  }

  bool ready() const override {return true;}
  std::string provider_id() const override {return "mock";}
  std::string model_id() const override {return "mock-zero-policy-v1";}

  PolicyPrediction predict(const std::string & task) override
  {
    PolicyPrediction prediction;
    prediction.request_id = "mock-" + std::to_string(++request_counter_);
    prediction.model_id = model_id();
    prediction.control_period = control_period_;
    prediction.actions.resize(horizon_);

    if (!task.empty()) {
      for (auto & action : prediction.actions) {
        action.linear.x = linear_velocity_;
        action.angular.z = angular_velocity_;
      }
    }
    return prediction;
  }

private:
  std::size_t horizon_;
  std::chrono::milliseconds control_period_;
  double linear_velocity_;
  double angular_velocity_;
  std::size_t request_counter_{0};
};

}  // namespace vehicle_runtime
