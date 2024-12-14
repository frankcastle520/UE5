// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Common/Containers/Array.h"
#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Containers/Function.h"
#include "uLang/Common/Text/UTF8String.h"
#include "uLang/Semantics/FilteredDefinitionRange.h"
#include "uLang/Semantics/MemberOrigin.h"
#include "uLang/Semantics/Revision.h"
#include "uLang/Semantics/SmallDefinitionArray.h"
#include "uLang/Semantics/StructOrClass.h"
#include "uLang/Semantics/VisitStamp.h"

namespace uLang
{
struct SAccessLevel;
struct SQualifier;
class CAstPackage;
class CAstCompilationUnit;
class CControlScope;
class CClass;
class CClassDefinition;
class CDataDefinition;
class CEnumeration;
class CEnumerator;
class CFunction;
class CInterface;
class CModule;
class CModulePart;
class CModuleAlias;
class CSnippet;
class CLogicalScope;
class CSemanticAnalyzerImpl;
class CSemanticProgram;
class CScopedAccessLevelDefinition;
class CTypeBase;
class CTypeAlias;
class CTypeScope;
class CTypeType;
class CTypeVariable;

/**
 * Stores a resolved definition and the context that it was resolved from
 */
struct SResolvedDefinition
{
    CDefinition*           _Definition;
    const CDataDefinition* _Context;

    SResolvedDefinition(CDefinition* Definition)                                 : _Definition(Definition), _Context(nullptr) {}
    SResolvedDefinition(CDefinition* Definition, const CDataDefinition* Context) : _Definition(Definition), _Context(Context) {}
};

/**
 * An array of resolved definitions and their associated contexts
 */
using SResolvedDefinitionArray = TArrayG<SResolvedDefinition, TInlineElementAllocator<1>>;

/**
 * A nested scope - program, module or class
 */
class VERSECOMPILER_API CScope
{
public:
    enum class EKind : uint8_t
    {
        Program,
        CompatConstraintRoot,
        Module,
        ModulePart,
        Snippet,
        Class,
        Function,
        ControlScope, // A nested scope within a function body
        Interface,
        Type,
        Enumeration
    };

    static const char* KindToCString(EKind Kind);

    CScope(EKind Kind, CScope* Parent, CSemanticProgram& Program) : _Kind(Kind), _Parent(Parent), _Program(Program) {}
    virtual ~CScope();

    // Delete the generated move/copy constructors so that they don't require full definitions of the types referenced by TSRefArray.
    CScope(const CScope&) = delete;
    CScope(CScope&&) = delete;

    virtual CSymbol GetScopeName() const = 0;
    virtual const CTypeBase* ScopeAsType() const { return nullptr; }
    virtual const CDefinition* ScopeAsDefinition() const { return nullptr; }

    virtual SAccessLevel GetDefaultDefinitionAccessLevel() const { return SAccessLevel::EKind::Internal; }

    ULANG_FORCEINLINE EKind GetKind() const { return _Kind; }
    ULANG_FORCEINLINE CScope* GetParentScope() const { return _Parent; }
    const CScope* GetScopeOfKind(EKind Kind) const;
    enum class EPathMode : uint8_t { Default, PrefixSeparator, PackageRelative, PackageRelativeWithRoot };
    CUTF8String GetScopePath(uLang::UTF8Char SeparatorChar = '.', EPathMode Mode = EPathMode::Default) const;
    const CModule* GetModule() const;
    CModule* GetModule();
    const CModulePart* GetModulePart() const;
    CModulePart* GetModulePart();
    CAstPackage* GetPackage() const;
    CAstCompilationUnit* GetCompilationUnit() const;
    const CSnippet* GetSnippet() const;
    const TSPtr<CSymbolTable>& GetSymbols() const;
    ULANG_FORCEINLINE CSemanticProgram& GetProgram() const { return _Program; }

    /// If this is a parametric type, get the scope of those parameters; otherwise returns this scope.
    const CScope& GetParametricTypeScope() const;

    /// Get the innermost logical scope that is or contains this scope.
    const CLogicalScope& GetLogicalScope() const;
    ULANG_FORCEINLINE CLogicalScope& GetLogicalScope() { return const_cast<CLogicalScope&>(static_cast<const CScope*>(this)->GetLogicalScope()); }

    /// Iff this scope is a logical scope, return it a pointer to it. Otherwise, return null.
    virtual const CLogicalScope* AsLogicalScopeNullable() const { return nullptr; }
    virtual CLogicalScope* AsLogicalScopeNullable() { return nullptr; }

    ULANG_FORCEINLINE bool IsLogicalScope() const { return AsLogicalScopeNullable() != nullptr; }

