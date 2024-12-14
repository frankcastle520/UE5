// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Common/Common.h"
#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Text/UTF8String.h"
#include "uLang/Semantics/Definition.h"
#include "uLang/Semantics/Expression.h"
#include "uLang/Semantics/SemanticScope.h"
#include "uLang/Semantics/SemanticTypes.h"

namespace uLang
{
// Forward declarations.
class CDataDefinition;
class CExprDefinition;

class VERSECOMPILER_API CTypeVariable : public CDefinition, public CNominalType
{
public:
    static const CDefinition::EKind StaticDefinitionKind = CDefinition::EKind::TypeVariable;
    static const ETypeKind StaticTypeKind = ETypeKind::Variable;

    // A parameter `X` of type `type(A, B)` is encoded as
    // @code
    // :type(Y, Y) where Y:type(A, Z), Z:type(Y, B)
    // @endcode
    // with all uses resolving to `Z`.  Upon instantiation,
    // @code
    // :type(Y, Y)
    // @endcode
    // is rewritten to effectively
    // @code
    // :type(Y, Z)
    // @endcode
    // Any negative uses (outside of the negative use above) is replaced with
    // `Y`, while positive uses (again, outside of the above) are replaced with
    // `Z`. `_ExplicitParam` points to the corresponding data definition. of the
    // original explicit argument.
    const CDataDefinition* _ExplicitParam = nullptr;
    // `_NegativeTypeVariable` points to `Y` in the original encoding.
    // `_ExplicitParam->_ImplicitParam` can be used to access `Z`.
    const CTypeVariable* _NegativeTypeVariable = nullptr;

    const CTypeBase* _NegativeType = nullptr;

    CTypeVariable(const CSymbol& Name, const CTypeBase* Type, CScope& EnclosingScope)
        : CDefinition(StaticDefinitionKind, EnclosingScope, Name)
        , CNominalType(StaticTypeKind, EnclosingScope.GetProgram())
        , _Type(Type)
    {}

    const CTypeBase* GetType() const { return _Type; }
    void SetType(const CTypeBase* Type) { _Type = Type; }

    virtual EComparability GetComparability() const override
    {
        const CTypeType* TypeType = GetType() ? GetType()->GetNormalType().AsNullable<CTypeType>() : nullptr;
        return TypeType ? TypeType->PositiveType()->GetNormalType().GetComparability() : EComparability::Incomparable;
    }

    // CNominalType interface.
    virtual const CDefinition* Definition() const override { return this; }

    // CTypeBase interface.
    virtual CUTF8String AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const override
    {
        return _ExplicitParam ? _ExplicitParam->_ImplicitParam->AsNameStringView() : AsNameStringView();
    }
    virtual bool CanBeCustomAccessorDataType() const override { return false; };

    // CDefinition interface.
    void SetAstNode(CExprDefinition* AstNode) { CDefinition::SetAstNode(AstNode); }
    CExprDefinition* GetAstNode() const { return static_cast<CExprDefinition*>(CDefinition::GetAstNode()); }

    void SetIrNode(CExprDefinition* AstNode) { CDefinition::SetIrNode(AstNode); }
    CExprDefinition* GetIrNode(bool bForce = false) const { return static_cast<CExprDefinition*>(CDefinition::GetIrNode(bForce)); }

    virtual bool IsPersistenceCompatConstraint() const override { return false; }

private:
    const CTypeBase* _Type = nullptr;
};
}
