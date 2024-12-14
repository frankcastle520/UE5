// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Parser/ParserPass.h"
#include "uLang/Common/Common.h"
#include "uLang/CompilerPasses/CompilerTypes.h" // for SProgramContext, SBuildContext, etc.
#include "uLang/Common/Misc/Optional.h"
#include "uLang/Common/Misc/Union.h"
#include "uLang/Common/Text/Unicode.h"
#include "uLang/Common/Text/StringUtils.h"
#include "uLang/Parser/VerseGrammar.h"


// TODO: (yiliang.siew) Should just fix these warnings in `VerseGrammar.h`.
#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wimplicit-fallthrough"
    #pragma clang diagnostic ignored "-Wparentheses"
    #pragma clang diagnostic ignored "-Wswitch-enum"
    #pragma clang diagnostic ignored "-Wswitch-default"
#endif
#ifdef __GNUC__
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
    #pragma GCC diagnostic ignored "-Wparentheses"
    #pragma GCC diagnostic ignored "-Wswitch-enum"
    #pragma GCC diagnostic ignored "-Wswitch-default"
#endif

#include "uLang/Syntax/vsyntax_types.h"
#include "uLang/Syntax/VstNode.h"

namespace uLang
{
    using namespace Verse::Vst;
    using SLocus = Verse::SLocus;  // Verse::Grammar::snippet also stores text start and end character/index
    
    template<typename T>
    using TSRef   = uLang::TSRef<T>;

    using char8  = Verse::Grammar::char8;
    using char32 = Verse::Grammar::char32;
    using nat    = Verse::Grammar::nat;
    using nat8   = Verse::Grammar::nat8;


    //=================================================================================================
    /// Storage for an accumulated capture of source text from a parse operation.
    struct generate_vst_capture
    {
        /// The string being captured thus far.
        CUTF8StringBuilder String;

        /// The nodes that have been created as part of the string being captured above.
        TArray<TSRef<Node>> Nodes;  // TSRefArray<Node> seems to give memory leaks during Linux ASAN/UBSAN tests
    };

    //=================================================================================================
    struct generate_common
    {
        // Common Types.
        using syntax_t   = TSRef<Node>;
        using syntaxes_t = TSRefArray<Node>;   // Was uLang::TArray<syntax_t>
        using error_t    = TSPtr<SGlitch>;     // Must use TSPtr<> rather than TSRef<> since Verse::Grammar::result<> needs default constructor
        using capture_t  = generate_vst_capture;
    };

    //=================================================================================================
    struct generate_vst : public Verse::Grammar::generate<generate_common>
    {
        enum class parse_behaviour : int8_t
        {
            ParseAll,
            ParseNoComments  // Allows for a slightly more optimized parse by skipping comments.
        };

        using block_t = Verse::Grammar::block<syntaxes_t, capture_t>;
        using result_t   = Verse::Grammar::result<syntax_t, error_t>;
        using token      = Verse::Grammar::token;
        using snippet    = Verse::Grammar::snippet;
        using text       = Verse::Grammar::text;
        using place      = Verse::Grammar::place;
        using mode       = Verse::Grammar::mode;

        //-------------------------------------------------------------------------------------------------
        generate_vst(const TSRef<uLang::CDiagnostics>& Diagnostics, const CUTF8String& SnippetPath, const parse_behaviour ParseBehaviour)
            : _Diagnostics(Diagnostics), _SnippetPath(SnippetPath), _ParseBehaviour(ParseBehaviour)
        {}

        //-------------------------------------------------------------------------------------------------
        static void SetClausePunctuation(const block_t& InBlock, Clause& InClause)
        {
            switch (InBlock.Punctuation)
            {
                case Verse::Grammar::punctuation::Colon:
                    InClause.SetPunctuation(Clause::EPunctuation::Colon);
                    // Force a newline after the clause if it doesn't have one since otherwise this
                    // is otherwise invalid syntax.
                    if (!InClause.HasNewLineAfter())
                    {
                        InClause.SetNewLineAfter(true);
                    }
                    break;
                case Verse::Grammar::punctuation::Braces:
                    InClause.SetPunctuation(Clause::EPunctuation::Braces);
                    break;
                case Verse::Grammar::punctuation::Ind:
                    InClause.SetPunctuation(Clause::EPunctuation::Indentation);
                    break;
                case Verse::Grammar::punctuation::Parens:
                    ULANG_FALLTHROUGH;
                case Verse::Grammar::punctuation::Brackets:
                    ULANG_FALLTHROUGH;
                case Verse::Grammar::punctuation::AngleBrackets:
                    ULANG_FALLTHROUGH;
                case Verse::Grammar::punctuation::Qualifier:
                    ULANG_FALLTHROUGH;
                case Verse::Grammar::punctuation::Dot:
                    ULANG_FALLTHROUGH;
                case Verse::Grammar::punctuation::None:
                    ULANG_FALLTHROUGH;
                default:
                    InClause.SetPunctuation(Clause::EPunctuation::Unknown);
                    break;
            }
        }

        //-------------------------------------------------------------------------------------------------
        static SLocus CombineLocus(const syntaxes_t& Nodes)
        {
            if (ULANG_ENSUREF(Nodes.IsFilled(), "No syntax nodes - cannot compute combined text range."))
            {
                SLocus Whence = Nodes[0]->Whence();
                for (int32_t Index = 1; Index < Nodes.Num(); ++Index)
                {
                    Whence |= Nodes[Index]->Whence();
                }
                return Whence;
            }

            return SLocus();
        }

        //-------------------------------------------------------------------------------------------------
        static SLocus CombineLocus(const NodeArray& Nodes)
        {
            if (ULANG_ENSUREF(Nodes.IsFilled(), "No syntax nodes - cannot compute combined text range."))
            {
                SLocus Whence = Nodes[0]->Whence();
                for (int32_t Index = 1; Index < Nodes.Num(); ++Index)
                {
                    Whence |= Nodes[Index]->Whence();
                }
                return Whence;
            }

            return SLocus();
        }

        //-------------------------------------------------------------------------------------------------
        static SLocus BlockElementsLocus(const block_t& Block)
        {
            return Block.Elements.IsFilled()
                ? CombineLocus(Block.Elements)
                : AsLocus(Block.BlockSnippet);
        }

        //-------------------------------------------------------------------------------------------------
        // Should have rest of system use `token` or more generic `nat8` since that is what is used internally.
        // Bridge between old and new parser. New parser has more information that isn't reflected in old system so should refactor.
        static vsyntax::res_t TokenToRes(const block_t& Block)
        {
            // Ensures it is null terminated
            const CUTF8String TokenStr(AsStringView(Block.Token));

            switch (Token8(TokenStr))
            {
                case token(u8""):
                    switch(Block.Punctuation)
                    {
                        case Verse::Grammar::punctuation::None:           return vsyntax::res_none;
                        case Verse::Grammar::punctuation::Parens:         return vsyntax::res_of;
                        case Verse::Grammar::punctuation::Brackets:       return vsyntax::res_of;
                        case Verse::Grammar::punctuation::Braces:         return vsyntax::res_none;  // `res_do` would seem more appropriate but using `res_none` for legacy code
                        case Verse::Grammar::punctuation::Colon:          return vsyntax::res_none;  // `res_do` would seem more appropriate but using `res_none` for legacy code
                        case Verse::Grammar::punctuation::AngleBrackets:  return vsyntax::res_none;
                        case Verse::Grammar::punctuation::Qualifier:      return vsyntax::res_none;
                        case Verse::Grammar::punctuation::Dot:            return vsyntax::res_none;
                        case Verse::Grammar::punctuation::Ind:            return vsyntax::res_none;
                        default:  return vsyntax::res_none;
                    }
                case token(u8"of"):   return vsyntax::res_of;
                case token(u8"do"):   return vsyntax::res_do;
                case token(u8"if"):   return vsyntax::res_if;
                case token(u8"else"): return vsyntax::res_else;
                case token(u8"then"): return vsyntax::res_then;
                default:              return vsyntax::res_none;
            }
        }

        //===============================================================================
        // Manipulation operations we must expose to parser.

        //-------------------------------------------------------------------------------------------------
        template<typename... MessageFragmentsType>
        error_t Err(const snippet& Location, const char* IssueIdCStr, MessageFragmentsType... MessageFragments) const
        {
            CUTF8StringBuilder Msg;
            Msg.Append("vErr:");
            Msg.Append(IssueIdCStr);
            Msg.Append(": ");

            // Concatenate message
            text MessageFragmentsArray[] = { MessageFragments... };
            for (size_t FragmentIndex = 0; FragmentIndex < sizeof...(MessageFragments); ++FragmentIndex)
            {
                const text& Fragment = MessageFragmentsArray[FragmentIndex];
                Msg.Append(AsStringView(Fragment));
            }

            SGlitchLocus Locus(_SnippetPath, AsLocus(Location), (uintptr_t)(nullptr));
            SGlitchResult Result(uLang::EDiagnostic::ErrSyntax_InternalError, Msg.MoveToString());
            return TSRef<SGlitch>::New(Move(Result), Move(Locus));
        }


        //-------------------------------------------------------------------------------------------------
        static void SyntaxesAppend(syntaxes_t& As, const syntax_t& A)
        {
            As.Push(A);
        }

        //-------------------------------------------------------------------------------------------------
        static nat SyntaxesLength(const syntaxes_t& As)
        {
            return As.Num();
        }

        //-------------------------------------------------------------------------------------------------
        static syntax_t SyntaxesElement(const syntaxes_t& As, nat i)
        {
            return As[static_cast<int32_t>(i)];
        }

        //-------------------------------------------------------------------------------------------------
        static void CaptureAppend(capture_t& S, const capture_t& T)
        {
            S.String.Append(T.String);
            for (const TSRef<Node>& Ref : T.Nodes)
            {
                TSRef<Node> NewRef = Ref;
                S.Nodes.Add(NewRef);
            }
        }

        //-------------------------------------------------------------------------------------------------
        static nat CaptureLength(const capture_t& S)
        {
            return S.String.ByteLen();
        }

        //-------------------------------------------------------------------------------------------------
        static char8 CaptureElement(const capture_t& S, nat i)
        {
            return S.String[static_cast<int32_t>(i)];
        }

        //-------------------------------------------------------------------------------------------------
        result_t Num(const snippet& Snippet, text Digits, text Fraction, text ExponentSign, text Exponent) const
        {
            CUTF8StringBuilder NumText;
            NumText.EnsureAllocatedExtra(
                static_cast<size_t>(3)  // extra space
                + Verse::Grammar::Length(Digits)
                + Verse::Grammar::Length(Fraction)
                + Verse::Grammar::Length(ExponentSign)
                + Verse::Grammar::Length(Exponent));

            NumText.Append(AsStringView(Digits));

            const bool bHasFraction = Verse::Grammar::Length(Fraction) > 0;
            if (bHasFraction)
            {
                NumText.Append('.');
                NumText.Append(AsStringView(Fraction));
            }

            const bool bHasExponent = Verse::Grammar::Length(Exponent) > 0;
            if (bHasExponent)
            {
                NumText.Append('e');
                NumText.Append(AsStringView(ExponentSign));
                NumText.Append(AsStringView(Exponent));
            }

            // Number literal
            if (!bHasFraction && !bHasExponent)
            {
                // It is an integer
                return TSRef<IntLiteral>::New(NumText, AsLocus(Snippet));
            }

            // It is a 64-bit float
            return TSRef<FloatLiteral>::New(NumText, FloatLiteral::EFormat::F64, AsLocus(Snippet));
        }

        //-------------------------------------------------------------------------------------------------
        result_t NumHex(const snippet& Snippet, text Digits) const
        {
            CUTF8StringBuilder HexString;
            HexString.Append("0x");
            HexString.Append(AsStringView(Digits));

            return TSRef<IntLiteral>::New(HexString, AsLocus(Snippet));
        }

