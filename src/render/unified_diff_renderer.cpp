#include "limine_manager/render/unified_diff_renderer.hpp"

#include <sstream>
#include <string_view>
#include <vector>

namespace limine_manager::render {
namespace {
std::vector<std::string> lines(std::string_view text) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start < text.size()) {
        const auto end = text.find('\n', start);
        if (end == std::string_view::npos) {
            result.emplace_back(text.substr(start));
            break;
        }
        result.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return result;
}
}

std::string UnifiedDiffRenderer::render(const application::ChangePlan& plan) const {
    if (!plan.has_changes()) return {};
    const auto before = lines(plan.installed);
    const auto after = lines(plan.generated);
    const std::size_t n = before.size(), m = after.size();
    std::vector<std::vector<std::size_t>> lcs(n + 1, std::vector<std::size_t>(m + 1));
    for (std::size_t i = n; i-- > 0;) {
        for (std::size_t j = m; j-- > 0;) {
            lcs[i][j] = before[i] == after[j] ? 1 + lcs[i + 1][j + 1]
                                               : std::max(lcs[i + 1][j], lcs[i][j + 1]);
        }
    }
    std::ostringstream out;
    out << "--- " << (plan.kind == application::ChangeKind::create ? "/dev/null" : plan.target.string()) << '\n';
    out << "+++ " << plan.target.string() << " (generated)\n";
    out << "@@ -1," << n << " +1," << m << " @@\n";
    std::size_t i = 0, j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && before[i] == after[j]) {
            out << ' ' << before[i] << '\n'; ++i; ++j;
        } else if (j < m && (i == n || lcs[i][j + 1] >= lcs[i + 1][j])) {
            out << '+' << after[j] << '\n'; ++j;
        } else {
            out << '-' << before[i] << '\n'; ++i;
        }
    }
    return out.str();
}
} // namespace limine_manager::render
