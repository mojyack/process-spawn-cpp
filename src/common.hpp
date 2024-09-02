#pragma once
#include <span>

namespace process {
using OnOutput = void(std::span<char> output);

enum class Status : int {
    Init,
    Running,
    Finished,
    Joined,
};

struct StartParams {
    std::span<const char* const> argv;         // must be null terminated
    std::span<const char* const> env     = {}; // must be null terminated
    const char*                  workdir = nullptr;
};

struct Result {
    enum class ExitReason : int {
        Exit,
        Signal,
    };

    ExitReason reason;
    int        code;
};
} // namespace process