        //-------------------------------------------------------------------------------------------------
        result_t Units(const snippet& Snippet, const syntax_t& Num, text Units) const
        {
            const SLocus Whence = AsLocus(Snippet);

            // Only called if Units has 1 or more characters
            switch (Units[0])
            {
                case 'f':
                {
                    CUTF8StringView FloatFormatSuffix = AsStringView(Units);
                    // advance the beginning past the 'f' format character
                    FloatFormatSuffix._Begin++;

                    // with an 'f' suffix we require Digits after the 'f'
                    if (FloatFormatSuffix.IsEmpty())
                    {
                        return NewGlitch(Whence, EDiagnostic::ErrSyntax_UnrecognizedFloatBitWidth);
                    }

                    // is the remaining suffix all Digits?
                    bool bIsAllDigits = true;
                    const UTF8Char* ChU8 = FloatFormatSuffix._Begin;
                    while (ChU8 != FloatFormatSuffix._End)
                    {
                        if (!CUnicode::IsDigitASCII(*ChU8))
                        {
                            bIsAllDigits = false;
                            break;
                        }

                        ChU8++;
                    }

                    if (!bIsAllDigits)
                    {
                        return NewGlitch(Whence,
                            EDiagnostic::ErrSyntax_Unimplemented,
                            CUTF8String(
                                "Unrecognized suffix on number literal `%s%.*s`",
                                Verse::PrettyPrintVst(Num).AsCString(),
                                Verse::Grammar::Length(Units),
                                Units.Start));
                    }

                    FloatLiteral::EFormat Format = FloatLiteral::EFormat::Unspecified;

                    // NOTE: Currently only 64 bit-floats are supported, but there are tests that test for 16/32 bit floating point literal parsing as well.
                    // TODO: (yiliang.siew) Implement quick-fix support for this and other trivial user-code problems in the Verse LSP. https://jira.it.epicgames.com/browse/SOL-3247
                    if (FloatFormatSuffix == "16")
                    {
                        Format = FloatLiteral::EFormat::F16;
                    }
                    else if (FloatFormatSuffix == "32")
                    {
                        Format = FloatLiteral::EFormat::F32;
                    }
                    else if (FloatFormatSuffix == "64")
                    {
                        Format = FloatLiteral::EFormat::F64;
                    }
                    else
                    {
                        return NewGlitch(Whence,
                            EDiagnostic::ErrSyntax_UnrecognizedFloatBitWidth,
                            CUTF8String(
                                "Unrecognized float literal bit width `%.*s` on number literal '%s'",
                                FloatFormatSuffix.ByteLen(),
                                FloatFormatSuffix._Begin,
                                Verse::PrettyPrintVst(Num).AsCString()));
                    }

                    CUTF8StringBuilder NumStr;

                    if (Num->GetElementType() == NodeType::FloatLiteral)
                    {
                        NumStr.Append(Num->As<FloatLiteral>().GetSourceText());
                    }
                    else if (Num->GetElementType() == NodeType::IntLiteral)
                    {
                        // A previously int literal will be converted to a float literal
                        NumStr.Append(Num->As<IntLiteral>().GetSourceText());
                    }
                    else
                    {
                        return NewGlitch(Whence,
                            EDiagnostic::ErrSyntax_UnrecognizedFloatBitWidth,
                            CUTF8String(
                                "float suffix `%.*s` on unexpected non-number `%s`",
                                FloatFormatSuffix.ByteLen(),
                                FloatFormatSuffix._Begin,
                                Verse::PrettyPrintVst(Num).AsCString()));
                    }

                    NumStr.Append(AsStringView(Units));
                    return TSRef<FloatLiteral>::New(NumStr, Format, Num->Whence() | Whence);
                }

                case 'r':
                    return NewGlitch(Whence,
                        EDiagnostic::ErrSyntax_Unimplemented,
                        CUTF8String("Rational number literal `%s%.*s` is not yet supported",
                            Verse::PrettyPrintVst(Num).AsCString(),
                            Verse::Grammar::Length(Units),
                            Units.Start));

                case 'c':
                    return NewGlitch(Whence,
                        EDiagnostic::ErrSyntax_Unimplemented,
                        CUTF8String("ASCII/UTF8 character uses `0o` as prefix followed by hexidecimal value - `%s%.*s` is not supported",
                            Verse::PrettyPrintVst(Num).AsCString(),
                            Verse::Grammar::Length(Units),
                            Units.Start));

                default:
                    // Units is unrecognized
                    return NewGlitch(Whence,
                        EDiagnostic::ErrSyntax_Unimplemented,
                        CUTF8String("Unrecognized suffix on number literal `%s%.*s`",
                            Verse::PrettyPrintVst(Num).AsCString(),
                            Verse::Grammar::Length(Units),
                            Units.Start));
            }
        }

        //-------------------------------------------------------------------------------------------------
        // Macro invocation m{a}, m(a){b}, etc
        //   - Macro: expr/identifier name of macro
        //   - Clause1: Usually (arguments) of macro when two+ clauses or do {body} if one clause
        //   - Clause2: Usually {body} of macro - including `then` clause in `if`
        //   - Clause3: Usually additional {body} of macro - such as `else` clause in `if`
        result_t Invoke(const snippet& Snippet, const syntax_t& MacroCommand, const block_t& Clause1, const block_t* Clause2, const block_t* Clause3) const
        {
            // Each clause block has context info:
            //   - token: optional token name before opening punctuation
            //   - punctuation: {None,Braces,Parens,Brackets,AngleBrackets,Qualifier,Dot,Colon,Ind}
            //   - form: {Commas,List}

            // Invoke() / Call() specifier key:
            // 
            //   macro0<spec2>()                  # Call(Call("macro0", "<spec2>), "()")
            //   macro1()<spec4>                  # Call(Call("macro1", "()"), "<spec4>")
            //   macro2<spec2>()<spec4>           # Call(Call(Call("macro2", "<spec2>), "()"), "<spec4>")
            //   macro3<spec3>{}                  # Invoke("macro3", "<spec3>{}")
            //   macro4{}<spec4>                  # Call(Invoke("macro3", "{}"), "<spec4>")
            //   macro5<spec3>{}<spec4>           # Call(Invoke("macro3", "<spec3>{}"), "<spec4>")
            //   macro6<spec2>(){}                # Invoke("macro3", "<spec2>()", "{}")
            //   macro7()<spec3>{}                # Invoke("macro3", "()", "<spec3>{}")
            //   macro8<spec2>()<spec3>{}         # Invoke("macro3", "<spec2>()", "<spec3>{}")
            //   macro9<spec2>()<spec3>{}<spec4>  # Call(Invoke("macro3", "<spec2>()", "<spec3>{}"), "<spec4>")
            // 
            // *Notes: `Call()` with angle brackets becomes `AppendSpecifier()`
            ClauseArray Clauses;
            Clauses.Reserve(3);

            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // Group first clause and process their specifiers
            // Append any specifiers
            if (Clause1.Specifiers.IsFilled())
            {
                // Rather than applying the specifiers to the block itself (or the whole macro
                // invocation), they are applied to the MacroCommand expression preceding it which
                // is usually an identifier.
                AppendSpecifiers(MacroCommand, Clause1.Specifiers);
            }
            TSRef<Clause> ArgClauseNode = TSRef<Clause>::New(
                uint8_t(TokenToRes(Clause1)), AsLocus(Clause1.BlockSnippet), AsClauseForm(Clause1));
            SetClausePunctuation(Clause1, *ArgClauseNode);
            ArgClauseNode->AppendChildren(Clause1.Elements);
            // For the cases of empty clauses, we still want to suffix trailing whitespace/comments to them.
            ProcessBlockPunctuationForClause(Clause1, ArgClauseNode);
            Clauses.Add(ArgClauseNode);

            // TODO: (yiliang.siew) This HACK is because the pretty-printer did not account for newlines before.
            // Therefore, we transfer any newlines before to the clause as a line after instead so that the pretty-printer
            // can understand whether vertical forms are desired.
            TransferFirstLeadingNewLineOfClauseMember(*ArgClauseNode, *ArgClauseNode);

            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // Group optional second clause
            if (Clause2)
            {
                // Append any specifiers
                if (Clause2->Specifiers.IsFilled())
                {
                    // Rather than applying the specifiers to the block itself (or the whole macro
                    // invocation), they are applied to the clause expression preceding it which
                    // is usually an identifier.
                    AppendSpecifiers(ArgClauseNode, Clause2->Specifiers);
                }

                vsyntax::res_t Reserved = TokenToRes(*Clause2);
                // Should essentially be the same as Clause1 above
                TSRef<Clause> DoClauseNode = TSRef<Clause>::New(
                    uint8_t(Reserved), AsLocus(Clause2->BlockSnippet), AsClauseForm(*Clause2));
                SetClausePunctuation(*Clause2, *DoClauseNode);
                DoClauseNode->AppendChildren(Clause2->Elements);
                ProcessBlockPunctuationForClause(*Clause2, DoClauseNode);
                // TODO: (yiliang.siew) This HACK is because the pretty-printer did not account for newlines before.
                // Therefore, we transfer any newlines before to the clause as a line after instead so that the pretty-printer
                // can understand whether vertical forms are desired. Here we transfer the newline to the clause directly preceding it.
                TransferFirstLeadingNewLineOfClauseMember(*DoClauseNode, *ArgClauseNode);

                Clauses.Add(DoClauseNode);
            }

            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // Group optional third clause
            if (Clause3)
            {
                // Append any specifiers
                if (Clause3->Specifiers.IsFilled())
                {
                    // Rather than applying the specifiers to the block itself (or the whole macro
                    // invocation), they are applied to the clause expression preceding it which
                    // is usually an identifier.
                    AppendSpecifiers(Clauses.Last(), Clause3->Specifiers);
                }

                // Should essentially be the same as Clause1 above
                TSRef<Clause> PostClauseNode = TSRef<Clause>::New(
                    uint8_t(TokenToRes(*Clause3)), AsLocus(Clause3->BlockSnippet), AsClauseForm(*Clause3));
                SetClausePunctuation(*Clause3, *PostClauseNode);
                PostClauseNode->AppendChildren(Clause3->Elements);
                ProcessBlockPunctuationForClause(*Clause3, PostClauseNode);

                // TODO: (yiliang.siew) This HACK is because the pretty-printer did not account for newlines before.
                // Therefore, we transfer any newlines before to the clause as a line after instead so that the pretty-printer
                // can understand whether vertical forms are desired. Here we transfer the newline to the clause directly preceding it.
                if (Clause2)
                {
                    TransferFirstLeadingNewLineOfClauseMember(*PostClauseNode, *Clauses[1]);
                }
                else
                {
                    TransferFirstLeadingNewLineOfClauseMember(*PostClauseNode, *ArgClauseNode);
                }

                // NOTE: (yiliang.siew) For `else` clauses, this helps to catch comments that lead the `else` token, such as:
                /*
                 * ```
                 * if (1 = 1):
                 *     4
                 * <#comment#>else:
                 *     7
                 * ```
                 *
                 */
                if (Clause3->Token == "else" && !Clause3->TokenLeading.String.IsEmpty())
                {
                    // TODO: (yiliang.siew) This is a little tricky because it's not clear how to handle newlines within the leading token punctuation appropriately in this case.
                    // We're just dealing with comments for now.
                    for (const TSRef<Node>& CurNode : Clause3->TokenLeading.Nodes)
                    {
                        if (CurNode->IsA<Comment>())
                        {
                            PostClauseNode->AppendPrefixComment(CurNode);
                        }
                    }
                }

                Clauses.Add(PostClauseNode);
            }

            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // Invocation of an expression other than an identifier
            SLocus Whence = AsLocus(Snippet);

            if (!MacroCommand->IsA<Identifier>())
            {
                return TSRef<Macro>::New(Whence, MacroCommand, Clauses);
            }


            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // Invocation of an identifier
            const Identifier&  MacroIdentifier = MacroCommand->As<Identifier>();
            const CUTF8String& MacroStr        = MacroIdentifier.GetSourceText();

            if (MacroStr == CUTF8StringView("stub"))
            {
                TSPtr<Placeholder> NewPlaceholderVstNode;

                if (Clauses.IsFilled() && Clauses[0]->GetChildren().IsFilled())
                {
                    CUTF8String tx = Verse::PrettyPrintVst(Clauses[0]->GetChildren()[0]);
                    NewPlaceholderVstNode = TSRef<Placeholder>::New(tx, Whence);
                }
                else
                {
                    NewPlaceholderVstNode = TSRef<Placeholder>::New(Whence);
                }

                return NewPlaceholderVstNode.AsRef();
            }

            if (MacroStr == CUTF8StringView("if"))
            {
                TSPtr<Clause> ConditionalClause;
                const TSRef<FlowIf> NewIfNode = TSRef<FlowIf>::New(Whence);
                const TSRef<Clause> IfIdentifierClause = TSRef<Clause>::New(MacroCommand->Whence(), Clause::EForm::Synthetic);
                NewIfNode->AddIfIdentifier(IfIdentifierClause);
                // We must also transfer the comments over from the `if` identifier to the newly-created clause.
                IfIdentifierClause->AppendPrefixComments(MacroCommand->GetPrefixComments());
                IfIdentifierClause->AppendPostfixComments(MacroCommand->GetPostfixComments());
                bool bConditionClause = false;
                bool bThenClause = false;
                bool bElseClause = false;

                for (const TSRef<Clause>& CurrentClause : Clauses)
                {
                    // Treat any clause after `else` encountered as unexpected otherwise handle known clause tags
                    vsyntax::res_t ClauseTag = bElseClause ? vsyntax::res_t::res_max : CurrentClause->GetTag<vsyntax::res_t>();
                    // Using `if` chain rather than `switch` to avoid static compilation error
                    if (ClauseTag == vsyntax::res_of)
                    {
                        // argument block - `if (block)` or `if of: block`
                        // - must be first block: error if condition block, then block or else block already encountered
                        // Multiple condition clause or condition clause after then block already prevented by syntax parser
                        NewIfNode->AddCondition(CurrentClause);
                        bConditionClause = true;
                    }
                    else if (ClauseTag == vsyntax::res_then)
                    {
                        // explicit `then` block - `if (condition[]) then: block` or without initial brackets `if: condition[] then: block`
                        // - error if no condition yet and no then block or else block already encountered
                        if (!bConditionClause)
                        {
                            // Missing condition [Could be moved to Semantic Analysis]
                            AppendGlitch(
                                CurrentClause->Whence(),
                                EDiagnostic::ErrSyntax_ExpectedIfCondition,
                                "Expected a condition block before `then` block while parsing `if`.");
                        }

                        if (bThenClause)
                        {
                            // Already present [Could be moved to Semantic Analysis]
                            AppendGlitch(
                                CurrentClause->Whence(),
                                EDiagnostic::ErrSyntax_UnexpectedClauseTag,
                                "Found more than one `then` block while parsing `if`.");
                        }

                        NewIfNode->AddBody(CurrentClause);
                        bThenClause = true;
                    }
                    else if (ClauseTag == vsyntax::res_none)
                    {
                        // main block - after initial brackets `if (condition[]): block` or without initial brackets `if: block`
                        // if no condition yet then condition block otherwise then block
                        // - error if then block or else block already encountered
                        if (!bConditionClause)
                        {
                            // Treat main block as a condition block
                            // Already having a `then` block should not be possible with a `none` tag

                            NewIfNode->AddCondition(CurrentClause);
                            bConditionClause = true;
                            continue;
                        }

                        if (!bThenClause)
                        {
                            // Treat main block as a `then` block
                            NewIfNode->AddBody(CurrentClause);
                            bThenClause = true;
                            continue;
                        }

                        // Both condition block and then block already present [seems impossible with syntax from parser]
                        AppendGlitch(
                            CurrentClause->Whence(),
                            EDiagnostic::ErrSyntax_UnexpectedClauseTag,
                            "Expected either condition block or then block to be unspecified though both are present while parsing `if`.");
                    }
                    else if (ClauseTag == vsyntax::res_else)
                    {
                        // `else` block
                        // - error if no condition yet and must be last block: no else block already encountered
                        if (!bConditionClause)
                        {
                            // Missing condition [Could be moved to Semantic Analysis]
                            AppendGlitch(
                                CurrentClause->Whence(),
                                EDiagnostic::ErrSyntax_ExpectedIfCondition,
                                "Expected a condition block before `else` block while parsing `if`.");
                        }

                        // If it is an `else if` then flatten it into this `if` as a multi-then clause `if`
                        if ((CurrentClause->GetChildCount() == 1) && CurrentClause->GetChildren()[0]->IsA<FlowIf>())
                        {
                            // NOTE: (yiliang.siew) We also have to transfer any comments here as part of the flattening process.
                            const TSRef<Node> FlowIfNode = CurrentClause->GetChildren()[0];
                            // NOTE: (yiliang.siew) This is the condition clause of the "else if" token.
                            if (FlowIfNode->GetChildCount() == 0)
                            {
                                AppendGlitch(CurrentClause->Whence(), EDiagnostic::ErrSyntax_ExpectedIfCondition, "Expected a condition block for an `else if` statement.");
                            }
                            else
                            {
                                const TSRef<Node> ClauseToTransferTo = FlowIfNode->GetChildren()[0];
                                Node::TransferPrefixComments(CurrentClause, ClauseToTransferTo);
                                Node::TransferPostfixComments(CurrentClause, ClauseToTransferTo);
                            }
                            // Note that any `if` children discovered here are already in the form desired, so
                            // just append them to the current `if` node. Note that the nested `if` may not have
                            // an `else` block itself so the flattened multi-`if` may also have no `else` block.
                            const TSRef<Node> NestedIf = CurrentClause->TakeChildAt(0);
                            Node::TransferChildren(NestedIf, NewIfNode);
                        }
                        else
                        {
                            // standard else block
                            NewIfNode->AddElseBody(CurrentClause);
                        }

                        bElseClause = true;
                    }
                    else
                    {
                        // Skip unexpected clause and accumulate error
                        AppendGlitch(
                            CurrentClause->Whence(),
                            EDiagnostic::ErrSyntax_UnexpectedClauseTag,
                            CUTF8String(
                                "Unexpected `%s` clause while parsing `if`.",
                                vsyntax::scan_reserved_t()[CurrentClause->GetTag<vsyntax::res_t>()]));
                    }
                }
                return NewIfNode;
            }
            return TSRef<Macro>::New(Whence, MacroCommand, Clauses);
        }

