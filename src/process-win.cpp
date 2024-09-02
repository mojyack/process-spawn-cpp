#include <array>
#include <string>
#include <thread>
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
    ensure(CreatePipe(&read_pipe, &write_pipe, &pipe_attributes, 0) != 0);
    ensure(SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0) != 0);
    return true;
}
} // namespace

auto Process::start(const StartParams& params) -> bool {
    ensure(status == Status::Init);
    ensure(!params.argv.empty());
    ensure(params.argv.back() == NULL);
    ensure(params.env.empty() || params.env.back() == NULL);
    status = Status::Running;

    for(auto i = 0; i < 3; i += 1) {
        ensure(create_pipe(pipes[i].output, pipes[i].input));
    }

    auto command_line = std::string();
    for(auto i = 0u; params.argv[i] != nullptr; i += 1) {
        command_line += params.argv[i];
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

    ensure(CreateProcessA(
               NULL,
               (LPSTR)command_line.data(),
               NULL,
               NULL,
               TRUE,
               0,
               NULL,
               (LPCSTR)params.workdir,
               &startup_info,
               &process_info) != 0,
           "CreateProcess failed");

    ensure(CloseHandle(pipes[0].output) == TRUE);

    process_handle = process_info.hProcess;
    thread_handle  = process_info.hThread;

    if(params.die_on_parent_exit) {
        auto job_object = CreateJobObjectW(NULL, NULL);
        ensure(job_object, "CreateJobObjectW failed, GetLastError: ", GetLastError());
        auto job_info = JOBOBJECT_EXTENDED_LIMIT_INFORMATION{
            .BasicLimitInformation = {
                .LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE,
            },
        };
        ensure(SetInformationJobObject(job_object, JobObjectExtendedLimitInformation, &job_info, sizeof(job_info)) != 0, "SetInformationJobObject failed, GetLastError: ", GetLastError());
        ensure(AssignProcessToJobObject(job_object, process_handle) != 0, "AssignProcessToJobObject failed, GetLastError: ", GetLastError());
    }

    return true;
}

auto Process::join(const bool force) -> std::optional<Result> {
    ensure(status == Status::Running || status == Status::Finished);
    status = Status::Joined;
    ensure(!force || TerminateProcess(process_handle, 1) != 0, "failed to kill process");

    auto exit_code = DWORD();
    ensure(GetExitCodeProcess(process_handle, &exit_code) != 0, "failed to get the exit code of the child process");

    ensure(CloseHandle(process_handle) != 0, "failed to close the process handle");
    ensure(CloseHandle(thread_handle) != 0, "failed to close the thread handle");

    ensure(CloseHandle(pipes[0].input) == TRUE);
    ensure(CloseHandle(pipes[1].input) == TRUE);
    ensure(CloseHandle(pipes[2].input) == TRUE);
    ensure(CloseHandle(pipes[1].output) == TRUE);
    ensure(CloseHandle(pipes[2].output) == TRUE);

    return Result{
        .reason = exit_code == 0 ? Result::ExitReason::Exit : Result::ExitReason::Signal,
        .code   = int(exit_code),
    };
}

auto Process::get_pid() const -> DWORD {
    return GetProcessId(process_handle);
}

auto Process::get_stdin() -> HANDLE {
    return pipes[0].input;
}

auto Process::get_status() const -> Status {
    return status;
}

auto Process::collect_outputs() -> bool {
    // child threads to collect the (stdout, stderr) outputs
    auto threads = std::array<std::thread, 2>(); // stdout, stderr
    for(auto i = 0u; i < threads.size(); i += 1) {
        threads[i] = std::thread([this, i]() {
            auto buf = std::array<char, 256>();
            auto len = DWORD();
            while(true) {
                if(ReadFile(pipes[i + 1].output, buf.data(), buf.size(), &len, NULL) != TRUE || len <= 0) {
                    break;
                }
                if(status == Status::Finished) {
                    // finished the main process
                    break;
                }
                auto callback = i == 0 ? on_stdout : on_stderr;
                if(callback) {
                    callback({buf.data(), size_t(len)});
                }
            }
        });
    }

    // main process
    const auto wait_process = WaitForSingleObject(process_handle, INFINITE);
    ensure(wait_process == WAIT_OBJECT_0, "wait_process failed. WaitForSingleObject(): ", wait_process);
    status   = Status::Finished;
    auto buf = std::array{' '};
    auto len = DWORD();
    ensure(WriteFile(pipes[1].input, buf.data(), buf.size(), &len, NULL) == TRUE, "failed to write to the child process. GetLastError: ", GetLastError());
    ensure(WriteFile(pipes[2].input, buf.data(), buf.size(), &len, NULL) == TRUE, "failed to write to the child process. GetLastError: ", GetLastError());
    for(auto& thread : threads) {
        thread.join();
    }
    return true;
}
} // namespace process
