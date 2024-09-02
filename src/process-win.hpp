#pragma once
#include <functional>
#include <optional>
#include <span>

#include <windows.h>

#include "common.hpp"

namespace process {
class Process {
  private:
    struct PipePair {
        HANDLE output;
        HANDLE input;
    };

    HANDLE   process_handle = nullptr;
    HANDLE   thread_handle  = nullptr;
    Status   status         = Status::Init;
    PipePair pipes[3];

  public:
    std::function<OnOutput> on_stdout;
    std::function<OnOutput> on_stderr;

    // argv.back() and env.back() must be NULL
    auto start(std::span<const char* const> argv, std::span<const char* const> env = {}, const char* workdir = nullptr) -> bool;
    auto join(bool force = false) -> std::optional<Result>;
    auto get_pid() const -> DWORD;
    auto get_stdin() -> HANDLE;
    auto get_status() const -> Status;
    auto collect_outputs() -> bool;
};
} // namespace process
