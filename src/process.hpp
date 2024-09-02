#pragma once
#include <functional>
#include <optional>

#include "common.hpp"

#define CUTIL_NS process
#include "util/fd.hpp"
#undef CUTIL_NS

namespace process {
class Process {
  private:
    pid_t          pid    = 0;
    Status         status = Status::Init;
    FileDescriptor stdin_fd;
    FileDescriptor stdout_fd;
    FileDescriptor stderr_fd;

  public:
    std::function<OnOutput> on_stdout;
    std::function<OnOutput> on_stderr;

    auto start(const StartParams& params) -> bool;
    auto join(bool force = false) -> std::optional<Result>;
    auto get_pid() const -> pid_t;
    auto get_stdin() -> FileDescriptor&;
    auto get_status() const -> Status;
    auto collect_outputs() -> bool;
};
} // namespace process
