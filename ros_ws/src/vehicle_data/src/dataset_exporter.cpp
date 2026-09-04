#include <rclcpp/rclcpp.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <rosbag2_cpp/reader.hpp>
#include <vehicle_interfaces/msg/policy_action.hpp>
#include <vehicle_interfaces/msg/policy_observation.hpp>
#include <vehicle_interfaces/msg/shadow_comparison.hpp>
#include <vehicle_interfaces/msg/training_action.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

struct Arguments
{
  std::filesystem::path episode_path;
  std::filesystem::path output_path;
  bool include_incomplete{false};
};

struct ObservationRecord
{
  vehicle_interfaces::msg::PolicyObservation observation;
  int64_t bag_timestamp_ns{0};
};

struct ExportStats
{
  std::size_t observation_count{0};
  std::size_t frame_count{0};
  std::size_t incomplete_count{0};
  std::size_t image_count{0};
};

void print_usage()
{
  std::cout
    << "Usage: dataset_exporter --episode DIR --output DIR [--include-incomplete]\n";
}

Arguments parse_arguments(const int argc, char ** argv)
{
  Arguments arguments;
  for (int index = 1; index < argc; ++index) {
    const std::string option = argv[index];
    if (option == "--episode" && index + 1 < argc) {
      arguments.episode_path = argv[++index];
    } else if (option == "--output" && index + 1 < argc) {
      arguments.output_path = argv[++index];
    } else if (option == "--include-incomplete") {
      arguments.include_incomplete = true;
    } else if (option == "-h" || option == "--help") {
      print_usage();
      std::exit(0);
    } else {
      throw std::invalid_argument("Unknown or incomplete option: " + option);
    }
  }
  if (arguments.episode_path.empty() || arguments.output_path.empty()) {
    print_usage();
    throw std::invalid_argument("--episode and --output are required");
  }
  arguments.episode_path = std::filesystem::weakly_canonical(arguments.episode_path);
  arguments.output_path = std::filesystem::absolute(arguments.output_path).lexically_normal();
  if (!std::filesystem::is_directory(arguments.episode_path)) {
    throw std::invalid_argument("Episode directory does not exist: " + arguments.episode_path.string());
  }
  if (std::filesystem::exists(arguments.output_path)) {
    throw std::invalid_argument("Output path already exists: " + arguments.output_path.string());
  }
  if (arguments.output_path == arguments.episode_path || arguments.output_path.empty()) {
    throw std::invalid_argument("Unsafe output path");
  }
  return arguments;
}

template<typename MessageT>
MessageT deserialize_message(
  const std::shared_ptr<rosbag2_storage::SerializedBagMessage> & bag_message)
{
  rclcpp::SerializedMessage serialized_message(*bag_message->serialized_data);
  rclcpp::Serialization<MessageT> serialization;
  MessageT message;
  serialization.deserialize_message(&serialized_message, &message);
  return message;
}

int64_t time_to_nanoseconds(const builtin_interfaces::msg::Time & time)
{
  return static_cast<int64_t>(time.sec) * 1000000000LL + time.nanosec;
}

std::string json_escape(const std::string & value)
{
  std::ostringstream stream;
  for (const unsigned char character : value) {
    switch (character) {
      case '\\': stream << "\\\\"; break;
      case '"': stream << "\\\""; break;
      case '\b': stream << "\\b"; break;
      case '\f': stream << "\\f"; break;
      case '\n': stream << "\\n"; break;
      case '\r': stream << "\\r"; break;
      case '\t': stream << "\\t"; break;
      default:
        if (character < 0x20U) {
          stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                 << static_cast<int>(character) << std::dec << std::setfill(' ');
        } else {
          stream << static_cast<char>(character);
        }
    }
  }
  return stream.str();
}

std::string sanitize_component(const std::string & value)
{
  std::string result;
  result.reserve(value.size());
  for (const unsigned char character : value) {
    if (std::isalnum(character) || character == '-' || character == '_') {
      result.push_back(static_cast<char>(character));
    } else {
      result.push_back('_');
    }
  }
  if (result.empty()) {
    return "unnamed";
  }
  return result.substr(0, 96);
}

std::string image_extension(const std::string & format)
{
  std::string lower = format;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](const unsigned char character) {
      return static_cast<char>(std::tolower(character));
    });
  if (lower.find("jpeg") != std::string::npos || lower.find("jpg") != std::string::npos) {
    return ".jpg";
  }
  if (lower.find("png") != std::string::npos) {
    return ".png";
  }
  return ".bin";
}

std::string utc_timestamp()
{
  const auto current = std::chrono::system_clock::now();
  const std::time_t current_time = std::chrono::system_clock::to_time_t(current);
  std::tm utc_time{};
  gmtime_r(&current_time, &utc_time);
  std::ostringstream stream;
  stream << std::put_time(&utc_time, "%Y-%m-%dT%H:%M:%SZ");
  return stream.str();
}

