#include "RegexMatch.h"

#include <filesystem>
#include <regex>

namespace
{
    std::wstring toWide(const std::string& utf8)
    {
        return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())).wstring();
    }
}

Result<bool> RegexMatch::matches(const std::string& patternUtf8, const std::string& textUtf8, bool caseSensitive)
{
    auto flags = std::regex_constants::ECMAScript;
    if (!caseSensitive)
    {
        flags |= std::regex_constants::icase;
    }

    try
    {
        const std::wregex regex(toWide(patternUtf8), flags);
        return Result<bool>::success(std::regex_search(toWide(textUtf8), regex));
    }
    catch (const std::regex_error&)
    {
        return Result<bool>::failure(Error(ErrorCode::InvalidArgument, "Invalid regex pattern"));
    }
}
