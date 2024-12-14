// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Common.h"

namespace vsyntax {
    // Reserved keyword enum
    enum res_t : uint8_t { res_none = 0, res_of, res_if, res_else, res_upon, res_where, res_catch, res_do, res_then, res_until, res_return, res_yield, res_break, res_continue, res_at, res_var, res_set, res_and, res_or, res_not, res_max };
    struct scan_reserved_t
    {
        constexpr const char* operator[](uint8_t res) const
        {
            constexpr const char* strings[] =
                { "",      "of",  "if",  "else",  "upon",  "where",  "catch",  "do",  "then",  "until",  "return",  "yield",  "break",  "continue",  "at",  "var",  "set",  "and", "or", "not", 0};
            static_assert(sizeof(strings) / sizeof(char*) == res_max + 1, "scan_reserved string table must match res_t");
            return strings[res];
        }
    };
}