void write_twist(std::ostream & output, const geometry_msgs::msg::Twist & twist)
{
  output << "{\"linear_x\":" << twist.linear.x
         << ",\"linear_y\":" << twist.linear.y
         << ",\"linear_z\":" << twist.linear.z
         << ",\"angular_x\":" << twist.angular.x
         << ",\"angular_y\":" << twist.angular.y
         << ",\"angular_z\":" << twist.angular.z << "}";
}

void write_string_array(std::ostream & output, const std::vector<std::string> & values)
{
  output << '[';
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0) {
      output << ',';
    }
    output << '"' << json_escape(values[index]) << '"';
  }
  output << ']';
}

void write_float_array(std::ostream & output, const std::vector<float> & values)
{
  output << '[';
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0) {
      output << ',';
    }
    output << values[index];
  }
  output << ']';
}

bool has_safety_reason(
  const vehicle_interfaces::msg::TrainingAction & action, const std::string & reason)
{
  return std::find(action.safety_reasons.begin(), action.safety_reasons.end(), reason) !=
         action.safety_reasons.end();
}

void write_training_action(
  std::ostream & output, const vehicle_interfaces::msg::TrainingAction & action)
{
  const bool obstacle_override = has_safety_reason(action, "obstacle_override") ||
    has_safety_reason(action, "manual_obstacle_override");
  const std::string safety_mode = obstacle_override ? "manual_obstacle_override" : "normal";
  output << "{\"schema\":\"" << json_escape(action.action_schema) << "\""
         << ",\"schema_hash\":\"" << json_escape(action.schema_hash) << "\""
         << ",\"feature_names\":";
  write_string_array(output, action.feature_names);
  output << ",\"feature_units\":";
  write_string_array(output, action.feature_units);
  output << ",\"values\":";
  write_float_array(output, action.values);
  output << ",\"source\":\"" << json_escape(action.source) << "\""
         << ",\"safety_mode\":\"" << safety_mode << "\""
         << ",\"obstacle_override\":" << (obstacle_override ? "true" : "false")
         << ",\"safety_intervened\":" << (action.safety_intervened ? "true" : "false")
         << ",\"safety_reasons\":";
  write_string_array(output, action.safety_reasons);
  output << '}';
}