    // Check if this module is the same or a child of another
    bool IsSameOrChildOf(const CScope* Other) const;

    // Determines if this is either a function body or a nested scope within a function body
    bool IsControlScope() const { return _Kind == EKind::ControlScope || _Kind == EKind::Function; }

    // Determines if inside a type scope, ignoring control scope
    bool IsInsideTypeScope() const;

    // Determines if this is a module or snippet scope.
    bool IsModuleOrSnippet() const { return GetKind() == EKind::Module || GetKind() == EKind::ModulePart || GetKind() == EKind::Snippet; }

    // Determines if the definitions in this scope are built-in.
    bool IsBuiltInScope() const;

    CModule& CreateModule(const CSymbol& ModuleName);
    CClassDefinition& CreateClass(const CSymbol& ClassName, CClass* Superclass = nullptr, TArray<CInterface*>&& SuperInterfaces = {}, EStructOrClass StructOrClass = EStructOrClass::Class);
    CEnumeration& CreateEnumeration(const CSymbol& EnumerationName);
    CInterface& CreateInterface(const CSymbol& InterfaceName, const TArray<CInterface*>& SuperInterfaces = {});
    TSRef<CFunction> CreateFunction(const CSymbol FunctionName);
    virtual void CreateNegativeFunction(const CFunction& PositiveFunction) const {}
    TSRef<CDataDefinition> CreateDataDefinition(const CSymbol VarName);
    TSRef<CDataDefinition> CreateDataDefinition(const CSymbol VarName, const CTypeBase* Type);
    virtual void CreateNegativeDataDefinition(const CDataDefinition& PositiveDataDefinition) const {}
    TSRef<CTypeAlias> CreateTypeAlias(const CSymbol Name);
    TSRef<CTypeVariable> CreateTypeVariable(const CSymbol Name, const CTypeBase* Type);
    TSRef<CModuleAlias> CreateModuleAlias(const CSymbol Name);
    TSRef<CScopedAccessLevelDefinition> CreateAccessLevelDefinition(TOptional<CSymbol> ClassName);

    // Using declarations
    void AddUsingScope(const CLogicalScope* UsingScope) { _UsingScopes.AddUnique(UsingScope); }
    const TArray<const CLogicalScope*>& GetUsingScopes() const { return _UsingScopes; }

    // Add a local context to infer from a using declaration - return nullptr if added and conflicting context if type/value domain was already previously added
    const CDataDefinition* AddUsingInstance(const CDataDefinition* UsingContext);
    const TArray<const CDataDefinition*>& GetUsingInstances() const { return _UsingInstances; }

    static void ResolvedDefnsAppend(SResolvedDefinitionArray* ResolvedDefns, const SmallDefinitionArray& Definitions);
    static void ResolvedDefnsAppendWithContext(SResolvedDefinitionArray* ResolvedDefns, const SmallDefinitionArray& Definitions, const CDataDefinition* Context);

    /// Look for a definition in this scope and all parent scopes and aliases
    SResolvedDefinitionArray ResolveDefinition(const CSymbol& Name, const SQualifier& Qualifier = SQualifier::Unknown(), const CAstPackage* ContextPackage = nullptr) const;

    TSRef<CControlScope> CreateNestedControlScope(CSymbol Name = CSymbol());

    const TSRefArray<CControlScope>& GetNestedControlScopes() const { return _NestedControlScopes; }

    TSRef<CTypeScope> CreateNestedTypeScope();

    /// Generates a new stamp id
    static VisitStampType GenerateNewVisitStamp();

    // Determines whether this scope was authored by Epic
    bool IsAuthoredByEpic() const;

    // Determines whether this scope can access Epic-internal definitions.
    // This differs from IsEpicInternal by allowing packages with Scope=InternalUser to access epic-internal definitions.
    bool CanAccessEpicInternal() const;

protected:
    friend class CSemanticAnalyzerImpl;
    friend class CDefinition;
    friend class CDataDefinition;
    // Returns whether some definition is accessible from this scope.
    // When checking accessibility, you probably want to use CDefinition::IsAccessibleFrom
    // instead of this.
    bool CanAccess(const CDefinition& Definition, const SAccessLevel& DefinitionAccessLevel) const;

    // If we are a program, module etc.
    EKind _Kind;

    // The enclosing scope for this scope
    CScope* _Parent;

    // The semantic program these types belongs to
    CSemanticProgram& _Program;

    // `using` declarations referring to other scopes / modules
    TArray<const CLogicalScope*> _UsingScopes;

    // `using` declarations referring to implied contexts / receivers
    TArray<const CDataDefinition*> _UsingInstances;

