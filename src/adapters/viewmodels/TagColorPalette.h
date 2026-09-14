#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <string>

// A small fixed set of tag colors so the tag panel's inline "Create tag" affordance (§1.2/§1.3
// decisions confirmed with the user) never needs a color-picker widget. No Qt dependency so it
// stays usable from TagListViewModel without pulling in extra deps.
namespace TagColorPalette
{
    constexpr std::array<const char*, 10> kColors = {
        "#E57373", "#F06292", "#BA68C8", "#7986CB", "#4FC3F7",
        "#4DB6AC", "#81C784", "#DCE775", "#FFB74D", "#A1887F",
    };

    // Deterministic pick so the same tag name always gets the same color across a session.
    inline std::string pickColorFor(const std::string& tagName)
    {
        const std::size_t index = std::hash<std::string>{}(tagName) % kColors.size();
        return kColors[index];
    }
}
