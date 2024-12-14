// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Diagnostics/Glitch.h"
#include "uLang/Common/Text/UTF8String.h"
#include "uLang/Syntax/VstNode.h"
#include "uLang/Semantics/Expression.h"

namespace uLang
{

namespace 
{
    constexpr SDiagnosticInfo DiagnosticInfos[] =
    {
    #define VISIT_DIAGNOSTIC(Code, Severity, EnumName, Description) {Code, EDiagnosticSeverity::Severity, Description},
        VERSE_ENUM_DIAGNOSTICS(VISIT_DIAGNOSTIC)
    #undef VISIT_DIAGNOSTIC
    };

    bool VerifyDiagnostics()
    {
        // Verify that the diagnostics are enumerated in ascending order by code, and that no code is duplicated.
        int32_t PreviousDiagnosticCode = INT32_MIN;
        for (const SDiagnosticInfo& DiagnosticInfo : DiagnosticInfos)
        {
            if (DiagnosticInfo.ReferenceCode <= PreviousDiagnosticCode)
            {
                ULANG_ERRORF(
                    "Diagnostic with code %i occurs following diagnostic with same or higher code %i. "
                    "Diagnostics must be in ascending order by code.",
                    DiagnosticInfo.ReferenceCode,
                    PreviousDiagnosticCode);
                return false;
            }
            PreviousDiagnosticCode = DiagnosticInfo.ReferenceCode;
        }
        return true;
    }
}

const SDiagnosticInfo& GetDiagnosticInfo(EDiagnostic ResultId)
{
    if(size_t(ResultId) >= ULANG_COUNTOF(DiagnosticInfos))
    {
        ULANG_ERRORF("Invalid diagnostic enum %zu", ResultId);
        ULANG_UNREACHABLE();
    }
    else
    {
        return DiagnosticInfos[size_t(ResultId)];
    }
}

EDiagnostic GetDiagnosticFromReferenceCode(uint16_t ReferenceCode)
{
    static const bool bVerifiedDiagnosticInfos = VerifyDiagnostics();
    ULANG_ENSUREF(bVerifiedDiagnosticInfos, "Diagnostics failed verification.");

    // The DiagnosticInfos array is sorted by reference code (see VerifyDiagnostics), so we can
    // look up the index in the array by binary search.
    uint32_t MinIndex = 0;
    uint32_t MaxIndex = ULANG_COUNTOF(DiagnosticInfos) - 1;
    while (MaxIndex >= MinIndex)
    {
        const uint32_t MidIndex = (MinIndex + MaxIndex) / 2;
        if (DiagnosticInfos[MidIndex].ReferenceCode < ReferenceCode)
        {
            MinIndex = MidIndex + 1;
        }
        else if (DiagnosticInfos[MidIndex].ReferenceCode > ReferenceCode)
        {
            MaxIndex = MidIndex - 1;
        }
        else
        {
            return EDiagnostic(MidIndex);
        }
    };
    ULANG_ERRORF("Failed to find diagnostic matching reference code %u", ReferenceCode);
    ULANG_UNREACHABLE();
}

SGlitchResult::SGlitchResult(EDiagnostic ResultId)
    : _Id(ResultId)
    , _Message(GetDiagnosticInfo(ResultId).Description)
{
}

SGlitchLocus::SGlitchLocus(const Verse::Vst::Node* VstNode)
    : _SnippetPath(VstNode ? VstNode->GetSnippetPath() : CUTF8String::GetEmpty())
    , _Range(VstNode ? VstNode->Whence() : STextRange())
    , _ResultPos(_Range.GetEnd())
    , _VstIdentifier(uintptr_t(VstNode))
{
}

SGlitchLocus::SGlitchLocus(const CAstNode* AstNode)
    : SGlitchLocus(AstNode ? AstNode->GetMappedVstNode() : (const Verse::Vst::Node*)nullptr)
{
    // TODO: Fill in _MemberInfo
}

CUTF8String SGlitchLocus::AsFormattedString() const
{
    return CUTF8String(
        "%s(%d,%d, %d,%d)",
        _SnippetPath.AsCString(),
        _Range.BeginRow() + 1, _Range.BeginColumn() + 1,
        _Range.EndRow() + 1, _Range.EndColumn() + 1);
}

CUTF8String SGlitch::FormattedString(const char* Message, const char* Path, const STextRange& Range, EDiagnosticSeverity Severity, EDiagnostic Diagnostic)
{
    const char* Category = "Verse compiler";
    bool bUseRefCode = Diagnostic != EDiagnostic::Ok;

    switch (Severity)
    {
        case EDiagnosticSeverity::Info: Category = "Verse compiler info"; break;
        case EDiagnosticSeverity::Warning: Category = "Verse compiler warning"; break;
        case EDiagnosticSeverity::Error: Category = "Verse compiler error"; break;
        case EDiagnosticSeverity::Ok: bUseRefCode = false; break;
        default: ULANG_UNREACHABLE();
    };

    return bUseRefCode
        ? CUTF8String(
            "%s(%d,%d, %d,%d): %s V%d: %s",
            Path,
            Range.BeginRow() + 1, Range.BeginColumn() + 1,
            Range.EndRow() + 1, Range.EndColumn() + 1,
            Category,
            GetDiagnosticInfo(Diagnostic).ReferenceCode,
            Message)
        : CUTF8String(
            "%s(%d,%d, %d,%d): %s: %s",
            Path,
            Range.BeginRow() + 1, Range.BeginColumn() + 1,
            Range.EndRow() + 1, Range.EndColumn() + 1,
            Category,
            Message);
}

TOptional<int32_t> ScanToRowCol(CUTF8StringView const& Source, const STextPosition& Position)
{
    // Scan to row, col
    uint32_t CurRow = 0;
    uint32_t CurCol = 0;
    const UTF8Char* Ch = Source._Begin;
    while (Ch < Source._End && (CurRow < Position._Row || (CurRow == Position._Row && CurCol < Position._Column)))
    {
        if (*Ch == '\n')
        {
            ++Ch;
            ++CurRow;
            CurCol = 0;
        }
        else if (*Ch == '\r')
        {
            ++Ch;
            ++CurRow;
            CurCol = 0;

            if (*Ch == '\n')
            {
                ++Ch;
            }
        }
        else
        {
            ++Ch;
            ++CurCol;
        }
    }

    if (CurRow == Position._Row && CurCol == Position._Column)
    {
        return int32_t(Ch - Source._Begin);
    }
    else
    {
        return TOptional<int32_t>();
    }
}

CUTF8StringView TextRangeToStringView(CUTF8StringView const& Source, STextRange const& Range)
{
    ULANG_ASSERT(Range.IsOrdered());
    TOptional<int32_t> BeginOffset = ScanToRowCol(Source, Range.GetBegin());
    TOptional<int32_t> EndOffset = ScanToRowCol(Source, Range.GetEnd());

    if (BeginOffset && EndOffset)
    {
        ULANG_ASSERT(BeginOffset.GetValue() <= EndOffset.GetValue());
        return Source.SubView(BeginOffset.GetValue(), EndOffset.GetValue() - BeginOffset.GetValue());
    }
    else
    {
        return CUTF8StringView();
    }
}

}  // namespace uLang