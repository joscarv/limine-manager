#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace limine_manager::domain {

enum class DiagnosticSeverity { info, warning, error };

struct Diagnostic {
    DiagnosticSeverity severity{DiagnosticSeverity::info};
    std::string code;
    std::string message;
};

class ValidationReport {
public:
    void add(DiagnosticSeverity severity, std::string code, std::string message);
    void info(std::string code, std::string message);
    void warning(std::string code, std::string message);
    void error(std::string code, std::string message);

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::size_t error_count() const noexcept;
    [[nodiscard]] std::size_t warning_count() const noexcept;
    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const noexcept;

private:
    std::vector<Diagnostic> diagnostics_;
};

[[nodiscard]] std::string_view to_string(DiagnosticSeverity severity) noexcept;

} // namespace limine_manager::domain
