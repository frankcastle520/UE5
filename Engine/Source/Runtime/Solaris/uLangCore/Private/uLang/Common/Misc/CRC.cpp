// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Common/Misc/CRC.h"

namespace uLang
{

namespace
{

/// Helper class for computing a 16 bit CRC lookup table
/// We are using the CRC-16-CCITT polynomial (0x1021),
/// but are using a bit reversed algorithm akin to CRC32 and CRC64 algorithms which saves one bit shift
class CCRC16Table
{
public:

    CCRC16Table()
    {
        static constexpr uint32_t ReversedCrcPoly = 0x8408u; // = 0x1021, bit reversed
        for (uint32_t Index = 0; Index < 256; ++Index)
        {
            uint32_t CRC = Index;
            for (uint32_t Bit = 8; Bit; --Bit)
            {
                if (CRC & 1)
                {
                    CRC = (CRC >> 1) ^ ReversedCrcPoly;
                }
                else
                {
                    CRC >>= 1;
                }
            }
            _Table[Index] = uint16_t(CRC);
        }
    }

    const uint16_t* GetTable() const { return _Table; }

private:

    uint16_t _Table[256];
};

const CCRC16Table CRC16Table;

/// Helper class for computing the 32 bit CRC lookup table
/// We are using the CRC-32 polynomial 0x04c11db7 as used by zip, PHP etc.,
/// but are using a bit reversed which saves one bit shift
class CCRC32Table
{
public:

    CCRC32Table()
    {
        static constexpr uint32_t ReversedCrcPoly = 0xedb88320u; // = 0x04c11db7, bit reversed
        for (uint32_t Index = 0; Index < 256; ++Index)
        {
            uint32_t CRC = Index;
            for (uint32_t Bit = 8; Bit; --Bit)
            {
                if (CRC & 1)
                {
                    CRC = (CRC >> 1) ^ ReversedCrcPoly;
                }
                else
                {
                    CRC >>= 1;
                }
            }
            _Table[Index] = CRC;
        }
    }

    const uint32_t* GetTable() const { return _Table; }

private:
    uint32_t _Table[256];
};

const CCRC32Table CRC32Table;

}

const uint16_t* CCRC16::_Table = CRC16Table.GetTable();

const uint32_t* CCRC32::_Table = CRC32Table.GetTable();

uint64_t CCRC64::GenerateSlow(const uint8_t* Begin, const uint8_t* End, uint64_t PrevCRC)
{
    static constexpr uint64_t ReversedCrcPoly = 0xC96C5795D7870F42u; // The _reversed_ polynomial

    uint64_t CRC = PrevCRC;
    while (Begin < End)
    {
        CRC ^= *Begin++;
        for (uint32_t Bit = 8; Bit; --Bit)
        {
            CRC = (CRC >> 1) ^ (ReversedCrcPoly & -int64_t(CRC & 1));
        }
    }
    return CRC;
}


}