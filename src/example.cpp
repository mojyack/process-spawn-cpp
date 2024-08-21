#include "macros/unwrap.hpp"
#if defined(_WIN32)
#include "process-win.hpp"
#else
#include "process.hpp"
#endif

auto main(const int argc, const char* const argv[]) -> int {
    ensure(argc >= 1);
    auto args = std::vector<const char*>();
    for(auto i = 1; i < argc; i += 1) {
        args.push_back(argv[i]);
    }
    args.push_back(NULL);

    auto outputs = std::string();
    auto errors  = std::string();
    auto process = process::Process();

    process.on_stdout = [&outputs](std::span<char> output) {
        outputs.insert(outputs.end(), output.begin(), output.end());
    };
    process.on_stderr = [&errors](std::span<char> output) {
        errors.insert(errors.end(), output.begin(), output.end());
    };

    ensure(process.start(args));
    while(process.get_status() == process::Status::Running) {
        ensure(process.collect_outputs());
    }
    unwrap(result, process.join());
    print("result:");
    print("  reason=", int(result.reason));
    print("  code=", result.code);
    print("  stdout=", outputs);
    print("  stderr=", errors);
    return 0;
}
