// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Semantics/AstPrinter.h"

#include "uLang/Common/Text/UTF8String.h"
#include "uLang/Common/Text/UTF8StringBuilder.h"
#include "uLang/Common/Text/UTF8StringView.h"
#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Semantics/Definition.h"
#include "uLang/Semantics/Expression.h"
#include "uLang/Semantics/SemanticTypes.h"
#include "uLang/Syntax/VstNode.h"

#include <cinttypes> // for PRIi64

namespace uLang
{
namespace
{
struct SPrintAstVisitor : public SAstVisitor
{
    const CSemanticProgram& _Program;
    uint32_t _IndentLevel{ 0 };
    CUTF8StringBuilder _StringBuilder;

    SPrintAstVisitor(const CSemanticProgram& Program) : _Program(Program) {}
    virtual ~SPrintAstVisitor() {}

    void AppendNewline() { _StringBuilder.AppendFormat("\n%*s", _IndentLevel, ""); }

    void Visit(const CAstNode& AstNode)
    {
        // Print the node class.
        _StringBuilder.AppendFormat("%s:", GetAstNodeTypeInfo(AstNode.GetNodeType())._CppClassName);
        _IndentLevel += 4;

        // Print the node's derived information.
        if (const CExpressionBase* Expression = AstNode.AsExpression())
        {
            const CTypeBase* ResultType = Expression->GetResultType(_Program);
            AppendNewline();
            _StringBuilder.AppendFormat("# CanFail()=%s DetermineInvokeTime()=%s GetResultType()=%s",
                Expression->CanFail() ? "true" : "false",
                InvokeTimeAsCString(Expression->DetermineInvokeTime(_Program)),
                ResultType ? ResultType->AsCode().AsCString() : "<nullptr>");
        }

        // Ask the AST node to enumerate its immediate fields and child nodes.
        AstNode.VisitImmediates(*this);

        // Print the children of everything but external packages.
        bool bPrintChildren = true;
        if (AstNode.GetNodeType() == EAstNodeType::Context_Package)
        {
            const CAstPackage* Package = static_cast<const CAstPackage*>(&AstNode);
            if (Package->_Role == EPackageRole::External)
            {
                bPrintChildren = false;
            }
        }
        if (bPrintChildren)
        {
            AstNode.VisitChildren(*this);
        }
        else
        {
            _StringBuilder.Append("# Children omitted for external package");
        }
        _IndentLevel -= 4;
    }

    virtual void VisitImmediate(const char* FieldName, CUTF8StringView Value) override
    {
        AppendNewline();
        _StringBuilder.AppendFormat("%s := \"", FieldName);
        _StringBuilder.Append(Value);
        _StringBuilder.Append('\"');
    }
    virtual void VisitImmediate(const char* FieldName, int64_t Value) override
    {
        AppendNewline();
        _StringBuilder.AppendFormat("%s := %" PRIi64, FieldName, Value);
    }
    virtual void VisitImmediate(const char* FieldName, double Value) override
    {
        AppendNewline();
        _StringBuilder.AppendFormat("%s := %a", FieldName, Value);
    }
    virtual void VisitImmediate(const char* FieldName, bool Value) override
    {
        AppendNewline();
        _StringBuilder.AppendFormat("%s := %s", FieldName, Value ? "true" : "false");
    }
    virtual void VisitImmediate(const char* FieldName, const CTypeBase* Type) override
    {
        AppendNewline();
        _StringBuilder.AppendFormat("%s := %s", FieldName, Type->AsCode().AsCString());
    }
    virtual void VisitImmediate(const char* FieldName, const CDefinition& Definition) override
    {
        AppendNewline();
        _StringBuilder.AppendFormat("%s := %s", FieldName, GetQualifiedNameString(Definition).AsCString());
    }
    virtual void VisitImmediate(const char* FieldName, const Verse::Vst::Node& VstNode) override
    {
        AppendNewline();
        _StringBuilder.AppendFormat("%s := %s(%d,%d, %d,%d)",
            FieldName,
            VstNode.GetElementName(),
            VstNode.Whence().BeginRow() + 1,
            VstNode.Whence().BeginColumn() + 1,
            VstNode.Whence().EndRow() + 1,
            VstNode.Whence().EndColumn() + 1);
    }

    virtual void Visit(const char* FieldName, CAstNode& AstNode) override
    {
        AppendNewline();
        _StringBuilder.AppendFormat("%s := ", FieldName);
        Visit(AstNode);
    }
    virtual void VisitElement(CAstNode& AstNode) override
    {
        AppendNewline();
        Visit(AstNode);
    }
    virtual void BeginArray(const char* FieldName, intptr_t Num) override
    {
        AppendNewline();
        _StringBuilder.AppendFormat("%s := array", FieldName);
        if (Num)
        {
            _StringBuilder.Append(':');
        }
        else
        {
            _StringBuilder.Append("{}");
        }
        _IndentLevel += 4;
    }
    virtual void EndArray() override { _IndentLevel -= 4; }
};
}

CUTF8String PrintAst(const CSemanticProgram& Program, const CAstNode& RootNode)
{
    SPrintAstVisitor Visitor(Program);
    Visitor.Visit(RootNode);
    return Visitor._StringBuilder.MoveToString();
}
}