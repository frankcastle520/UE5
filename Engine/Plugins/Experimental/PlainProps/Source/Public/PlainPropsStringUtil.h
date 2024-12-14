// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <string_view>
#include <array>
#include "HAL/Platform.h" // uint32 etc

namespace PlainProps::Private
{

inline constexpr void Append(char*& ToIt, std::string_view From)
{
    for (char Char : From)
    {
        *ToIt++ = Char;
    }
}

template <const std::string_view&... Strs>
struct TConcat
{
    inline static constexpr std::size_t Len = (Strs.size() + ... + 0);

    static constexpr std::array<char, Len + 1> Chars = []()
    {    
        std::array<char, Len + 1> Out;
        char* OutIt = Out.data();
        (Append(OutIt, Strs), ...);
        *OutIt = '\0';
        return Out;
    }();

    inline static constexpr std::string_view Value{Chars.data(), Len};
};

template<uint64 N>
struct THexString
{
	inline static constexpr char At(uint32 Idx)
	{
		uint64 Nibble =	(N >> 4 * Idx) & uint64(15);
		return static_cast<char>(Nibble > 9 ? Nibble - 10 + 'A' : Nibble + '0'); 
	}

	static constexpr char Chars[16] = { At(15), At(14), At(13), At(12), At(11), At(10), At(9), At(8), At(7), At(6), At(5), At(4), At(3), At(2), At(1), At(0) };

	inline static constexpr uint32 CalcLen()
	{
		uint32 Idx = 15;
		for (; Idx && (N >> (Idx * 4)) == 0; --Idx) {}
		return Idx + 1;
	}

	inline static constexpr uint32 Len = CalcLen();
	inline static constexpr std::string_view Value{Chars + 16 - Len, Len};
};

} // namespace PlainProps::Private

namespace PlainProps
{

template <const std::string_view&... Strs>
inline constexpr std::string_view Concat = Private::TConcat<Strs...>::Value;

template <uint64 N>
inline constexpr std::string_view HexString = Private::THexString<N>::Value;

} // namespace PlainProps