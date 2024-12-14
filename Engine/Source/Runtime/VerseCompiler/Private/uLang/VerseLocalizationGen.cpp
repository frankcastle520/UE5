// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/VerseLocalizationGen.h"

#include "uLang/Common/Text/FilePathUtils.h"
#include "uLang/Diagnostics/Diagnostics.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Syntax/VstNode.h"
#include "uLang/Toolchain/CommandLine.h"

#include <cmath>
#include <cstdio>

namespace {
	namespace Private {
        using namespace uLang;

        struct FImpl : SAstVisitor
		{
			const CSemanticProgram& Program;
			CDiagnostics& Diagnostics;
            TArray<FSolLocalizationInfo>& LocalizationInfo;
            TArray<FSolLocalizationInfo>& StringInfo;

			explicit FImpl(const CSemanticProgram& Program,
                           CDiagnostics& Diagnostics,
                           TArray<FSolLocalizationInfo>& LocalizationInfo,
                           TArray<FSolLocalizationInfo>& StringInfo)
				: Program(Program)
				, Diagnostics(Diagnostics)
				, LocalizationInfo(LocalizationInfo)
                , StringInfo(StringInfo)
			{
			}

			void ScrapeProgram()
			{
				Visit(*Program._AstProject);
			}

			//-------------------------------------------------------------------------------------------------
			template<typename... ResultArgsType>
			void AppendGlitch(const CAstNode& AstNode, ResultArgsType&&... ResultArgs)
			{
				SGlitchResult Glitch(ForwardArg<ResultArgsType>(ResultArgs)...);
				if (AstNode.GetNodeType() == Cases<EAstNodeType::Context_Package, EAstNodeType::Definition_Module>
					&& (!AstNode.GetMappedVstNode() || !AstNode.GetMappedVstNode()->Whence().IsValid()))
				{
					Diagnostics.AppendGlitch(Move(Glitch), SGlitchLocus());
				}
				else
				{
					ULANG_ASSERTF(
						AstNode.GetMappedVstNode() && AstNode.GetMappedVstNode()->Whence().IsValid(),
						"Expected valid whence for node used as glitch locus on %s id:%i - %s",
						AstNode.GetErrorDesc().AsCString(),
						GetDiagnosticInfo(Glitch._Id).ReferenceCode,
						Glitch._Message.AsCString());
					Diagnostics.AppendGlitch(Move(Glitch), SGlitchLocus(&AstNode));
				}
			}

            //-------------------------------------------------------------------------------------------------
            void ScrapeString(const CExprString& StringAst)
            {
                SGlitchLocus GlitchLocus(&StringAst);
                StringInfo.Emplace(StringAst._String, GlitchLocus.AsFormattedString());
            }

