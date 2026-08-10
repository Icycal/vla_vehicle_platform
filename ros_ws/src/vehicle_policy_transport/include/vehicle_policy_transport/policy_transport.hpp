#pragma once

#include <geometry_msgs/msg/twist.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace vehicle_policy_transport
{

struct PolicyImage
{
  std::string key;
  std::string format;
  std::vector<uint8_t> data;
};

struct PolicyObservationInput
{
  std::string observation_id;
  std::string schema_version;
  std::string task;
  int64_t generated_at_ns{0};
  int64_t valid_until_ns{0};
  std::vector<PolicyImage> images;
  std::vector<std::string> state_keys;
  std::vector<float> state;
  std::vector<bool> state_valid;
};

struct PolicyPrediction
{
  std::string request_id;
  std::string observation_id;
  std::string model_id;
  std::chrono::milliseconds control_period{50};
  std::vector<geometry_msgs::msg::Twist> actions;
};

struct PolicyDebugResult
{
  std::string run_id;
  std::string provider_id;
  std::string model_id;
  std::string schema_version;
  std::string result_json;
  std::vector<uint8_t> processed_image_jpeg;
};
class PolicyTransport
{
public:
  virtual ~PolicyTransport() = default;
  virtual void refresh() = 0;
  virtual bool ready() const = 0;
  virtual std::string provider_id() const = 0;
  virtual std::string model_id() const = 0;
  virtual std::string status_message() const = 0;
  virtual PolicyPrediction predict(const PolicyObservationInput & observation) = 0;
  virtual PolicyDebugResult debug(
    const PolicyObservationInput & observation,
    const std::string & run_id,
    const std::string & stage) = 0;
};

}  // namespace vehicle_policy_transport
