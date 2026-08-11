#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <rclcpp/rclcpp.hpp>
#include <vehicle_interfaces/msg/component_state.hpp>
#include <vehicle_interfaces/srv/control_component.hpp>
#include <vehicle_interfaces/srv/control_profile.hpp>
#include <vehicle_interfaces/srv/read_component_log.hpp>
#include <vehicle_interfaces/srv/list_components.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace
{
struct CommandResult
{
  int exit_code{-1};
  std::string output;
};

CommandResult run_command(const std::vector<std::string> & arguments)
{
  if (arguments.empty()) {throw std::invalid_argument("Command is empty");}
  std::array<int, 2> pipes{};
  if (pipe(pipes.data()) != 0) {throw std::runtime_error("Unable to create command pipe");}
  const pid_t pid = fork();
  if (pid < 0) {
    close(pipes[0]);
    close(pipes[1]);
    throw std::runtime_error("Unable to fork command");
  }
  if (pid == 0) {
    dup2(pipes[1], STDOUT_FILENO);
    dup2(pipes[1], STDERR_FILENO);
    close(pipes[0]);
    close(pipes[1]);
    std::vector<char *> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto & argument : arguments) {argv.push_back(const_cast<char *>(argument.c_str()));}
    argv.push_back(nullptr);
    execvp(argv.front(), argv.data());
    _exit(127);
  }
  close(pipes[1]);
  std::string output;
  std::array<char, 4096> buffer{};
  ssize_t count = 0;
  while ((count = read(pipes[0], buffer.data(), buffer.size())) > 0) {
    output.append(buffer.data(), static_cast<std::size_t>(count));
  }
  close(pipes[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
  return {exit_code, output};
}

std::map<std::string, std::string> parse_properties(const std::string & text)
{
  std::map<std::string, std::string> result;
  std::istringstream input(text);
  std::string line;
  while (std::getline(input, line)) {
    const auto separator = line.find('=');
    if (separator != std::string::npos) {result[line.substr(0, separator)] = line.substr(separator + 1);}
  }
  return result;
}

std::int64_t integer_value(const std::map<std::string, std::string> & values, const std::string & key)
{
  const auto found = values.find(key);
  if (found == values.end() || found->second.empty()) {return 0;}
  try {return std::stoll(found->second);}
  catch (const std::exception &) {return 0;}
}

std::string trim(std::string value)
{
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {return {};}
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}
}  // namespace

class OperationOrchestrator : public rclcpp::Node
{
public:
  OperationOrchestrator() : Node("operation_orchestrator")
  {
    load_configuration();
    list_service_ = create_service<vehicle_interfaces::srv::ListComponents>(
      "/vehicle/operations/list_components",
      [this](const std::shared_ptr<vehicle_interfaces::srv::ListComponents::Request>,
      std::shared_ptr<vehicle_interfaces::srv::ListComponents::Response> response) {
        refresh_states();
        std::lock_guard<std::mutex> lock(state_mutex_);
        for (const auto & id : component_order_) {response->components.push_back(states_.at(id));}
        response->profiles = profile_order_;
      });
    component_service_ = create_service<vehicle_interfaces::srv::ControlComponent>(
      "/vehicle/operations/control_component",
      [this](const std::shared_ptr<vehicle_interfaces::srv::ControlComponent::Request> request,
      std::shared_ptr<vehicle_interfaces::srv::ControlComponent::Response> response) {
        std::lock_guard<std::mutex> control_lock(control_mutex_);
        try {
          std::set<std::string> visited;
          response->accepted = control_component(request->component_id, request->action, request->force, visited);
          refresh_states();
          response->component = state_for(request->component_id);
          response->message = response->accepted ? "组件操作已完成" : "组件操作被拒绝";
        } catch (const std::exception & error) {
          response->accepted = false;
          response->message = error.what();
          response->component = state_for(request->component_id);
        }
      });
    profile_service_ = create_service<vehicle_interfaces::srv::ControlProfile>(
      "/vehicle/operations/control_profile",
      [this](const std::shared_ptr<vehicle_interfaces::srv::ControlProfile::Request> request,
      std::shared_ptr<vehicle_interfaces::srv::ControlProfile::Response> response) {
        std::lock_guard<std::mutex> control_lock(control_mutex_);
        try {
          const auto found = profiles_.find(request->profile_id);
          if (found == profiles_.end()) {throw std::invalid_argument("Unknown profile: " + request->profile_id);}
          auto components = found->second;
          if (request->action == "stop") {std::reverse(components.begin(), components.end());}
          for (const auto & component : components) {
            std::set<std::string> visited;
            if (!control_component(component, request->action, request->action == "stop", visited)) {
              throw std::runtime_error("Profile operation failed at " + component);
            }
          }
          refresh_states();
          response->accepted = true;
          response->message = "场景操作已完成";
          for (const auto & id : component_order_) {response->components.push_back(state_for(id));}
        } catch (const std::exception & error) {
          response->accepted = false;
          response->message = error.what();
        }
      });
    log_service_ = create_service<vehicle_interfaces::srv::ReadComponentLog>(
      "/vehicle/operations/read_component_log",
      [this](const std::shared_ptr<vehicle_interfaces::srv::ReadComponentLog::Request> request,
      std::shared_ptr<vehicle_interfaces::srv::ReadComponentLog::Response> response) {
        RCLCPP_INFO(get_logger(), "Loading component log for %s", request->component_id.c_str());
        const auto found = components_.find(request->component_id);
        if (found == components_.end()) {
          response->accepted = false;
          response->message = "Unknown component";
          return;
        }
        constexpr std::size_t lines = 160;
        std::ifstream input(found->second.log_path);
        if (!input) {
          response->accepted = false;
          response->message = "Component log is not available yet";
          return;
        }
        std::deque<std::string> tail;
        std::string line;
        while (std::getline(input, line)) {
          tail.push_back(line);
          if (tail.size() > lines) {tail.pop_front();}
        }
        std::ostringstream output;
        for (const auto & value : tail) {output << value << '\n';}
        response->accepted = true;
        response->message = "Log loaded";
        response->content = output.str();
        RCLCPP_INFO(get_logger(), "Loaded %zu log bytes for %s", response->content.size(), request->component_id.c_str());
        RCLCPP_INFO(get_logger(), "Loaded %zu log bytes for %s", response->content.size(), request->component_id.c_str());
      });
    refresh_states();
    RCLCPP_INFO(get_logger(), "Operation orchestrator loaded %zu components and %zu profiles",
      components_.size(), profiles_.size());
  }

private:
  struct ComponentDefinition
  {
    std::string id;
    std::string display_name;
    std::string group_name;
    std::string unit;
    std::string log_path;
    std::vector<std::string> dependencies;
    std::vector<std::string> expected_nodes;
    std::vector<std::string> health_topics;
  };

  void load_configuration()
  {
    component_order_ = declare_parameter<std::vector<std::string>>("component_ids", std::vector<std::string>{});
    if (component_order_.empty()) {throw std::invalid_argument("component_ids must not be empty");}
    for (const auto & id : component_order_) {
      ComponentDefinition definition;
      definition.id = id;
      const auto prefix = "components." + id + ".";
      definition.display_name = declare_parameter<std::string>(prefix + "display_name", id);
      definition.group_name = declare_parameter<std::string>(prefix + "group_name", "other");
      definition.unit = declare_parameter<std::string>(prefix + "unit", "");
      definition.log_path = declare_parameter<std::string>(prefix + "log_path", "");
      definition.dependencies = declare_parameter<std::vector<std::string>>(prefix + "dependencies", std::vector<std::string>{});
      definition.expected_nodes = declare_parameter<std::vector<std::string>>(prefix + "expected_nodes", std::vector<std::string>{});
      definition.health_topics = declare_parameter<std::vector<std::string>>(prefix + "health_topics", std::vector<std::string>{});
      if (definition.unit.empty() || definition.log_path.empty() || definition.log_path.rfind("/home/wheeltec/vla_vehicle_platform/run/log/components/", 0) != 0 || definition.unit.find_first_not_of(
          "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@_.-") != std::string::npos)
      {
        throw std::invalid_argument("Invalid systemd unit for component " + id);
      }
      components_[id] = definition;
    }
    for (const auto & [id, definition] : components_) {
      for (const auto & dependency : definition.dependencies) {
        if (!components_.count(dependency)) {throw std::invalid_argument(id + " has unknown dependency " + dependency);}
      }
    }
    profile_order_ = declare_parameter<std::vector<std::string>>("profile_ids", std::vector<std::string>{});
    for (const auto & id : profile_order_) {
      auto values = declare_parameter<std::vector<std::string>>("profiles." + id + ".components", std::vector<std::string>{});
      for (const auto & component : values) {
        if (!components_.count(component)) {throw std::invalid_argument(id + " has unknown component " + component);}
      }
      profiles_[id] = std::move(values);
    }
  }

  vehicle_interfaces::msg::ComponentState inspect(const ComponentDefinition & definition)
  {
    vehicle_interfaces::msg::ComponentState state;
    state.component_id = definition.id;
    state.display_name = definition.display_name;
    state.group_name = definition.group_name;
    state.unit_name = definition.unit;
    state.dependencies = definition.dependencies;
    state.expected_nodes = definition.expected_nodes;
    state.health_topics = definition.health_topics;
    const auto command = run_command({"systemctl", "--user", "show", definition.unit,
      "--property=LoadState,ActiveState,SubState,MainPID,ExecMainStatus,ActiveEnterTimestampMonotonic"});
    const auto properties = parse_properties(command.output);
    const auto load_state = properties.count("LoadState") ? properties.at("LoadState") : "not-found";
    const auto active_state = properties.count("ActiveState") ? properties.at("ActiveState") : "inactive";
    const auto sub_state = properties.count("SubState") ? properties.at("SubState") : "dead";
    state.pid = integer_value(properties, "MainPID");
    state.last_exit_code = static_cast<std::int32_t>(integer_value(properties, "ExecMainStatus"));
    state.managed = active_state == "active" || active_state == "activating" || active_state == "deactivating";
    bool recently_stopped = false;
    {
      std::lock_guard<std::mutex> lock(recent_stop_mutex_);
      const auto stopped = recently_stopped_.find(definition.id);
      if (stopped != recently_stopped_.end()) {
        recently_stopped = std::chrono::steady_clock::now() - stopped->second < 15s;
        if (!recently_stopped) {recently_stopped_.erase(stopped);}
      }
    }

    const auto graph_nodes = get_node_names();
    std::set<std::string> normalized_nodes;
    for (const auto & node_name : graph_nodes) {
      normalized_nodes.insert(node_name.empty() || node_name.front() != '/' ? node_name : node_name.substr(1));
    }
    bool nodes_present = definition.expected_nodes.empty();
    for (const auto & expected : definition.expected_nodes) {
      const auto clean = expected.empty() || expected.front() != '/' ? expected : expected.substr(1);
      if (normalized_nodes.count(clean)) {nodes_present = true;}
      else {nodes_present = false; break;}
    }
    bool topics_present = definition.health_topics.empty();
    for (const auto & topic : definition.health_topics) {
      if (!get_publishers_info_by_topic(topic).empty()) {topics_present = true;}
      else {topics_present = false; break;}
    }
    state.healthy = state.managed && active_state == "active" && nodes_present && topics_present;
    if (load_state != "loaded") {
      state.state = "unavailable";
      state.message = "systemd unit is not installed";
    } else if (!state.managed && nodes_present && recently_stopped) {
      state.state = "stopping";
      state.message = "systemd stopped; waiting for ROS graph cleanup";
    } else if (!state.managed && nodes_present && !definition.expected_nodes.empty()) {
      state.state = "external";
      state.message = "ROS nodes are running outside the orchestrator";
    } else if (active_state == "activating") {
      state.state = "starting";
      state.message = sub_state;
    } else if (active_state == "deactivating") {
      state.state = "stopping";
      state.message = sub_state;
    } else if (active_state == "failed") {
      state.state = "failed";
      state.message = "Unit failed with exit code " + std::to_string(state.last_exit_code);
    } else if (active_state == "active") {
      state.state = state.healthy ? "running" : "degraded";
      state.message = state.healthy ? "ROS nodes and health topics are available" : "Unit active but ROS health checks are incomplete";
    } else {
      state.state = "stopped";
      state.message = "Component is stopped";
    }
    if (state.managed) {
      const auto entered = integer_value(properties, "ActiveEnterTimestampMonotonic");
      timespec clock{};
      clock_gettime(CLOCK_MONOTONIC, &clock);
      const auto now_us = static_cast<std::int64_t>(clock.tv_sec) * 1000000LL + clock.tv_nsec / 1000;
      state.uptime_seconds = entered > 0 && now_us > entered ? (now_us - entered) / 1000000.0 : 0.0;
    }
    state.can_start = state.state == "stopped" || state.state == "failed";
    state.can_stop = state.managed && (state.state == "running" || state.state == "degraded" || state.state == "starting");
    state.can_restart = state.can_stop;
    return state;
  }

  void refresh_states()
  {
    std::unordered_map<std::string, vehicle_interfaces::msg::ComponentState> updated;
    for (const auto & id : component_order_) {updated[id] = inspect(components_.at(id));}
    std::lock_guard<std::mutex> lock(state_mutex_);
    states_ = std::move(updated);
  }

  vehicle_interfaces::msg::ComponentState state_for(const std::string & id)
  {
    std::lock_guard<std::mutex> lock(state_mutex_);
    const auto found = states_.find(id);
    return found == states_.end() ? vehicle_interfaces::msg::ComponentState{} : found->second;
  }

  bool control_component(
    const std::string & id, const std::string & action, bool force, std::set<std::string> & visited)
  {
    const auto found = components_.find(id);
    if (found == components_.end()) {throw std::invalid_argument("Unknown component: " + id);}
    if (!visited.insert(id).second) {return true;}
    if (action == "start" || action == "restart") {
      for (const auto & dependency : found->second.dependencies) {
        if (!control_component(dependency, "start", false, visited)) {return false;}
      }
    }
    if (action == "stop") {
      refresh_states();
      for (const auto & dependent_id : component_order_) {
        const auto & definition = components_.at(dependent_id);
        if (std::find(definition.dependencies.begin(), definition.dependencies.end(), id) == definition.dependencies.end()) {continue;}
        const auto dependent = state_for(dependent_id);
        if (!dependent.managed) {continue;}
        if (!force) {throw std::runtime_error("Stop dependent component first: " + dependent_id);}
        if (!control_component(dependent_id, "stop", true, visited)) {return false;}
      }
    }
    if (action != "start" && action != "stop" && action != "restart") {
      throw std::invalid_argument("Unsupported action: " + action);
    }
    const auto result = run_command({"systemctl", "--user", "--no-ask-password", action, found->second.unit});
    if (result.exit_code != 0) {throw std::runtime_error(trim(result.output));}
    {
      std::lock_guard<std::mutex> lock(recent_stop_mutex_);
      if (action == "stop") {recently_stopped_[id] = std::chrono::steady_clock::now();}
      else {recently_stopped_.erase(id);}
    }
    std::this_thread::sleep_for(250ms);
    return true;
  }

  std::vector<std::string> component_order_;
  std::vector<std::string> profile_order_;
  std::unordered_map<std::string, ComponentDefinition> components_;
  std::unordered_map<std::string, std::vector<std::string>> profiles_;
  std::unordered_map<std::string, vehicle_interfaces::msg::ComponentState> states_;
  std::mutex state_mutex_;
  std::mutex control_mutex_;
  std::mutex recent_stop_mutex_;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point> recently_stopped_;
  rclcpp::Service<vehicle_interfaces::srv::ListComponents>::SharedPtr list_service_;
  rclcpp::Service<vehicle_interfaces::srv::ControlComponent>::SharedPtr component_service_;
  rclcpp::Service<vehicle_interfaces::srv::ControlProfile>::SharedPtr profile_service_;
  rclcpp::Service<vehicle_interfaces::srv::ReadComponentLog>::SharedPtr log_service_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  try {rclcpp::spin(std::make_shared<OperationOrchestrator>());}
  catch (const std::exception & error) {
    std::cerr << "operation_orchestrator failed: " << error.what() << std::endl;
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}