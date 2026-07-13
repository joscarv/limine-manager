#include "limine_manager/domain/validation.hpp"

#include <algorithm>
#include <utility>

namespace limine_manager::domain {

void ValidationReport::add(DiagnosticSeverity severity, std::string code, std::string message) {
    diagnostics_.push_back({severity, std::move(code), std::move(message)});
}

void ValidationReport::info(std::string code, std::string message) {
    add(DiagnosticSeverity::info, std::move(code), std::move(message));
}

void ValidationReport::warning(std::string code, std::string message) {
    add(DiagnosticSeverity::warning, std::move(code), std::move(message));
}

void ValidationReport::error(std::string code, std::string message) {
    add(DiagnosticSeverity::error, std::move(code), std::move(message));
}

bool ValidationReport::valid() const noexcept {
    return error_count() == 0;
}

std::size_t ValidationReport::error_count() const noexcept {
    return static_cast<std::size_t>(
        std::count_if(diagnostics_.begin(), diagnostics_.end(),
                      [](const auto &item) { return item.severity == DiagnosticSeverity::error; }));
}

std::size_t ValidationReport::warning_count() const noexcept {
    return static_cast<std::size_t>(
        std::count_if(diagnostics_.begin(), diagnostics_.end(), [](const auto &item) {
            return item.severity == DiagnosticSeverity::warning;
        }));
}

const std::vector<Diagnostic> &ValidationReport::diagnostics() const noexcept {
    return diagnostics_;
}

std::string_view to_string(DiagnosticSeverity severity) noexcept {
    switch (severity) {
    case DiagnosticSeverity::info:
        return "INFO";
    case DiagnosticSeverity::warning:
        return "WARN";
    case DiagnosticSeverity::error:
        return "ERROR";
    }
    return "UNKNOWN";
}

} // namespace limine_manager::domain