void write_frame(
  std::ofstream & frames,
  const std::filesystem::path & output_path,
  const std::string & episode_id,
  const std::size_t frame_index,
  const ObservationRecord & record,
  const std::optional<vehicle_interfaces::msg::PolicyAction> & action,
  const std::optional<vehicle_interfaces::msg::TrainingAction> & target_action,
  const std::optional<vehicle_interfaces::msg::TrainingAction> & executed_action,
  const std::optional<vehicle_interfaces::msg::ShadowComparison> & comparison,
  ExportStats & stats)
{
  const auto & observation = record.observation;
  const std::string frame_prefix = [&]() {
      std::ostringstream stream;
      stream << std::setw(6) << std::setfill('0') << frame_index;
      return stream.str();
    }();

  frames << std::setprecision(10);
  frames << "{\"schema_version\":\"vehicle.dataset.frame.v1\""
         << ",\"frame_index\":" << frame_index
         << ",\"episode_id\":\"" << json_escape(episode_id) << "\""
         << ",\"observation_id\":\"" << json_escape(observation.observation_id) << "\""
         << ",\"bag_timestamp_ns\":" << record.bag_timestamp_ns
         << ",\"generated_at_ns\":" << time_to_nanoseconds(observation.generated_at)
         << ",\"task\":\"" << json_escape(observation.task) << "\"";

  frames << ",\"images\":[";
  for (std::size_t index = 0; index < observation.images.size(); ++index) {
    if (index > 0) {
      frames << ',';
    }
    const auto & image = observation.images[index];
    const std::string key = index < observation.image_keys.size() ?
      observation.image_keys[index] : "observation.images.unknown";
    const std::string file_name = frame_prefix + "_" + sanitize_component(key) +
      image_extension(image.format);
    const std::filesystem::path relative_path = std::filesystem::path("images") / file_name;
    std::ofstream image_output(output_path / relative_path, std::ios::binary);
    if (!image_output) {
      throw std::runtime_error("Unable to write image: " + (output_path / relative_path).string());
    }
    image_output.write(
      reinterpret_cast<const char *>(image.data.data()),
      static_cast<std::streamsize>(image.data.size()));
    if (!image_output) {
      throw std::runtime_error("Failed while writing image: " + (output_path / relative_path).string());
    }
    ++stats.image_count;
    frames << "{\"key\":\"" << json_escape(key) << "\""
           << ",\"format\":\"" << json_escape(image.format) << "\""
           << ",\"path\":\"" << json_escape(relative_path.generic_string()) << "\""
           << ",\"bytes\":" << image.data.size() << "}";
  }
  frames << ']';

  const std::size_t state_count = std::min({
      observation.state_keys.size(), observation.state.size(), observation.state_valid.size()});
  frames << ",\"state\":[";
  for (std::size_t index = 0; index < state_count; ++index) {
    if (index > 0) {
      frames << ',';
    }
    frames << "{\"key\":\"" << json_escape(observation.state_keys[index]) << "\""
           << ",\"value\":" << observation.state[index]
           << ",\"valid\":" << (observation.state_valid[index] ? "true" : "false") << "}";
  }
  frames << ']';

  frames << ",\"policy\":{";
  if (action) {
    frames << "\"available\":true"
           << ",\"request_id\":\"" << json_escape(action->request_id) << "\""
           << ",\"model_id\":\"" << json_escape(action->model_id) << "\""
           << ",\"schema_version\":\"" << json_escape(action->schema_version) << "\""
           << ",\"control_period_ns\":"
           << (static_cast<int64_t>(action->control_period.sec) * 1000000000LL +
        action->control_period.nanosec)
           << ",\"actions\":[";
    for (std::size_t index = 0; index < action->actions.size(); ++index) {
      if (index > 0) {
        frames << ',';
      }
      write_twist(frames, action->actions[index]);
    }
    frames << ']';
  } else {
    frames << "\"available\":false";
  }
  frames << '}';

  frames << ",\"target\":{";
  if (executed_action) {
    frames << "\"available\":true,\"source\":\""
           << json_escape(executed_action->source) << "\",\"action\":";
    write_training_action(frames, *executed_action);
    if (target_action) {
      frames << ",\"requested_action\":";
      write_training_action(frames, *target_action);
    }
    if (comparison) {
      frames << ",\"legacy_shadow\":{\"executed_twist\":";
      write_twist(frames, comparison->executed);
      frames << ",\"predicted_twist\":";
      write_twist(frames, comparison->predicted);
      frames << ",\"linear_absolute_error\":" << comparison->linear_absolute_error
             << ",\"angular_absolute_error\":" << comparison->angular_absolute_error
             << ",\"within_threshold\":" << (comparison->within_threshold ? "true" : "false")
             << '}';
    }
  } else if (comparison) {
    frames << "\"available\":true,\"source\":\"shadow.executed_twist\",\"twist\":";
    write_twist(frames, comparison->executed);
    frames << ",\"predicted_twist\":";
    write_twist(frames, comparison->predicted);
  } else {
    frames << "\"available\":false";
  }
  frames << "}}\n";
}

void write_manifest(
  const Arguments & arguments,
  const std::string & episode_id,
  const ExportStats & stats)
{
  std::ofstream manifest(arguments.output_path / "dataset_manifest.json");
  if (!manifest) {
    throw std::runtime_error("Unable to write dataset manifest");
  }
  manifest << "{\n"
           << "  \"schema_version\": \"vehicle.dataset.v1\",\n"
           << "  \"created_at\": \"" << utc_timestamp() << "\",\n"
           << "  \"source_episode_id\": \"" << json_escape(episode_id) << "\",\n"
           << "  \"source_episode_path\": \""
           << json_escape(arguments.episode_path.string()) << "\",\n"
           << "  \"frame_file\": \"frames.jsonl\",\n"
           << "  \"image_root\": \"images\",\n"
           << "  \"target_source\": \"chitu.action.executed with shadow fallback\",\n"
           << "  \"include_incomplete\": "
           << (arguments.include_incomplete ? "true" : "false") << ",\n"
           << "  \"observation_count\": " << stats.observation_count << ",\n"
           << "  \"frame_count\": " << stats.frame_count << ",\n"
           << "  \"incomplete_count\": " << stats.incomplete_count << ",\n"
           << "  \"image_count\": " << stats.image_count << "\n"
           << "}\n";
}

}  // namespace