        //-------------------------------------------------------------------------------------------------
        result_t Native(const snippet& Snippet, const text& Name) const
        {
            return TSRef<Identifier>::New(AsStringView(Name), AsLocus(Snippet));
        }

        //-------------------------------------------------------------------------------------------------
        result_t Native(const snippet& Snippet, const char* NameCStr) const
        {
            return TSRef<Identifier>::New(CUTF8StringView(NameCStr), AsLocus(Snippet));
        }

        //-------------------------------------------------------------------------------------------------
        result_t Ident(const snippet& Snippet, const text& NameA, const text& NameB, const text& NameC) const
        {
            CUTF8StringBuilder Name;
            Name.EnsureAllocatedExtra((Verse::Grammar::Length(NameA) + Verse::Grammar::Length(NameB)) | Verse::Grammar::Length(NameC));
            Name.Append(AsStringView(NameA));
            Name.Append(AsStringView(NameB));
            Name.Append(AsStringView(NameC));

            return TSRef<Identifier>::New(Name, AsLocus(Snippet));
        }

        //-------------------------------------------------------------------------------------------------
        result_t QualIdent(const snippet& Snippet, const block_t& QualifierBlock, text Name) const
        {
            if (!QualifierBlock.PunctuationLeading.String.IsEmpty())
            {
                const int32_t NumLeadingNewLines = CountNumTrailingNewLines(QualifierBlock.PunctuationLeading.String.ToStringView());
                const TSRef<Node>& FirstNodeInQualifier = QualifierBlock.Elements.First();
                FirstNodeInQualifier->SetNumNewLinesBefore(FirstNodeInQualifier->NumNewLinesBefore() + NumLeadingNewLines);
                for (const TSRef<Node>& CurNode : QualifierBlock.PunctuationLeading.Nodes)
                {
                    if (CurNode->IsA<Comment>())
                    {
                        FirstNodeInQualifier->AppendPrefixComment(CurNode);
                    }
                }
            }

            if (!QualifierBlock.ElementsTrailing.String.IsEmpty() && !QualifierBlock.Elements.IsEmpty())
            {
                const int32_t NumTrailingNewLines = CountNumLeadingNewLines(QualifierBlock.ElementsTrailing.String.ToStringView());
                const TSRef<Node>& LastNodeInQualifier = QualifierBlock.Elements.Last();
                LastNodeInQualifier->SetNumNewLinesAfter(LastNodeInQualifier->NumNewLinesAfter() + NumTrailingNewLines);
                for (const TSRef<Node>& CurNode : QualifierBlock.ElementsTrailing.Nodes)
                {
                    if (CurNode->IsA<Comment>())
                    {
                        LastNodeInQualifier->AppendPostfixComment(CurNode);
                    }
                }
            }

            if (QualifierBlock.Form == Verse::Grammar::form::List && QualifierBlock.Elements.Num() > 1)
            {
                return NewGlitch(AsLocus(Snippet),
                    EDiagnostic::ErrSyntax_Unimplemented,
                    "Semicolons and newlines in qualified identifiers are not yet implemented.");
            }

            // Translate qualified identifiers to an identifier with the qualifiers as children.
            TSRef<Identifier> Result = TSRef<Identifier>::New(AsStringView(Name), AsLocus(Snippet));
            Result->AppendChildren(QualifierBlock.Elements);

            // NOTE: (yiliang.siew) Again, we're purposely re-jiggering the comments here from trailing the expression to leading the
            // identifier instead so that the pretty-printer will print things in the right order.
            if (!QualifierBlock.PunctuationTrailing.String.IsEmpty())
            {
                const int32_t NumLeadingNewLines = CountNumLeadingNewLines(QualifierBlock.ElementsTrailing.String.ToStringView());
                Result->SetNumNewLinesAfter(NumLeadingNewLines);
                for (const TSRef<Node>& CurNode : QualifierBlock.PunctuationTrailing.Nodes)
                {
                    if (CurNode->IsA<Comment>())
                    {
                        Result->AppendPrefixComment(CurNode);
                    }
                }
            }

            return Result;
        }

        //-------------------------------------------------------------------------------------------------
        result_t PrefixAttribute(const snippet& Snippet, const syntax_t& Attribute, const syntax_t& Base) const
        {
            PrependAttributeNode(Snippet, Attribute, Base);
            return Base;
        }

        //-------------------------------------------------------------------------------------------------
        result_t PostfixAttribute(const snippet& Snippet, const syntax_t& Base, const syntax_t& Attribute) const
        {
            return NewGlitch(AsLocus(Snippet),
                EDiagnostic::ErrSyntax_Unimplemented,
                "Postfixed attributes are not yet supported.");
        }

        //-------------------------------------------------------------------------------------------------
        // This is near the top-level entry point for parsing the entire snippet.
        TSRef<Clause> File(const block_t& Block) const
        {
            syntaxes_t Result = Block.Elements;
            // NOTE: (yiliang.siew) For any comments remaining that haven't been added, just add them as block-level comments
            // after everything else.
            // TODO: (yiliang.siew) This doesn't take newlines leading the nodes here into account, nor newlines between these nodes.
            for (const TSRef<Node>& TrailingNode : Block.ElementsTrailing.Nodes)
            {
                Result.Add(TrailingNode);
            }

            const TSRef<Clause> BlockAsClause = TSRef<Clause>::New(Result, BlockElementsLocus(Block), AsClauseForm(Block));

            return BlockAsClause;
        }

        //-------------------------------------------------------------------------------------------------
        // mode::Open   - Call function that cannot fail: Func(X) / Func of X
        // mode::Closed   - Call function that may fail:    Func[X] / Func at X
        // mode::With - Attach specifier to expression: Expr<specifier> / Expr with specifier
        // mode::None - error if instantiated
        result_t Call(const snippet& Snippet, mode Mode, const syntax_t& ReceiverSyntax, const block_t& CallBlock) const
        {
            // Re categorize Mode:with `<>` as syntax element with appended specifier
            if (Mode == mode::With)
            {
                AppendSpecifier(ReceiverSyntax, CallBlock);
                return ReceiverSyntax;
            }
            const SLocus Whence = AsLocus(Snippet);
            const bool bCanFail = (Mode == mode::Closed);  // Func[]

            // This handles adding comments for things like `F(G<#Comment#>) := 0`
            TSRefArray<Node> FinalCallBlockElements;
            bool bUseMutatedBlockElements = false;
            if (!CallBlock.ElementsTrailing.Nodes.IsEmpty())
            {
                const int32_t NumCallBlockElements = CallBlock.Elements.Num();
                FinalCallBlockElements.Reserve(NumCallBlockElements + CallBlock.ElementsTrailing.Nodes.Num());
                FinalCallBlockElements = CallBlock.Elements;
                // If there are no elements inside the call block (i.e. `(<#Comment#>)`), we'll add any trailing nodes as block-level items
                // regardless of what they are, since that is the most appropriate.
                if (NumCallBlockElements == 0)
                {
                    for (const TSRef<Node>& CurNode : CallBlock.ElementsTrailing.Nodes)
                    {
                        if (CurNode->IsA<Comment>())
                        {
                            FinalCallBlockElements.Add(CurNode);
                        }
                    }
                }
                // If there _are_other elements inside the call block and these are trailing it, we suffix them to the last node
                // in the call block. For now only comments are supported.
                else
                {
                    const TSRef<Node>& LastElementInCallBlock = CallBlock.Elements.Last();
                    for (const TSRef<Node>& CurNode : CallBlock.ElementsTrailing.Nodes)
                    {
                        if (CurNode->IsA<Comment>())
                        {
                            LastElementInCallBlock->AppendPostfixComment(CurNode);
                        }
                    }
                }
                bUseMutatedBlockElements = true;
            }

            // Is there a way in which CallBlock.Specifiers may be filled - and then processed?
            TSRef<Clause> ParametersClause(TSRef<Clause>::New(
                bUseMutatedBlockElements ? FinalCallBlockElements : CallBlock.Elements,
                AsLocus(CallBlock.BlockSnippet),
                AsClauseForm(CallBlock)));

            if (ReceiverSyntax->IsA<PrePostCall>())
            {
                // Member Access Chaining Transform
                // Convert from:
                //    call( PPC(a,'.',b,'.',c), arg1, arg2, arg3 )
                // to:
                //    PPC( a,'.', b, '.', c, clause(arg1, arg2, arg3))
                const auto& PpcChain = ReceiverSyntax.As<PrePostCall>();
                if (auto Aux = PpcChain->GetAux())
                {
                    auto ChildCount = PpcChain->GetChildCount();
                    ULANG_ASSERTF(ChildCount > 0, "Invalid PrePostCall");
                    auto LastChild = PpcChain->GetChildren()[ChildCount - 1];
                    LastChild->AppendAux(Aux->TakeChildren());
                    PpcChain->RemoveAux();
                }

                // NOTE: (yiliang.siew) Because any postfix comments were originally appended in `Trailing`, like for
                // the syntax:
                /*
                 * ```
                 * A.foo<# comment #>(1)
                 * ```
                 *
                 * The `PrePostCall` node of `foo` would have the `comment` suffixed to it, since this was done before
                 * we added the clause of the argument block (i.e. `(1)`). Therefore in order to maintain this correct
                 * association for the pretty-printer, we transfer any postfix comments from the `PrePostCall` node of
                 * `foo` (which encompasses the entirety of `foo(1)`) to be suffixed to the identifier of `foo` itself.
                 */
                Node::TransferPostfixComments(ReceiverSyntax, ReceiverSyntax->AccessChildren().Last());
                // Now that the comments have been transferred, we can append the parameters clause.
                PpcChain->AppendCallArgs(bCanFail, ParametersClause);
                PpcChain->CombineWhenceWith(Whence);

                return PpcChain;
            }
            else
            {
                const auto NewCall = TSRef<PrePostCall>::New(Whence | ReceiverSyntax->Whence());
                NewCall->AppendChild(ReceiverSyntax);
                NewCall->AppendCallArgs(bCanFail, ParametersClause);
                
                return NewCall;
            }
        }

        //-------------------------------------------------------------------------------------------------
        result_t Parenthesis(const block_t& Block) const
        {
            return BlockAsSingleExpression(Block);
        }

        //-------------------------------------------------------------------------------------------------
        result_t Char8(const snippet& Snippet, char8 Char8) const
        {
            return TSRef<CharLiteral>::New(
                CUTF8StringView((UTF8Char*)&Char8, (UTF8Char*)&Char8 + 1),  //AsStringView(Snippet.Text),
                CharLiteral::EFormat::UTF8CodeUnit,
                AsLocus(Snippet));
        }

        //-------------------------------------------------------------------------------------------------
        result_t Char32(const snippet& Snippet, char32 Char32, bool bCode, bool bBackslash) const
        {
            // $Revisit - could store predetermined Char32, bCode, and bBackslash
            SUTF8CodePoint CodePoint = CUnicode::EncodeUTF8(Char32);
            return TSRef<CharLiteral>::New(
                CUTF8StringView(CodePoint.Units, CodePoint.Units + CodePoint.NumUnits),
                (!bCode && CodePoint.NumUnits == 1)
                    ? CharLiteral::EFormat::UTF8CodeUnit
                    : CharLiteral::EFormat::UnicodeCodePoint,
                AsLocus(Snippet));
        }

