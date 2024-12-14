// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Semantics/Definition.h"
#include "uLang/Semantics/Expression.h"
#include "uLang/Semantics/Revision.h"
#include "uLang/Semantics/Signature.h"
#include "uLang/Semantics/ControlScope.h"
#include "uLang/Semantics/SemanticTypes.h"
#include "uLang/Common/Text/Named.h"
#include "uLang/Common/Misc/Optional.h"

namespace uLang
{

enum class EFunctionStringFlag : uint16_t
{
    Qualified        = 1<<1,  // Prepends the scope `(/MyModule:)` (unless async or native) and then the name of the function
    QualifiedDotted  = 1<<2,  // Prepends the scope `MyModule.` (unless async or native) and then the name of the function

    // Common sets
    NoScope_        = 0x0,              // function(:Type1,:Type2)
    Overload_       = Qualified,        // (/MyModule:)function(:Type1,:Type2)
    OverloadDotted_ = QualifiedDotted,  // MyModule.function(:Type1,:Type2)

    // Masks
    Scoped_         = Qualified|QualifiedDotted
};

/* No longer used, but good ideas
enum class EFunctionStringFlag : uint16_t
{
    // Name part - omitted or one of:
    Name          = 1<<0,  // Prepend name of function
    Qualified     = 1<<1,  // Prepends the scope `(/MyModule:)` and then the name of the function

    // Type signature part - omitted or one of:
    SigSimple     = 1<<2,  // Include just parentheses ()
    SigTyped      = 1<<3,  // Include parentheses (), any parameter names and types and result type (param1 : Type1, param2 : Type2) : ResultType
    SigDefaults   = 1<<4,  // Include parentheses (), any parameter names, types and defaults and result type (param1 : Type1, param2 : Type2 = default2 ) : ResultType

    // Body - omitted or one of:
    BodyIndicator = 1<<5,  // {...} for uLang body and empty for atomic C++ body
    Body          = 1<<6,  // full uLang body or empty for atomic C++ body

    // Spacing - omitted (canonical with multi-line) or:
    Inline        = 1<<7,  // try to have everything on one line

        // Masks
        Default_ = Qualified|SigDefaults|Body,  // MyClass@function(param1 : Type1, param2 : Type2 = default2 ) : ResultType { }
        DefaultUnnamed_ = SigDefaults|Body,     // (param1 : Type1, param2 : Type2 = default2 ) : ResultType { }
        DefaultIdent_ = Qualified|SigSimple,    // MyClass@function()

        Named_ = Name|Qualified,
        Signature_ = SigSimple|SigTyped|SigDefaults,
        Body_ = BodyIndicator|Body,
        Unimplemented_ = SigTyped|SigDefaults|Body
};
*/

/** Distinguishes extension field accessor functions from other functions. */
enum class EExtensionFieldAccessorKind
{
    Function,
    ExtensionDataMember,
    ExtensionMethod
};

/**
 * Function scope, signature and body.
 *
 * All sub-expressions have their code text indexes relative to this containing context.
 **/
class VERSECOMPILER_API CFunction : public CDefinition, public CLogicalScope
{
public:
    static const CDefinition::EKind StaticDefinitionKind = CDefinition::EKind::Function;
    friend class CExprFunctionDefinition;

    /// Signature - parameter interface
    SSignature _Signature;

    EExtensionFieldAccessorKind _ExtensionFieldAccessorKind = EExtensionFieldAccessorKind::Function;

    const CFunctionType* _NegativeType = nullptr;

    CFunction(int32_t Index, const CSymbol& FunctionName, CScope& EnclosingScope);

    int32_t Index() const;

    //~ Begin CScope interface
    virtual CSymbol GetScopeName() const override { return CNamed::GetName(); }
    virtual const CDefinition* ScopeAsDefinition() const override { return this; }
    //~ End CScope interface

    void SetOverriddenDefinition(const CFunction& OverriddenDefinition) { CDefinition::SetOverriddenDefinition(OverriddenDefinition); }
    void SetOverriddenDefinition(const CFunction* OverriddenDefinition) { CDefinition::SetOverriddenDefinition(OverriddenDefinition); }
    const CFunction* GetOverriddenDefinition() const
    {
        const CDefinition* OverriddenDefinition = CDefinition::GetOverriddenDefinition();
        return OverriddenDefinition ? &OverriddenDefinition->AsChecked<CFunction>() : nullptr;
    }
    const CFunction& GetBaseOverriddenDefinition() const
    {
        return CDefinition::GetBaseOverriddenDefinition().AsChecked<CFunction>();
    }

