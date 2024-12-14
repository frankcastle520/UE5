// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Common.h"

namespace uLang
{

template<class KeyType>
struct TDefaultHashTraits
{
    static uint32_t GetKeyHash(const KeyType& Key) { return GetTypeHash(Key); }
};

// Default hash function for pointers.
inline uint32_t GetTypeHash(const void* Key)
{
	const uintptr_t PtrInt = reinterpret_cast<uintptr_t>(Key) >> 4;
    uint32_t Hash = static_cast<uint32_t>(PtrInt);    
    Hash ^= Hash >> 16;
    Hash *= 0x85ebca6b;
    Hash ^= Hash >> 13;
    Hash *= 0xc2b2ae35;
    Hash ^= Hash >> 16;
    return Hash;
}

// A few useful specializations
template<> struct TDefaultHashTraits<int32_t> { static uint32_t GetKeyHash(const int32_t Key) { return uint32_t(Key); } };
template<> struct TDefaultHashTraits<int64_t> { static uint32_t GetKeyHash(const int64_t Key) { return uint32_t(Key); } };

/**
 * Combines two hash values to get a third.
 * Note - this function is not commutative.
 *
 * WARNING!  This function is subject to change and should only be used for creating
 *           combined hash values which don't leave the running process,
 *           e.g. GetTypeHash() overloads.
 */
inline uint32_t HashCombineFast(uint32_t A, uint32_t B)
{
    return A ^ (B + 0x9e3779b9 + (A << 6) + (A >> 2));
}
}