        //-------------------------------------------------------------------------------------------------
        result_t Path(const snippet& Snippet, text Value) const
        {
            return TSRef<PathLiteral>::New(AsStringView(Value), AsLocus(Snippet));
        }

        //-------------------------------------------------------------------------------------------------
        result_t Escape(const snippet& Snippet, const syntax_t& Escaped) const
        {
            return TSRef<Verse::Vst::Escape>::New(AsLocus(Snippet), Escaped);
        }

        //-------------------------------------------------------------------------------------------------
        // Literal string span within quoted string or markup
        result_t StringLiteral(const snippet& Snippet, const capture_t& String) const
        {
            // Note that Snippet is just the string - and not any surrounding double quotes.
            return TSRef<Verse::Vst::StringLiteral>::New(AsLocus(Snippet), String.String);
        }

        //-------------------------------------------------------------------------------------------------
        // Form string from StringLiteral, StringInterpolate
        result_t String(const snippet& Snippet, const syntaxes_t& Splices) const
        {
            const int32_t SpliceNum = Splices.Num();

            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // Empty string
            if (SpliceNum == 0)
            {
                return TSRef<Verse::Vst::StringLiteral>::New(AsLocus(Snippet), "");
            }

            // Note that Snippet includes any surrounding double quotes so crop so that it is similar to `StringLiteral()`
            snippet UnquotedSnipppet = CropSnippet1(Snippet);
            SLocus  UnquotedLocus = AsLocus(UnquotedSnipppet);

            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // Simple string literal or single interpolated value like "{SomeInt}"
            if (SpliceNum == 1)
            {
                if(Splices[0]->IsA<Verse::Vst::StringLiteral>())
                {
                    return Splices[0];
                }
                else
                {
                    // Wrap in a InterpolatedString node so extra processing can be done on it
                    const TSRef<InterpolatedString> InterpolatedStringNode = TSRef<InterpolatedString>::New(UnquotedLocus, AsStringView(UnquotedSnipppet.Text));
                    InterpolatedStringNode->AppendChild(Splices[0]);
                    return InterpolatedStringNode;
                }
            }
            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // Interpolated string
            result_t CallResult = Call(UnquotedSnipppet, mode::Open, TSRef<Identifier>::New(ConcatenateIdentifierText, AsLocus0(UnquotedSnipppet)), block_t{UnquotedSnipppet, Splices});
            if (!CallResult)
            {
                return CallResult;
            }

            // Force the clause for the interpolated string to have no semicolons or newlines, since that would not be valid syntax anyway.
            const Node* PPC = CallResult->Get();
            if (PPC->IsA<PrePostCall>() && PPC->GetChildren()[1]->IsA<Verse::Vst::Clause>())
            {
                PPC->GetChildren()[1]->As<Verse::Vst::Clause>().SetForm(Verse::Vst::Clause::EForm::NoSemicolonOrNewline);
            }

            // Wrap in a InterpolatedString node so extra processing can be done on it
            const TSRef<InterpolatedString> InterpolatedStringNode = TSRef<InterpolatedString>::New(UnquotedLocus, AsStringView(UnquotedSnipppet.Text));
            InterpolatedStringNode->AppendChild(*CallResult);

            return InterpolatedStringNode;

        }

        //-------------------------------------------------------------------------------------------------
        // Interpolation expression within quoted string or markup
        result_t StringInterpolate(const snippet& Snippet, place Place, bool bBrace, const block_t& Block) const
        {
            if (Place == place::String)
            {
                // Can make different call than `ToString()` here if needed.
                //if (Block.Elements.IsFilled() && Block.Elements.Last()->IsA<CharLiteral>() && Block.Elements.Last()->As<CharLiteral>()._Format == CharLiteral::EFormat::UnicodeCodePoint)
                //{
                //    // Cannot use `ToString` because it conflicts with other types via overloading.
                //    return Call(Snippet, mode::Open, TSRef<Identifier>::New("ToStringFromChar32", AsLocus0(Snippet)), Block);
                //}
                // Generate conversion call here:
                return Call(Snippet, mode::Open, TSRef<Identifier>::New(ToStringIdentifierText, AsLocus0(Snippet)), Block);
            }

            if (Place != place::Content)
            {
                return NewGlitch(
                    AsLocus(Snippet),
                    EDiagnostic::ErrSyntax_Unimplemented,
                    CUTF8String(
                        "`%.*s` %s is not supported here",
                        Verse::Grammar::Length(Snippet.Text),
                        Snippet.Text.Start,
                        bBrace ? "interpolation" : "markup"));
            }

            // Seems like markup
            return Call(Snippet, mode::Open, TSRef<Identifier>::New("ToMarkup", AsLocus0(Snippet)), Block);
        }

        //-------------------------------------------------------------------------------------------------
        // Span of text whose meaning is defined by place
        void Text(capture_t& Capture, const snippet& Snippet, place Place) const
        {
            Capture.String.Append(AsStringView(Snippet.Text));
        }

        //-------------------------------------------------------------------------------------------------
        // Backslash in string or markup like \r
        void StringBackslash(capture_t& Capture, const snippet& Snippet, place Place, char8 Backslashed) const
        {
            if (Place == place::Content || Place == place::String)
            {
                // Pass through backslashed control characters as-is.
                char8 Char8 = Backslashed == 'n' ? '\n' : Backslashed == 'r' ? '\r' : Backslashed == 't' ? '\t' : Backslashed;

                Capture.String.Append(Char8);
            }
            else
            {
                // Keep the escape sequence
                Capture.String.Append(AsStringView(Snippet.Text));
            }
        }

        //-------------------------------------------------------------------------------------------------
        // [MaxVerse] Form markup content from StringLiteral, StringInterpolate
        result_t Content(const snippet& Snippet, const syntaxes_t& Splices) const
        {
            return NewGlitch(
                AsLocus(Snippet),
                EDiagnostic::ErrSyntax_Unimplemented,
                "Markup content from string is not yet supported.");

            // Will eventually look something like this:
            //return String(Snippet, Splices);
        }

        //-------------------------------------------------------------------------------------------------
        // [MaxVerse] Form markup content array from Content array
        result_t Contents(const snippet& Snippet, const capture_t& Leading, const syntaxes_t& Splices) const
        {
            return NewGlitch(
                AsLocus(Snippet),
                EDiagnostic::ErrSyntax_Unimplemented,
                "Markup from content array is not yet supported.");

            // Will eventually look something like this:
            //return Call(Snippet, mode::Open, TSRef<Identifier>::New("array", AsLocus0(Snippet)), block_t{ Snippet, Splices });
        }

        //-------------------------------------------------------------------------------------------------
        // [MaxVerse] Macro invocation constructing markup from Content(s)
        result_t InvokeMarkup(const snippet& Snippet, text StartToken, const capture_t& Leading, const syntax_t& Macro, block_t* Clause1, block_t* DoClause, const capture_t& TokenLeading, const capture_t& PreContent, const syntax_t& Content, const capture_t& PostContent) const
        {
            return NewGlitch(
                AsLocus(Snippet),
                EDiagnostic::ErrSyntax_Unimplemented,
                "Markup construction is not yet supported.");
        }

        //-------------------------------------------------------------------------------------------------
        void NewLine(capture_t& Capture, const snippet& Snippet, const place Place) const
        {
            // If we are currently capturing space information, we want to know if there is a newline after
            // the current capture.
            const CUTF8StringView& SnippetStringView = AsStringView(Snippet.Text);
            // The check against `place::Space` keeps this limited to only being applied to comments for now.
            if (Place == place::Space && Capture.Nodes.Num() != 0)
            {
                const int32_t NumTrailingNewLines = CountNumTrailingNewLines(SnippetStringView);
                TSRef<Node> LastNodeInCapture = Capture.Nodes.Last();
                LastNodeInCapture->SetNumNewLinesAfter(NumTrailingNewLines);
            }
            Capture.String.Append(SnippetStringView);
        }

        //-------------------------------------------------------------------------------------------------
        void Semicolon(capture_t& Capture, const snippet& Snippet) const
        {
            Capture.String.Append(AsStringView(Snippet.Text));
        }

        //-------------------------------------------------------------------------------------------------
        syntax_t Leading(const capture_t& Capture, const syntax_t& Syntax) const
        {
            // NOTE: (yiliang.siew) We capture the number of consecutive newlines here and indicate in the node how many
            // of these should be printed out.
            if (!Capture.String.IsEmpty())
            {
                const int32_t NumLeadingNewLines = CountNumLeadingNewLines(Capture.String.ToStringView());
                // TODO: (yiliang.siew) We can't prefix newlines to the comment in the capture yet, because there is the
                // assumption in the pretty-printer about vertical forms are determined, and thus we transfer the first
                // leading newline from some clauses' members to the clause itself. Refer to the HACK in `Invoke` for
                // details. Once the pretty-printer gets fixed and this HACK removed, this should prefix the first item in
                // the capture with the newlines, if any.
                Syntax->SetNumNewLinesBefore(NumLeadingNewLines);
            }
            if (Capture.Nodes.IsFilled())
            {
                /*
                 * Because our prefix attributes are stored on VST node definitions, we have
                 * the situation where the syntax:
                 *
                 * ```
                 * <#C0#>@<#C1#>attrib1
                 * c := class {}
                 * ```
                 *
                 * results in the VST definition of `c` getting `C0` prefixed to it, while `C1` is prefixed to the `attrib1` attribute clause.
                 * This results in ambiguity with the similar syntax:
                 *
                 * ```
                 * @<#C1#>attrib1
                 * <#C0#>c := class {}
                 * ```
                 *
                 * Which would end up with the same VST structure if we do not do this processing here.
                 */
                // NOTE: (yiliang.siew) We look at the current syntax and see if it has a prepend attribute clause
                // that we can prefix the comments to so that we can distinguish the actual VST structure better as
                // described above and thus roundtrip the syntax correctly.
                TSPtr<Node> SyntaxToAppendTo = Syntax;
                if (Syntax->HasAttributes())
                {
                    const TSPtr<Clause>& SyntaxAttributes = Syntax->GetAux();
                    for (const auto& AttributeClause : SyntaxAttributes->GetChildren())
                    {
                        if (AttributeClause->IsA<Clause>() && AttributeClause->As<Clause>().GetForm() == Clause::EForm::IsPrependAttributeHolder)
                        {
                            SyntaxToAppendTo = AttributeClause;
                            break;
                        }
                    }
                }

                // NOTE: (yiliang.siew) This is a special case for qualified identifiers, since they are unlike other
                // VST nodes in that a child identifier acts as the identifier while its parent(s) are the qualifiers.
                // If so, we re-associate the comment here so that it will be roundtripped appropriately in the pretty-printer.
                if (Syntax->IsA<Identifier>())
                {
                    Identifier& SyntaxAsIdentifier = Syntax->As<Identifier>();
                    if (SyntaxAsIdentifier.IsQualified())
                    {
                        for (const TSRef<Node>& CurrentNode : Capture.Nodes)
                        {
                            if (CurrentNode->IsA<Comment>())
                            {
                                SyntaxAsIdentifier._QualifierPreComments.Add(CurrentNode);
                            }
                        }

                        return Syntax;
                    }
                }
                for (const TSRef<Node>& CurrentNode : Capture.Nodes)
                {
                    if (CurrentNode->IsA<Comment>())
                    {
                        SyntaxToAppendTo->AppendPrefixComment(CurrentNode);
                    }
                }
            }
            return Syntax;
        }

        //-------------------------------------------------------------------------------------------------
        syntax_t Trailing(const syntax_t& Syntax, const capture_t& Capture) const
        {
            // NOTE: (yiliang.siew) We capture the number of consecutive newlines here and indicate in the node how many
            // of these should be printed out.
            if (!Capture.String.IsEmpty())
            {
                const int32_t NumLeadingNewLines = CountNumLeadingNewLines(Capture.String.ToStringView());
                Syntax->SetNumNewLinesAfter(NumLeadingNewLines);
            }
            if (Capture.Nodes.IsFilled())
            {
                for (const TSRef<Node>& Node : Capture.Nodes)
                {
                    if (Node->IsA<Comment>())
                    {
                        Syntax->AppendPostfixComment(Node);
                    }
                }
            }

            return Syntax;
        }

        TSRef<Clause> MakeSpecifier(const syntax_t& Attr) const
        {
            TSRef<Clause> SpecifierClause = TSRef<Clause>::New(Attr->Whence(), Clause::EForm::IsAppendAttributeHolder);
            SpecifierClause->AppendChild(Attr);
            return SpecifierClause;
        }

