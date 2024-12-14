// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Semantics/Definition.h"
#include "uLang/Semantics/SemanticScope.h"
#include "uLang/Common/Text/Symbol.h"
#include "uLang/Common/Misc/Optional.h"
#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Containers/Map.h"

namespace uLang
{
class CExprDefinition;
class CExprIdentifierFunction;
class CTypeBase;

static SAccessLevel::EKind DefaultVarAccessLevelKind = SAccessLevel::EKind::Public;

struct SClassVarAccessorFunctions
{
    TMap<int, const CFunction*> Getters;
    TMap<int, const CFunction*> Setters;

    CSymbol GetterName;
    CSymbol SetterName;

    explicit operator bool() const
    {
        return Getters.Num() && Setters.Num();
    }
};

/**
 *  Joining structure, making data-members attributable.
 **/
class VERSECOMPILER_API CDataDefinition : public CDefinition
{
public:
    static const CDefinition::EKind StaticDefinitionKind = CDefinition::EKind::Data;
    friend class CExprDataDefinition;

    bool _bNamed = false;  // Named member - must be explicitly ?named rather than determined by index

    // The type of this data definition in the negative position.
    const CTypeBase* _NegativeType = nullptr;

    // A parameter `X` of type `type` is encoded as `:type(X, X) where X:type`.
    // `_ImplicitParam` points to the corresponding type variable.
    const CTypeVariable* _ImplicitParam = nullptr;

    CDataDefinition(const CSymbol& IdentName, CScope& EnclosingScope)
        : CDataDefinition(IdentName, EnclosingScope, nullptr)
    {
    }

    CDataDefinition(const CSymbol& IdentName, CScope& EnclosingScope, const CTypeBase* Type)
        : CDefinition(StaticDefinitionKind, EnclosingScope, IdentName)
        , _Type(Type)
    {
    }

    // CDefinition interface.
    void SetPrototypeDefinition(const CDataDefinition& PrototypeDefinition) { CDefinition::SetPrototypeDefinition(PrototypeDefinition); }
    const CDataDefinition* GetPrototypeDefinition() const { return static_cast<const CDataDefinition*>(CDefinition::GetPrototypeDefinition()); }

    void SetAstNode(CExprDefinition* AstNode);
    CExprDefinition* GetAstNode() const;

    void SetIrNode(CExprDefinition* AstNode);
    CExprDefinition* GetIrNode(bool bForce = false) const;

    void             SetType(const CTypeBase* Type) { _Type = Type; }
    const CTypeBase* GetType() const { return _Type; }

    CUTF8String GetScopePath(uLang::UTF8Char SeparatorChar = '.', CScope::EPathMode Mode = CScope::EPathMode::Default) const;

    void SetOverriddenDefinition(const CDataDefinition& OverriddenDefinition) { CDefinition::SetOverriddenDefinition(OverriddenDefinition); }
    const CDataDefinition* GetOverriddenDefinition() const
    {
        const CDefinition* OverriddenDefinition = CDefinition::GetOverriddenDefinition();
        return OverriddenDefinition ? &OverriddenDefinition->AsChecked<CDataDefinition>() : nullptr;
    }
    const CDataDefinition& GetBaseOverriddenDefinition() const
    {
        return CDefinition::GetBaseOverriddenDefinition().AsChecked<CDataDefinition>();
    }

    void SetHasInitializer()
    {
        ULANG_ASSERT(GetPrototypeDefinition() == this);
        _bHasInitializer = true;
    }
    bool HasInitializer() const
    {
        return GetPrototypeDefinition()->_bHasInitializer;
    }

    void SetVarAccessLevel(TOptional<SAccessLevel>&& AccessLevel)
    {
        ULANG_ASSERT(GetPrototypeDefinition() == this);
        ULANG_ASSERT(IsVar());
        _VarAccessLevel = Move(AccessLevel);
    }
    void SetIsVar()
    {
        ULANG_ASSERT(GetPrototypeDefinition() == this);
        _bIsVar = true;
    }
    const TOptional<SAccessLevel>& SelfVarAccessLevel() const
    {
        ULANG_ASSERT(IsVar());
        return GetPrototypeDefinition()->_VarAccessLevel;
    }
    bool IsVar() const
    {
        return GetPrototypeDefinition()->_bIsVar;
    }
    SAccessLevel DerivedVarAccessLevel() const
    {
        ULANG_ASSERT(IsVar());
        return GetDefinitionAccessibilityRoot().AsChecked<CDataDefinition>().SelfVarAccessLevel().Get(DefaultVarAccessLevelKind);
    }

    bool IsVarWritableFrom(const CScope&) const;

    const CDataDefinition& GetDefinitionVarAccessibilityRoot() const
    {
        return GetDefinitionAccessibilityRoot().AsChecked<CDataDefinition>();
    }

    bool IsModuleScopedVar() const;

    void MarkPersistenceCompatConstraint() const;

    virtual bool IsPersistenceCompatConstraint() const override;

    SClassVarAccessorFunctions _OptionalAccessors;
    bool CanHaveCustomAccessors() const;

private:
    TOptional<SAccessLevel> _VarAccessLevel;
    const CTypeBase* _Type{nullptr};
    mutable bool _bPersistenceCompatConstraint{false};
    bool _bIsVar{false};
    bool _bHasInitializer{ false };
};

}  // namespace uLang
