// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "uLang/Common/Containers/Set.h"
#include "uLang/Common/Text/UTF8String.h"
#include "uLang/Parser/ReservedSymbols.inl"
#include "uLang/SourceProject/UploadedAtFNVersion.h"
#include "uLang/SourceProject/VerseVersion.h"

namespace uLang
{
class CSymbol;

enum class EIsReservedSymbolResult : uint8_t
{
    NotReserved,
    Reserved,
    ReservedFuture
};

#define VISIT_RESERVED_SYMBOL(Name, Symbol, Reservation, VerseVersion, FNVersion) Name,
/// Represents the set of reserved symbols in the semantic analyzer (not the parser!)
enum class EReservedSymbol : uint32_t
{
    VERSE_ENUMERATE_RESERVED_SYMBOLS(VISIT_RESERVED_SYMBOL)
};
#undef VISIT_RESERVED_SYMBOL

/// Gets the corresponding reserved string.
VERSECOMPILER_API CUTF8StringView GetReservedSymbol(const EReservedSymbol Identifier);

/// Gets the type of reservation this symbol has.
VERSECOMPILER_API EIsReservedSymbolResult GetReservationForSymbol(const EReservedSymbol Identifier, const uint32_t CurrentVerseVersion, const uint32_t CurrentUploadedAtFNVersion);
/// @overload
VERSECOMPILER_API EIsReservedSymbolResult GetReservationForSymbol(const CSymbol& Identifier, const uint32_t CurrentVerseVersion, const uint32_t CurrentUploadedAtFNVersion);

/// Gets all reserved tokens (both the parser and semantic analyzer).
VERSECOMPILER_API TSet<CUTF8String> GetReservedSymbols(const uint32_t CurrentVerseVersion, const uint32_t CurrentUploadedAtFNVersion);
}    // namespace uLang