        //-------------------------------------------------------------------------------------------------
        result_t PrefixToken(const snippet& Snippet, mode Mode, text Symbol, const block_t& RightBlock, bool bLift, const syntaxes_t& Specifiers = { }) const
        {
            const CUTF8String SymbolStr(AsStringView(Symbol));
            const SLocus      Whence = AsLocus(Snippet);

            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            if (bLift)
            {
                AppendGlitch(
                    Whence,
                    EDiagnostic::ErrSyntax_InternalError,
                    CUTF8String(
                        "%s:%u:%u: Lifting prefix operator '%s' is not yet supported.",
                        _SnippetPath.AsCString(),
                        Whence.BeginRow() + 1,
                        Whence.BeginColumn() + 1,
                        *SymbolStr));
                return TSRef<Placeholder>::New(Whence);
            }


            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // First check prefix tokens that allow for no RightBlock
             
            // Can't use Symbol directly since it is not null terminated
            nat8 TokenId = Token8(SymbolStr);

            switch (TokenId)
            {
                case token(u8"return"):
                {
                    const auto NewControlNode = TSRef<Control>::New(Whence, Control::EKeyword::Return);

                    if (RightBlock.Elements.IsFilled())
                    {
                        // Number of children checked in `Desugarer.cpp` in `DesugarControl()`
                        NewControlNode->AppendChildren(RightBlock.Elements);
                    }

                    return NewControlNode;
                }

                case token(u8"break"):
                {
                    const auto NewControlNode = TSRef<Control>::New(Whence, Control::EKeyword::Break);

                    if (RightBlock.Elements.IsFilled())
                    {
                        // Number of children checked in `Desugarer.cpp` in `DesugarControl()`
                        NewControlNode->AppendChildren(RightBlock.Elements);
                    }

                    return NewControlNode;
                }

                case token(u8"yield"):
                    AppendGlitch(
                        Whence,
                        EDiagnostic::ErrSyntax_Unimplemented,
                        "'yield' is reserved for future use...");
                    return TSRef<Placeholder>::New(Whence);

                case token(u8"continue"):
                    AppendGlitch(
                        Whence,
                        EDiagnostic::ErrSyntax_Unimplemented,
                        "'continue' is reserved for future use...");
                    return TSRef<Placeholder>::New(Whence);

                default:
                    break;
            }

            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // Check prefix tokens that expect a RightBlock
            const syntax_t RightExpr   = BlockAsSingleExpression(RightBlock);
            const SLocus   TokenWhence = AsTokenLocus(Snippet, Symbol);

            switch (TokenId)
            {
                case token(u8"-"):
                {
                    const auto AddSubNode = TSRef<BinaryOpAddSub>::New(Whence);
                    AddSubNode->AppendSubOperation(TokenWhence, RightExpr);
                    return AddSubNode;
                }

                case token(u8"+"):
                {
                    const auto AddSubNode = TSRef<BinaryOpAddSub>::New(Whence);
                    AddSubNode->AppendAddOperation(TokenWhence, RightExpr);
                    return AddSubNode;
                }

                case token(u8":"):
                    // Note that `X:t=V` parses as `(X):=((:t)=V)` which is rearranged to `(X:t):=(V)` in `DefineFromType()`
                    return TSRef<TypeSpec>::New(Whence, RightExpr);

                case token(u8"?"):
                {
                    const auto PpcNode = RightExpr->IsA<PrePostCall>()
                        ? RightExpr.As<PrePostCall>()
                        : TSRef<PrePostCall>::New(RightExpr, Whence);
                    TSRef<Clause> QMark = PpcNode->PrependQMark(TokenWhence);
                    PpcNode->CombineWhenceWith(TokenWhence);
                    return PpcNode;
                }

                case token(u8"^"):
                {
                    const auto PpcNode = RightExpr->IsA<PrePostCall>()
                        ? RightExpr.As<PrePostCall>()
                        : TSRef<PrePostCall>::New(RightExpr, Whence);
                    TSRef<Clause> Hat = PpcNode->PrependHat(TokenWhence);
                    PpcNode->CombineWhenceWith(TokenWhence);
                    return PpcNode;
                }

                case token(u8"not"):
                    return TSRef<PrefixOpLogicalNot>::New(Whence, RightExpr);

                case token(u8"set"):
                    return TSRef<Mutation>::New(Whence, RightExpr, Mutation::EKeyword::Set);

                case token(u8"var"):
                {
                    TSRef<Mutation> Result = TSRef<Mutation>::New(Whence, RightExpr, Mutation::EKeyword::Var);
                    for (const syntax_t& Specifier : Specifiers)
                    {
                        Result->AppendAux(MakeSpecifier(Specifier));
                    }
                    return Result;
                }

                default:
                    return NewGlitch(Whence, EDiagnostic::ErrSyntax_Unimplemented, CUTF8String("Prefix `%s` operator is unimplemented.", *SymbolStr));
            }
        }  // PrefixToken()

        //-------------------------------------------------------------------------------------------------
        // Prefix bracket expression - usually used to specify arrays and maps: [Left]Right
        result_t PrefixBrackets(const snippet& Snippet, const block_t& LeftBlock, const block_t& RightBlock) const
        {
            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            const SLocus Whence = AsLocus(Snippet);

            if (RightBlock.Punctuation == Verse::Grammar::punctuation::Braces)
            {
                return NewGlitch(Whence,
                    EDiagnostic::ErrSyntax_Unimplemented,
                    CUTF8String(
                        "Braced operator'[]' is not currently supported: `%.*s`",
                        Verse::Grammar::Length(Snippet.Text),
                        Snippet.Text.Start));
            }

            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // array '[]element_type' or map '[key_type]value_type' specifier
            
            // Could alternatively create Call(Snippet, mode::Closed, "prefix'[]'", RightBlock, Leftlock)

            const syntax_t RightExpr = BlockAsSingleExpression(RightBlock);

            // Make PrePostCall node
            const auto RhsPpc = RightExpr->IsA<PrePostCall>()
                ? RightExpr.As<PrePostCall>()
                : TSRef<PrePostCall>::New(RightExpr, RightExpr->Whence());

            // Determine bracket locus
            const SLocus KeyWhence = AsLocus(LeftBlock.BlockSnippet);
            const SLocus BracketsWhence(KeyWhence.BeginRow(), KeyWhence.BeginColumn() - 1u, KeyWhence.EndRow(), KeyWhence.EndColumn() + 1u);

            const auto Args = LeftBlock.Elements.IsEmpty()
                // array specifier '[]element_type'
                ? TSRef<Clause>::New(
                    syntaxes_t(),
                    0,
                    BracketsWhence,
                    Clause::EForm::Synthetic)
                // map specifier '[key_type]value_type'?
                : TSRef<Clause>::New(
                    LeftBlock.Elements,
                    LeftBlock.Elements.Num(),
                    BracketsWhence,
                    AsClauseForm(LeftBlock));

            RhsPpc->PrependCallArgs(true, Args);
            RhsPpc->CombineWhenceWith(Whence);

            return RhsPpc;
        }

        //-------------------------------------------------------------------------------------------------
        result_t InfixToken(const snippet& Snippet, mode Mode, const syntax_t& Left, text Symbol, const syntax_t& Right) const
        {
            // Some of these nodes previously used location of operator though now using location of whole expression
            const SLocus Whence       = AsLocus(Snippet);
            const SLocus SymbolWhence = SLocus(Left->Whence().GetEnd(), Right->Whence().GetBegin());
            const CUTF8String SymbolStr(AsStringView(Symbol));

            // Can't use `Symbol` directly since it is not null terminated
            switch (Token8(SymbolStr))
            {
                case token(u8"="):
                    return TSRef<BinaryOpCompare>::New(Whence, Left, BinaryOpCompare::op::eq, Right);

                case token(u8"<>"):
                    return TSRef<BinaryOpCompare>::New(Whence, Left, BinaryOpCompare::op::noteq, Right);

                case token(u8"<"):
                    return TSRef<BinaryOpCompare>::New(Whence, Left, BinaryOpCompare::op::lt, Right);

                case token(u8"<="):
                    return TSRef<BinaryOpCompare>::New(Whence, Left, BinaryOpCompare::op::lteq, Right);

                case token(u8">"):
                    return TSRef<BinaryOpCompare>::New(Whence, Left, BinaryOpCompare::op::gt, Right);

                case token(u8">="):
                    return TSRef<BinaryOpCompare>::New(Whence, Left, BinaryOpCompare::op::gteq, Right);

                case token(u8"+"):
                {
                    if (Left->IsA<BinaryOpAddSub>())
                    {
                        Left->As<BinaryOpAddSub>().AppendAddOperation(SymbolWhence, Right);
                        Left->CombineWhenceWith(Right->Whence());
                        return Left;
                    }

                    const auto NewOpAdd = TSRef<BinaryOpAddSub>::New(Whence, Left);
                    NewOpAdd->AppendAddOperation(SymbolWhence, Right);
                    return NewOpAdd;
                }

                case token(u8"-"):
                {
                    if (Left->IsA<BinaryOpAddSub>())
                    {
                        // NOTE: (yiliang.siew) If we do this, we must also transfer any postfix comments from the `Left`
                        // node to its current rightmost leaf, since otherwise the pretty-printer would treat this as a
                        // postfix comment of the entire operation, which would place it in the wrong position.
                        if (Left->GetPostfixComments().Num() != 0)
                        {
                            const TSPtr<Node> RightmostChildOfLeft = Left->GetRightmostChild();
                            if (RightmostChildOfLeft)
                            {
                                Node::TransferPostfixComments(Left, RightmostChildOfLeft.AsRef());
                            }
                        }
                        Left->As<BinaryOpAddSub>().AppendSubOperation(SymbolWhence, Right);
                        Left->CombineWhenceWith(Right->Whence());
                        return Left;
                    }

                    const auto NewOpSub = TSRef<BinaryOpAddSub>::New(Whence, Left);
                    NewOpSub->AppendSubOperation(SymbolWhence, Right);
                    return NewOpSub;
                }

                case token(u8"*"):
                {
                    if (Left->IsA<BinaryOpMulDivInfix>())
                    {
                        ULANG_ENSUREF(!Right->IsA<BinaryOpMulDivInfix>(), "RHS is a MulDiv node");
                        // NOTE: (yiliang.siew) If we do this, we must also transfer any postfix comments from the `Left`
                        // node to its current rightmost leaf, since otherwise the pretty-printer would treat this as a
                        // postfix comment of the entire operation, which would place it in the wrong position.
                        if (Left->GetPostfixComments().Num() != 0)
                        {
                            const TSPtr<Node> RightmostChildOfLeft = Left->GetRightmostChild();
                            if (RightmostChildOfLeft)
                            {
                                Node::TransferPostfixComments(Left, RightmostChildOfLeft.AsRef());
                            }
                        }
                        Left->As<BinaryOpMulDivInfix>().AppendMulOperation(SymbolWhence, Right);
                        Left->CombineWhenceWith(Right->Whence());
                        return Left;
                    }

                    const auto NewOpMul = TSRef<BinaryOpMulDivInfix>::New(Whence, Left);
                    NewOpMul->AppendMulOperation(SymbolWhence, Right);
                    return NewOpMul;
                }

                case token(u8"/"):
                {
                    if (Left->IsA<BinaryOpMulDivInfix>())
                    {
                        ULANG_ENSUREF(!Right->IsA<BinaryOpMulDivInfix>(), "RHS is a MulDiv node");
                        // NOTE: (yiliang.siew) If we do this, we must also transfer any postfix comments from the `Left`
                        // node to its current rightmost leaf, since otherwise the pretty-printer would treat this as a
                        // postfix comment of the entire operation, which would place it in the wrong position.
                        if (Left->GetPostfixComments().Num() != 0)
                        {
                            const TSPtr<Node> RightmostChildOfLeft = Left->GetRightmostChild();
                            if (RightmostChildOfLeft)
                            {
                                Node::TransferPostfixComments(Left, RightmostChildOfLeft.AsRef());
                            }
                        }
                        Left->As<BinaryOpMulDivInfix>().AppendDivOperation(SymbolWhence, Right);
                        Left->CombineWhenceWith(Right->Whence());
                        return Left;
                    }

                    const auto NewOpDiv = TSRef<BinaryOpMulDivInfix>::New(Whence, Left);
                    NewOpDiv->AppendDivOperation(SymbolWhence, Right);
                    return NewOpDiv;
                }

                case token(u8"."):
                {
                    ULANG_ASSERTF(Right->IsA<Identifier>(), "Illegal syntax : dot must always be followed by identifier.");
                    if (Left->IsA<PrePostCall>())
                    {
                        // NOTE: (yiliang.siew) If we do this, we must also transfer any postfix comments from the `Left`
                        // node to its current rightmost leaf, since otherwise the pretty-printer would treat this as a
                        // postfix comment of the entire operation, which would place it in the wrong position.
                        if (Left->GetPostfixComments().Num() != 0)
                        {
                            const TSPtr<Node> RightmostChildOfLeft = Left->GetRightmostChild();
                            if (RightmostChildOfLeft)
                            {
                                Node::TransferPostfixComments(Left, RightmostChildOfLeft.AsRef());
                            }
                        }
                        // Member Access Chaining Transform
                        // Convert from:
                        //    PrePostCall(PrePostCall(a 'dot' b) 'dot' c)
                        // to:
                        //    dot(a 'dot' b 'dot' c)
                        Left->As<PrePostCall>().AppendDotIdent(Whence, Right.As<Identifier>());
                        Left->CombineWhenceWith(Right->Whence());
                        return Left;
                    }
                    else
                    {
                        const auto NewPpc = TSRef<PrePostCall>::New(Whence);
                        NewPpc->AppendChild(Left);
                        ULANG_ASSERTF(Right->IsA<Identifier>(), "Illegal syntax : dot must always be followed by identifier.");
                        NewPpc->AppendDotIdent(Whence, Right.As<Identifier>());
                        return NewPpc;
                    }
                }

                case token(u8"and"):
                    return TSRef<BinaryOpLogicalAnd>::New(Whence, Left, Right);

                case token(u8"or"):
                    return TSRef<BinaryOpLogicalOr>::New(Whence, Left, Right);

                case token(u8":"):
                    return TSRef<TypeSpec>::New(Whence, Left, Right);

                case token(u8".."):
                    return TSRef<BinaryOpRange>::New(Whence, Left, Right);

                case token(u8"->"):
                    return TSRef<BinaryOpArrow>::New(Whence, Left, Right);

                default:
                    return NewGlitch(Whence, EDiagnostic::ErrSyntax_Unimplemented, CUTF8String("Infix `%s` operator is unimplemented.", *SymbolStr));
            }
        }  // InfixToken

