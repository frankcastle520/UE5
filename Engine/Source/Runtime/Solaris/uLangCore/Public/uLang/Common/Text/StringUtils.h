// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Containers/Array.h"
#include "uLang/Common/Text/UTF8String.h"
#include "uLang/Common/Text/TextRange.h"
namespace uLang
{
    class CUTF8StringView;

    ULANGCORE_API bool SplitString(const char* ToSplit, const char* Delim, CUTF8StringView& OutLeft, CUTF8StringView& OutRight);
    ULANGCORE_API bool SplitString(const char* ToSplit, const UTF8Char Delim, CUTF8StringView& OutLeft, CUTF8StringView& OutRight, bool bReverse = false);
    ULANGCORE_API CUTF8String ToUpper(const CUTF8StringView& Str);

    /**
     * Converts an index-based range of Unicode code points (start, end) to a row/column-based range (start row/column, end row/column).
     *
     * @param IndexRange    The range of Unicode code points to convert.
     * @param SourceText    The full source text that corresponds to the range about to be converted.
     *
     * @return              A row-column range that is equivalent to the index-based range.
     */
    ULANGCORE_API STextRange IndexRangeToTextRange(const SIdxRange& IndexRange, const CUTF8StringView& SourceText);

    ULANGCORE_API int32_t CountNumLeadingNewLines(const CUTF8StringView& Text);

    ULANGCORE_API int32_t CountNumTrailingNewLines(const CUTF8StringView& Text);

    ULANGCORE_API bool HasTrailingNewLine(const CUTF8StringView& Text);

    ULANGCORE_API int32_t GetCurrentIndentationLevel(const int32_t IndentSize, const CUTF8StringView& Text);

    ULANGCORE_API CUTF8String FindLongestCommonPrefix(const TArray<CUTF8String> Strings);
}
