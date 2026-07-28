#pragma once

#include "vehicle_policy_transport/policy_transport.hpp"

#include <mutex>

namespace vehicle_policy_transport
{

class UnixSocketPolicyTransport final : public PolicyTransport
{
public:
  UnixSocketPolicyTransport(std::string socket_path, std::chrono::milliseconds timeout);

  void refresh() override;
  bool ready() const override;
  std::string provider_id() const override;
  std::string model_id() const override;
  std::string status_message() const override;
  PolicyPrediction predict(const PolicyObservationInput & observation) override;

private:
  std::string socket_path_;
  std::chrono::milliseconds timeout_;
  mutable std::mutex state_mutex_;
  bool ready_{false};
  std::string provider_id_{"unix_socket"};
  std::string model_id_;
  std::string status_message_{"Policy runtime not checked"};
};

}  // namespace vehicle_policy_transport