        //-------------------------------------------------------------------------------------------------
        result_t DefineFromType(const snippet& Snippet, const syntax_t& Left, const block_t& RightBlock) const
        {
            syntax_t Right = BlockAsSingleExpression(RightBlock);

            // For "a:b <cmp> c", the parser generates:
            //   define_from_type(a, infix_token(<cmp>, prefix_token(u8":", b), c)).
            // The case where there is no trailing comparison operator is also generated as:
            //   define_from_type(a, prefix_token(u8":", b)).
            // This is to allow interpreting e.g. "a:b<c" as "a=(:b)<c". That is, a is any value of
            // the type b that is less than c. The simpler interpretation of "a:Int<3" as "a:(Int<3)"
            // suffers from a category error due to comparing a type with an integer.
            // To avoid changing the rest of the compiler to consume this `a=(:b)` syntax instead of
            // "a:b", transform this simple case (without a trailing comparison operator) back to
            // the "a:b" form.

            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // `X:t` parses as `X=(:t)` which is rearranged to `X:t` here
            if (Right->IsA<TypeSpec>() && Right->GetChildCount() == 1)
            {
                Right->AppendChildAt(Left, 0);
                Right->CombineWhenceWith(Left->Whence());
                return Right;
            }

            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // `X:t where t:u` parses as `X=(:t where (t:u)` which is rearranged to `(X:t) where (t:u)` here
            if (Right->IsA<Where>()
                && !Right->IsEmpty()
                && Right->GetChildren()[0]->IsA<TypeSpec>()
                && (Right->GetChildren()[0]->GetChildCount() == 1))
            {
                syntax_t WhereLeft = Right->GetChildren()[0];
                WhereLeft->AppendChildAt(Left, 0);
                WhereLeft->CombineWhenceWith(Left->Whence());
                Right->CombineWhenceWith(Left->Whence());
                return Right;
            }

            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // `X:t=V` parses as `(X):=((:t)=V)` which is rearranged to `(X:t):=(V)` here
            if (Right->IsA<Assignment>())
            {
                Assignment&     AssignOp   = Right->As<Assignment>();
                syntax_t        LeftAssign = AssignOp.GetOperandLeft();
                Assignment::EOp AssignKind = AssignOp.GetTag<Assignment::EOp>();

                if ((AssignKind == Assignment::EOp::assign)
                    && LeftAssign->IsA<TypeSpec>())
                {
                    // Move Left element to typespec first child
                    LeftAssign->AppendChildAt(Left, 0);
                    LeftAssign->CombineWhenceWith(Left->Whence());

                    // Swap Assignment node with Definition node
                    NodeArray AssignOperands = AssignOp.TakeChildren();
                    const int32_t NumNewLinesAfterNewlines = AssignOp.NumNewLinesAfter();
                    const TSRef<Clause> WrappedClause = AsWrappedClause(AssignOperands[1]);
                    TSRef<Definition> NewDefinition = TSRef<Definition>::New(Left->Whence() | AssignOp.Whence(), LeftAssign,
                        // Later definition code expects definition RHS to always be wrapped in a clause
                        // $Revisit - Clause wrapper seems redundant and could be removed in the future]
                        WrappedClause);
                    NewDefinition->SetNumNewLinesAfter(NumNewLinesAfterNewlines);
                    // TODO: (yiliang.siew) This is a HACK, but we will move the newline before from the first
                    // child of the clause of the typespec to be a line after the typespec. This is in keeping
                    // with the expectations of the pretty-printer for the time being.
                    // TransferFirstLeadingNewLineOfClauseMember(*WrappedClause);
                    if (WrappedClause->GetChildCount() > 0 && WrappedClause->GetChildren()[0]->HasNewLinesBefore())
                    {
                        const int32_t CurrentNumNewLinesBefore = WrappedClause->GetChildren()[0]->NumNewLinesBefore();
                        WrappedClause->GetChildren()[0]->SetNumNewLinesBefore(CurrentNumNewLinesBefore - 1);
                        LeftAssign->SetNumNewLinesAfter(LeftAssign->NumNewLinesAfter() + 1);
                    }

                    return NewDefinition;
                }
            }

            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // `X:t:=V` parses as `(X):=((:t):=V)` which is rearranged to `(X:t):=(V)` here
            if (Right->IsA<Definition>())
            {
                Definition& DefOp   = Right->As<Definition>();
                syntax_t    LeftDef = DefOp.GetOperandLeft();

                if (LeftDef->IsA<TypeSpec>())
                {
                    // A `:= `after a type spec should be an error and `=` should be suggested instead
                    // Keeping for now while code is transitioned.
                    
                    // Move Left element to typespec first child
                    LeftDef->AppendChildAt(Left, 0);
                    LeftDef->CombineWhenceWith(Left->Whence());
                    Right->CombineWhenceWith(LeftDef->Whence());

                    return Right;
                }
            }

            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // `a:b<op>c` case is translated to: `a:type{:b<op>c}`
            const SLocus RightWhence = Right->Whence();
            TSRef<Clause> RightClause = TSRef<Clause>::New(Right, RightWhence, Clause::EForm::NoSemicolonOrNewline);
            TSRef<Macro> TypeMacro = TSRef<Macro>::New(
                RightWhence,
                TSRef<Identifier>::New("type", RightWhence.GetBegin()),
                ClauseArray{ RightClause });
            return TSRef<TypeSpec>::New(AsLocus(Snippet), Left, TypeMacro);
        }

        //-------------------------------------------------------------------------------------------------
        result_t InfixBlock(const snippet& Snippet, const syntax_t& Left, text Symbol, const block_t& RightBlock) const
        {
            // TODO: (yiliang.siew) We currently do not support having effect specifiers on the definition type.
            // When we implement support for this, this check should be removed.
            if (Left->IsA<Verse::Vst::TypeSpec>() && !Left->IsEmpty())
            {
                const TSPtr<Verse::Vst::Node> TypeSpecRhs = Left->GetRightmostChild();
                if (TypeSpecRhs)
                {
                    if (const TSPtr<Verse::Vst::Clause> Aux = TypeSpecRhs->GetAux(); Aux && !Aux->IsEmpty())
                    {
                        for (const TSRef<Verse::Vst::Node>& AuxElement : Aux->GetChildren())
                        {
                            const Verse::Vst::Clause* AuxChildClause = AuxElement->AsNullable<Verse::Vst::Clause>();
                            if (!AuxChildClause)
                            {
                                continue;
                            }
                            if (AuxChildClause->GetForm() == Verse::Vst::Clause::EForm::IsAppendAttributeHolder)
                            {
                                AppendGlitch(AuxChildClause->Whence(), EDiagnostic::ErrSyntax_Unimplemented, "Open world specifiers :t<spec> are not yet supported.");
                            }
                        }
                    }
                }
            }

            if (Verse::Grammar::Length(Symbol) == 0)
            {
                // tokenless definition
                return DefineFromType(Snippet, Left, RightBlock);
            }


            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // First test for assignments that append individual children

            bool bDanglingRHS = RightBlock.Elements.IsEmpty() && (RightBlock.Punctuation != Verse::Grammar::punctuation::Braces);

            // Can't use Symbol directly since it is not null terminated
            const CUTF8String SymbolStr(AsStringView(Symbol));
            const nat8        SymbolId = Token8(SymbolStr);
            const SLocus      Whence = AsLocus(Snippet);
            const SLocus      TokenWhence = AsTokenLocusPrefix(RightBlock.BlockSnippet, Symbol);

            if ((SymbolId == token(u8":=")) || (SymbolId == token(u8"is")))
            {
                // Input looks like a function or a definition
                //
                // Left := RightBlock
                //
                // funcName( a1:t1, a2:t2 ) : t3 = {bodys}
                // x:Int = 123
                // Color = enum{...}

                // If there were no expressions or braces on the RHS, produce a dangling equals error.
                if (bDanglingRHS)
                {
                    AppendGlitch(TokenWhence, EDiagnostic::ErrSyntax_DanglingEquals);
                }

                // Wrap the RHS expression(s) in a Clause node.
                const syntax_t RhsClause = AsWrappedClause(BlockAsSingleExpression(RightBlock));

                return TSRef<Definition>::New(Whence, Left, RhsClause);
            }

            if (SymbolId == token(u8"=>"))
            {
                // #NewParser Allow dangling `=>` or prevent like `=`?
                //if (bDanglingRHS)
                //{
                //    AppendGlitch(TokenWhence, EDiagnostic::ErrSyntax_DanglingEquals, "Dangling `=>` with no expressions or empty braced block `{}` on its right hand side.");
                //}
                syntax_t RightBlockExpr = BlockAsSingleExpression(RightBlock);
                TSRef<Clause> WrappedClause = AsWrappedClause(RightBlockExpr);
                SetClausePunctuation(RightBlock, *WrappedClause);

                return TSRef<Lambda>::New(Whence, Left, WrappedClause);
            }

            if (SymbolId == token(u8"where"))
            {
                // This means that there are multiple sub-expressions for the `where` conditions.
                if (RightBlock.Form == Verse::Grammar::form::List && RightBlock.Elements.Num() > 1)
                {
                    return NewGlitch(
                        Whence,
                        EDiagnostic::ErrSyntax_Unimplemented,
                        "Semicolons and newlines in `where` clauses are not yet implemented.");
                }

                return TSRef<Verse::Vst::Where>::New(Whence, Left, RightBlock.Elements);
            }


            //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            // Test for assignments that add block of expressions as single expression operand
            const syntax_t RightExpr = BlockAsSingleExpression(RightBlock);

            switch (SymbolId)
            {
                case token(u8"="):
                    // If there were no expressions or braces on the RHS, produce a dangling equals error.
                    if (bDanglingRHS)
                    {
                        AppendGlitch(TokenWhence, EDiagnostic::ErrSyntax_DanglingEquals, "Dangling `=` assignment with no expressions or empty braced block `{}` on its right hand side.");
                    }

                    // Note that `X:t=V` parses as `(X):=((:t)=V)` which is rearranged to `(X:t):=(V)` in `DefineFromType()`
                    return TSRef<Assignment>::New(Whence, Left, Assignment::EOp::assign, RightExpr);

                case token(u8"+="):
                    // If there were no expressions or braces on the RHS, produce a dangling equals error.
                    if (bDanglingRHS)
                    {
                        AppendGlitch(TokenWhence, EDiagnostic::ErrSyntax_DanglingEquals, "Dangling `+=` plus assignment with no expressions or empty braced block `{}` on its right hand side.");
                    }

                    return TSRef<Assignment>::New(Whence, Left, Assignment::EOp::addAssign, RightExpr);

                case token(u8"-="):
                    if (bDanglingRHS)
                    {
                        AppendGlitch(TokenWhence, EDiagnostic::ErrSyntax_DanglingEquals, "Dangling `-=` subtract assignment with no expressions or empty braced block `{}` on its right hand side.");
                    }

                    return TSRef<Assignment>::New(Whence, Left, Assignment::EOp::subAssign, RightExpr);

                case token(u8"*="):
                    if (bDanglingRHS)
                    {
                        AppendGlitch(TokenWhence, EDiagnostic::ErrSyntax_DanglingEquals, "Dangling `*=` multiply assignment with no expressions or empty braced block `{}` on its right hand side.");
                    }

                    return TSRef<Assignment>::New(Whence, Left, Assignment::EOp::mulAssign, RightExpr);

                case token(u8"/="):
                    if (bDanglingRHS)
                    {
                        AppendGlitch(TokenWhence, EDiagnostic::ErrSyntax_DanglingEquals, "Dangling `/=` divide assignment with no expressions or empty braced block `{}` on its right hand side.");
                    }

                    return TSRef<Assignment>::New(Whence, Left, Assignment::EOp::divAssign, RightExpr);

                default:
                    return NewGlitch(Whence, EDiagnostic::ErrSyntax_Unimplemented, CUTF8String("Infix `%s` operator is unimplemented.", *SymbolStr));
            }
        }

        //-------------------------------------------------------------------------------------------------
        result_t PostfixToken(const snippet& Snippet, mode Mode, const syntax_t& Left, text Symbol) const
        {
            const auto PpcNode = Left->IsA<PrePostCall>()
                ? Left.As<PrePostCall>()
                : TSRef<PrePostCall>::New(Left, Left->Whence());

            const CUTF8String SymbolStr(AsStringView(Symbol));
            const SLocus      Whence      = AsLocus(Snippet);
            const SLocus      TokenWhence = AsTokenLocusPostfix(Snippet, Symbol);

            // Can't use Symbol directly since it is not null terminated
            switch (Token8(SymbolStr))
            {
            case token(u8"?"):
            {
                Node::TransferPostfixComments(PpcNode, PpcNode->AccessChildren().Last());
                PpcNode->AppendQMark(TokenWhence);
                PpcNode->CombineWhenceWith(Whence);
                break;
            }

            case token(u8"^"):
            {
                // NOTE: (yiliang.siew) This may seem counter-intuitive, but the syntax:
                /*
                 * ```
                 * A.B<#comment#>^
                 * ```
                 *
                 * Translates to having the `PrePostCall` operation of `B^` having the `comment` suffixed to it.
                 * Because we append the `^` syntax ourselves during roundtripping, in order to have the comment
                 * appear in the right order, we transfer any suffix comments from the `PrePostCall` operation to the `B`
                 * identifier itself so that they can be pretty-printed in the right order.
                 */
                Node::TransferPostfixComments(PpcNode, PpcNode->AccessChildren().Last());
                PpcNode->AppendHat(TokenWhence);
                PpcNode->CombineWhenceWith(Whence);
                break;
            }

            case token(u8"ref"):
            {
                return NewGlitch(Whence, EDiagnostic::ErrSyntax_Unimplemented, "Postfix `ref` is unimplemented");
            }

            default:
                ULANG_ERRORF(
                    "%s:%u:%u: Unrecognized postfix operator '%s'.",
                    _SnippetPath.AsCString(),
                    Whence.BeginRow() + 1,
                    Whence.BeginColumn() + 1,
                    *SymbolStr);
                return TSRef<Placeholder>::New(Whence);
            }

            return PpcNode;
        }


        //===============================================================================
        // Optional string callbacks which don't contribute to abstract syntax.
        
