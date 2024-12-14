// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Containers/SharedPointerArray.h"
#include "uLang/Common/Containers/UniquePointer.h"
#include "uLang/Semantics/SemanticClass.h"

namespace uLang
{

/**
 * An unknown type - can optionally contain a list of possibly valid types
 */
class CUnknownType : public CNormalType
{
public:
    static constexpr ETypeKind StaticTypeKind = ETypeKind::Unknown;

    /// Valid replacements for this type (if any)
    mutable TArray<const CTypeBase *> _SuggestedTypes;

    CUnknownType(const CSymbol& Name, CScope& EnclosingScope) : CNormalType(ETypeKind::Unknown, EnclosingScope.GetProgram()), _Name(Name) {}

    virtual CUTF8String AsCodeRecursive(ETypeSyntaxPrecedence /*OuterPrecedence*/, TArray<const CFlowType*>& /*VisitedFlowTypes*/, bool bLinkable) const override { return "$unknown"; }

    virtual bool CanBeCustomAccessorDataType() const override { return false; }

    /// The name of the unknown type.
    const CSymbol& _Name;
};

/**
 * Error expression - "~er~" produced when an expression couldn't be analyzed due to an error.
 **/
class VERSECOMPILER_API CExprError : public CExpressionBase
{
public:
    CExprError(TUPtr<CUnknownType>&& ExprType = nullptr, bool bCanFail = false);

    virtual EAstNodeType GetNodeType() const override    { return EAstNodeType::Error_; }
    virtual CUTF8String GetErrorDesc() const override    { return "error"; }
    virtual const CTypeBase* GetResultType(const CSemanticProgram& Program) const override;
    virtual bool CanFail() const override { return _bCanFail; }

    // We hold onto child expressions in service of semantic analysis -- we may want info
    // for nested expressions, which may be well-formed, even if this parent was not fully formed.
    void AppendChild(TSPtr<CAstNode>&& Child) { _UnknownChildren.Add(Move(Child)); }

    virtual void VisitChildren(SAstVisitor& Visitor) const override { Visitor.VisitArray("UnknownChildren", _UnknownChildren); }

    virtual bool operator==(const CExpressionBase& Other) const override { return false; } // Never equal

private:
    TUPtr<CUnknownType> _ExprType;
    TSPtrArray<CAstNode> _UnknownChildren;
    bool _bCanFail;
};

/**
 * Placeholder expression - "~ph~" produced by placeholder nodes in the Vst.
 **/
class VERSECOMPILER_API CExprPlaceholder : public CExpressionBase
{
public:
    CExprPlaceholder(TUPtr<CUnknownType>&& ExprType = nullptr);

    virtual EAstNodeType GetNodeType() const override    { return EAstNodeType::Placeholder_; }
    virtual CUTF8String GetErrorDesc() const override    { return "placeholder"; }
    virtual const CTypeBase* GetResultType(const CSemanticProgram& Program) const override;

    virtual bool operator==(const CExpressionBase& Other) const override { return false; } // Never equal

private:
    TUPtr<CUnknownType> _ExprType;
};

}  // namespace uLang
