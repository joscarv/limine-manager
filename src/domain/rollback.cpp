#include "limine_manager/domain/rollback.hpp"

namespace limine_manager::domain {

std::string to_string(RollbackBootMode mode) {
    switch (mode) {
    case RollbackBootMode::normal_root:
        return "normal-root";
    case RollbackBootMode::managed_snapshot:
        return "managed-snapshot";
    case RollbackBootMode::unknown:
        return "unknown";
    }
    return "unknown";
}

std::string to_string(RollbackSeverity severity) {
    switch (severity) {
    case RollbackSeverity::info:
        return "INFO";
    case RollbackSeverity::warning:
        return "WARNING";
    case RollbackSeverity::error:
        return "ERROR";
    }
    return "ERROR";
}

} // namespace limine_manager::domain
