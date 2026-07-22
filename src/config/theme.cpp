#include "limine_manager/config/theme.hpp"

#include <algorithm>
#include <cctype>

namespace limine_manager::config {
namespace {
std::string normalize(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        if (ch == '_')
            return '-';
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

const std::vector<ThemePreset> &themes() {
    static const std::vector<ThemePreset> presets {
        {"none", "None", {}},
        {"tokyo-night",
         "Tokyo Night",
         {{"backdrop", "1A1B26"},
          {"interface_branding_colour", "7AA2F7"},
          {"interface_help_colour", "9ECE6A"},
          {"interface_help_colour_bright", "BB9AF7"},
          {"term_background", "001A1B26"},
          {"term_background_bright", "24283B"},
          {"term_foreground", "C0CAF5"},
          {"term_foreground_bright", "FFFFFF"},
          {"term_palette", "15161E;F7768E;9ECE6A;E0AF68;7AA2F7;BB9AF7;7DCFFF;A9B1D6"},
          {"term_palette_bright", "414868;FF899D;9FE044;FABA4A;8DB0FF;C7A9FF;A4DAFF;C0CAF5"}}},
        {"catppuccin",
         "Catppuccin Mocha",
         {{"backdrop", "1E1E2E"},
          {"interface_branding_colour", "89B4FA"},
          {"interface_help_colour", "A6E3A1"},
          {"interface_help_colour_bright", "CBA6F7"},
          {"term_background", "001E1E2E"},
          {"term_background_bright", "313244"},
          {"term_foreground", "CDD6F4"},
          {"term_foreground_bright", "FFFFFF"},
          {"term_palette", "11111B;F38BA8;A6E3A1;F9E2AF;89B4FA;CBA6F7;94E2D5;BAC2DE"},
          {"term_palette_bright", "45475A;F5A2B8;B1F0AB;FAE8C8;9AC2FA;D5B4FA;A6E9DE;CDD6F4"}}},
        {"nord",
         "Nord",
         {{"backdrop", "2E3440"},
          {"interface_branding_colour", "88C0D0"},
          {"interface_help_colour", "A3BE8C"},
          {"interface_help_colour_bright", "81A1C1"},
          {"term_background", "002E3440"},
          {"term_background_bright", "3B4252"},
          {"term_foreground", "D8DEE9"},
          {"term_foreground_bright", "ECEFF4"},
          {"term_palette", "2E3440;BF616A;A3BE8C;EBCB8B;81A1C1;B48EAD;88C0D0;E5E9F0"},
          {"term_palette_bright", "4C566A;BF616A;A3BE8C;EBCB8B;81A1C1;B48EAD;8FBCBB;ECEFF4"}}},
        {"dracula",
         "Dracula",
         {{"backdrop", "282A36"},
          {"interface_branding_colour", "BD93F9"},
          {"interface_help_colour", "50FA7B"},
          {"interface_help_colour_bright", "FF79C6"},
          {"term_background", "00282A36"},
          {"term_background_bright", "44475A"},
          {"term_foreground", "F8F8F2"},
          {"term_foreground_bright", "FFFFFF"},
          {"term_palette", "21222C;FF5555;50FA7B;F1FA8C;BD93F9;FF79C6;8BE9FD;F8F8F2"},
          {"term_palette_bright", "6272A4;FF6E6E;69FF94;FFFFA5;D6ACFF;FF92DF;A4FFFF;FFFFFF"}}},
        {"gruvbox",
         "Gruvbox Dark",
         {{"backdrop", "282828"},
          {"interface_branding_colour", "FABD2F"},
          {"interface_help_colour", "B8BB26"},
          {"interface_help_colour_bright", "83A598"},
          {"term_background", "00282828"},
          {"term_background_bright", "3C3836"},
          {"term_foreground", "EBDBB2"},
          {"term_foreground_bright", "FBF1C7"},
          {"term_palette", "282828;CC241D;98971A;D79921;458588;B16286;689D6A;A89984"},
          {"term_palette_bright", "928374;FB4934;B8BB26;FABD2F;83A598;D3869B;8EC07C;EBDBB2"}}},
    };
    return presets;
}
} // namespace

const ThemePreset *find_theme(std::string name) {
    name = normalize(std::move(name));
    for (const auto &theme : themes()) {
        if (theme.name == name)
            return &theme;
    }
    return nullptr;
}

std::vector<ThemePreset> available_themes() {
    return themes();
}

} // namespace limine_manager::config
