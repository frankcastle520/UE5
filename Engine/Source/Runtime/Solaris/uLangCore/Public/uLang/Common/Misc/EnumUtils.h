// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Defines all bitwise operators for enum classes so it can be (mostly) used as a regular flags enum
#define ULANG_ENUM_BIT_FLAGS(Enum, ...) \
    __VA_ARGS__ Enum& operator|=(Enum& Lhs, Enum Rhs) { return Lhs = (Enum)((__underlying_type(Enum))Lhs | (__underlying_type(Enum))Rhs); } \
    __VA_ARGS__ Enum& operator&=(Enum& Lhs, Enum Rhs) { return Lhs = (Enum)((__underlying_type(Enum))Lhs & (__underlying_type(Enum))Rhs); } \
    __VA_ARGS__ Enum& operator^=(Enum& Lhs, Enum Rhs) { return Lhs = (Enum)((__underlying_type(Enum))Lhs ^ (__underlying_type(Enum))Rhs); } \
    __VA_ARGS__ Enum  operator| (Enum  Lhs, Enum Rhs) { return (Enum)((__underlying_type(Enum))Lhs | (__underlying_type(Enum))Rhs); } \
    __VA_ARGS__ Enum  operator& (Enum  Lhs, Enum Rhs) { return (Enum)((__underlying_type(Enum))Lhs & (__underlying_type(Enum))Rhs); } \
    __VA_ARGS__ Enum  operator^ (Enum  Lhs, Enum Rhs) { return (Enum)((__underlying_type(Enum))Lhs ^ (__underlying_type(Enum))Rhs); } \
    __VA_ARGS__ bool  operator! (Enum  E)             { return !(__underlying_type(Enum))E; } \
    __VA_ARGS__ Enum  operator~ (Enum  E)             { return (Enum)~(__underlying_type(Enum))E; }

namespace uLang
{

template<typename Enum>
inline bool Enum_HasAllFlags(Enum Flags, Enum Contains)
{
    return (((__underlying_type(Enum))Flags) & (__underlying_type(Enum))Contains) == ((__underlying_type(Enum))Contains);
}

template<typename Enum>
inline bool Enum_HasAnyFlags(Enum Flags, Enum Contains)
{
    return (((__underlying_type(Enum))Flags) & (__underlying_type(Enum))Contains) != 0;
}

}