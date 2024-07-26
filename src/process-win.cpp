#include <array>
#include <iostream>
#include <string>

#include <wtypesbase.h>

#include "macros/assert.hpp"
#include "process-win.hpp"

namespace process {
namespace {
auto create_pipe(HANDLE& read_pipe, HANDLE& write_pipe) -> bool {
    auto pipe_attributes = SECURITY_ATTRIBUTES{
        .nLength              = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = NULL,
        .bInheritHandle       = TRUE,
    };
    assert_b(CreatePipe(&read_pipe, &write_pipe, &pipe_attributes, 0) != 0);
    assert_b(SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0) != 0);
    return true;
}
} // namespace

auto Process::start(const std::span<const char* const> argv, const std::span<const char* const> env, const char* const workdir) -> bool {
    assert_b(status == Status::Init);
    assert_b(!argv.empty());
    assert_b(argv.back() == NULL);
    assert_b(env.empty() || env.back() == NULL);
    status = Status::Running;

    for(int i = 0; i < 3; i += 1) {
        assert_b(create_pipe(pipes[i].output, pipes[i].input));
    }

    auto command_line = std::string();
    for(auto i = 0u; argv[i] != nullptr; i += 1) {
        command_line += argv[i];
        command_line += " ";
    }

    auto startup_info = STARTUPINFO{
        .cb         = sizeof(STARTUPINFO),
        .dwFlags    = STARTF_USESTDHANDLES,
        .hStdInput  = pipes[0].output,
        .hStdOutput = pipes[1].input,
        .hStdError  = pipes[2].input,
    };

    auto process_info = PROCESS_INFORMATION();

    assert_b(CreateProcess(
                 NULL,
                 (LPSTR)command_line.data(),
                 NULL,
                 NULL,
                 TRUE,
                 0,
                 NULL,
                 (LPCSTR)workdir,
                 &startup_info,
                 &process_info) != 0,
             "CreateProcess failed");

    process_handle = process_info.hProcess;
    thread_handle  = process_info.hThread;

    return true;
}

auto Process::join(const bool force) -> std::optional<Result> {
    assert_o(status == Status::Running || status == Status::Finished);
    status = Status::Joined;
    assert_o(!force || TerminateProcess(process_handle, 1) != 0, "failed to kill process");

    auto exit_code = DWORD();
    assert_o(GetExitCodeProcess(process_handle, &exit_code) != 0, "failed to get the exit code of the child process");

    assert_o(CloseHandle(process_handle) != 0, "failed to close the process handle");
    assert_o(CloseHandle(thread_handle) != 0, "failed to close the thread handle");

    for(auto i = 0; i < 3; i += 1) {
        assert_o(CloseHandle(pipes[i].output), "failed to close the output pipe");
    }

    return Result{
        .reason = exit_code == 0 ? Result::ExitReason::Exit : Result::ExitReason::Signal,
        .code   = int(exit_code),
    };
}

auto Process::get_pid() const -> DWORD {
    auto pid = GetProcessId(process_handle);
    assert_b(pid != 0, "failed to get the process id. GetLastError: ", GetLastError());
    return pid;
}

auto Process::get_stdin() -> HANDLE {
    return pipes[0].input;
}

auto Process::get_status() const -> Status {
    return status;
}

auto Process::collect_outputs() -> bool {
    const auto handles      = std::array{pipes[1].output, pipes[2].output};
    const auto wait_process = WaitForSingleObject(process_handle, 0);
    if(wait_process == WAIT_OBJECT_0) {
        status = Status::Finished;
    }
    for(auto i = 0; i < 2; i += 1) {
        auto buf = std::array<char, 256>();
        while(true) {
            auto bytes_avail = DWORD();
            if(PeekNamedPipe(handles[i], NULL, 0, NULL, &bytes_avail, NULL) != 0 || bytes_avail == 0) {
                break;
            }
            auto len = DWORD();
            if(ReadFile(handles[i], buf.data(), buf.size(), &len, NULL) != 0 || len <= 0) {
                break;
            }
            auto callback = i == 0 ? on_stdout : on_stderr;
            if(callback) {
                callback({buf.data(), size_t(len)});
            }
        }
    }
    return true;
}
} // namespace process
