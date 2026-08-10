#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace vehicle_ops
{

class JobManager
{
public:
  JobManager(std::filesystem::path project_root, std::filesystem::path jobs_root);
  ~JobManager();

  std::string create(const std::string & request_body);
  std::string list() const;
  std::optional<std::string> get(const std::string & job_id) const;
  std::optional<std::string> log(const std::string & job_id) const;
  std::optional<std::string> cancel(const std::string & job_id);

private:
  struct Job;
  std::filesystem::path resolve_input(const std::string & value) const;
  std::filesystem::path resolve_output(const std::string & value) const;
  std::shared_ptr<Job> find_locked(const std::string & job_id) const;
  void run(const std::shared_ptr<Job> & job);
  void persist(const Job & job) const;
  void load_history();

  std::filesystem::path project_root_;
  std::filesystem::path jobs_root_;
  mutable std::mutex mutex_;
  std::shared_ptr<Job> active_job_;
  std::vector<std::shared_ptr<Job>> jobs_;
  std::thread worker_;
};

}  // namespace vehicle_ops