        //void BlankLine(capture_t& Capture, const snippet& Snippet, place Place) const  {}
        //void LinePrefix(capture_t& Capture, const snippet& Snippet) const              {}


        //-------------------------------------------------------------------------------------------------
        void Indent(capture_t& Capture, const snippet& Snippet, place Place) const
        {}

        //-------------------------------------------------------------------------------------------------
        void LineCmt(capture_t& Capture, const snippet& Snippet, const place Place, const capture_t& Comments) const
        {
            if ((_ParseBehaviour == parse_behaviour::ParseNoComments) || !Snippet.Text)
            {
                return;
            }
            const CUTF8StringView CommentText = AsStringView(Snippet.Text);
            Capture.String.Append(CommentText);
            Capture.Nodes.Add(TSRef<Comment>::New(Comment::EType::line, CommentText, AsLocus(Snippet)));
        }

        //-------------------------------------------------------------------------------------------------
        void BlockCmt(capture_t& Capture, const snippet& Snippet, const place Place, const capture_t& Comments) const
        {
            if ((_ParseBehaviour == parse_behaviour::ParseNoComments) || !Snippet.Text)
            {
                return;
            }
            const CUTF8StringView CommentText = AsStringView(Snippet.Text);
            Capture.String.Append(CommentText);
            Capture.Nodes.Add(TSRef<Comment>::New(Comment::EType::block, CommentText, AsLocus(Snippet)));
        }

        //-------------------------------------------------------------------------------------------------
        void IndCmt(capture_t& Capture, const snippet& Snippet, const place Place, const capture_t& Comments) const
        {
            if ((_ParseBehaviour == parse_behaviour::ParseNoComments) || !Snippet.Text)
            {
                return;
            }
            const CUTF8StringView CommentText = AsStringView(Snippet.Text);
            Capture.String.Append(CommentText);
            Capture.Nodes.Add(TSRef<Comment>::New(Comment::EType::ind, CommentText, AsLocus(Snippet)));
        }

        //-------------------------------------------------------------------------------------------------
        void MarkupTrim(capture_t& Capture) const
        {
            Capture.String.Reset();
        }

        //-------------------------------------------------------------------------------------------------
        void MarkupStart(capture_t& Capture, const snippet& Snippet) const
        {
            // Capture.String.IsEmpty();
        }

        //-------------------------------------------------------------------------------------------------
        void MarkupTag(capture_t& Capture, const snippet& Snippet) const
        {
            // Capture.String.IsEmpty();
        }

        //-------------------------------------------------------------------------------------------------
        void MarkupStop(capture_t& Capture, const snippet& Snippet) const
        {
            // Capture.String.IsEmpty();
        }

        //-------------------------------------------------------------------------------------------------
        // Gets the snippet file path currently being parsed
        const CUTF8String& GetSnippetPath() const  { return _SnippetPath; }

    protected:

        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Internal methods

        //-------------------------------------------------------------------------------------------------
        void AppendSpecifier(const syntax_t& Base, const block_t& SpecifierBlock) const
        {
            // $Revisit - Note that a <specifier> is not differentiated from an @attribute apart from the
            // fact that a <specifier> may only occur after an element and an @attribute may only occur
            // before an element (currently). They should be differentiated in the future.

            syntax_t     Specifier = BlockAsSingleExpression(SpecifierBlock);
            const SLocus Whence    = Specifier->Whence();

            // Ensure only one specifier expression
            if (SpecifierBlock.Elements.Num() != 1)
            {
                AppendGlitch(
                    Specifier->Whence(),
                    EDiagnostic::ErrSyntax_ExpectedExpression,
                    CUTF8String(
                        "%s:%u:%u: Specifier must be single identifier.",
                        _SnippetPath.AsCString(),
                        Whence.BeginRow() + 1,
                        Whence.BeginColumn() + 1));
            }

            // specifier nodes need to be wrapped in a Clause to hold the attribute comments
            const TSRef<Clause> CommentClause = TSRef<Clause>::New(Whence, Clause::EForm::IsAppendAttributeHolder);

            CommentClause->AppendChild(Specifier);
            Base->AppendAux(CommentClause);
        }

        //-------------------------------------------------------------------------------------------------
        void AppendSpecifiers(const syntax_t& Base, const syntaxes_t& Specifiers) const
        {
            // $Revisit - Note that a <specifier> is not differentiated from an @attribute apart from the
            // fact that a <specifier> may only occur after an element and an @attribute may only occur
            // before an element (currently). They should be differentiated in the future.

            for (const syntax_t& Specifier : Specifiers)
            {
                // specifier nodes need to be wrapped in a Clause to hold the attribute comments
                const TSRef<Clause> CommentClause = TSRef<Clause>::New(Specifier->Whence(), Clause::EForm::IsAppendAttributeHolder);

                CommentClause->AppendChild(Specifier);
                Base->AppendAux(CommentClause);
            }
        }

        //-------------------------------------------------------------------------------------------------
        static void AppendAttributeNode(const snippet& Snippet, const syntax_t& Base, const syntax_t& Attribute)
        {
            // $Revisit - Note that a <specifier> is not differentiated from an @attribute apart from the
            // fact that a <specifier> may only occur after an element and an @attribute may only occur
            // before an element (currently). They should be differentiated in the future.
            
            // attribute nodes need to be wrapped in a Clause to hold the attribute comments
            const TSRef<Clause> CommentClause = TSRef<Clause>::New(Attribute->Whence(), Clause::EForm::IsAppendAttributeHolder);

            CommentClause->AppendChild(Attribute);
            Base->AppendAux(CommentClause);
        }

        //-------------------------------------------------------------------------------------------------
        static void PrependAttributeNode(const snippet& Snippet, const syntax_t& Attribute, const syntax_t& Base)
        {
            // $Revisit - Note that a <specifier> is not differentiated from an @attribute apart from the
            // fact that a <specifier> may only occur after an element and an @attribute may only occur
            // before an element (currently). They should be differentiated in the future.
            
            // attribute nodes need to be wrapped in a Clause to hold the attribute comments
            const TSRef<Clause> CommentClause = TSRef<Clause>::New(Attribute->Whence(), Clause::EForm::IsPrependAttributeHolder);

            CommentClause->AppendChild(Attribute);
            Base->PrependAux(CommentClause);
        }

        //-------------------------------------------------------------------------------------------------
        syntax_t BlockAsSingleExpression(const block_t& Block) const
        {
            /*
             * NOTE: (yiliang.siew) This adds trailing comments to expressions such as:
             *
             * ```
             * dem():int =
             *     # this is a comment
             *     c:int = 5
             *     <# leading comment #> foo() # trailing foo() invocation
             * ```
             *
             * Since they seem to be only exposed in this callback from the parser.
             *
             * We also add leading newlines to expressions, which is important for syntax such as:
             * ```
             * f():void  <#hello#>     =  <#ohnoes#>
             *     return 5
             * ```
             * The `TypeSpec` node does not get a newline after it, but the `Control` node would get a leading newline instead.
             */
            // NOTE: (yiliang.siew) If the block has elements, prefix/suffix comments to the first/last elements of
            // the block and also set the leading/trailing newlines to the first/last elements as well.
            // If the block has no elements, add all leading/trailing comments as block-level elements in the clause and
            // set the leading/trailing newlines as appropriate.
            const uint32_t NumBlockElements = Block.Elements.Num();
            TSRefArray<Node> MutableBlockElements;
            TSRefArray<Node> LeadingBlockElements;
            if (NumBlockElements == 0)
            {
                MutableBlockElements.Reserve(NumBlockElements + Block.ElementsTrailing.Nodes.Num());
            }
            // We check the string instead of just the nodes since currently whitespace alone doesn't generate any nodes for being captured.
            if (!Block.PunctuationLeading.String.IsEmpty())
            {
                LeadingBlockElements.Reserve(Block.PunctuationLeading.Nodes.Num());
                // If there are no elements in the block, just add any comment nodes as block-level comments.
                for (const TSRef<Node>& CurNode : Block.PunctuationLeading.Nodes)
                {
                    if (CurNode->IsA<Comment>())
                    {
                        LeadingBlockElements.Add(CurNode);
                    }
                }
                if (NumBlockElements == 0)
                {
                    if (LeadingBlockElements.Num() != 0)
                    {
                        const int32_t NumLeadingNewLines = CountNumTrailingNewLines(Block.PunctuationLeading.String.ToStringView());
                        const TSRef<Node>& FirstElementInBlock = LeadingBlockElements.First();
                        FirstElementInBlock->SetNumNewLinesBefore(NumLeadingNewLines);
                    }
                }
                else
                {
                    const int32_t NumLeadingNewLines = CountNumTrailingNewLines(Block.PunctuationLeading.String.ToStringView());
                    const TSRef<Node>& FirstElementInBlock = Block.Elements.First();
                    FirstElementInBlock->SetNumNewLinesBefore(NumLeadingNewLines);
                }
            }
            if (!Block.ElementsTrailing.String.IsEmpty())
            {
                if (NumBlockElements == 0)
                {
                    for (const TSRef<Node>& CurNode : Block.ElementsTrailing.Nodes)
                    {
                        if (CurNode->IsA<Comment>())
                        {
                            MutableBlockElements.Add(CurNode);
                        }
                    }
                    if (MutableBlockElements.Num() != 0)
                    {
                        const CUTF8StringView& ElementsTrailingStringView = Block.ElementsTrailing.String.ToStringView();
                        const int32_t NumTrailingNewLines = CountNumLeadingNewLines(ElementsTrailingStringView);
                        MutableBlockElements.Last()->SetNumNewLinesAfter(NumTrailingNewLines);
                    }
                }
                else
                {
                    const TSRef<Node>& LastElementInBlock = Block.Elements.Last();
                    const int32_t NumTrailingNewLines = CountNumLeadingNewLines(Block.ElementsTrailing.String.ToStringView());
                    LastElementInBlock->SetNumNewLinesAfter(NumTrailingNewLines);
                    for (const TSRef<Node>& CurNode : Block.ElementsTrailing.Nodes)
                    {
                        if (CurNode->IsA<Comment>())
                        {
                            LastElementInBlock->AppendPostfixComment(CurNode);
                        }
                    }
                    // TODO: (yiliang.siew) This should honestly just apply to any node that has a semicolon after,
                    // not just clauses.
                    if (LastElementInBlock->IsA<Clause>() && Block.ElementsTrailing.String == ";")
                    {
                        LastElementInBlock->As<Clause>().SetForm(Clause::EForm::HasSemicolonOrNewline);
                    }
                }
            }
            TSPtr<Node> Result = nullptr;
            if (Block.Punctuation == Verse::Grammar::punctuation::Parens)
            {
                Result = TSRef<Parens>::New(
                    NumBlockElements ? BlockElementsLocus(Block) : AsLocus(Block.BlockSnippet),
                    AsClauseForm(Block),  // Alternatively just use Clause::EForm::Synthetic?
                    NumBlockElements == 0 ? MutableBlockElements : Block.Elements);
            }
            else if (NumBlockElements == 1 && Block.Punctuation != Verse::Grammar::punctuation::Braces)
            {
                // If only one element then return it
                Result = Block.Elements.First();
            }
            else
            {
                const Clause::EForm Form = AsClauseForm(Block);
                if (Form == Clause::EForm::NoSemicolonOrNewline
                    && Block.Punctuation == Verse::Grammar::punctuation::None)
                {
                    ULANG_ASSERT(Block.Form == Verse::Grammar::form::Commas);
                    Result = TSRef<Commas>::New(
                        NumBlockElements ? BlockElementsLocus(Block) : AsLocus(Block.BlockSnippet),
                        NumBlockElements == 0 ? MutableBlockElements : Block.Elements);
                }
                else
                {
                    Result = TSRef<Clause>::New(
                        NumBlockElements == 0 ? MutableBlockElements : Block.Elements,
                        NumBlockElements ? BlockElementsLocus(Block) : AsLocus(Block.BlockSnippet),
                        Form
                        );
                }
            }
            ULANG_ASSERT(Result.IsValid());
            for (const TSRef<Node>& CurNode : LeadingBlockElements)
            {
                ULANG_ASSERT(CurNode->IsA<Comment>());
                Result->AppendPrefixComment(CurNode);
            }
            if (Result->IsA<Clause>())
            {
                SetClausePunctuation(Block, Result->As<Clause>());
            }
            return Result.AsRef();
        }

        //-------------------------------------------------------------------------------------------------
        // Ensure that syntax element is wrapped in a clause - if it is already a clause then just pass it on
        const TSRef<Clause> AsWrappedClause(const syntax_t& Element) const
        {
            if (Element->IsA<Clause>())
            {
                // Already a clause node - just return it
                //return TSRef<Clause>(Element->As<Clause>());
                return *reinterpret_cast<const TSRef<Clause>* >(&Element);
            }

                                                                  // Synthetic might make sense here though it currently confuses round-tripping to a string
            return TSRef<Clause>::New(Element, Element->Whence(), Clause::EForm::NoSemicolonOrNewline);
        }

