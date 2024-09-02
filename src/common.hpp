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

struct Result {
    enum class ExitReason : int {
        Exit,
        Signal,
    };

    ExitReason reason;
    int        code;
};
} // namespace process
