#pragma once

#include "vehicle_policy_transport/policy_transport.hpp"

#include <cstddef>

namespace vehicle_policy_transport
{

class MockPolicyTransport final : public PolicyTransport
{
public:
  MockPolicyTransport(
    std::size_t horizon,
    std::chrono::milliseconds control_period,
    double linear_velocity,
    double angular_velocity);

  void refresh() override;
  bool ready() const override;
  std::string provider_id() const override;
  std::string model_id() const override;
  std::string status_message() const override;
  PolicyPrediction predict(const PolicyObservationInput & observation) override;

private:
  std::size_t horizon_;
  std::chrono::milliseconds control_period_;
  double linear_velocity_;
  double angular_velocity_;
  std::size_t request_counter_{0};
};

}  // namespace vehicle_policy_transport