int main(int argc, char ** argv)
{
  try {
    const Arguments arguments = parse_arguments(argc, argv);
    std::map<std::string, ObservationRecord> observations;
    std::map<std::string, vehicle_interfaces::msg::PolicyAction> actions;
    std::map<std::string, vehicle_interfaces::msg::TrainingAction> target_actions;
    std::map<std::string, vehicle_interfaces::msg::TrainingAction> executed_actions;
    std::map<std::string, vehicle_interfaces::msg::ShadowComparison> comparisons;
    std::vector<std::string> observation_order;

    rosbag2_cpp::Reader reader;
    reader.open(arguments.episode_path.string());
    while (reader.has_next()) {
      const auto bag_message = reader.read_next();
      if (bag_message->topic_name == "/vla/observation") {
        auto observation = deserialize_message<vehicle_interfaces::msg::PolicyObservation>(bag_message);
        if (observation.observation_id.empty()) {
          continue;
        }
        const std::string observation_id = observation.observation_id;
        if (observations.find(observation_id) == observations.end()) {
          observation_order.push_back(observation_id);
        }
        observations[observation_id] = {
          std::move(observation), bag_message->time_stamp};
      } else if (bag_message->topic_name == "/vla/policy_action") {
        auto action = deserialize_message<vehicle_interfaces::msg::PolicyAction>(bag_message);
        if (!action.observation_id.empty()) {
          const std::string observation_id = action.observation_id;
          actions[observation_id] = std::move(action);
        }
      } else if (bag_message->topic_name == "/chitu/action/target") {
        auto action = deserialize_message<vehicle_interfaces::msg::TrainingAction>(bag_message);
        if (!action.observation_id.empty()) {
          target_actions[action.observation_id] = std::move(action);
        }
      } else if (bag_message->topic_name == "/chitu/action/executed") {
        auto action = deserialize_message<vehicle_interfaces::msg::TrainingAction>(bag_message);
        if (!action.observation_id.empty()) {
          executed_actions[action.observation_id] = std::move(action);
        }
      } else if (bag_message->topic_name == "/vla/shadow_comparison") {
        auto comparison = deserialize_message<vehicle_interfaces::msg::ShadowComparison>(bag_message);
        if (!comparison.observation_id.empty()) {
          const std::string observation_id = comparison.observation_id;
          comparisons[observation_id] = std::move(comparison);
        }
      }
    }

    const std::string episode_id = arguments.episode_path.filename().string();
    const std::filesystem::path temporary_path =
      arguments.output_path.string() + ".partial";
    if (std::filesystem::exists(temporary_path)) {
      throw std::runtime_error("Temporary output path already exists: " + temporary_path.string());
    }
    std::filesystem::create_directories(temporary_path / "images");

    ExportStats stats;
    stats.observation_count = observation_order.size();
    std::ofstream frames(temporary_path / "frames.jsonl");
    if (!frames) {
      throw std::runtime_error("Unable to write frames.jsonl");
    }

    try {
      for (const auto & observation_id : observation_order) {
        const auto action_iterator = actions.find(observation_id);
        const auto target_iterator = target_actions.find(observation_id);
        const auto executed_iterator = executed_actions.find(observation_id);
        const auto comparison_iterator = comparisons.find(observation_id);
        const bool complete = executed_iterator != executed_actions.end() ||
          comparison_iterator != comparisons.end();
        if (!complete) {
          ++stats.incomplete_count;
          if (!arguments.include_incomplete) {
            continue;
          }
        }
        const std::optional<vehicle_interfaces::msg::PolicyAction> action =
          action_iterator == actions.end() ? std::nullopt :
          std::optional<vehicle_interfaces::msg::PolicyAction>(action_iterator->second);
        const std::optional<vehicle_interfaces::msg::TrainingAction> target_action =
          target_iterator == target_actions.end() ? std::nullopt :
          std::optional<vehicle_interfaces::msg::TrainingAction>(target_iterator->second);
        const std::optional<vehicle_interfaces::msg::TrainingAction> executed_action =
          executed_iterator == executed_actions.end() ? std::nullopt :
          std::optional<vehicle_interfaces::msg::TrainingAction>(executed_iterator->second);
        const std::optional<vehicle_interfaces::msg::ShadowComparison> comparison =
          comparison_iterator == comparisons.end() ? std::nullopt :
          std::optional<vehicle_interfaces::msg::ShadowComparison>(comparison_iterator->second);
        write_frame(
          frames, temporary_path, episode_id, stats.frame_count,
          observations.at(observation_id), action, target_action, executed_action, comparison, stats);
        ++stats.frame_count;
      }
      frames.close();
      Arguments temporary_arguments = arguments;
      temporary_arguments.output_path = temporary_path;
      write_manifest(temporary_arguments, episode_id, stats);
      std::filesystem::rename(temporary_path, arguments.output_path);
    } catch (...) {
      frames.close();
      std::filesystem::remove_all(temporary_path);
      throw;
    }

    std::cout << "episode_id=" << episode_id << '\n'
              << "observation_count=" << stats.observation_count << '\n'
              << "frame_count=" << stats.frame_count << '\n'
              << "incomplete_count=" << stats.incomplete_count << '\n'
              << "image_count=" << stats.image_count << '\n'
              << "output=" << arguments.output_path << '\n'
              << "DATASET_EXPORT_PASS\n";
    return 0;
  } catch (const std::exception & error) {
    std::cerr << "Dataset export failed: " << error.what() << '\n';
    return 1;
  }
}
