#include <array>
#include <cstdio>

#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "macros/assert.hpp"
#include "process.hpp"

namespace process {
auto Process::start(const std::span<const char* const> argv, const std::span<const char* const> env, const char* const workdir) -> bool {
    ensure(status == Status::Init);
    ensure(!argv.empty());
    ensure(argv.back() == NULL);
    ensure(env.empty() || env.back() == NULL);
    status = Status::Running;

    for(auto i = 0; i < 3; i += 1) {
        auto fd = std::array<int, 2>();
        ensure(pipe(fd.data()) >= 0);
        pipes[i].output = FileDescriptor(fd[0]);
        pipes[i].input  = FileDescriptor(fd[1]);
    }

    pid = fork();
    ensure(pid >= 0);
    if(pid != 0) {
        for(auto i = 0; i < 3; i += 1) {
            auto& use   = (i == 0 ? pipes[i].input : pipes[i].output);
            auto& nouse = (i == 0 ? pipes[i].output : pipes[i].input);
            if(fcntl(use.as_handle(), F_SETFL, O_NONBLOCK) == -1) {
                return false;
            }
            nouse.close();
        }
        return true;
    }

    for(auto i = 0; i < 3; i += 1) {
        auto& use = (i == 0 ? pipes[i].output : pipes[i].input);
        dup2(use.as_handle(), i);
        pipes[i].input.close();
        pipes[i].output.close();
    }
    ensure(workdir == nullptr || chdir(workdir) != -1);
    execve(argv[0], const_cast<char* const*>(argv.data()), env.empty() ? environ : const_cast<char* const*>(env.data()));
    warn("exec() failed: ", strerror(errno));
    _exit(1);
}

auto Process::join(const bool force) -> std::optional<Result> {
    ensure(status == Status::Running || status == Status::Finished);
    status = Status::Joined;

    ensure(!force || kill(pid, SIGKILL) != -1, "failed to kill process");

    auto status = int();
    ensure(waitpid(pid, &status, 0) != -1);

    const auto exitted = bool(WIFEXITED(status));
    return Result{
        .reason = exitted ? Result::ExitReason::Exit : Result::ExitReason::Signal,
        .code   = exitted ? WEXITSTATUS(status) : WTERMSIG(status),
    };
}

auto Process::get_pid() const -> pid_t {
    return pid;
}

auto Process::get_stdin() -> FileDescriptor& {
    return pipes[0].input;
}

auto Process::get_status() const -> Status {
    return status;
}

auto Process::collect_outputs() -> bool {
    auto fds = std::array{
        pollfd{.fd = pipes[1].output.as_handle(), .events = POLLIN},
        pollfd{.fd = pipes[2].output.as_handle(), .events = POLLIN},
    };

    ensure(poll(fds.data(), fds.size(), -1) != -1);
    for(auto i = 0; i < 2; i += 1) {
        if(fds[i].revents & POLLHUP) {
            // target has exitted
            status = Status::Finished;
        }
        if(fds[i].revents & POLLIN) {
            auto buf = std::array<char, 256>();
            while(true) {
                const auto len = read(fds[i].fd, buf.data(), buf.size());
                if((len < 0 && errno == EAGAIN) || len == 0) {
                    break;
                }
                ensure(len > 0);
                auto callback = i == 0 ? on_stdout : on_stderr;
                if(callback) {
                    callback({buf.data(), size_t(len)});
                }
            }
        }
    }

    return true;
}
} // namespace process
