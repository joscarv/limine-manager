#include "limine_manager/domain/kernel_cmdline.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace limine_manager::domain {
namespace {

bool is_key(const std::string& argument, std::string_view key) {
    return argument == key ||
           (argument.size() > key.size() && argument.compare(0, key.size(), key) == 0 &&
            argument[key.size()] == '=');
}

std::string quote_argument(const std::string& argument) {
    if (argument.find_first_of(" \t\r\n\"\\") == std::string::npos) return argument;
    std::string result{"\""};
    for (const char ch : argument) {
        if (ch == '\"' || ch == '\\') result.push_back('\\');
        result.push_back(ch);
    }
    result.push_back('\"');
    return result;
}

} // namespace

KernelCommandLine::KernelCommandLine(std::vector<std::string> arguments)
    : arguments_(std::move(arguments)) {}

KernelCommandLine KernelCommandLine::parse(std::string_view text) {
    std::vector<std::string> arguments;
    std::string current;
    bool escaped = false;
    char quote = '\0';

    auto flush = [&]() {
        if (!current.empty()) {
            arguments.push_back(std::move(current));
            current.clear();
        }
    };

    for (const char ch : text) {
        if (escaped) {
            current.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (quote != '\0') {
            if (ch == quote) quote = '\0';
            else current.push_back(ch);
            continue;
        }
        if (ch == '\'' || ch == '\"') {
            quote = ch;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            flush();
            continue;
        }
        current.push_back(ch);
    }

    if (escaped) throw std::invalid_argument("Kernel command line ends with an escape character");
    if (quote != '\0') throw std::invalid_argument("Kernel command line contains an unterminated quote");
    flush();
    return KernelCommandLine(std::move(arguments));
}

const std::vector<std::string>& KernelCommandLine::arguments() const noexcept { return arguments_; }
bool KernelCommandLine::empty() const noexcept { return arguments_.empty(); }

std::optional<std::string> KernelCommandLine::value(std::string_view key) const {
    for (const auto& argument : arguments_) {
        if (argument.size() > key.size() && argument.compare(0, key.size(), key) == 0 &&
            argument[key.size()] == '=') {
            return argument.substr(key.size() + 1);
        }
    }
    return std::nullopt;
}

std::vector<std::string> KernelCommandLine::values(std::string_view key) const {
    std::vector<std::string> result;
    for (const auto& argument : arguments_) {
        if (argument.size() > key.size() && argument.compare(0, key.size(), key) == 0 &&
            argument[key.size()] == '=') {
            result.push_back(argument.substr(key.size() + 1));
        }
    }
    return result;
}

bool KernelCommandLine::contains(std::string_view key) const {
    for (const auto& argument : arguments_) if (is_key(argument, key)) return true;
    return false;
}

void KernelCommandLine::set(std::string key, std::string value) {
    const std::string replacement = std::move(key) + '=' + std::move(value);
    for (auto& argument : arguments_) {
        const auto separator = replacement.find('=');
        if (is_key(argument, std::string_view(replacement).substr(0, separator))) {
            argument = replacement;
            return;
        }
    }
    arguments_.push_back(replacement);
}

void KernelCommandLine::erase(std::string_view key) {
    std::erase_if(arguments_, [key](const auto& argument) { return is_key(argument, key); });
}

std::string KernelCommandLine::render() const {
    std::string result;
    for (const auto& argument : arguments_) {
        if (!result.empty()) result.push_back(' ');
        result += quote_argument(argument);
    }
    return result;
}

} // namespace limine_manager::domain
