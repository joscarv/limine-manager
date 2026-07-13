#include "limine_manager/infrastructure/snapper_client.hpp"

#include <charconv>
#include <stdexcept>
#include <unordered_map>

namespace limine_manager::infrastructure {
namespace {
std::vector<std::string> parse_csv_row(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (quoted) {
            if (ch == '"' && i + 1 < line.size() && line[i + 1] == '"') { field += '"'; ++i; }
            else if (ch == '"') quoted = false;
            else field += ch;
        } else if (ch == '"') quoted = true;
        else if (ch == ',') { fields.push_back(std::move(field)); field.clear(); }
        else field += ch;
    }
    if (quoted) throw std::runtime_error("Malformed CSV output from snapper");
    fields.push_back(std::move(field));
    return fields;
}

template <typename Callback>
void for_each_line(const std::string& text, Callback callback) {
    std::size_t begin = 0;
    while (begin < text.size()) {
        const auto end = text.find('\n', begin);
        auto line = text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) callback(line);
        begin = end == std::string::npos ? text.size() : end + 1;
    }
}
}

SnapperConfig SnapperClient::get_config() const {
    const auto result = runner_.run({"snapper", "--config", config_, "--csvout", "--no-headers",
                                     "get-config", "--columns", "key,value"});
    if (result.exit_code != 0) throw std::runtime_error("snapper get-config failed: " + result.output);

    std::unordered_map<std::string, std::string> values;
    for_each_line(result.output, [&](const std::string& line) {
        const auto fields = parse_csv_row(line);
        if (fields.size() == 2) values[fields[0]] = fields[1];
    });
    return {config_, values["SUBVOLUME"], values["FSTYPE"]};
}

std::vector<SnapshotInfo> SnapperClient::list() const {
    const auto result = runner_.run({"snapper", "--config", config_, "--csvout", "--no-headers",
                                     "list", "--disable-used-space",
                                     "--columns", "number,date,description,read-only"});
    if (result.exit_code != 0) throw std::runtime_error("snapper list failed: " + result.output);

    std::vector<SnapshotInfo> snapshots;
    for_each_line(result.output, [&](const std::string& line) {
        const auto fields = parse_csv_row(line);
        if (fields.size() != 4) throw std::runtime_error("Unexpected snapper CSV column count");
        if (fields[0] == "0") return;
        unsigned long number{};
        const auto parsed = std::from_chars(fields[0].data(), fields[0].data() + fields[0].size(), number);
        if (parsed.ec != std::errc{} || parsed.ptr != fields[0].data() + fields[0].size()) {
            throw std::runtime_error("Invalid snapshot number: " + fields[0]);
        }
        snapshots.push_back({number, fields[1], fields[2], fields[3] == "yes"});
    });
    return snapshots;
}

} // namespace limine_manager::infrastructure
