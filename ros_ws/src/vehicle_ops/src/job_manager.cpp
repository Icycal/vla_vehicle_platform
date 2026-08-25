#include "job_manager.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace vehicle_ops
{
namespace
{
using Json = nlohmann::json;

std::string utc_timestamp()
{
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  gmtime_r(&time, &tm);
  std::ostringstream output;
  output << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

std::string make_job_id()
{
  static std::atomic<unsigned long> sequence{0};
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
  return "job-" + std::to_string(milliseconds) + "-" + std::to_string(sequence.fetch_add(1));
}

bool is_within(const std::filesystem::path & child, const std::filesystem::path & parent)
{
  const auto normalized_child = child.lexically_normal();
  const auto normalized_parent = parent.lexically_normal();
  auto child_iterator = normalized_child.begin();
  for (auto parent_iterator = normalized_parent.begin();
    parent_iterator != normalized_parent.end(); ++parent_iterator, ++child_iterator)
  {
    if (child_iterator == normalized_child.end() || *child_iterator != *parent_iterator) {
      return false;
    }
  }
  return true;
}

std::string read_tail(const std::filesystem::path & path, std::size_t limit)
{
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {return {};}
  stream.seekg(0, std::ios::end);
  const auto position = stream.tellg();
  const auto size = position > 0 ? static_cast<std::size_t>(position) : 0;
  stream.seekg(static_cast<std::streamoff>(size > limit ? size - limit : 0), std::ios::beg);
  std::ostringstream output;
  output << stream.rdbuf();
  return output.str();
}

std::string required_string(const Json & object, const char * key)
{
  if (!object.contains(key) || !object.at(key).is_string()) {
    throw std::invalid_argument(std::string("Parameter '") + key + "' is required");
  }
  const auto value = object.at(key).get<std::string>();
  if (value.empty()) {throw std::invalid_argument(std::string("Parameter '") + key + "' is required");}
  return value;
}
}  // namespace

struct JobManager::Job
{
  std::string id;
  std::string type;
  std::string state{"queued"};
  Json parameters{Json::object()};
  std::vector<std::string> command;
  std::filesystem::path directory;
  std::filesystem::path log_path;
  std::string requested_at;
  std::string started_at;
  std::string ended_at;
  std::string cancellation_reason;
  pid_t pid{-1};
  int exit_code{-1};
  bool cancel_requested{false};
};

JobManager::JobManager(std::filesystem::path project_root, std::filesystem::path jobs_root)
: project_root_(std::filesystem::weakly_canonical(std::move(project_root))),
  jobs_root_(std::filesystem::weakly_canonical(std::move(jobs_root)))
{
  if (!std::filesystem::is_directory(project_root_)) {
    throw std::invalid_argument("Job project_root is not a directory");
  }
  std::filesystem::create_directories(jobs_root_);
  load_history();
}

JobManager::~JobManager()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_job_ && active_job_->pid > 0) {
      active_job_->cancel_requested = true;
      active_job_->cancellation_reason = "Vehicle Ops shutdown";
      ::kill(-active_job_->pid, SIGTERM);
    }
  }
  if (worker_.joinable()) {worker_.join();}
}

std::filesystem::path JobManager::resolve_input(const std::string & value) const
{
  const std::filesystem::path requested(value);
  if (requested.empty() || requested.is_absolute()) {
    throw std::invalid_argument("Input path must be project-relative");
  }
  const auto resolved = std::filesystem::weakly_canonical(project_root_ / requested);
  const std::vector<std::filesystem::path> roots{
    std::filesystem::weakly_canonical(project_root_ / "datasets"),
    std::filesystem::weakly_canonical(project_root_ / "run/test"),
    std::filesystem::weakly_canonical(project_root_ / "run/ops/exports")};
  if (!std::filesystem::exists(resolved) ||
    std::none_of(roots.begin(), roots.end(), [&](const auto & root) {return is_within(resolved, root);}))
  {
    throw std::invalid_argument("Input path must exist under datasets/ or run/test/");
  }
  return resolved;
}

std::filesystem::path JobManager::resolve_output(const std::string & value) const
{
  const std::filesystem::path requested(value);
  if (requested.empty() || requested.is_absolute()) {
    throw std::invalid_argument("Output path must be project-relative");
  }
  const auto resolved = std::filesystem::weakly_canonical(project_root_ / requested);
  const std::vector<std::filesystem::path> roots{
    std::filesystem::weakly_canonical(project_root_ / "datasets"),
    std::filesystem::weakly_canonical(project_root_ / "run/test"),
    std::filesystem::weakly_canonical(project_root_ / "run/ops/exports")};
  if (std::none_of(roots.begin(), roots.end(), [&](const auto & root) {return is_within(resolved, root);})) {
    throw std::invalid_argument("Output path must stay under datasets/, run/test/ or run/ops/exports/");
  }
  return resolved;
}

