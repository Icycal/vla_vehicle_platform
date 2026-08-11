#include <rclcpp/rclcpp.hpp>
#include <vehicle_interfaces/msg/episode_state.hpp>
#include <vehicle_interfaces/msg/storage_category.hpp>
#include <vehicle_interfaces/msg/storage_item.hpp>
#include <vehicle_interfaces/srv/cleanup_storage.hpp>
#include <vehicle_interfaces/srv/get_storage_status.hpp>
#include <vehicle_interfaces/srv/list_storage_items.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace
{
struct CategoryDefinition
{
  std::string id;
  std::string display_name;
  fs::path root;
  bool cleanup_allowed{false};
};

std::uint64_t path_size(const fs::path & path)
{
  std::error_code error;
  const auto status = fs::symlink_status(path, error);
  if (error || fs::is_symlink(status)) {return 0;}
  if (fs::is_regular_file(status)) {return fs::file_size(path, error);}
  if (!fs::is_directory(status)) {return 0;}
  std::uint64_t bytes = 0;
  fs::recursive_directory_iterator iterator(path, fs::directory_options::skip_permission_denied, error);
  const fs::recursive_directory_iterator end;
  while (!error && iterator != end) {
    const auto entry_status = iterator->symlink_status(error);
    if (error) {break;}
    if (fs::is_symlink(entry_status)) {
      iterator.disable_recursion_pending();
    } else if (fs::is_regular_file(entry_status)) {
      bytes += iterator->file_size(error);
      if (error) {error.clear();}
    }
    iterator.increment(error);
    if (error) {error.clear();}
  }
  return bytes;
}

builtin_interfaces::msg::Time modified_time(const fs::path & path)
{
  builtin_interfaces::msg::Time result;
  std::error_code error;
  const auto file_time = fs::last_write_time(path, error);
  if (error) {return result;}
  const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
    file_time - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
  const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
    system_time.time_since_epoch()).count();
  result.sec = static_cast<std::int32_t>(nanoseconds / 1000000000LL);
  result.nanosec = static_cast<std::uint32_t>(nanoseconds % 1000000000LL);
  return result;
}

bool valid_item_id(const std::string & value)
{
  if (value.empty() || value == "." || value == "..") {return false;}
  const fs::path path(value);
  return !path.has_parent_path() && path.filename().string() == value;
}
}

