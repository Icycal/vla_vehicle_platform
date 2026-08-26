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
std::string MockPolicyTransport::action_schema() const {return "vehicle.twist_chunk.v1";}
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
  prediction.action_schema = action_schema();
  prediction.action_features = {"linear_x", "angular_z"};
  prediction.action_units = {"m/s", "rad/s"};
  prediction.control_period = control_period_;
  prediction.action_vectors.resize(horizon_, std::vector<float>(2, 0.0F));
  prediction.actions.resize(horizon_);
  if (!observation.task.empty()) {
    for (std::size_t index = 0; index < prediction.actions.size(); ++index) {
      auto & action = prediction.actions[index];
      action.linear.x = linear_velocity_;
      action.angular.z = angular_velocity_;
      prediction.action_vectors[index][0] = static_cast<float>(linear_velocity_);
      prediction.action_vectors[index][1] = static_cast<float>(angular_velocity_);
    }
  }
  return prediction;
}

PolicyDebugResult MockPolicyTransport::debug(
  const PolicyObservationInput & observation,
  const std::string & run_id,
  const std::string & stage)
{
  if (stage != "preprocess" && stage != "inference") {
    throw std::runtime_error("unsupported debug stage");
  }
  PolicyDebugResult result;
  result.run_id = run_id;
  result.provider_id = provider_id();
  result.model_id = model_id();
  result.schema_version = "vehicle.vla.debug.v1";
  result.result_json = "{\"schema_version\":\"vehicle.vla.debug.v1\",\"debug_run_id\":\"" +
    run_id + "\",\"stage\":\"" + stage + "\",\"provider_id\":\"mock\",\"model_id\":\"" +
    model_id() + "\",\"observation\":{\"observation_id\":\"" + observation.observation_id +
    "\"},\"safety\":{\"operation_mode\":\"shadow\",\"publishes_control\":false}}";
  return result;
}
}  // namespace vehicle_policy_transport