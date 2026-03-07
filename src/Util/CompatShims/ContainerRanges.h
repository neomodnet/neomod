#pragma once

#include "config.h"

#include <vector>
#include <ranges>

namespace Mc {

template <class R, class T>
concept ContainerCompatibleRange =
    std::ranges::input_range<R> && std::convertible_to<std::ranges::range_reference_t<R>, T>;

// NOLINTBEGIN(cppcoreguidelines-missing-std-forward)

template <typename Container, typename R>
    requires ContainerCompatibleRange<R, typename Container::value_type>
constexpr void append_range(Container& c, R&& rg) {
    c.insert(c.end(), std::ranges::begin(rg), std::ranges::end(rg));
}

template <typename Container, typename R>
    requires ContainerCompatibleRange<R, typename Container::value_type>
constexpr void assign_range(Container& c, R&& rg) {
    c.assign(std::ranges::begin(rg), std::ranges::end(rg));
}

template <typename Container, typename R>
    requires ContainerCompatibleRange<R, typename Container::value_type>
constexpr typename Container::iterator insert_range(Container& c, typename Container::const_iterator pos, R&& rg) {
    return c.insert(pos, std::ranges::begin(rg), std::ranges::end(rg));
}

// NOLINTEND(cppcoreguidelines-missing-std-forward)

}  // namespace Mc