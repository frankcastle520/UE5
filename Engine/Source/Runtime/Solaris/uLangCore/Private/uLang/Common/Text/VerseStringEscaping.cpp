// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Common/Text/VerseStringEscaping.h"
#include "uLang/Common/Text/UTF8String.h"
#include "uLang/Common/Text/UTF8StringBuilder.h"
#include "uLang/Common/Text/UTF8StringView.h"

uLang::CUTF8String uLang::VerseStringEscaping::EscapeString(const uLang::CUTF8StringView& StringView)
{
    CUTF8StringBuilder EscapedText;
    for (const UTF8Char* Ch = StringView._Begin; Ch < StringView._End; ++Ch)
    {
        switch (*Ch)
        {
        case '\t': EscapedText.Append("\\t"); break;
        case '\n': EscapedText.Append("\\n"); break;
        case '\r': EscapedText.Append("\\r"); break;
        case '"':  EscapedText.Append("\\\""); break;
        case '#':  EscapedText.Append("\\#"); break;
        case '&':  EscapedText.Append("\\&"); break;
        case '\'': EscapedText.Append("\\'"); break;
        case '<':  EscapedText.Append("\\<"); break;
        case '>':  EscapedText.Append("\\>"); break;
        case '\\': EscapedText.Append("\\\\"); break;
        case '{':  EscapedText.Append("\\{"); break;
        case '}':  EscapedText.Append("\\}"); break;
        case '~':  EscapedText.Append("\\~"); break;
        default:
            if (*Ch < 0x20u || *Ch >= 0x7F)
            {
                EscapedText.AppendFormat("{0o%02x}", *Ch);
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
