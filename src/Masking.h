#pragma once
#include <type_traits>

template<typename E>
concept EnumUInt32 = std::is_enum_v<E> && std::is_same_v<std::underlying_type_t<E>, uint32_t>;

template<typename E, typename... Args> requires EnumUInt32<E> && (std::same_as<E, Args> && ...)
constexpr uint32_t asMask(E e, Args... rest) {
	return ((uint32_t{1} << static_cast<uint32_t>(e)) | ... | (uint32_t{1} << static_cast<uint32_t>(rest)));
}
