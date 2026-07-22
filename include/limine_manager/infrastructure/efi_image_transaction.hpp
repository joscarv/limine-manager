#pragma once

#include <filesystem>
#include <functional>
#include <string_view>

namespace limine_manager::infrastructure {

using RollbackErrorReporter = std::function<void(std::string_view)>;

class EfiImageTransaction {
  public:
    explicit EfiImageTransaction(std::filesystem::path image,
                                 RollbackErrorReporter error_reporter = {});
    ~EfiImageTransaction() noexcept;

    EfiImageTransaction(const EfiImageTransaction &) = delete;
    EfiImageTransaction &operator=(const EfiImageTransaction &) = delete;
    EfiImageTransaction(EfiImageTransaction &&) = delete;
    EfiImageTransaction &operator=(EfiImageTransaction &&) = delete;

    void commit();
    void rollback();

    [[nodiscard]] const std::filesystem::path &image() const noexcept {
        return image_;
    }
    [[nodiscard]] const std::filesystem::path &backup() const noexcept {
        return backup_;
    }
    [[nodiscard]] bool active() const noexcept {
        return active_;
    }

  private:
    void report_destructor_error(std::string_view message) const noexcept;

    std::filesystem::path image_;
    std::filesystem::path backup_;
    RollbackErrorReporter error_reporter_;
    bool active_ {true};
};

} // namespace limine_manager::infrastructure