class StorageManager final : public rclcpp::Node
{
public:
  StorageManager()
  : Node("storage_manager")
  {
    project_root_ = fs::path(declare_parameter<std::string>(
      "project_root", "/home/wheeltec/vla_vehicle_platform"));
    warning_used_percent_ = declare_parameter<double>("warning_used_percent", 80.0);
    critical_used_percent_ = declare_parameter<double>("critical_used_percent", 90.0);
    const auto category_ids = declare_parameter<std::vector<std::string>>(
      "category_ids", std::vector<std::string>{});
    for (const auto & id : category_ids) {
      CategoryDefinition definition;
      definition.id = id;
      definition.display_name = declare_parameter<std::string>(
        "categories." + id + ".display_name", id);
      const auto relative = declare_parameter<std::string>("categories." + id + ".path", "");
      definition.root = project_root_ / relative;
      definition.cleanup_allowed = declare_parameter<bool>(
        "categories." + id + ".cleanup_allowed", false);
      categories_.emplace(id, std::move(definition));
      category_order_.push_back(id);
    }

    episode_subscription_ = create_subscription<vehicle_interfaces::msg::EpisodeState>(
      "/vehicle/episode_state", rclcpp::QoS(1).reliable().transient_local(),
      [this](vehicle_interfaces::msg::EpisodeState::SharedPtr message) {
        std::lock_guard<std::mutex> lock(mutex_);
        active_episode_id_ =
          message->state == vehicle_interfaces::msg::EpisodeState::STATE_RECORDING ||
          message->state == vehicle_interfaces::msg::EpisodeState::STATE_FINALIZING ?
          message->episode_id : "";
      });

    status_service_ = create_service<vehicle_interfaces::srv::GetStorageStatus>(
      "/vehicle/storage/get_status",
      [this](const std::shared_ptr<vehicle_interfaces::srv::GetStorageStatus::Request>,
      std::shared_ptr<vehicle_interfaces::srv::GetStorageStatus::Response> response) {
        on_status(*response);
      });
    list_service_ = create_service<vehicle_interfaces::srv::ListStorageItems>(
      "/vehicle/storage/list_items",
      [this](const std::shared_ptr<vehicle_interfaces::srv::ListStorageItems::Request> request,
      std::shared_ptr<vehicle_interfaces::srv::ListStorageItems::Response> response) {
        on_list(request->category_id, *response);
      });
    cleanup_service_ = create_service<vehicle_interfaces::srv::CleanupStorage>(
      "/vehicle/storage/cleanup",
      [this](const std::shared_ptr<vehicle_interfaces::srv::CleanupStorage::Request> request,
      std::shared_ptr<vehicle_interfaces::srv::CleanupStorage::Response> response) {
        on_cleanup(*request, *response);
      });
  }

private:
  bool is_protected_item(const CategoryDefinition & category, const std::string & item_id) const
  {
    if (!category.cleanup_allowed) {return true;}
    if (category.id == "episodes") {
      std::lock_guard<std::mutex> lock(mutex_);
      return !active_episode_id_.empty() && item_id == active_episode_id_;
    }
    if (category.id == "jobs") {
      std::ifstream stream(category.root / item_id / "status.json");
      if (stream) {
        const std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        return content.find("\"state\": \"running\"") != std::string::npos ||
               content.find("\"state\": \"queued\"") != std::string::npos;
      }
    }
    return false;
  }

  vehicle_interfaces::msg::StorageItem make_item(
    const CategoryDefinition & category, const fs::directory_entry & entry) const
  {
    vehicle_interfaces::msg::StorageItem item;
    item.item_id = entry.path().filename().string();
    item.display_name = item.item_id;
    std::error_code error;
    item.path = fs::relative(entry.path(), project_root_, error).string();
    if (error) {item.path = entry.path().string();}
    item.bytes = path_size(entry.path());
    item.modified_at = modified_time(entry.path());
    item.is_protected = is_protected_item(category, item.item_id);
    item.message = item.is_protected ?
      (category.cleanup_allowed ? "正在使用，禁止删除" : "该分类只读") : "可清理";
    return item;
  }

  std::vector<vehicle_interfaces::msg::StorageItem> items_for(
    const CategoryDefinition & category) const
  {
    std::vector<vehicle_interfaces::msg::StorageItem> items;
    std::error_code error;
    if (!fs::is_directory(category.root, error)) {return items;}
    for (fs::directory_iterator iterator(
      category.root, fs::directory_options::skip_permission_denied, error), end;
      !error && iterator != end; iterator.increment(error))
    {
      const auto status = iterator->symlink_status(error);
      if (error) {error.clear(); continue;}
      if (fs::is_symlink(status)) {continue;}
      items.push_back(make_item(category, *iterator));
    }
    std::sort(items.begin(), items.end(), [](const auto & left, const auto & right) {
      if (left.modified_at.sec != right.modified_at.sec) {
        return left.modified_at.sec > right.modified_at.sec;
      }
      return left.modified_at.nanosec > right.modified_at.nanosec;
    });
    return items;
  }