std::string JobManager::create(const std::string & request_body)
{
  Json request;
  try {request = Json::parse(request_body);}
  catch (const Json::exception &) {throw std::invalid_argument("Request body must be valid JSON");}
  const auto type = required_string(request, "job_type");
  const auto parameters = request.value("parameters", Json::object());
  if (!parameters.is_object()) {throw std::invalid_argument("parameters must be an object");}

  auto job = std::make_shared<Job>();
  job->id = make_job_id();
  job->type = type;
  job->parameters = parameters;
  job->requested_at = utc_timestamp();
  job->directory = jobs_root_ / job->id;
  job->log_path = job->directory / "job.log";

  const auto scripts = project_root_ / "scripts";
  if (type == "dataset.export_episode") {
    job->command = {(scripts / "export_episode.sh").string(),
      resolve_input(required_string(parameters, "episode")).string(),
      resolve_output(required_string(parameters, "output")).string()};
  } else if (type == "dataset.inspect") {
    job->command = {(scripts / "inspect_dataset.sh").string(),
      resolve_input(required_string(parameters, "dataset")).string(),
      resolve_output(required_string(parameters, "output")).string()};
  } else if (type == "dataset.split") {
    job->command = {(scripts / "split_vehicle_dataset.sh").string(),
      resolve_output(required_string(parameters, "output")).string(),
      resolve_input(required_string(parameters, "dataset")).string()};
  } else if (type == "dataset.archive_lerobot") {
    const auto datasets = parameters.value("datasets", Json::array());
    if (!datasets.is_array() || datasets.empty()) {throw std::invalid_argument("Parameter 'datasets' must contain at least one dataset");}
    job->command = {(scripts / "archive_dataset.sh").string(), resolve_output(required_string(parameters, "output")).string()};
    for (const auto & dataset : datasets) {
      if (!dataset.is_string()) {throw std::invalid_argument("Each dataset path must be a string");}
      job->command.push_back(resolve_input(dataset.get<std::string>()).string());
    }
  } else if (type == "dataset.convert_lerobot") {
    const auto datasets = parameters.value("datasets", Json::array());
    if (datasets.is_array() && !datasets.empty()) {
      job->command = {(scripts / "convert_lerobot_dataset.sh").string(), resolve_output(required_string(parameters, "output")).string()};
      for (const auto & dataset : datasets) {
        if (!dataset.is_string()) {throw std::invalid_argument("Each dataset path must be a string");}
        job->command.push_back(resolve_input(dataset.get<std::string>()).string());
      }
    } else {
      job->command = {(scripts / "convert_lerobot_dataset.sh").string(),
        resolve_output(required_string(parameters, "output")).string(),
        resolve_input(required_string(parameters, "dataset")).string()};
    }
  } else if (type == "policy.health_check") {
    if (!parameters.empty()) {throw std::invalid_argument("policy.health_check accepts no parameters");}
    job->command = {(scripts / "verify_smolvla_model_offline.sh").string()};
  } else if (type == "policy.runtime_test") {
    if (!parameters.empty()) {throw std::invalid_argument("policy.runtime_test accepts no parameters");}
    job->command = {(scripts / "test_smolvla_runtime.sh").string()};
  } else if (type == "policy.runtime_switch") {
    const auto provider = required_string(parameters, "provider");
    if (provider != "mock" && provider != "smolvla") {
      throw std::invalid_argument("Parameter 'provider' must be mock or smolvla");
    }
    if (parameters.size() != 1) {
      throw std::invalid_argument("policy.runtime_switch only accepts the provider parameter");
    }
    job->command = {(scripts / "switch_policy_runtime.sh").string(), provider};
  } else {
    throw std::invalid_argument("Unsupported job_type");
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_job_) {throw std::runtime_error("Another job is already running");}
    if (worker_.joinable()) {worker_.join();}
    std::filesystem::create_directories(job->directory);
    jobs_.insert(jobs_.begin(), job);
    if (jobs_.size() > 50) {jobs_.resize(50);}
    active_job_ = job;
    persist(*job);
    worker_ = std::thread([this, job]() {run(job);});
  }
  return get(job->id).value();
}

