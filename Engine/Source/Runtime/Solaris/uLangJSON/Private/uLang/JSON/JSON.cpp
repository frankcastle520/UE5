// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/JSON/JSON.h"
#include "uLang/Common/Text/UTF8StringBuilder.h"

namespace uLang
{

CUTF8String EscapeJSON(const UTF8Char Ch)
{
    switch (Ch)
    {
        case '\b':
            return "\\b";
        case '\f':
            return "\\f";
        case '\n':
            return "\\n";
        case '\r':
            return "\\r";
        case '\t':
            return "\\t";
        case '\"':
            return "\\\"";
        case '\\':
            return "\\\\";
        default:
            if (Ch < 0x20u)
            {
                return CUTF8String("\\u%04X", Ch);
            }
            else
            {
                CUTF8StringBuilder StringBuilder;
                StringBuilder.Append(Ch);
                return StringBuilder.MoveToString();
            }
            break;
    }
}

CUTF8String EscapeJSON(const CUTF8StringView& RawText)
{
    CUTF8StringBuilder EscapedText;
    for (const UTF8Char* Ch = RawText._Begin; Ch < RawText._End; ++Ch)
    {
        switch (*Ch)
        {
        case '\b': EscapedText.Append("\\b"); break;
        case '\f': EscapedText.Append("\\f"); break;
        case '\n': EscapedText.Append("\\n"); break;
        case '\r': EscapedText.Append("\\r"); break;
        case '\t': EscapedText.Append("\\t"); break;
        case '\"': EscapedText.Append("\\\""); break;
        case '\\': EscapedText.Append("\\\\"); break;
        default: 
            if (*Ch < 0x20u)
            {
                EscapedText.AppendFormat("\\u%04X", *Ch);
            }
            else
            {
                EscapedText.Append(*Ch);
            }
            break;
        }
    }
    return EscapedText.MoveToString();
}

} // namespace uLang