    const CFunction& GetBaseCoercedOverriddenFunction() const
    {
        const CFunction* I = this;
        for (;;)
        {
            if (I->GetPrototypeDefinition()->IsCoercedOverride())
            {
                break;
            }
            const CFunction* Next = I->GetOverriddenDefinition();
            if (!Next)
            {
                break;
            }
            I = Next;
        }
        return *I;
    }

    void SetSignature(SSignature&& Signature, SemanticRevision NextRevision);
    void MapSignature(const CFunctionType& FuncType, SemanticRevision NextRevision);

    TSPtr<CExpressionBase> GetBodyAst() const { ULANG_ASSERTF(!GetIrNode(true), "Called AST function on when IR available");  return GetAstNode() ? GetAstNode()->Value() : nullptr; }

    TSPtr<CExprClassDefinition> GetBodyClassDefinitionAst() const;

    TSPtr<CExprInterfaceDefinition> GetBodyInterfaceDefinitionAst() const;

    TSPtr<CExpressionBase> GetReturnTypeAst() const { ULANG_ASSERTF(!GetIrNode(true), "Called AST function on when IR available");  return GetAstNode() ? GetAstNode()->ValueDomain() : nullptr; }
    
    TSPtr<CExpressionBase> GetDefineeAst() const { ULANG_ASSERTF(!GetIrNode(true), "Called AST function on when IR available"); return GetAstNode() ? GetAstNode()->Element() : nullptr; }

    CExpressionBase* GetBodyIr() const { return GetIrNode()->Value(); }

    CExprClassDefinition* GetBodyClassDefinitionIr() const;

    CExprInterfaceDefinition* GetBodyInterfaceDefinitionIr() const;

    CExpressionBase* GetReturnTypeIr() const { return GetIrNode()->ValueDomain(); }

    SemanticRevision GetRevision() const { return CMath::Max(_SignatureRevision, _BodyRevision); }
    SemanticRevision GetSignatureRevision() const { return _SignatureRevision; }
    SemanticRevision GetBodyRevision() const { return _BodyRevision; }

    /**
        * This is a holdover from old semantics, where functions were assumed to be a member of a class.
        * THIS IS NO LONGER THE CASE, and we should forgo continued use of this function (we use it 
        * in the few places where this assumption still holds true).
        * 
        * @TODO: SOL-1567, we should never need to explicitly query for a function's class, when 
        *        functions could belong to a module, interface, other function etc.
        */
    TOptional<const CClass*> GetMaybeClassScope() const;
    TOptional<const CModule*> GetMaybeModuleScope() const;
    TOptional<const CNominalType*> GetMaybeContextType() const;

    /** Returns a decorated name for this function that includes its signature, for use in overloading. */
    CUTF8String GetDecoratedName(uint16_t StrFlags = uint16_t(EFunctionStringFlag::Overload_)) const;

    void MarkCoercion() { _bCoercion = true; }

    bool IsCoercion() const { return _bCoercion; }

    void MarkCoercedOverride() { _bCoercedOverride = true; }

    bool IsCoercedOverride() const { return _bCoercedOverride; }

    bool HasImplementation() const;
    bool IsNative() const;

    // CDefinition interface.
    void SetPrototypeDefinition(const CFunction& PrototypeDefinition) { CDefinition::SetPrototypeDefinition(PrototypeDefinition); }
    const CFunction* GetPrototypeDefinition() const { return static_cast<const CFunction*>(CDefinition::GetPrototypeDefinition()); }

    void SetAstNode(CExprFunctionDefinition* AstNode);
    CExprFunctionDefinition* GetAstNode() const;

    void SetIrNode(CExprFunctionDefinition* AstNode);
    CExprFunctionDefinition* GetIrNode(bool bForce = false) const;

    virtual const CLogicalScope* DefinitionAsLogicalScopeNullable() const override { return this; }

    virtual bool IsPersistenceCompatConstraint() const override { return false; }

    mutable bool _bIsAccessorOfSomeClassVar = false; // HACK: using mutable here to set this after the fact

private:
    const int32_t _Index;

    /// `true` if this function was generated by the IR generator to apply
    /// coercions to the argument to and result of some other function.
    bool _bCoercion{false};

    /// `true` if this function needed a coercion to be generated to match an
    /// overridden function.  Note both this function and the coercion will have
    /// their overridden definition set.
    bool _bCoercedOverride{false};

    /// Revision of the signature
    SemanticRevision _SignatureRevision;

    /// Revision of the body
    SemanticRevision _BodyRevision;
};

}  // namespace uLang
