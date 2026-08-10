#include "vehicle_policy_transport/unix_socket_policy_transport.hpp"

#include "policy_protocol.pb.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace vehicle_policy_transport
{
namespace
{

constexpr char kProtocolVersion[] = "1";
constexpr std::size_t kMaxFrameSize = 32U * 1024U * 1024U;

class SocketHandle
{
public:
  explicit SocketHandle(const int descriptor) : descriptor_(descriptor) {}
  ~SocketHandle() {if (descriptor_ >= 0) {::close(descriptor_);}}
  int get() const {return descriptor_;}

private:
  int descriptor_;
};

void send_all(const int descriptor, const void * data, const std::size_t size)
{
  const auto * bytes = static_cast<const uint8_t *>(data);
  std::size_t sent = 0;
  while (sent < size) {
    const auto result = ::send(descriptor, bytes + sent, size - sent, MSG_NOSIGNAL);
    if (result <= 0) {
      throw std::runtime_error("socket send failed: " + std::string(std::strerror(errno)));
    }
    sent += static_cast<std::size_t>(result);
  }
}

void receive_all(const int descriptor, void * data, const std::size_t size)
{
  auto * bytes = static_cast<uint8_t *>(data);
  std::size_t received = 0;
  while (received < size) {
    const auto result = ::recv(descriptor, bytes + received, size - received, 0);
    if (result <= 0) {
      throw std::runtime_error("socket receive failed: " + std::string(std::strerror(errno)));
    }
    received += static_cast<std::size_t>(result);
  }
}

vla::policy::v1::Envelope transact(
  const std::string & socket_path,
  const std::chrono::milliseconds timeout,
  const vla::policy::v1::Envelope & request)
{
  if (socket_path.size() >= sizeof(sockaddr_un::sun_path)) {
    throw std::runtime_error("policy socket path is too long");
  }
  const int descriptor = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (descriptor < 0) {
    throw std::runtime_error("socket creation failed: " + std::string(std::strerror(errno)));
  }
  SocketHandle socket_handle(descriptor);
  const timeval socket_timeout{
    static_cast<time_t>(timeout.count() / 1000),
    static_cast<suseconds_t>((timeout.count() % 1000) * 1000)};
  ::setsockopt(descriptor, SOL_SOCKET, SO_SNDTIMEO, &socket_timeout, sizeof(socket_timeout));
  ::setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &socket_timeout, sizeof(socket_timeout));

  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::strncpy(address.sun_path, socket_path.c_str(), sizeof(address.sun_path) - 1);
  if (::connect(descriptor, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
    throw std::runtime_error("policy runtime connection failed: " + std::string(std::strerror(errno)));
  }

  std::string payload;
  if (!request.SerializeToString(&payload) || payload.empty() || payload.size() > kMaxFrameSize) {
    throw std::runtime_error("failed to serialize policy request");
  }
  const uint32_t network_size = htonl(static_cast<uint32_t>(payload.size()));
  send_all(descriptor, &network_size, sizeof(network_size));
  send_all(descriptor, payload.data(), payload.size());

  uint32_t response_network_size = 0;
  receive_all(descriptor, &response_network_size, sizeof(response_network_size));
  const std::size_t response_size = ntohl(response_network_size);
  if (response_size == 0 || response_size > kMaxFrameSize) {
    throw std::runtime_error("invalid policy response frame size");
  }
  std::string response_payload(response_size, '\0');
  receive_all(descriptor, response_payload.data(), response_payload.size());
  vla::policy::v1::Envelope response;
  if (!response.ParseFromString(response_payload)) {
    throw std::runtime_error("failed to parse policy response");
  }
  if (response.protocol_version() != kProtocolVersion) {
    throw std::runtime_error("policy protocol version mismatch");
  }
  if (response.has_error_response()) {
    throw std::runtime_error(
            response.error_response().code() + ": " + response.error_response().message());
  }
  return response;
}

void populate_observation(
  vla::policy::v1::PredictRequest * target,
  const PolicyObservationInput & observation)
{
  target->set_observation_id(observation.observation_id);
  target->set_schema_version(observation.schema_version);
  target->set_task(observation.task);
  target->set_generated_at_ns(observation.generated_at_ns);
  target->set_valid_until_ns(observation.valid_until_ns);
  for (const auto & image : observation.images) {
    auto * output = target->add_images();
    output->set_key(image.key);
    output->set_format(image.format);
    output->set_data(image.data.data(), image.data.size());
  }
  const auto state_count = std::min({
    observation.state_keys.size(), observation.state.size(), observation.state_valid.size()});
  for (std::size_t index = 0; index < state_count; ++index) {
    auto * output = target->add_state();
    output->set_key(observation.state_keys[index]);
    output->set_value(observation.state[index]);
    output->set_valid(observation.state_valid[index]);
  }
}
}  // namespace

UnixSocketPolicyTransport::UnixSocketPolicyTransport(
  std::string socket_path, const std::chrono::milliseconds timeout)
: socket_path_(std::move(socket_path)), timeout_(timeout)
{
  refresh();
}

void UnixSocketPolicyTransport::refresh()
{
  try {
    vla::policy::v1::Envelope request;
    request.set_protocol_version(kProtocolVersion);
    request.set_message_id("health");
    request.mutable_health_request();
    const auto response = transact(socket_path_, timeout_, request);
    if (!response.has_health_response()) {
      throw std::runtime_error("missing health response");
    }
    std::lock_guard<std::mutex> lock(state_mutex_);
    ready_ = response.health_response().ready();
    provider_id_ = response.health_response().provider_id();
    model_id_ = response.health_response().model_id();
    status_message_ = response.health_response().message();
  } catch (const std::exception & error) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    ready_ = false;
    status_message_ = error.what();
  }
}

