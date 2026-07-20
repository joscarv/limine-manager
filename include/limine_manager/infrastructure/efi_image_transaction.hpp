#pragma once

#include <filesystem>

namespace limine_manager::infrastructure {

class EfiImageTransaction {
  public:
    explicit EfiImageTransaction(std::filesystem::path image);
    ~EfiImageTransaction();

    EfiImageTransaction(const EfiImageTransaction &) = delete;
    EfiImageTransaction &operator=(const EfiImageTransaction &) = delete;
    EfiImageTransaction(EfiImageTransaction &&) = delete;
    EfiImageTransaction &operator=(EfiImageTransaction &&) = delete;

    void commit();
    void rollback();

    [[nodiscard]] const std::filesystem::path &image() const noexcept { return image_; }
    [[nodiscard]] const std::filesystem::path &backup() const noexcept { return backup_; }
    [[nodiscard]] bool active() const noexcept { return active_; }

  private:
    std::filesystem::path image_;
    std::filesystem::path backup_;
    bool active_{true};
};

} // namespace limine_manager::infrastructure
