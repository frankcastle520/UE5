// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Common.h"

namespace uLang
{

/// Helper class for computing a 16 bit CRC
/// We are using the CRC-16-CCITT polynomial (0x1021),
/// but are using a bit reversed algorithm akin to CRC32 and CRC64 algorithms which saves one bit shift
class CCRC16
{
public:

    /// Generate CRC16 from a string of bytes
    /// You can compute the CRC of two concatenated strings by computing the CRC of the first string,
    /// then passing the result into the PrevCRC argument when computing the CRC of the second string
    ULANG_FORCEINLINE static uint16_t Generate(const uint8_t * Begin, const uint8_t * End, uint16_t PrevCRC = 0)
    {
        const uint16_t* Table = _Table;
        uint16_t CRC = PrevCRC;
        while (Begin < End)
        {
            CRC = (CRC >> 8) ^ Table[(CRC ^ *Begin++) & 0xff];
        }
        return CRC;
    }

private:

    ULANGCORE_API static const uint16_t* _Table;
};

/// Helper class for computing a 32 bit CRC
/// We are using the CRC-32 polynomial 0x04c11db7 as used by zip, PHP etc.,
/// but are using a bit reversed which saves one bit shift
class CCRC32
{
public:

    /// Generate CRC32 from a string of bytes
    /// You can compute the CRC of two concatenated strings by computing the CRC of the first string,
    /// then passing the result into the PrevCRC argument when computing the CRC of the second string
    ULANG_FORCEINLINE static uint32_t Generate(const uint8_t * Begin, const uint8_t * End, uint32_t PrevCRC = 0)
    {
        const uint32_t* Table = _Table;
        uint32_t CRC = PrevCRC;
        while (Begin < End)
        {
            CRC = (CRC >> 8) ^ Table[(CRC ^ *Begin++) & 0xff];
        }
        return CRC;
    }

private:

    ULANGCORE_API static const uint32_t* _Table;
};

/// Helper class for computing a 64 bit CRC
/// We are using the ECMA CRC-64 polynomial 0x42F0E1EBA9EA3693
class CCRC64
{
public:

    /// Generate CRC64 from a string of bytes - slow but needs no table
    /// You can compute the CRC of two concatenated strings by computing the CRC of the first string,
    /// then passing the result into the PrevCRC argument when computing the CRC of the second string
    static uint64_t GenerateSlow(const uint8_t* Begin, const uint8_t* End, uint64_t PrevCRC = 0);
};

} // namespace uLang