        /**
         * This function checks a `block_t`'s leading/trailing elements/punctuation, and mutates the clause as
         * necessary to what the final VST hierarchy should be like. This mostly handles comments and trailing/leading
         * newlines.
         *
         * @param InBlock 		The block that contains the current captured elements.
         * @param InClause 	The clause to mutate.
         */
        static void ProcessBlockPunctuationForClause(const block_t& InBlock, TSRef<Clause> InClause)
        {
            if (!InBlock.PunctuationLeading.String.IsEmpty())
            {
                for (const TSRef<Node>& Element : InBlock.PunctuationLeading.Nodes)
                {
                    if (Element->IsA<Comment>())
                    {
                        InClause->AppendPrefixComment(Element);
                    }
                }
                const CUTF8StringView PunctuationLeadingStringView = InBlock.PunctuationLeading.String.ToStringView();
                const int32_t NumTrailingNewLines = CountNumTrailingNewLines(PunctuationLeadingStringView);
                if (NumTrailingNewLines > 0)
                {
                    InClause->SetNumNewLinesBefore(NumTrailingNewLines);
                }
            }
            // NOTE: (yiliang.siew) Since it is possible that the trailing elements can include comments that are either
            // preceded/suffixed by whitespace, we check first and associate as needed.
            if (!InBlock.ElementsTrailing.String.IsEmpty() || !InBlock.PunctuationTrailing.String.IsEmpty())
            {
                TSRef<Node> NodeToSuffixCommentsTo = InClause;
                const int32_t NumInClauseChildren = InClause->GetChildCount();
                // NOTE: (yiliang.siew) If there are no children in the clause at all, we add each comment as a
                // block-level element inside of the clause instead of having one comment and having the rest of the
                // comment suffixed to it. The reason for this is that we do not want to assume "groups" of comments for
                // the VST (users can make a block comment for that) in terms of mutating the tree.  It's more intuitive
                // to allow deleting of individual leaf nodes this way than associating them with any other VST node.
                // Plus, it's also easier to inspect the tree in a debugger.
                if (NumInClauseChildren == 0)
                {
                    for (const TSRef<Node>& Element : InBlock.ElementsTrailing.Nodes)
                    {
                        if (Element->IsA<Comment>())
                        {
                            InClause->AppendChild(Element);
                        }
                    }
                }
                else
                {
                    NodeToSuffixCommentsTo = InClause->GetChildren().Last();
                    for (const TSRef<Node>& Element : InBlock.ElementsTrailing.Nodes)
                    {
                        if (Element->IsA<Comment>())
                        {
                            NodeToSuffixCommentsTo->AppendPostfixComment(Element);
                        }
                    }
                }
                for (const TSRef<Node>& PunctuationElement : InBlock.PunctuationTrailing.Nodes)
                {
                    if (PunctuationElement->IsA<Comment>())
                    {
                        InClause->AppendPostfixComment(PunctuationElement);
                    }
                }
                NodeArray& InClausePostfixComments = InClause->AccessPostfixComments();
                const CUTF8StringView ElementsTrailingStringView = InBlock.ElementsTrailing.String.ToStringView();
                // These are newlines that immediately trail the last element in the clause, before any other comments.
                // e.g. `/n/n/n#comment`
                const int32_t NumLeadingNewLinesTrailingElements = CountNumLeadingNewLines(ElementsTrailingStringView);
                if (NumLeadingNewLinesTrailingElements > 0)
                {
                    // TODO: (yiliang.siew) Technically this is wrong, but because we cannot distinguish at
                    // the moment intermingled comments and newlines in terms of the order, we cannot yet
                    // make this determination accurately.
                    if (InClause->GetChildCount() != 0)
                    {
                        const TSRef<Node>& LastElementInClause = InClause->AccessChildren().Last();
                        LastElementInClause->SetNumNewLinesAfter(NumLeadingNewLinesTrailingElements);
                    }
                    else
                    {
                        // TODO: (yiliang.siew) This is possible with the syntax `{\n\n\n    }`. In such cases, we can't really
                        // reproduce the newlines since we cannot capture this in the current VST. The VST would need to be
                        // able to capture whitespace as separate VST elements in order for this to work.
                    }
                }
                const int32_t NumTrailingNewLines = CountNumTrailingNewLines(ElementsTrailingStringView);
                if (NumTrailingNewLines > 0 && !InClausePostfixComments.IsEmpty())    // e.g. "#comment/n/n"
                {
                    InClausePostfixComments.Last()->SetNumNewLinesAfter(NumTrailingNewLines);
                }
                if (!InBlock.PunctuationTrailing.String.IsEmpty())
                {
                    const int32_t PunctuationTrailingNumLeadingNewLines = CountNumLeadingNewLines(InBlock.PunctuationTrailing.String.ToStringView());
                    if (PunctuationTrailingNumLeadingNewLines > 0)
                    {
                        InClause->SetNumNewLinesAfter(InClause->NumNewLinesAfter() + PunctuationTrailingNumLeadingNewLines);
                    }
                }
            }
        }

        static Clause::EForm AsClauseForm(const block_t& Block)
        {
            // NOTE: (yiliang.siew) We process each of the clause's elements that have been captured thus far, in order
            // to attach newline information, comments, and so on.
            if (Block.Elements.IsFilled() && Block.ElementsTrailing.String.IsFilled())
            {
                const int32_t NumTrailingNewLines = CountNumTrailingNewLines(Block.ElementsTrailing.String.ToStringView());
                if (NumTrailingNewLines > 0)
                {
                    Block.Elements.Last()->SetNumNewLinesAfter(NumTrailingNewLines);
                    // NOTE: (yiliang.siew) Here, we transfer any trailing newlines from trailing comments over to the block's
                    // last element, since the full number of trailing newlines is known here to the block, but not to the comment
                    // at the time when it is added (as part of the `NewLine` callback.) This avoids "doubling-up" on newlines
                    // when blockcmts end an expression.
                    if (!Block.ElementsTrailing.Nodes.IsEmpty())
                    {
                        Block.ElementsTrailing.Nodes.Last()->SetNewLineAfter(false);
                    }
                }
            }
            // $Revisit - block_t has additional information that is not being passed on
            // return ((Block.Punctuation == Verse::Grammar::punctuation::Ind) || (Block.Form == Verse::Grammar::form::List))
            // Because the parser sets blocks to `List` form by default, we only consider that it could
            // have a semicolon if there were more than a single element in the block.
            if (Block.Form == Verse::Grammar::form::List && Block.Elements.Num() > 1)
            {
                return Clause::EForm::HasSemicolonOrNewline;
            }
            else
            {
                return Clause::EForm::NoSemicolonOrNewline;
            }
        }

        //-------------------------------------------------------------------------------------------------
        static bool TransferFirstLeadingNewLineOfClauseMember(Clause& InClause, Clause& ClauseToApplyTrailingNewLineTo)
        {
            if (InClause.GetChildCount() == 0 || !InClause.GetChildren()[0]->HasNewLinesBefore())
            {
                return false;
            }

            TSRef<Node>& InClauseFirstChild = InClause.AccessChildren()[0];
            InClauseFirstChild->SetNumNewLinesBefore(InClauseFirstChild->NumNewLinesBefore() - 1);
            ClauseToApplyTrailingNewLineTo.SetNumNewLinesAfter(ClauseToApplyTrailingNewLineTo.NumNewLinesAfter() + 1);

            return true;
        }

        //-------------------------------------------------------------------------------------------------
        static CUTF8StringView AsStringView(text InText)
        {
            return CUTF8StringView(reinterpret_cast<const UTF8Char *>(InText.Start), reinterpret_cast<const UTF8Char *>(InText.Stop));
        }

        //-------------------------------------------------------------------------------------------------
        // Convert null terminated string to parser token and then convert to nat8 form
        static nat8 Token8(const CUTF8String& TokenStr)
        {
            return nat8(token((const char8*)TokenStr.AsUTF8()));
        }

        //-------------------------------------------------------------------------------------------------
        // Crop snippet by 1 on either side
        static snippet CropSnippet1(const snippet& Snippet)
        {
            snippet CroppedSnippet;
            
            CroppedSnippet.Text        = text(Snippet.Text.Start + 1, Snippet.Text.Stop - 1);
            CroppedSnippet.StartLine   = Snippet.StartLine,
            CroppedSnippet.StopLine    = Snippet.StopLine,
            CroppedSnippet.StartColumn = Snippet.StartColumn + 1u,
            CroppedSnippet.StopColumn  = Snippet.StopColumn - 1u;

            return CroppedSnippet;
        }

        //-------------------------------------------------------------------------------------------------
        static SLocus AsLocus(const snippet& Snippet)
        {
            // Converts from snippet:
            //   int64_t startline;   // inclusive, 1-based
            //   int64_t startpos;    // inclusive, 1-based
            //   int64_t endline;     // inclusive, 1-based
            //   int64_t endpos;      // exclusive, 1-based
            //
            // To SLocus:
            //    uint32_t _BeginRow;   // inclusive, 0-based
            //    uint32_t _BeginColum; // inclusive, 0-based
            //    uint32_t _EndRow;     // inclusive, 0-based
            //    uint32_t _EndColumn;  // exclusive, 0-based

            return SLocus{
                static_cast<uint32_t>(Snippet.StartLine) - 1u,
                static_cast<uint32_t>(Snippet.StartColumn) - 1u,
                static_cast<uint32_t>(Snippet.StopLine) - 1u,
                static_cast<uint32_t>(Snippet.StopColumn) - 1u
            };
        }

        //-------------------------------------------------------------------------------------------------
        // Make a locus of zero size just before the first character of the snippet
        // - used for synthetically inserted code to ensure that the locations do not overlap.
        static SLocus AsLocus0(const snippet& Snippet)
        {
            return SLocus{
                static_cast<uint32_t>(Snippet.StartLine) - 1u,
                static_cast<uint32_t>(Snippet.StartColumn) - 1u,
                static_cast<uint32_t>(Snippet.StartLine) - 1u,
                static_cast<uint32_t>(Snippet.StartColumn) - 1u,
            };
        }

        //-------------------------------------------------------------------------------------------------
        static SLocus AsTokenLocus(const snippet& Snippet, const text& TokenText)
        {
            return SLocus{
                static_cast<uint32_t>(Snippet.StartLine) - 1u,
                static_cast<uint32_t>(Snippet.StartColumn) - 1u,
                static_cast<uint32_t>(Snippet.StartLine) - 1u,
                static_cast<uint32_t>(Snippet.StartColumn + Verse::Grammar::Length(TokenText)) - 1u
            };
        }

        //-------------------------------------------------------------------------------------------------
        static SLocus AsTokenLocusPostfix(const snippet& Snippet, const text& TokenText)
        {
            return SLocus{
                static_cast<uint32_t>(Snippet.StopLine) - 1u,
                static_cast<uint32_t>(Snippet.StopColumn - Verse::Grammar::Length(TokenText)) - 1u,
                static_cast<uint32_t>(Snippet.StopLine) - 1u,
                static_cast<uint32_t>(Snippet.StopColumn) - 1u
            };
        }

        //-------------------------------------------------------------------------------------------------
        static SLocus AsTokenLocusPrefix(const snippet& Snippet, const text& TokenText)
        {
            return SLocus{
                static_cast<uint32_t>(Snippet.StartLine) - 1u,
                static_cast<uint32_t>(Snippet.StartColumn - Verse::Grammar::Length(TokenText)) - 1u,
                static_cast<uint32_t>(Snippet.StartLine) - 1u,
                static_cast<uint32_t>(Snippet.StartColumn) - 1u
            };
        }

        //-------------------------------------------------------------------------------------------------
        static SLocus LocusTokenPostfix(const SLocus& Locus, const text& TokenText)
        {
            return SLocus{
                Locus.EndRow(),
                Locus.EndColumn(),
                Locus.EndRow(),
                Locus.EndColumn() + static_cast<uint32_t>(Verse::Grammar::Length(TokenText))
            };
        }


        //-------------------------------------------------------------------------------------------------
        template<typename... ResultArgsType>
        TSPtr<SGlitch> NewGlitch(const SLocus& Whence, EDiagnostic Diagnostic, ResultArgsType&&... ResultArgs) const noexcept
        {
            return TSPtr<SGlitch>::New(
                SGlitchResult(Diagnostic, uLang::ForwardArg<ResultArgsType>(ResultArgs)...),
                SGlitchLocus(_SnippetPath, Whence, (uintptr_t)(nullptr)));
        }

        //-------------------------------------------------------------------------------------------------
        template<typename... ResultArgsType>
        void AppendGlitch(const SLocus& Whence, EDiagnostic Diagnostic, ResultArgsType&&... ResultArgs) const noexcept
        {
            _Diagnostics->AppendGlitch(TSRef<SGlitch>::New(
                SGlitchResult(Diagnostic, uLang::ForwardArg<ResultArgsType>(ResultArgs)...),
                SGlitchLocus(_SnippetPath, Whence, (uintptr_t)(nullptr))));
        }


        //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        // Data members
        // Should be stateless - because the parser doesn't invoke callbacks in left-to-right order
        TSRef<uLang::CDiagnostics> _Diagnostics;

        /// The path to the snippet being parsed and a VST being generated for.
        const CUTF8String& _SnippetPath;

        /// The behaviour that has been set for this current parse operation.
        parse_behaviour _ParseBehaviour;

    };  // generate_vst

    //-------------------------------------------------------------------------------------------------
    void CParserPass::ProcessSnippet(const TSRef<Verse::Vst::Snippet>& OutVst, const CUTF8StringView& TextSnippet, const SBuildContext& BuildContext) const
    {
        OutVst->Empty();

        NodeArray VstRoot;
        generate_vst VstGenerator(BuildContext._Diagnostics, OutVst->_Path, generate_vst::parse_behaviour::ParseAll);

        CUTF8String NullTerminatedString = TextSnippet;

        generate_vst::result_t Result = Verse::Grammar::File(
            VstGenerator, nat(NullTerminatedString.ByteLen()), reinterpret_cast<const char8*>(NullTerminatedString.AsUTF8()));
        
        if (Result)
        {
            const TSRef<Verse::Vst::Clause> FileClause = Result->As<Verse::Vst::Clause>();
            Verse::Vst::Node::TransferChildren(FileClause, OutVst);
            OutVst->SetForm(FileClause->GetForm());

            if (OutVst->GetChildCount())
            {
                OutVst->SetWhence(generate_vst::CombineLocus(OutVst->GetChildren()));
            }
        }
        else
        {
            BuildContext._Diagnostics->AppendGlitch(Result.GetError());
        }
    }

} // namespace uLang


#if defined(__clang__)
	#pragma clang diagnostic pop
#endif
#ifdef __GNUC__
	#pragma GCC diagnostic pop
#endif
