#pragma once
#include <type_traits>

template<typename E>
concept EnumUInt32 = std::is_enum_v<E> && std::is_same_v<std::underlying_type_t<E>, uint32_t>;

constexpr uint32_t asMask(EnumUInt32 auto e) {
	return 1 << static_cast<uint32_t>(e);
}

// binary overload to enforce equal types in variadic overload
template<typename T>
constexpr uint32_t asMask(T x, T y) {
	return asMask(x) | asMask(y);
}

template<typename T, typename... Args>
constexpr uint32_t asMask(T x, T y, Args... rest) {
	return asMask(x) | asMask(y, rest...);
}
