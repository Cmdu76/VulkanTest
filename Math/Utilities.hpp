#ifndef NU_UTILITIES_HPP
#define NU_UTILITIES_HPP

#include "../System/Prerequisites.hpp"

#include <algorithm>
#include <type_traits>

namespace nu
{

template <typename T, typename T2 = I32>
inline T2 floor(const T& value)
{
	T2 i = T2(value);
	return i - (i > value);
}

template <typename T, typename T2 = I32>
inline T2 ceil(const T& value)
{
	T2 i = T2(value);
	return i + (i < value);
}

template <typename T>
inline T clamp(const T& x, const T& lower, const T& upper)
{
	return std::max<T>(lower, std::min<T>(x, upper));
}

template <typename T, typename T2>
inline T lerp(const T& range_start, const T& range_end, const T2& percent)
{
	return range_start + percent * (range_end - range_start);
}

template <typename T>
inline T lerp(const T& range_start, const T& range_end, const T& percent)
{
	return lerp<T, T>(range_start, range_end, percent);
}

template <typename T>
inline T fade(T t)
{
	return t * t * t * (t * (t * 6 - 15) + 10);
}

template <typename T>
inline bool inRange(T val, T range_start, T range_end)
{
	return val >= range_start && val < range_end;
}

template <typename T>
inline bool equals(const T& a, const T& b, const T& epsilon = std::numeric_limits<F32>::epsilon())
{
	return a == b;
}

template <> inline bool equals<F32>(const F32& a, const F32& b, const F32& epsilon)
{
	return std::fabs(a - b) < epsilon;
}

inline void hashCombine(size_t& seed, size_t hash)
{
	hash += 0x9e3779b9 + (seed << 6) + (seed >> 2);
	seed ^= hash;
}

} // namespace nu

#endif // NU_UTILITIES_HPP