  void on_status(vehicle_interfaces::srv::GetStorageStatus::Response & response) const
  {
    std::error_code error;
    const auto space = fs::space(project_root_, error);
    if (!error) {
      response.total_bytes = space.capacity;
      response.available_bytes = space.available;
      response.used_bytes = space.capacity - space.available;
      response.used_percent = space.capacity > 0 ?
        static_cast<float>(100.0 * static_cast<double>(response.used_bytes) /
        static_cast<double>(space.capacity)) : 0.0F;
    }
    response.level = response.used_percent >= critical_used_percent_ ? "critical" :
      response.used_percent >= warning_used_percent_ ? "warning" : "normal";
    response.message = response.level == "normal" ? "存储空间正常" :
      response.level == "warning" ? "存储空间接近告警阈值" : "存储空间严重不足";
    for (const auto & id : category_order_) {
      const auto & definition = categories_.at(id);
      vehicle_interfaces::msg::StorageCategory category;
      category.category_id = id;
      category.display_name = definition.display_name;
      std::error_code relative_error;
      category.path = fs::relative(definition.root, project_root_, relative_error).string();
      if (relative_error) {category.path = definition.root.string();}
      const auto items = items_for(definition);
      category.item_count = items.size();
      for (const auto & item : items) {category.bytes += item.bytes;}
      category.cleanup_allowed = definition.cleanup_allowed;
      category.automatic_cleanup = false;
      category.message = definition.cleanup_allowed ? "支持选择性清理" : "只读保护";
      response.categories.push_back(std::move(category));
    }
  }

  void on_list(
    const std::string & category_id,
    vehicle_interfaces::srv::ListStorageItems::Response & response) const
  {
    const auto found = categories_.find(category_id);
    if (found == categories_.end()) {
      response.message = "未知存储分类";
      return;
    }
    response.items = items_for(found->second);
    response.accepted = true;
    response.message = "存储明细已加载";
  }

  void on_cleanup(
    const vehicle_interfaces::srv::CleanupStorage::Request & request,
    vehicle_interfaces::srv::CleanupStorage::Response & response)
  {
    const auto found = categories_.find(request.category_id);
    if (found == categories_.end()) {response.message = "未知存储分类"; return;}
    const auto & category = found->second;
    if (!category.cleanup_allowed) {response.message = "该存储分类禁止清理"; return;}
    if (request.item_ids.empty() || request.item_ids.size() > 100) {
      response.message = "请选择 1 到 100 个项目";
      return;
    }

    std::vector<fs::path> targets;
    for (const auto & item_id : request.item_ids) {
      if (!valid_item_id(item_id)) {response.message = "非法项目 ID"; return;}
      const auto target = category.root / item_id;
      std::error_code error;
      const auto status = fs::symlink_status(target, error);
      if (error || !fs::exists(status) || fs::is_symlink(status)) {
        response.message = "项目不存在或不允许清理: " + item_id;
        return;
      }
      if (is_protected_item(category, item_id)) {
        response.message = "项目正在使用或受保护: " + item_id;
        return;
      }
      response.bytes += path_size(target);
      targets.push_back(target);
    }
    response.item_count = targets.size();
    if (request.dry_run) {
      response.accepted = true;
      response.message = "清理预览已生成";
      return;
    }
    for (const auto & target : targets) {
      std::error_code error;
      if (category.id == "component_logs" && fs::is_regular_file(target, error)) {
        error.clear();
        fs::resize_file(target, 0, error);
      } else {
        fs::remove_all(target, error);
      }
      if (error) {response.message = "清理失败: " + target.filename().string(); return;}
    }
    response.accepted = true;
    response.message = "选中项目已清理";
  }

  fs::path project_root_;
  double warning_used_percent_{80.0};
  double critical_used_percent_{90.0};
  std::vector<std::string> category_order_;
  std::unordered_map<std::string, CategoryDefinition> categories_;
  mutable std::mutex mutex_;
  std::string active_episode_id_;
  rclcpp::Subscription<vehicle_interfaces::msg::EpisodeState>::SharedPtr episode_subscription_;
  rclcpp::Service<vehicle_interfaces::srv::GetStorageStatus>::SharedPtr status_service_;
  rclcpp::Service<vehicle_interfaces::srv::ListStorageItems>::SharedPtr list_service_;
  rclcpp::Service<vehicle_interfaces::srv::CleanupStorage>::SharedPtr cleanup_service_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<StorageManager>());
  rclcpp::shutdown();
  return 0;
}
