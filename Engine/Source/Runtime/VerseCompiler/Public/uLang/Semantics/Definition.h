// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Common/Common.h"
#include "uLang/Common/Misc/Optional.h"
#include "uLang/Common/Text/Named.h"
#include "uLang/Semantics/Attributable.h"
#include "uLang/Semantics/VisitStamp.h"
#include "uLang/Semantics/AccessLevel.h"

namespace uLang
{
class CScope;
class CLogicalScope;
class CNominalType;

#define VERSE_ENUM_DEFINITION_KINDS(v) \
    v(Class, "class") \
    v(Data, "data") \
    v(Enumeration, "enumeration") \
    v(Enumerator, "enumerator") \
    v(Function, "function") \
    v(Interface, "interface") \
    v(Module, "module") \
    v(ModuleAlias, "module alias") \
    v(TypeAlias, "type alias") \
    v(TypeVariable, "type variable")

/// Information about a given qualifier.
struct SQualifier
{
    enum class EType : uint8_t
    {
        Local,    // i.e. `(local:)`, which is limited to a function's enclosing scopes.
        NominalType,  // This encompases any nominal type or module.
        Unknown    // Sentinel value for invalid/unresolved qualifiers.
    };

    /// The definition that the qualifier is referring to.
    const CNominalType* _Definition;

    EType _Type;

    static SQualifier NominalType(const CNominalType& NominalType)
    {
        return {._Definition = &NominalType, ._Type = EType::NominalType};
    };
    static SQualifier Local()
    {
        return {._Definition = nullptr, ._Type = EType::Local};
    };
    static SQualifier Unknown()
    {
        return {._Definition = nullptr, ._Type = EType::Unknown};
    };

    bool IsUnspecified() const
    {
        return _Type == EType::Unknown;
    }

    bool operator==(const SQualifier& Other) const
    {
        return _Type == Other._Type && _Definition == Other._Definition;
    }
};

/**
 * Reference to a pair of AST and IR nodes
 **/
template<class AstNodeType>
class TAstNodeRef
{
public:
    AstNodeType* GetAstNode() const { return _AstNode; }
    AstNodeType* GetIrNode(bool bForce = false) const { return (bForce || _IrNode) ? _IrNode : _AstNode; }
    void SetAstNode(AstNodeType* AstNode) { ULANG_ASSERTF(!_IrNode, "Called AST function when IR available"); _AstNode = AstNode; }
    void SetIrNode(AstNodeType* IrNode) { _IrNode = IrNode; }

private:
    AstNodeType* _AstNode{ nullptr };
    AstNodeType* _IrNode{ nullptr };
};

using CAstNodeRef = TAstNodeRef<CExpressionBase>;

/**
 * The base class of scoped definitions.
 */
class VERSECOMPILER_API CDefinition : public CAttributable, public CNamed, public TAstNodeRef<CExpressionBase>, public CSharedMix
{
public:

    enum class EKind : uint8_t
    {
    #define VISIT_KIND(Name, ...) Name,
        VERSE_ENUM_DEFINITION_KINDS(VISIT_KIND)
    #undef VISIT_KIND
    };

    CScope& _EnclosingScope;

    // An integer that represents the order the definition occurred in the parent scope:
    // The absolute value shouldn't be used, but if this value is greater than another definition
    // from the same scope, this definition occurred after the other.
    const int32_t _ParentScopeOrdinal;

    // If the definition overrides via an explicit qualifier, this is the qualifier type
    SQualifier _Qualifier;

    CDefinition(EKind Kind, CScope& EnclosingScope, const CSymbol& Name);

    ~CDefinition();

