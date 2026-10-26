#ifndef WOT_STOCK_SCALEFORM_RESOURCES_HPP
#define WOT_STOCK_SCALEFORM_RESOURCES_HPP

#include <string>
#include <vector>

namespace wot
{
// aadf60 normalizes case and separators before its first filesystem lookup.
// IME movies can arrive with an absolute SDK path containing the resource root.
inline std::string stockScaleformInitialResourcePath(const char* filename)
{
    std::string result(filename);
    for (char& value : result)
    {
        if (value == '\\') value = '/';
        else if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
    }
    if (result.find("/ime.") != std::string::npos)
    {
        const auto gui = result.find("/gui/");
        if (gui != std::string::npos) result.erase(0, gui + 1);
    }
    return result;
}

// GFx concatenates relative URLs without resolving dot segments. Archive
// DataSections do not have the OS parent-directory semantics GFx expects.
inline std::string stockScaleformResourcePath(const char* filename)
{
    const std::string path(filename);
    if (path.empty() || path[0] == '/' || path[0] == '\\')
        return path;
    for (std::size_t i = 0; i < path.size(); ++i)
    {
        if (path[i] == '/' || path[i] == '\\')
            break;
        if (path[i] == ':' && i + 1 < path.size() &&
            (path[i + 1] == '/' || path[i + 1] == '\\'))
            return path; // Preserve GFx absolute/protocol URL handling.
    }

    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start < path.size())
    {
        const std::size_t end = path.find_first_of("/\\", start);
        const std::string part = path.substr(start, end - start);
        if (part == ".." && !parts.empty() && parts.back() != "..")
            parts.pop_back();
        else if (!part.empty() && part != ".")
            parts.push_back(part);
        if (end == std::string::npos)
            break;
        start = end + 1;
    }

    std::string result;
    for (std::size_t i = 0; i < parts.size(); ++i)
    {
        if (i != 0)
            result += '/';
        result += parts[i];
    }
    // Do not turn a directory-only URL into the empty rootSection lookup.
    if (result.empty())
        result = ".";
    if (path.back() == '/' || path.back() == '\\')
        result += '/';
    return result;
}

inline std::string stockScaleformFallbackResourcePath(const std::string& first)
{
    // The original resource reader also rebases this SDK-relative resource URL.
    const std::string path = first.compare(0, 10, "../res/gui") == 0 ? first.substr(7) : first;
    return stockScaleformResourcePath(path.c_str());
}
}

#endif
