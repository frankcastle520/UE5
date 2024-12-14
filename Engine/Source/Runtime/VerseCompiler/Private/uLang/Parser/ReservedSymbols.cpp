// Copyright Epic Games, Inc. All Rights Reserved.
#include "uLang/Parser/ReservedSymbols.h"

#include "uLang/Common/Common.h"
#include "uLang/Common/Containers/Map.h"
#include "uLang/Common/Text/Symbol.h"
#include "uLang/Common/Text/UTF8StringView.h"
#include "uLang/Parser/VerseGrammar.h"

// Matching the code in `VerseGrammar.h`
#if defined(__cpp_char8_t)
using char8 = char8_t;
#else
using char8 = char;
#endif

namespace uLang
{

struct SReservedSymbol
{
    const char8* Name;
    uint32_t VerseVersion;
    uint32_t FNVersion;
    EReservedSymbol Symbol;
    EIsReservedSymbolResult Reservation;
};

#define VISIT_RESERVED_SYMBOL(Name, Symbol, Reservation, VerseVersion, FNVersion) {Symbol, VerseVersion, FNVersion, EReservedSymbol::Name, Reservation},
static const SReservedSymbol ReservedSymbols[] = {VERSE_ENUMERATE_RESERVED_SYMBOLS(VISIT_RESERVED_SYMBOL)};
#undef VISIT_RESERVED_SYMBOL

CUTF8StringView GetReservedSymbol(const EReservedSymbol Identifier)
{
    if (ULANG_ENSUREF(static_cast<uint32_t>(Identifier) < ULANG_COUNTOF(ReservedSymbols), "Identifier was invalid or reserved keywords were not initialized properly!"))
    {
        return {reinterpret_cast<const char*>(ReservedSymbols[static_cast<uint32_t>(Identifier)].Name)};
    }
    ULANG_UNREACHABLE();
}

EIsReservedSymbolResult GetReservationForSymbol(const EReservedSymbol Identifier, const uint32_t CurrentVerseVersion, const uint32_t CurrentUploadedAtFNVersion)
{
    if (ULANG_ENSUREF(static_cast<uint32_t>(Identifier) < ULANG_COUNTOF(ReservedSymbols), "Identifier was invalid or reserved keywords were not initialized properly!"))
    {
        const EIsReservedSymbolResult DefinedReservation = ReservedSymbols[static_cast<uint32_t>(Identifier)].Reservation;
        const uint32_t VerseVersion = ReservedSymbols[static_cast<uint32_t>(Identifier)].VerseVersion;
        const uint32_t UploadedAtFNVersion = ReservedSymbols[static_cast<uint32_t>(Identifier)].FNVersion;
        switch (DefinedReservation)
        {
            case EIsReservedSymbolResult::NotReserved:
                break;
            // Means that it should be reserved right now or not at all, with version gates determining if it is reserved right now.
            case EIsReservedSymbolResult::Reserved:
                if (CurrentVerseVersion >= VerseVersion && CurrentUploadedAtFNVersion >= UploadedAtFNVersion)
                {
                    return EIsReservedSymbolResult::Reserved;
                }
                break;
            // Means that it is either reserved right now, or is treated as being reserved in the future. The version gates whether
            // or not it is the former or latter.
            case EIsReservedSymbolResult::ReservedFuture:
                if (CurrentVerseVersion >= VerseVersion && CurrentUploadedAtFNVersion >= UploadedAtFNVersion)
                {
                    return EIsReservedSymbolResult::Reserved;
                }
                else
                {
                    return EIsReservedSymbolResult::ReservedFuture;
                }
            default:
                ULANG_UNREACHABLE();
        }
    }
    return EIsReservedSymbolResult::NotReserved;
}

static TMap<CUTF8String, EReservedSymbol> MakeSymbolSetCache()
{
    TMap<CUTF8String, EReservedSymbol> SymbolSetCache;
#define VISIT_RESERVED_SYMBOL(Name, Symbol, Reservation, VerseVersion, FNVersion) SymbolSetCache.Insert(reinterpret_cast<const char*>(Symbol), EReservedSymbol::Name);
    VERSE_ENUMERATE_RESERVED_SYMBOLS(VISIT_RESERVED_SYMBOL);
#undef VISIT_RESERVED_SYMBOL
    return SymbolSetCache;
}

EIsReservedSymbolResult GetReservationForSymbol(const CSymbol& Identifier, const uint32_t CurrentVerseVersion, const uint32_t CurrentUploadedAtFNVersion)
{
    static TMap<CUTF8String, EReservedSymbol> SymbolSetCache = MakeSymbolSetCache();
    if (const EReservedSymbol* IsReservedSymbol = SymbolSetCache.Find(Identifier.AsStringView()))
    {
        return GetReservationForSymbol(*IsReservedSymbol, CurrentVerseVersion, CurrentUploadedAtFNVersion);
    }
    return EIsReservedSymbolResult::NotReserved;
}

TSet<CUTF8String> GetReservedSymbols(const uint32_t CurrentVerseVersion, const uint32_t CurrentUploadedAtFNVersion)
{
    TSet<CUTF8String> Result;
    for (const Verse::Grammar::token_info& Token : Verse::Grammar::Tokens)
    {
        if (const CUTF8StringView TokenView = CUTF8StringView(reinterpret_cast<const char*>(Token.Symbol)); !TokenView.IsEmpty())
        {
            Result.Insert(TokenView);
        }
    }
    // Some symbols are only reserved starting from a given Verse language version onwards, or after a given UEFN version.
#define VISIT_RESERVED_SYMBOL(Name, Symbol, Reservation, VerseVersion, FNVersion)                                                                \
    if (GetReservationForSymbol(EReservedSymbol::Name, CurrentVerseVersion, CurrentUploadedAtFNVersion) != EIsReservedSymbolResult::NotReserved) \
    {                                                                                                                                            \
        Result.Insert(reinterpret_cast<const char*>(Symbol));                                                                                    \
    }

    VERSE_ENUMERATE_RESERVED_SYMBOLS(VISIT_RESERVED_SYMBOL)
#undef VISIT_RESERVED_SYMBOL
    return Result;
}

}    // namespace uLang
