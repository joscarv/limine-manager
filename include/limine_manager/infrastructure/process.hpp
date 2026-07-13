#pragma once

#include <string>
#include <vector>

namespace limine_manager::infrastructure {

struct ProcessResult {
    int exit_code{};
    std::string output;
};

class ProcessRunner {
public:
    virtual ~ProcessRunner() = default;
    virtual ProcessResult run(const std::vector<std::string>& arguments) const = 0;
};

class PosixProcessRunner final : public ProcessRunner {
public:
    ProcessResult run(const std::vector<std::string>& arguments) const override;
};

} // namespace limine_manager::infrastructure
