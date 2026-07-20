#include "limine_manager/config/config_loader.hpp"
#include "limine_manager/config/theme.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace limine_manager::config {
namespace {
std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool parse_bool(const std::string &value, std::size_t line) {
    const auto normalized = lower(trim(value));
    if (normalized == "true" || normalized == "yes" || normalized == "1")
        return true;
    if (normalized == "false" || normalized == "no" || normalized == "0")
        return false;
    throw std::runtime_error("Invalid boolean at line " + std::to_string(line) + ": " + value);
}

std::size_t parse_size(const std::string &value, std::size_t line) {
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoull(trim(value), &consumed);
        if (consumed != trim(value).size())
            throw std::invalid_argument("trailing");
        return static_cast<std::size_t>(parsed);
    } catch (...) {
        throw std::runtime_error("Invalid non-negative integer at line " + std::to_string(line) +
                                 ": " + value);
    }
}

std::vector<std::string> parse_list(const std::string &value) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const auto end = value.find(',', begin);
        auto item =
            trim(value.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
        if (!item.empty())
            result.push_back(std::move(item));
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    return result;
}

std::string join(const std::vector<std::string> &values) {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i)
            out << ", ";
        out << values[i];
    }
    return out.str();
}

void assign(AppConfig &config, const std::string &section, const std::string &key,
            const std::string &value, std::size_t line) {
    if (section == "manager") {
        if (key == "schema_version")
            config.schema_version = parse_size(value, line);
        else
            throw std::runtime_error("Unknown key [manager]." + key + " at line " +
                                     std::to_string(line));
    } else if (section == "system") {
        if (key == "boot_mount")
            config.system.boot_mount = value;
        else if (key == "limine_config")
            config.system.limine_config = value;
        else if (key == "kernel_cmdline_file")
            config.system.kernel_cmdline_file = value;
        else if (key == "modules_root")
            config.system.modules_root = value;
        else if (key == "cpuinfo")
            config.system.cpuinfo = value;
        else if (key == "mkinitcpio_config")
            config.system.mkinitcpio_config = value;
        else
            throw std::runtime_error("Unknown key [system]." + key + " at line " +
                                     std::to_string(line));
    } else if (section == "snapper") {
        if (key == "config")
            config.snapper_config = value;
        else if (key == "snapshots_subvolume")
            config.snapshots_subvolume = value;
        else if (key == "snapshots_directory")
            config.snapshots_directory = value;
        else if (key == "max_snapshots")
            config.max_snapshots = parse_size(value, line);
        else if (key == "include_read_write")
            config.include_read_write_snapshots = parse_bool(value, line);
        else
            throw std::runtime_error("Unknown key [snapper]." + key + " at line " +
                                     std::to_string(line));
    } else if (section == "backups") {
        if (key == "retain")
            config.backup_retention = parse_size(value, line);
        else
            throw std::runtime_error("Unknown key [backups]." + key + " at line " +
                                     std::to_string(line));
    } else if (section == "menu") {
        if (key == "root_title")
            config.root_menu_title = value;
        else if (key == "snapshots_title")
            config.snapshots_menu_title = value;
        else if (key == "root_expanded")
            config.root_menu_expanded = parse_bool(value, line);
        else if (key == "snapshots_expanded")
            config.snapshots_menu_expanded = parse_bool(value, line);
        else
            throw std::runtime_error("Unknown key [menu]." + key + " at line " +
                                     std::to_string(line));
    } else if (section == "kernels") {
        if (key == "include")
            config.include_kernels = parse_list(value);
        else if (key == "exclude")
            config.exclude_kernels = parse_list(value);
        else if (key == "order")
            config.kernel_order = parse_list(value);
        else
            throw std::runtime_error("Unknown key [kernels]." + key + " at line " +
                                     std::to_string(line));
    } else if (section == "theme") {
        if (key == "name")
            config.theme_name = lower(value);
        else
            throw std::runtime_error("Unknown key [theme]." + key + " at line " +
                                     std::to_string(line));
    } else if (section == "automation") {
        if (key == "enabled")
            config.automation_enabled = parse_bool(value, line);
        else if (key == "snapper")
            config.automation_snapper = parse_bool(value, line);
        else if (key == "pacman")
            config.automation_pacman = parse_bool(value, line);
        else if (key == "debounce_seconds")
            config.automation_debounce_seconds = parse_size(value, line);
        else if (key == "runtime_directory")
            config.automation_runtime_directory = value;
        else
            throw std::runtime_error("Unknown key [automation]." + key + " at line " +
                                     std::to_string(line));
    } else if (section == "secure_boot") {
        if (key == "protect_config")
            config.secure_boot_protect_config = parse_bool(value, line);
        else if (key == "automatic_apply")
            config.secure_boot_automatic_apply = parse_bool(value, line);
        else if (key == "efi_executable")
            config.secure_boot_efi_executable = value == "auto" ? std::filesystem::path{} : std::filesystem::path(value),
            config.system.limine_efi_executable = config.secure_boot_efi_executable;
        else
            throw std::runtime_error("Unknown key [secure_boot]." + key + " at line " +
                                     std::to_string(line));
    } else if (section == "limine") {
        config.limine_options[key] = value;
    } else {
        throw std::runtime_error("Unknown section [" + section + "] at line " +
                                 std::to_string(line));
    }
}

