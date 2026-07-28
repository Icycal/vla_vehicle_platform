#include "vehicle_policy_transport/mock_policy_transport.hpp"

#include <stdexcept>

namespace vehicle_policy_transport
{

MockPolicyTransport::MockPolicyTransport(
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

void MockPolicyTransport::refresh() {}
bool MockPolicyTransport::ready() const {return true;}
std::string MockPolicyTransport::provider_id() const {return "mock";}
std::string MockPolicyTransport::model_id() const {return "mock-zero-policy-v1";}
std::string MockPolicyTransport::status_message() const {return "In-process mock policy ready";}

PolicyPrediction MockPolicyTransport::predict(const PolicyObservationInput & observation)
{
  if (observation.images.empty()) {
    throw std::runtime_error("at least one image is required");
  }
  PolicyPrediction prediction;
  prediction.request_id = "mock-" + std::to_string(++request_counter_);
  prediction.observation_id = observation.observation_id;
  prediction.model_id = model_id();
  prediction.control_period = control_period_;
  prediction.actions.resize(horizon_);
  if (!observation.task.empty()) {
    for (auto & action : prediction.actions) {
      action.linear.x = linear_velocity_;
      action.angular.z = angular_velocity_;
    }
  }
  return prediction;
}

}  // namespace vehicle_policy_transport