    // If this definition has the given visit stamp, return false.
    // Otherwise, mark this definition *and all definitions it overrides* with the visit stamp and
    // return true.
    // Use CScope::GenerateNewVisitStamp to get a new visit stamp.
    bool TryMarkOverriddenAndConstrainedDefinitionsVisited(VisitStampType VisitStamp) const
    {
        if (_PrototypeDefinition->_LastOverridingVisitStamp == VisitStamp)
        {
            return false;
        }

        // Mark this definition and the definitions overridden by it as visited.
        for (const CDefinition* OverriddenDefinition = this;
            OverriddenDefinition;
            OverriddenDefinition = OverriddenDefinition->GetOverriddenDefinition())
        {
            OverriddenDefinition->GetPrototypeDefinition()->_LastOverridingVisitStamp = VisitStamp;
        }

        if (_ConstrainedDefinition)
        {
            _ConstrainedDefinition->GetPrototypeDefinition()->_LastOverridingVisitStamp = VisitStamp;
        }

        return true;
    }

    // Casts
    EKind GetKind() const { return _Kind; }

    template<typename TDefinition>
    TDefinition& AsChecked();

    template<typename TDefinition>
    TDefinition const& AsChecked() const;

    template<typename TDefinition>
    bool IsA() const { return _Kind == TDefinition::StaticDefinitionKind; }

    template<typename TDefinition>
    TDefinition* AsNullable() { return IsA<TDefinition>() ? static_cast<TDefinition*>(this) : nullptr; }

    template<typename TDefinition>
    TDefinition const* AsNullable() const { return IsA<TDefinition>() ? static_cast<TDefinition const*>(this) : nullptr; }

    // Accessors
    const CDefinition* GetOverriddenDefinition() const
    {
        return _OverriddenDefinition;
    }
    const CDefinition& GetBaseOverriddenDefinition() const
    {
        const CDefinition* BaseOverriddenDefinition = this;
        while (BaseOverriddenDefinition->GetOverriddenDefinition() != nullptr)
        {
            BaseOverriddenDefinition = BaseOverriddenDefinition->GetOverriddenDefinition();
        }
        return *BaseOverriddenDefinition;
    }

    const CDefinition* GetPrototypeDefinition() const { return _PrototypeDefinition; }

    void SetConstrainedDefinition(const CDefinition& ConstrainedDefinition)
    {
        ULANG_ASSERTF(_PrototypeDefinition == this, "Setting constrained definition on instantiated definition");
        _ConstrainedDefinition = &ConstrainedDefinition;
    }
    const CDefinition* GetConstrainedDefinition() const
    {
        return _PrototypeDefinition->_ConstrainedDefinition;
    }

    CExpressionBase* GetAstNode() const { return _PrototypeDefinition->CAstNodeRef::GetAstNode(); }
    CExpressionBase* GetIrNode(bool bForce = false) const { return _PrototypeDefinition->CAstNodeRef::GetIrNode(bForce); }

    void SetAccessLevel(const TOptional<SAccessLevel>& AccessLevel)
    {
        ULANG_ASSERTF(_PrototypeDefinition == this, "Setting access level on instantiated definition");
        _AccessLevel = AccessLevel;
    }
    // AccessLevel declared on this specific definition.
    const TOptional<SAccessLevel>& SelfAccessLevel() const { return _PrototypeDefinition->_AccessLevel; }
    // This is the true AccessLevel of this definition. This is what you want to use most of the time when checking if
    // it's accessible.
    SAccessLevel DerivedAccessLevel() const;

    // Returns whether this is a member of some instantiated type like a class or interface.
    bool IsInstanceMember() const;

    // Returns whether this definition has the deprecated attribute.
    bool IsDeprecated() const;

    // Returns whether this definition has the experimental attribute.
    bool IsExperimental() const;

    // Returns whether this is a final field of an inheritable type like a class or interface.
    bool IsFinal() const;

    // If the definition is native, returns the native specifier expression.
    const CExpressionBase* GetNativeSpecifierExpression() const;

    // Returns whether this is a native definition.
    bool IsNative() const;

    // Returns whether this is a built-in definition.
    bool IsBuiltIn() const;

    // If this definition has a corresponding scope, yield it.
    virtual const CLogicalScope* DefinitionAsLogicalScopeNullable() const { return nullptr; }