void validate_config(const AppConfig &config) {
    if (config.schema_version != 1)
        throw std::runtime_error("Unsupported configuration schema_version " +
                                 std::to_string(config.schema_version) +
                                 "; supported version is 1");
    if (config.system.boot_mount.empty())
        throw std::runtime_error("system.boot_mount cannot be empty");
    if (config.system.limine_config.empty())
        throw std::runtime_error("system.limine_config cannot be empty");
    if (config.snapper_config.empty())
        throw std::runtime_error("snapper.config cannot be empty");
    if (config.snapshots_subvolume.empty())
        throw std::runtime_error("snapper.snapshots_subvolume cannot be empty");
    if (config.snapshots_directory.empty())
        throw std::runtime_error("snapper.snapshots_directory cannot be empty");
    if (config.root_menu_title.empty())
        throw std::runtime_error("menu.root_title cannot be empty");
    if (config.snapshots_menu_title.empty())
        throw std::runtime_error("menu.snapshots_title cannot be empty");
    if (!find_theme(config.theme_name))
        throw std::runtime_error("Unknown theme '" + config.theme_name +
                                 "'. Supported themes: none, tokyo-night, catppuccin, nord, "
                                 "dracula, gruvbox");
    if (config.automation_debounce_seconds > 3600)
        throw std::runtime_error("automation.debounce_seconds must be <= 3600");
    if (config.automation_runtime_directory.empty())
        throw std::runtime_error("automation.runtime_directory cannot be empty");
    for (const auto &item : config.include_kernels) {
        if (std::find(config.exclude_kernels.begin(), config.exclude_kernels.end(), item) !=
            config.exclude_kernels.end())
            throw std::runtime_error("Kernel '" + item +
                                     "' appears in both include and exclude lists");
    }
}
} // namespace

