#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace limine_manager::domain {

class KernelCommandLine {
  public:
    static KernelCommandLine parse(std::string_view text);

    [[nodiscard]] const std::vector<std::string> &arguments() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::optional<std::string> value(std::string_view key) const;
    [[nodiscard]] std::vector<std::string> values(std::string_view key) const;
    [[nodiscard]] bool contains(std::string_view key) const;

    void set(std::string key, std::string value);
    void erase(std::string_view key);

    [[nodiscard]] std::string render() const;

  private:
    explicit KernelCommandLine(std::vector<std::string> arguments);
    std::vector<std::string> arguments_;
};

} // namespace limine_manager::domain