void JobManager::run(const std::shared_ptr<Job> & job)
{
  const auto pid = ::fork();
  if (pid == 0) {
    ::setpgid(0, 0);
    const int log_file = ::open(job->log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0640);
    if (log_file < 0) {_exit(126);}
    ::dup2(log_file, STDOUT_FILENO);
    ::dup2(log_file, STDERR_FILENO);
    ::close(log_file);
    if (::chdir(project_root_.c_str()) != 0) {_exit(126);}
    std::string bash{"/usr/bin/bash"};
    std::vector<char *> arguments{bash.data()};
    for (auto & value : job->command) {arguments.push_back(value.data());}
    arguments.push_back(nullptr);
    ::execv(bash.c_str(), arguments.data());
    _exit(127);
  }

  if (pid < 0) {
    std::lock_guard<std::mutex> lock(mutex_);
    job->state = "failed";
    job->ended_at = utc_timestamp();
    job->exit_code = 127;
    active_job_.reset();
    persist(*job);
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    job->pid = pid;
    job->state = "running";
    job->started_at = utc_timestamp();
    if (job->cancel_requested) {::kill(-job->pid, SIGTERM);}
    persist(*job);
  }

  int status = 0;
  pid_t waited = -1;
  do {waited = ::waitpid(pid, &status, 0);} while (waited < 0 && errno == EINTR);
  std::lock_guard<std::mutex> lock(mutex_);
  job->ended_at = utc_timestamp();
  if (job->cancel_requested) {job->state = "cancelled";}
  else if (waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0) {job->state = "succeeded";}
  else {job->state = "failed";}
  job->exit_code = waited != pid ? 127 :
    (WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
  job->pid = -1;
  active_job_.reset();
  persist(*job);
}

void JobManager::persist(const Job & job) const
{
  const Json output{
    {"schema_version", "vehicle.ops.job.v1"}, {"id", job.id}, {"job_type", job.type},
    {"state", job.state}, {"parameters", job.parameters}, {"requested_at", job.requested_at},
    {"started_at", job.started_at}, {"ended_at", job.ended_at}, {"pid", job.pid},
    {"exit_code", job.exit_code}, {"cancellation_reason", job.cancellation_reason},
    {"log_path", std::filesystem::relative(job.log_path, project_root_).string()}};
  std::ofstream(job.directory / "status.json") << std::setw(2) << output << '\n';
  std::ofstream(job.directory / "request.json") << std::setw(2) << Json{
    {"job_type", job.type}, {"parameters", job.parameters}} << '\n';
}

std::shared_ptr<JobManager::Job> JobManager::find_locked(const std::string & job_id) const
{
  const auto found = std::find_if(jobs_.begin(), jobs_.end(), [&](const auto & job) {return job->id == job_id;});
  return found == jobs_.end() ? nullptr : *found;
}

std::string JobManager::list() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  Json output{{"schema_version", "vehicle.ops.jobs.v1"}, {"jobs", Json::array()}};
  for (const auto & job : jobs_) {
    output["jobs"].push_back(Json::parse(read_tail(job->directory / "status.json", 256 * 1024)));
  }
  return output.dump() + "\n";
}

std::optional<std::string> JobManager::get(const std::string & job_id) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  const auto job = find_locked(job_id);
  if (!job) {return std::nullopt;}
  return read_tail(job->directory / "status.json", 256 * 1024);
}

std::optional<std::string> JobManager::log(const std::string & job_id) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  const auto job = find_locked(job_id);
  if (!job) {return std::nullopt;}
  return read_tail(job->log_path, 128 * 1024);
}

std::optional<std::string> JobManager::cancel(const std::string & job_id)
{
  std::lock_guard<std::mutex> lock(mutex_);
  const auto job = find_locked(job_id);
  if (!job) {return std::nullopt;}
  if (job->state != "running" && job->state != "queued") {
    throw std::runtime_error("Job is no longer running");
  }
  if (job->type == "policy.runtime_switch") {
    throw std::runtime_error("Policy Runtime switch jobs cannot be cancelled");
  }
  job->cancel_requested = true;
  job->cancellation_reason = "Cancelled by Vehicle Ops operator";
  if (job->pid > 0) {::kill(-job->pid, SIGTERM);}
  persist(*job);
  return read_tail(job->directory / "status.json", 256 * 1024);
}

void JobManager::load_history()
{
  std::vector<std::filesystem::path> directories;
  for (const auto & entry : std::filesystem::directory_iterator(jobs_root_)) {
    if (entry.is_directory()) {directories.push_back(entry.path());}
  }
  std::sort(directories.rbegin(), directories.rend());
  for (const auto & directory : directories) {
    try {
      const auto stored = Json::parse(read_tail(directory / "status.json", 256 * 1024));
      auto job = std::make_shared<Job>();
      job->id = stored.at("id").get<std::string>();
      job->type = stored.at("job_type").get<std::string>();
      job->state = stored.at("state").get<std::string>();
      job->parameters = stored.value("parameters", Json::object());
      job->requested_at = stored.value("requested_at", "");
      job->started_at = stored.value("started_at", "");
      job->ended_at = stored.value("ended_at", "");
      job->exit_code = stored.value("exit_code", -1);
      job->cancellation_reason = stored.value("cancellation_reason", "");
      job->directory = directory;
      job->log_path = directory / "job.log";
      if (job->state == "queued" || job->state == "running") {
        job->state = "failed";
        job->ended_at = utc_timestamp();
        job->cancellation_reason = "Vehicle Ops restarted before job completion";
        persist(*job);
      }
      jobs_.push_back(job);
      if (jobs_.size() == 50) {break;}
    } catch (const std::exception &) {continue;}
  }
}

}  // namespace vehicle_ops
