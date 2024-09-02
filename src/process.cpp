#include <array>
#include <cstdio>

#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "macros/unwrap.hpp"
#include "process.hpp"

namespace process {
namespace {
struct AutoPipe {
    FileDescriptor output;
    FileDescriptor input;

    static auto create() -> std::optional<AutoPipe> {
        auto fd = std::array<int, 2>();
        ensure(pipe2(fd.data(), O_NONBLOCK | O_CLOEXEC) >= 0);
        return AutoPipe{
            fd[0],
            fd[1],
        };
    }
};
} // namespace

auto Process::start(const StartParams& params) -> bool {
    ensure(status == Status::Init);
    ensure(!params.argv.empty());
    ensure(params.argv.back() == NULL);
    ensure(params.env.empty() || params.env.back() == NULL);
    status = Status::Running;

    unwrap_mut(stdin_pipe, AutoPipe::create());
    unwrap_mut(stdout_pipe, AutoPipe::create());
    unwrap_mut(stderr_pipe, AutoPipe::create());

    const auto ppid = getpid();

    pid = fork();
    ensure(pid >= 0);
    if(pid != 0) {
        stdin_fd  = std::move(stdin_pipe.input);
        stdout_fd = std::move(stdout_pipe.output);
        stderr_fd = std::move(stderr_pipe.output);
        return true;
    }

    if(params.die_on_parent_exit) {
        if(prctl(PR_SET_PDEATHSIG, SIGTERM) == -1) {
            warn("prctl() failed: ", strerror(errno));
            _exit(1);
        }
        if(getppid() != ppid) {
            print("parent process exitted");
            _exit(0);
        }
    }

    dup2(stdin_pipe.output.as_handle(), 0);
    dup2(stdout_pipe.input.as_handle(), 1);
    dup2(stderr_pipe.input.as_handle(), 2);

    if(params.workdir != nullptr && chdir(params.workdir) == -1) {
        warn("chdir() to ", params.workdir, "failed: ", strerror(errno));
        _exit(1);
    }
    execve(params.argv[0], const_cast<char* const*>(params.argv.data()), params.env.empty() ? environ : const_cast<char* const*>(params.env.data()));
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
    return stdin_fd;
}

auto Process::get_status() const -> Status {
    return status;
}

auto Process::collect_outputs() -> bool {
    auto fds = std::array{
        pollfd{.fd = stdout_fd.as_handle(), .events = POLLIN, .revents = 0},
        pollfd{.fd = stderr_fd.as_handle(), .events = POLLIN, .revents = 0},
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
