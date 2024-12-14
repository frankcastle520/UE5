// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Text/Symbol.h"

namespace uLang
{

/**
 * Common parent class for objects with a name.
 * Used for identifying, sorting, etc.
 **/
class CNamed
{
public:

    // Common Methods

    ULANG_FORCEINLINE CNamed()                                             {}
    ULANG_FORCEINLINE explicit CNamed(const CSymbol& Name) : _Name(Name)   {}
    ULANG_FORCEINLINE CNamed(const CNamed& Source) : _Name(Source._Name)   {}
    ULANG_FORCEINLINE CNamed & operator=(const CNamed & Source)            { _Name = Source._Name; return *this; }

    // Converter Methods

    ULANG_FORCEINLINE operator const CSymbol& () const                     { return _Name; }

    // Comparison Methods - used for sorting etc.

    ULANG_FORCEINLINE EEquate Compare(const CSymbol& Name) const           { return _Name.Compare(Name); }
    ULANG_FORCEINLINE bool operator==(const CSymbol& Name) const           { return _Name == Name; }
    ULANG_FORCEINLINE bool operator!=(const CSymbol& Name) const           { return _Name != Name; }
    ULANG_FORCEINLINE bool operator<=(const CSymbol& Name) const           { return _Name <= Name; }
    ULANG_FORCEINLINE bool operator>=(const CSymbol& Name) const           { return _Name >= Name; }
    ULANG_FORCEINLINE bool operator<(const CSymbol& Name) const            { return _Name < Name; }
    ULANG_FORCEINLINE bool operator>(const CSymbol& Name) const            { return _Name > Name; }

    // Accessor Methods

    ULANG_FORCEINLINE const CSymbol& GetName() const                       { return _Name; }
    ULANG_FORCEINLINE SymbolId       GetNameId() const                     { return _Name.GetId(); }
    ULANG_FORCEINLINE void           SetName(const CSymbol & Name)         { _Name = Name; }

    ULANG_FORCEINLINE CUTF8StringView AsNameStringView() const             { return _Name.AsStringView(); }
    ULANG_FORCEINLINE const char*     AsNameCString() const                { return _Name.AsCString(); }
    ULANG_FORCEINLINE UTF8Char        AsNameFirstByte() const              { return _Name.FirstByte(); }

protected:

    // Data Members

    CSymbol _Name;

};  // CNamed

}