bool UnixSocketPolicyTransport::ready() const
{
  std::lock_guard<std::mutex> lock(state_mutex_);
  return ready_;
}

std::string UnixSocketPolicyTransport::provider_id() const
{
  std::lock_guard<std::mutex> lock(state_mutex_);
  return provider_id_;
}

std::string UnixSocketPolicyTransport::model_id() const
{
  std::lock_guard<std::mutex> lock(state_mutex_);
  return model_id_;
}

std::string UnixSocketPolicyTransport::status_message() const
{
  std::lock_guard<std::mutex> lock(state_mutex_);
  return status_message_;
}

PolicyPrediction UnixSocketPolicyTransport::predict(const PolicyObservationInput & observation)
{
  try {
    vla::policy::v1::Envelope request;
    request.set_protocol_version(kProtocolVersion);
    request.set_message_id(observation.observation_id);
    populate_observation(request.mutable_predict_request(), observation);

    const auto response = transact(socket_path_, timeout_, request);
    if (!response.has_predict_response()) {
      throw std::runtime_error("missing predict response");
    }
    const auto & source = response.predict_response();
    if (source.observation_id() != observation.observation_id) {
      throw std::runtime_error("policy response observation ID mismatch");
    }
    PolicyPrediction prediction;
    prediction.request_id = source.request_id();
    prediction.observation_id = source.observation_id();
    prediction.model_id = source.model_id();
    prediction.control_period = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::nanoseconds(source.control_period_ns()));
    prediction.actions.reserve(source.actions_size());
    for (const auto & source_action : source.actions()) {
      geometry_msgs::msg::Twist action;
      action.linear.x = source_action.linear_x();
      action.linear.y = source_action.linear_y();
      action.linear.z = source_action.linear_z();
      action.angular.x = source_action.angular_x();
      action.angular.y = source_action.angular_y();
      action.angular.z = source_action.angular_z();
      prediction.actions.push_back(action);
    }
    if (prediction.actions.empty() || prediction.control_period.count() <= 0) {
      throw std::runtime_error("policy response contains no executable actions");
    }
    return prediction;
  } catch (const std::exception & error) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    ready_ = false;
    status_message_ = error.what();
    throw;
  }
}

PolicyDebugResult UnixSocketPolicyTransport::debug(
  const PolicyObservationInput & observation,
  const std::string & run_id,
  const std::string & stage)
{
  try {
    vla::policy::v1::Envelope request;
    request.set_protocol_version(kProtocolVersion);
    request.set_message_id(run_id);
    auto * debug_request = request.mutable_debug_request();
    debug_request->set_debug_run_id(run_id);
    debug_request->set_stage(stage);
    populate_observation(debug_request->mutable_observation(), observation);

    const auto response = transact(socket_path_, timeout_, request);
    if (!response.has_debug_response()) {
      throw std::runtime_error("missing debug response");
    }
    const auto & source = response.debug_response();
    if (source.debug_run_id() != run_id) {
      throw std::runtime_error("policy debug response run ID mismatch");
    }
    PolicyDebugResult result;
    result.run_id = source.debug_run_id();
    result.provider_id = source.provider_id();
    result.model_id = source.model_id();
    result.schema_version = source.schema_version();
    result.result_json = source.result_json();
    result.processed_image_jpeg.assign(
      source.processed_image_jpeg().begin(), source.processed_image_jpeg().end());
    return result;
  } catch (const std::exception & error) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    status_message_ = error.what();
    throw;
  }
}
}  // namespace vehicle_policy_transport
