// EroBooks prototype addition, 2026-10-15. GPL-3.0.
#pragma once
#include <string_view>

namespace OStrands::ExternalDrips
{
    // Papyrus/BSFixedString can retain a different first-seen capitalization.
    constexpr bool EqualASCII(std::string_view a, std::string_view b)
    {
        if (a.size() != b.size()) return false;
        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c; };
        for (std::size_t i = 0; i < a.size(); ++i)
            if (lower(a[i]) != lower(b[i])) return false;
        return true;
    }
}