LoadedConfig ConfigLoader::load(const std::optional<std::filesystem::path> &explicit_path) const {
    LoadedConfig loaded;
    const auto path = explicit_path.value_or(default_path_);
    const bool exists = filesystem_.exists(path);
    if (!exists) {
        if (explicit_path)
            throw std::runtime_error("Configuration file not found: " + path.string());
        validate_config(loaded.value);
        return loaded;
    }

    if (!filesystem_.readable(path))
        throw std::runtime_error("Unable to read configuration file: " + path.string());
    loaded.source = path;

    std::istringstream input(filesystem_.read_text(path));
    std::string section;
    std::string line_text;
    std::size_t line = 0;
    while (std::getline(input, line_text)) {
        ++line;
        auto text = trim(line_text);
        if (text.empty() || text.front() == '#' || text.front() == ';')
            continue;
        if (text.front() == '[' && text.back() == ']') {
            section = lower(trim(text.substr(1, text.size() - 2)));
            if (section.empty())
                throw std::runtime_error("Empty section at line " + std::to_string(line));
            continue;
        }
        const auto equal = text.find('=');
        if (equal == std::string::npos)
            throw std::runtime_error("Expected key=value at line " + std::to_string(line));
        if (section.empty())
            throw std::runtime_error("Key outside a section at line " + std::to_string(line));
        const auto key = lower(trim(text.substr(0, equal)));
        const auto value = trim(text.substr(equal + 1));
        if (key.empty())
            throw std::runtime_error("Empty key at line " + std::to_string(line));
        assign(loaded.value, section, key, value, line);
    }
    validate_config(loaded.value);
    return loaded;
}

std::string ConfigLoader::render(const LoadedConfig &loaded) const {
    const auto &c = loaded.value;
    std::ostringstream out;
    out << "# Effective limine-manager configuration\n";
    out << "# Source: " << (loaded.source ? loaded.source->string() : "built-in defaults")
        << "\n\n";
    out << "[manager]\n"
        << "schema_version = " << c.schema_version << "\n\n";
    out << "[system]\n"
        << "boot_mount = " << c.system.boot_mount.string() << '\n'
        << "limine_config = " << c.system.limine_config.string() << '\n'
        << "kernel_cmdline_file = " << c.system.kernel_cmdline_file.string() << '\n'
        << "modules_root = " << c.system.modules_root.string() << '\n'
        << "cpuinfo = " << c.system.cpuinfo.string() << "\n\n";
    out << "[snapper]\n"
        << "config = " << c.snapper_config << '\n'
        << "snapshots_subvolume = " << c.snapshots_subvolume << '\n'
        << "snapshots_directory = " << c.snapshots_directory.string() << '\n'
        << "max_snapshots = " << c.max_snapshots << '\n'
        << "include_read_write = " << (c.include_read_write_snapshots ? "true" : "false") << "\n\n";
    out << "[backups]\n"
        << "retain = " << c.backup_retention << "\n\n";
    out << "[menu]\n"
        << "root_title = " << c.root_menu_title << '\n'
        << "snapshots_title = " << c.snapshots_menu_title << '\n'
        << "root_expanded = " << (c.root_menu_expanded ? "true" : "false") << '\n'
        << "snapshots_expanded = " << (c.snapshots_menu_expanded ? "true" : "false") << "\n\n";
    out << "[kernels]\n"
        << "include = " << join(c.include_kernels) << '\n'
        << "exclude = " << join(c.exclude_kernels) << '\n'
        << "order = " << join(c.kernel_order) << "\n\n";
    out << "[theme]\n"
        << "name = " << c.theme_name << "\n\n";
    out << "[automation]\n"
        << "enabled = " << (c.automation_enabled ? "true" : "false") << '\n'
        << "snapper = " << (c.automation_snapper ? "true" : "false") << '\n'
        << "pacman = " << (c.automation_pacman ? "true" : "false") << '\n'
        << "debounce_seconds = " << c.automation_debounce_seconds << '\n'
        << "runtime_directory = " << c.automation_runtime_directory.string() << "\n\n";
    out << "[secure_boot]\n"
        << "protect_config = " << (c.secure_boot_protect_config ? "true" : "false") << '\n'
        << "automatic_apply = " << (c.secure_boot_automatic_apply ? "true" : "false") << '\n'
        << "efi_executable = " << (c.secure_boot_efi_executable.empty() ? "auto" : c.secure_boot_efi_executable.string()) << "\n\n";
    out << "[limine]\n";
    for (const auto &[key, value] : c.limine_options)
        out << key << " = " << value << '\n';
    return out.str();
}

} // namespace limine_manager::config
