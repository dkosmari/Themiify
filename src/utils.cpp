#include <cctype>
#include <cstdio>
#include <iostream>
#include <ranges>
#include <string_view>
#include <tuple>

#include <sys/iosupport.h>

#include "utils.h"

using std::cout;
using std::cerr;
using std::endl;

bool CreateParentDirectories(const std::filesystem::path& inputPath) {
    std::filesystem::path fullPath = inputPath;
    std::filesystem::path parentPath = fullPath.parent_path();

    if (!(std::filesystem::create_directories(parentPath))) {
        return false;
    }

    return true;
}

void DeletePath(const std::filesystem::path& inputPath) {
    if (inputPath.empty()) {
        cerr << "attempting to delete empty path!" << endl;
        return;
    }
    if (!exists(inputPath)) {
        // cerr << inputPath << " could not be found!" << endl;
        return;
    }

    // TODO: why not use remove_all()?
    if (is_directory(inputPath)) {
        for (auto& entry : std::filesystem::directory_iterator(inputPath)) {
            DeletePath(entry.path());
            cout << "Successfully deleted " << entry.path() << endl;
        }
    }
    else {
        cout << "Deleting " << inputPath << "..." << endl;
    }

    try {
        remove(inputPath);
    }
    catch (const std::exception& e) {
        cerr << "Error deleting: " << e.what() << endl;
        return;
    }
}

static
std::u32string
format_codepoint(char32_t c) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "U+%04X", c);
    std::u32string result;
    for (unsigned char c : std::string_view{buffer})
        result += char32_t{c};
    return result;
}

std::filesystem::path
sanitize_element(const std::filesystem::path& input) {
    std::u32string output_str;
    std::u32string input_str = input.u32string();
    for (auto c : input_str) {
        switch (c) {
            case U'/':
            case U'\\':
            case U'<':
            case U'>':
            case U':':
            case U'"':
            case U'|':
            case U'?':
            case U'*':
            case 127: // "DEL"
                output_str += U'_';
                break;
            default:
                // non-printable ASCII are forbidden
                if (c < 32)
                    output_str += U'_';
                // WORKAROUND: the Wii U needs filenames encoded in Shift-JIS, but wut
                // does not implement any conversion. So we'll enforce ASCII-only for now.
                else if (c > 127)
#if 0
                    output_str += format_codepoint(c);
#else
                    (void)format_codepoint; // do nothing
#endif
                else
                    output_str += c;
        }
    }
    // Do not allow it to end with '.'
    while (output_str.ends_with(U'.'))
        output_str.pop_back();
    return output_str;
}

std::filesystem::path
sanitize(const std::filesystem::path& input) {
    std::filesystem::path output;
    auto is_newlib_root = [](const std::filesystem::path& name) -> bool
    {
        return GetDeviceOpTab(name.c_str()) != nullptr;
    };

    for (auto [index, element] : input | std::views::enumerate) {
        if (index == 0 && is_newlib_root(element))
            output /= element; // do not sanitize root element
        else
            output /= sanitize_element(element);
    }
    return output;
}

std::string
as_lower_case(const std::string& input)
{
    std::string output = input;
    for (char &c : output)
        c = std::tolower(static_cast<unsigned char>(c));
    return output;
}

std::filesystem::path
theme_id_to_cached_thumbnail_path(const std::string& themeID)
{
    return THEMIIFY_THUMBNAILS / (make_theme_id_filename(themeID) + ".webp");
}

std::filesystem::path
make_utheme_filename(const std::string& slug,
                     const std::string& hexID) {
    return sanitize(THEMES_ROOT / (slug + "-" + hexID + ".utheme"));
}

std::string
make_theme_id_filename(const std::string& themeID)
{
    std::string result = themeID;
    // Simply delete the ':' from the ID.
    std::erase(result, ':');
    return result;
}

// TODO: this belongs to installer.cpp
std::filesystem::path
make_theme_folder_name(const std::string& name,
                       const std::optional<std::string>& themeID)
{
    std::string result = name;
    if (themeID) {
        std::string clean_theme_id = *themeID;
        std::erase(clean_theme_id, ':');
        result += " (" + clean_theme_id + ")";
    }
    return sanitize_element(result);
}


// Used by the split() functions.
namespace {

    std::tuple<std::string::size_type,
               std::vector<std::string>::size_type>
    find_first_of(const std::string& haystack,
                  const std::vector<std::string>& needles,
                  std::string::size_type start = 0)
    {
        std::string::size_type result_pos = std::string::npos;
        std::vector<std::string>::size_type result_index = 0;
        for (std::vector<std::string>::size_type i = 0; i < needles.size(); ++i) {
            auto pos = haystack.find(needles[i], start);
            if (pos < result_pos) {
                result_pos = pos;
                result_index = i;
            }
        }
        return { result_pos, result_index };
    }

} // namespace

std::vector<std::string>
split(const std::string& input,
      const std::vector<std::string>& separators,
      bool compress,
      std::size_t max_tokens)
{
    std::vector<std::string> result;

    using size_type = std::string::size_type;
    auto [sep_start, sep_index] = find_first_of(input, separators);
    size_type tok_start = 0;

    // Loop until no more separators are found.
    while (sep_start != std::string::npos) {
        if (!compress || sep_start > tok_start) {
            // If this token reaches the maximum allowed, stop the looop
            if (max_tokens && result.size() + 1 == max_tokens)
                break;
            result.push_back(input.substr(tok_start, sep_start - tok_start));
        }
        tok_start = sep_start + separators[sep_index].size();
        if (tok_start >= input.size())
            break;
        std::tie(sep_start, sep_index) = find_first_of(input, separators, tok_start);
    }
    // The remainder of the string is the last token, unless (compress && empty)
    if (!compress || tok_start < input.size())
        result.push_back(input.substr(tok_start));
    return result;
}

std::vector<std::string>
split(const std::string& input,
      const std::string& separator,
      bool compress,
      std::size_t max_tokens)
{
    return split(input, std::vector{separator}, compress, max_tokens);
}

std::string
join(const std::vector<std::string>& tokens,
     const std::string& separator)
{
    if (tokens.empty())
        return "";

    // Optimize allocation for result: calculate the final capacity.
    std::string::size_type size = 0;
    for (const auto& token : tokens)
        size += token.size();
    size += (tokens.size() - 1) * separator.size();

    std::string result;
    result.reserve(size);

    for (auto [index, token] : tokens | std::views::enumerate) {
        if (index > 0)
            result += separator;
        result += token;
    }

    return result;
}