    // Nested control scopes
    TSRefArray<CControlScope> _NestedControlScopes;

    // Nested type scopes
    TSRefArray<CTypeScope> _NestedTypeScopes;
};

/**
 * A scope that can contain definitions
 */
class VERSECOMPILER_API CLogicalScope : public CScope
{
public:

    CLogicalScope(EKind Kind, CScope* Parent, CSemanticProgram& Program) : CScope(Kind, Parent, Program), _LastVisitStamp(0) {}
    virtual ~CLogicalScope();

    // Delete the generated move/copy constructors so that they don't require full definitions of the types referenced by TSRefArray.
    CLogicalScope(const CLogicalScope&) = delete;
    CLogicalScope(CLogicalScope&&) = delete;

    /// Iterates through all the logical scopes nested inside this scope
    EIterateResult IterateRecurseLogicalScopes(const TFunction<EVisitResult(const CLogicalScope&)>& Functor) const;
    EIterateResult IterateRecurseLogicalScopes(TFunction<EVisitResult(const CLogicalScope&)>&& Functor) const;

    const TArray<TSRef<CDefinition>>& GetDefinitions() const { return _Definitions; }

    template<typename FilterClass>
    TFilteredDefinitionRange<FilterClass> GetDefinitionsOfKind() const;

    virtual SmallDefinitionArray FindDefinitions(
        const CSymbol& Name,
        EMemberOrigin Origin = EMemberOrigin::InheritedOrOriginal,
        const SQualifier& Qualifier = SQualifier::Unknown(),
        const CAstPackage* ContextPackage = nullptr,
        VisitStampType VisitStamp = GenerateNewVisitStamp()) const;

    template<typename FilterClass>
    FilterClass* FindFirstDefinitionOfKind(
        const CSymbol& Name,
        EMemberOrigin Origin = EMemberOrigin::InheritedOrOriginal,
        const SQualifier& Qualifier = SQualifier::Unknown(),
        const CAstPackage* ContextPackage = nullptr,
        VisitStampType VisitStamp = GenerateNewVisitStamp()) const;

    virtual void SetRevision(SemanticRevision Revision);
    SemanticRevision GetRevision() const { return _CumulativeRevision; }

    // If this scope has the given visit stamp, return false.
    // Otherwise, mark this scope with the visit stamp and return true.
    // Use CScope::GenerateNewVisitStamp to get a new visit stamp.
    ULANG_FORCEINLINE bool TryMarkVisited(VisitStampType VisitStamp) const
    {
        ULANG_ASSERTF(VisitStamp >= _LastVisitStamp, "Guard against situations where this is used in a nested context.");

        if (_LastVisitStamp == VisitStamp)
        {
            return false;
        }
        else
        {
            _LastVisitStamp = VisitStamp;
            return true;
        }
    }

    // Allocates an ordinal for the next definition in this scope.
    int32_t AllocateNextDefinitionOrdinal()
    {
        return _NextDefinitionOrdinal++;
    }

    // CScope interface.
    virtual const CLogicalScope* AsLogicalScopeNullable() const override { return this; }
    virtual CLogicalScope* AsLogicalScopeNullable() override { return this; }

    SQualifier AsQualifier() const;

protected:
    friend class CScope;

    // All definitions in this scope.
    TArray<TSRef<CDefinition>> _Definitions;

    /// When anything in this class (methods, data members etc.) or its subclasses was last modified/deleted
    SemanticRevision _CumulativeRevision = 1; // Initialize semantic revision to 1 to trigger full rebuild on first compile

    // To make sure we don't visit the same scope twice during an iteration
    mutable VisitStampType _LastVisitStamp { 0 };

private:
    // The next ordinal to assign to definitions within this scope.
    int32_t _NextDefinitionOrdinal{ 0 };
};

template<typename FilterClass>
TFilteredDefinitionRange<FilterClass> CLogicalScope::GetDefinitionsOfKind() const
{
    return TFilteredDefinitionRange<FilterClass>(_Definitions.begin(), _Definitions.end());
}

template<typename FilterClass>
FilterClass* CLogicalScope::FindFirstDefinitionOfKind(const CSymbol& Name, EMemberOrigin Origin, const SQualifier& Qualifier, const CAstPackage* ContextPackage, VisitStampType VisitStamp) const
{
    SmallDefinitionArray Definitions = FindDefinitions(Name, Origin, Qualifier, ContextPackage, VisitStamp);
    for (CDefinition* Definition : Definitions)
    {
        if (FilterClass* Result = Definition->AsNullable<FilterClass>())
        {
            return Result;
        }
    }
    return nullptr;
}

};