			//-------------------------------------------------------------------------------------------------
			void ScrapeLocalization(const CExprDefinition& DefinitionAst)
			{
				TSPtr<CExpressionBase> Value = DefinitionAst.Value();
				if (!Value)
				{
                    /* We allowed this in an earlier version of the compiler in the case of a localization in an
                    * abstract class, and will allow it now until we have support for breaking changes for
                    * to-be-published projects, without breaking already published projects (SOL-5053).
					AppendGlitch(
						DefinitionAst,
						EDiagnostic::ErrSyntax_InternalError,
						"Value is missing for localization.");
                    */
					return;
				}

				if (Value->GetNodeType() == EAstNodeType::External)
				{ // We are in a digest, and there is nothing to see here
					return;
				}
				if (Value->GetNodeType() != EAstNodeType::Invoke_Type)
				{
					AppendGlitch(
						DefinitionAst,
						EDiagnostic::ErrSyntax_InternalError,
						"Expected a type invocation here.");
					return;
				}

				Value = static_cast<CExprInvokeType&>(*Value)._Argument;

				if (Value->GetNodeType() != EAstNodeType::Invoke_Invocation)
				{
					AppendGlitch(
						DefinitionAst,
						EDiagnostic::ErrSyntax_InternalError,
						"Expected an invocation for localization.");
					return;
				}

				CExprInvocation& Invocation = static_cast<CExprInvocation&>(*Value);

				if (!Invocation.GetArgument())
				{
					AppendGlitch(
						DefinitionAst,
						EDiagnostic::ErrSyntax_InternalError,
						"No arguments for localization.");
					return;
				}

				if (Invocation.GetArgument()->GetNodeType() != EAstNodeType::Invoke_MakeTuple)
				{
					AppendGlitch(
						DefinitionAst,
						EDiagnostic::ErrSyntax_InternalError,
						"Expected a tuple for localization,");
					return;
				}

				CExprMakeTuple& MakeTuple = static_cast<CExprMakeTuple&>(*Invocation.GetArgument());

				if (MakeTuple.SubExprNum() < 2)
				{
					AppendGlitch(
						DefinitionAst,
						EDiagnostic::ErrSyntax_InternalError,
						"To few arguments for localization");
					return;
				}

				if (!MakeTuple.GetSubExprs()[0])
				{
					AppendGlitch(
						DefinitionAst,
						EDiagnostic::ErrSyntax_InternalError,
						"No path for localization.");
					return;
				}

				if (MakeTuple.GetSubExprs()[0]->GetNodeType() != EAstNodeType::Literal_String)
				{
					AppendGlitch(
						DefinitionAst,
						EDiagnostic::ErrSyntax_InternalError,
						"Localization path must be a string at this point.");
					return;
				}
				const CUTF8String& Path = static_cast<CExprString&>(*MakeTuple.GetSubExprs()[0])._String;

				if (!MakeTuple.GetSubExprs()[0])
				{
					AppendGlitch(
						DefinitionAst,
						EDiagnostic::ErrSyntax_InternalError,
						"No default for localization.");
					return;
				}

				if (MakeTuple.GetSubExprs()[1]->GetNodeType() != EAstNodeType::Literal_String)
				{
					AppendGlitch(
						DefinitionAst,
						EDiagnostic::ErrSyntax_InternalError,
						"Localization default must be a string.");
					return;
				}
				const CUTF8String& Default = static_cast<CExprString&>(*MakeTuple.GetSubExprs()[1])._String;

				SGlitchLocus GlitchLocus(&DefinitionAst);
                LocalizationInfo.Emplace(Path, Default, GlitchLocus.AsFormattedString());
			}

			// SAstVisitor interface 
			void Visit(CAstNode& Node)
			{
				const EAstNodeType NodeType = Node.GetNodeType();
				if (NodeType == EAstNodeType::Definition_Function)
				{
					CExprFunctionDefinition& Function = static_cast<CExprFunctionDefinition&>(Node);
					if (Function._Function->HasAttributeClass(Program._localizes, Program))
					{
						ScrapeLocalization(Function);
					}
				}
				else if (NodeType == EAstNodeType::Definition_Data)
				{
					CExprDataDefinition& DataDefAst = static_cast<CExprDataDefinition&>(Node);
					if (DataDefAst._DataMember->HasAttributeClass(Program._localizes, Program))
					{
						ScrapeLocalization(DataDefAst);
					}
				}
                else if (NodeType == EAstNodeType::Literal_String)
                {
                    ScrapeString(static_cast<CExprString&>(Node));
                }
				Node.VisitChildren(*this);
			}

			virtual void Visit(const char* FieldName, CAstNode& AstNode) override { Visit(AstNode); }
			virtual void VisitElement(CAstNode& AstNode) override { Visit(AstNode); }
		};

	}
}

namespace uLang
{
void FVerseLocalizationGen::operator()(const CSemanticProgram& Program,
    CDiagnostics& Diagnostics,
    TArray<FSolLocalizationInfo>& LocalizationInfo,
    TArray<FSolLocalizationInfo>& StringInfo) const
{
    ::Private::FImpl Impl(Program, Diagnostics, LocalizationInfo, StringInfo);
    Impl.ScrapeProgram();
}
}

