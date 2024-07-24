#pragma once
#include <span>
#include <string>
#include <thread>

#include "util/fd.hpp"

namespace process {
enum class Status : int {
    Init,
    Running,
    Joined,
};

struct Result {
    enum class ExitReason : int {
        Exit,
        Signal,
    };

    ExitReason  reason;
    int         code;
    std::string out;
    std::string err;
};

class Process {
  private:
    struct FDPair {
        FileDescriptor output;
        FileDescriptor input;
    };

    pid_t               pid    = 0;
    Status              status = Status::Init;
    FDPair              pipes[3];
    std::string         outputs[2];
    std::thread         output_collector;
    EventFileDescriptor output_collector_event;

    auto output_collector_main() -> void;
    auto collect_outputs() -> void;

  public:
    // argv.back() and env.back() must be NULL
    auto start(std::span<const char* const> argv, std::span<const char* const> env = {}, const char* workdir = nullptr) -> bool;
    auto join(bool force = false) -> std::optional<Result>;
    auto get_pid() const -> pid_t;
    auto get_stdin() -> FileDescriptor&;
    auto get_status() const -> Status;
};
} // namespace process
