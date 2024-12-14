// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlainPropsStringUtil.h"

namespace PlainProps::Private
{
	using namespace std::literals;

	static constexpr std::string_view Hello = "Hello";
	static constexpr std::string_view Space = " ";
	static constexpr std::string_view World = "World";
	static_assert("Hello World"sv == Concat<Hello, Space, World>);
	
	static_assert("0"sv == HexString<0>);
	static_assert("1"sv == HexString<1>);
	static_assert("9"sv == HexString<9>);
	static_assert("A"sv == HexString<0xA>);
	static_assert("F"sv == HexString<0xF>);
	static_assert("10"sv == HexString<0x10>);
	static_assert("FF"sv == HexString<0xFF>);
	static_assert("100"sv == HexString<0x100>);
	static_assert("FFF"sv == HexString<0xFFF>);
	static_assert("1000"sv == HexString<0x1000>);
	static_assert("FEDCBA9876543210"sv == HexString<0xFEDCBA9876543210>);
}