    // These classes of functions are helpers to get to the scope of where
    // the definition or definition's var access scope is defined. In Verse,
    // access scopes are inherited when not explicitly defined. Note, this isn't
    // the same as saying the same specifier is passed along the inheritance chain.
    //
    // For example:
    // M1<public> := module {
    //     Base<public> := class {
    //         Field<internal>:int = 42
    //     }
    //     M2<public> := module:
    //         Child<public> := class(Base) {
    //             Field<override>:int = 50
    //         }
    // }
    // M2.Child's Field property is still accessible as internal to M1, not internal to M2.
    const CDefinition& GetDefinitionAccessibilityRoot() const
    {
        return *GetBaseOverriddenDefinition().GetPrototypeDefinition();
    }

    bool IsAccessibleFrom(const CScope&) const;

    virtual bool IsPersistenceCompatConstraint() const = 0;

    void SetOverriddenDefinition(const CDefinition& OverriddenDefinition)
    {
        SetOverriddenDefinition(&OverriddenDefinition);
    }
    void SetOverriddenDefinition(const CDefinition* OverriddenDefinition)
    {
        _OverriddenDefinition = OverriddenDefinition;
    }

    // Find a definition that corresponds to the nearest enclosing scope with a corresponding definition.
    // If there is none, returns nullptr.
    const CDefinition* GetEnclosingDefinition() const;

    /// Determines the qualifier for this definition, even if not explicitly specified (from the original source).
    SQualifier GetImplicitQualifier() const;

protected:

    // Setters that should be wrapped by a subclass to restrict the parameter type.
    void SetPrototypeDefinition(const CDefinition& PrototypeDefinition)
    {
        ULANG_ASSERTF(PrototypeDefinition.GetPrototypeDefinition() == &PrototypeDefinition, "Setting instantiated definition as prototype");
        _PrototypeDefinition = &PrototypeDefinition;
    }
    void SetAstNode(CExpressionBase* AstNode)
    {
        ULANG_ASSERTF(_PrototypeDefinition == this, "Setting IR node on instantiated definition");
        CAstNodeRef::SetAstNode(AstNode);
    }
    void SetIrNode(CExpressionBase* IrNode)
    {
        ULANG_ASSERTF(_PrototypeDefinition == this, "Setting IR node on instantiated definition");
        CAstNodeRef::SetIrNode(IrNode);
    }

private:
    const EKind _Kind;

    // The stamp for the last time an overriding definition was visited.
    mutable VisitStampType _LastOverridingVisitStamp{};

    // If the definition was instantiated from a polymorphic prototype, this will reference the
    // prototype definition. Otherwise, it will reference this definition.
    const CDefinition* _PrototypeDefinition{ this };

    // If the definition overrides an inherited definition, this will reference the inherited definition.
    const CDefinition* _OverriddenDefinition{ nullptr };

    // If this definition is a constraint on some other definition, this will reference the constrained definition.
    const CDefinition* _ConstrainedDefinition{ nullptr };

    // The definition's access level.
    TOptional<SAccessLevel> _AccessLevel;
};

VERSECOMPILER_API CUTF8String GetQualifiedNameString(const CDefinition& Definition);
VERSECOMPILER_API const char* DefinitionKindAsCString(CDefinition::EKind Kind);

template<typename TDefinition>
TDefinition& CDefinition::AsChecked()
{
    ULANG_ASSERTF(IsA<TDefinition>(), "Failed to cast %s to %s.", DefinitionKindAsCString(_Kind), DefinitionKindAsCString(TDefinition::StaticDefinitionKind));
    return *static_cast<TDefinition*>(this);
}

template<typename TDefinition>
TDefinition const& CDefinition::AsChecked() const
{
    ULANG_ASSERTF(IsA<TDefinition>(), "Failed to cast %s to %s.", DefinitionKindAsCString(_Kind), DefinitionKindAsCString(TDefinition::StaticDefinitionKind));
    return *static_cast<const TDefinition*>(this);
}

}  // namespace uLang
