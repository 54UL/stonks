#pragma once

#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>

namespace stnks
{
    template<typename E>
    struct EnumTraits;

    template<typename E>
    constexpr const char* EnumToString(E val)
    {
        for (auto& [e, name] : EnumTraits<E>::values)
            if (e == val) return name;
        return "Unknown";
    }

    template<typename E>
    E EnumFromString(const std::string& str)
    {
        for (auto& [e, name] : EnumTraits<E>::values)
            if (str == name) return e;
        return EnumTraits<E>::values[0].first;
    }

    template<typename E>
    constexpr std::size_t EnumCount()
    {
        return sizeof(EnumTraits<E>::values) / sizeof(EnumTraits<E>::values[0]);
    }

    template<typename E>
    constexpr auto EnumValues() -> const std::pair<E, const char*>*
    {
        return EnumTraits<E>::values;
    }

} // namespace stnks
