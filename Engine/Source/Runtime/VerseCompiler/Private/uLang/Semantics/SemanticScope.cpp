// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Semantics/SemanticScope.h"

#include "uLang/Common/Algo/AnyOf.h"
#include "uLang/Semantics/AvailableAttributeUtils.h"
#include "uLang/Semantics/ControlScope.h"
#include "uLang/Semantics/MemberOrigin.h"
#include "uLang/Semantics/ModuleAlias.h"
#include "uLang/Semantics/ScopedAccessLevelType.h"
#include "uLang/Semantics/SemanticEnumeration.h"
#include "uLang/Semantics/SemanticProgram.h"
#include "uLang/Semantics/SmallDefinitionArray.h"
#include "uLang/Semantics/StructOrClass.h"
#include "uLang/Semantics/TypeAlias.h"
#include "uLang/Semantics/TypeScope.h"
#include "uLang/Semantics/TypeVariable.h"
#include "uLang/Semantics/VisitStamp.h"

namespace uLang
{

//=======================================================================================
// CScope
//=======================================================================================

CScope::~CScope()
{
}

const CScope* CScope::GetScopeOfKind(EKind Kind) const
{
    for (const CScope* Scope = this; Scope; Scope = Scope->_Parent)
    {
        if (Scope->_Kind == Kind)
        {
            return Scope;
        }
    }

    return nullptr;
}

CUTF8String CScope::GetScopePath(uLang::UTF8Char SeparatorChar, EPathMode Mode) const
{
    // If needed, determine scope of package
    const CScope* PackageRootScope = nullptr;
    const CScope* RelativeScope = nullptr;
    if (Mode == EPathMode::PackageRelative || Mode == EPathMode::PackageRelativeWithRoot)
    {
        // Is this scope underneath a package?
        if (const CAstPackage* Package = GetPackage())
        {
            // Yes, use root module of the package
            PackageRootScope = Package->_RootModule;

            // Shall we include the root module itself in the scope path?
            if (PackageRootScope && Mode == EPathMode::PackageRelativeWithRoot)
            {
                // Yes, but not if it's the top level module
                const CScope* PackageRootParentScope = PackageRootScope->GetParentScope();
                if (PackageRootParentScope && PackageRootParentScope->GetKind() == CScope::EKind::ModulePart)
                {
                    // Safe to go one step up, we're not at the top level module yet
                    PackageRootScope = PackageRootParentScope;
                }
            }
        }
        else
        {
            // This can happen for the built-in Verse definitions. Just use the module as the
            // package root.
            ULANG_ASSERTF(IsBuiltInScope(),
                "Did not expect null package for %s",
                GetScopePath('/', EPathMode::PrefixSeparator).AsCString());
            PackageRootScope = GetModule();
        }

        ULANG_ENSUREF(PackageRootScope, "Package-relative scope path for scope `%s` can not be determined.", GetScopeName().AsCString());
        RelativeScope = PackageRootScope;
    }

    // Count symbols
    int32_t NumSymbols = 0;
    for (const CScope* Scope = this;
        Scope != RelativeScope;
        Scope = Scope->GetParametricTypeScope().GetParentScope())
    {
        if (Scope->IsLogicalScope() && Scope->GetKind() != CScope::EKind::CompatConstraintRoot)
        {
            ++NumSymbols;
        }
    }

    // Gather symbols
    const CScope** Scopes = (const CScope**)alloca(NumSymbols * sizeof(const CScope*));
    int32_t Index = NumSymbols;
    for (const CScope* Scope = this;
        Scope != RelativeScope;
        Scope = Scope->GetParametricTypeScope().GetParentScope())
    {
        if (Scope->IsLogicalScope() && Scope->GetKind() != CScope::EKind::CompatConstraintRoot)
        {
            // Help out static analysis to realize that Scopes must be non-null here (because we shouldn't reach here unless NumSymbols>0).
            ULANG_ASSERT(Scopes);

            Scopes[--Index] = Scope;
        }
    }
    ULANG_ASSERT(Index == 0);

    // Build path
    CUTF8StringBuilder Path;
    for (Index = 0; Index < NumSymbols; ++Index)
    {
        const CScope* Scope = Scopes[Index];
        if (Scope->GetKind() == CScope::EKind::CompatConstraintRoot)
        {
            continue;
        }

        // Use the parent function scope name of parametric types for display.
        Scope = &Scope->GetParametricTypeScope();

        const CSymbol& ScopeName = Scope->GetScopeName();
        if (ScopeName.IsNull())
        {
            continue;
        }

        if (Path.IsFilled() || Mode == EPathMode::PrefixSeparator)
        {
            Path.Append(SeparatorChar);
        }

        Path.Append(ScopeName.AsStringView());
    }

    return Path.MoveToString();
}

const CModule* CScope::GetModule() const
{
    for (const CScope* Scope = this; Scope; Scope = Scope->_Parent)
    {
        if (Scope->_Kind == EKind::Module)
        {
            return static_cast<const CModule*>(Scope);
        }

        if (Scope->_Kind == EKind::ModulePart)
        {
            return static_cast<const CModulePart*>(Scope)->CModulePart::GetModule();
        }
    }

    return nullptr;
}

CModule* CScope::GetModule()
{
    return const_cast<CModule*>(const_cast<const CScope*>(this)->GetModule());
}

const CModulePart* CScope::GetModulePart() const
{
    return static_cast<const CModulePart*>(GetScopeOfKind(EKind::ModulePart));
}

CModulePart* CScope::GetModulePart()
{
    return const_cast<CModulePart*>(const_cast<const CScope*>(this)->GetModulePart());
}

CAstPackage* CScope::GetPackage() const
{
    for (const CScope* Scope = this; Scope; Scope = Scope->_Parent)
    {
        if (Scope->_Kind == EKind::Module)
        {
            return static_cast<const CModule*>(Scope)->GetIrPackage();
        }
        if (Scope->_Kind == EKind::ModulePart)
        {
            return static_cast<const CModulePart*>(Scope)->GetIrPackage();
        }
    }
    return nullptr;
}

CAstCompilationUnit* CScope::GetCompilationUnit() const
{
    CAstPackage* Package = GetPackage();
    return Package ? Package->_CompilationUnit : nullptr;
}

const CSnippet* CScope::GetSnippet() const
{
    return static_cast<const CSnippet*>(GetScopeOfKind(EKind::Snippet));
}

const TSPtr<CSymbolTable>& CScope::GetSymbols() const
{
    return _Program.GetSymbols();
}

const CScope& CScope::GetParametricTypeScope() const
{
    const CScope* Scope = this;
    if ((GetKind() == EKind::Class || GetKind() == EKind::Interface) &&
        (_Parent && _Parent->GetKind() == CScope::EKind::Function))
    {
        Scope = _Parent;
    }
    return *Scope;
}

const CLogicalScope& CScope::GetLogicalScope() const
{
    const CScope* Scope = this;
    while (true)
    {
        const CLogicalScope* LogicalScope = Scope->AsLogicalScopeNullable();
        if (LogicalScope)
        {
            return *LogicalScope;
        }

        Scope = Scope->_Parent;
    }
}

bool CScope::IsSameOrChildOf(const CScope* Other) const
{
    for (const CScope* Scope = this; Scope; Scope = Scope->_Parent)
    {
        if (Scope == Other)
        {
            return true;
        }
    }

    return false;
}

bool CScope::IsInsideTypeScope() const
{
    const CScope* Scope = this;
    while (Scope && Scope->_Kind == EKind::ControlScope)
    {
        Scope = Scope->_Parent;
    }
    return Scope && Scope->_Kind == EKind::Type;
}

bool CScope::IsBuiltInScope() const
{
    const CAstPackage* Package = GetPackage();
    return Package && Package == GetProgram()._BuiltInPackage;
}

CModule& CScope::CreateModule(const CSymbol& ModuleName)
{
    // Modules are always nested directly inside their enclosing logical scope, only module parts know the exact scope hierarchy
    TSRef<CModule> NewModule = TSRef<CModule>::New(ModuleName, GetLogicalScope());
    CModule& Module = *NewModule;
    GetLogicalScope()._Definitions.Add(Move(NewModule));

    return Module;
}

CClassDefinition& CScope::CreateClass(const CSymbol& ClassName, CClass* Superclass, TArray<CInterface*>&& SuperInterfaces, EStructOrClass StructOrClass)
{
    TSRef<CClassDefinition> NewClass = TSRef<CClassDefinition>::New(ClassName, *this, Superclass, Move(SuperInterfaces), StructOrClass);
    CClassDefinition& Class = *NewClass;
    GetLogicalScope()._Definitions.Add(Move(NewClass));
    return Class;
}

TSRef<CScopedAccessLevelDefinition> CScope::CreateAccessLevelDefinition(TOptional<CSymbol> ClassName)
{
    TSRef<CScopedAccessLevelDefinition> NewClass = TSRef<CScopedAccessLevelDefinition>::New(ClassName, *this);
    if (!NewClass->_IsAnonymous)
    {
        GetLogicalScope()._Definitions.Add(NewClass);
    }
    return NewClass;
}

CInterface& CScope::CreateInterface(const CSymbol& InterfaceName, const TArray<CInterface*>& SuperInterfaces)
{
    TSRef<CInterface> NewInterface = TSRef<CInterface>::New(InterfaceName, *this, SuperInterfaces);
    CInterface& Interface = *NewInterface;
    GetLogicalScope()._Definitions.Add(Move(NewInterface));
    return Interface;
}

CEnumeration& CScope::CreateEnumeration(const CSymbol& EnumerationName)
{
    TSRef<CEnumeration> NewEnumeration = TSRef<CEnumeration>::New(EnumerationName, *this);
    CEnumeration& Enumeration = *NewEnumeration;
    GetLogicalScope()._Definitions.Add(Move(NewEnumeration));
    return Enumeration;
}

TSRef<CFunction> CScope::CreateFunction(const CSymbol FunctionName)
{
    TSRef<CFunction> NewFunction = TSRef<CFunction>::New(
        _Program.NextFunctionIndex(),
        FunctionName,
        *this);

    GetLogicalScope()._Definitions.Add(NewFunction);

    return NewFunction;
}

TSRef<CDataDefinition> CScope::CreateDataDefinition(const CSymbol VarName)
{
    TSRef<CDataDefinition> NewDataDef = TSRef<CDataDefinition>::New(VarName, *this);
    GetLogicalScope()._Definitions.Add(NewDataDef);
    return NewDataDef;
}

TSRef<CDataDefinition> CScope::CreateDataDefinition(const CSymbol VarName, const CTypeBase* Type)
{
    TSRef<CDataDefinition> NewDataDef = TSRef<CDataDefinition>::New(VarName, *this, Type);
    GetLogicalScope()._Definitions.Add(NewDataDef);
    return NewDataDef;
}

TSRef<CTypeAlias> CScope::CreateTypeAlias(const CSymbol Name)
{
    TSRef<CTypeAlias> NewTypeAlias = TSRef<CTypeAlias>::New(Name, *this);
    GetLogicalScope()._Definitions.Add(NewTypeAlias);
    return NewTypeAlias;
}

TSRef<CTypeVariable> CScope::CreateTypeVariable(const CSymbol Name, const CTypeBase* Type)
{
    TSRef<CTypeVariable> NewTypeVariable = TSRef<CTypeVariable>::New(Name, Type, *this);
    GetLogicalScope()._Definitions.Add(NewTypeVariable);
    return NewTypeVariable;
}

TSRef<CModuleAlias> CScope::CreateModuleAlias(const CSymbol Name)
{
    TSRef<CModuleAlias> NewModuleAlias = TSRef<CModuleAlias>::New(Name, *this);
    GetLogicalScope()._Definitions.Add(NewModuleAlias);
    return NewModuleAlias;
}

const CDataDefinition* CScope::AddUsingInstance(const CDataDefinition* UsingContext)
{
    const CTypeBase* NewType = UsingContext->GetType();
    
    for (const CDataDefinition* ExistingDef : _UsingInstances)
    {
        // Ensure existing types are not same type or subtype of the using context type.
        // - an unrelated type is best though a using context type which is a subtype is
        // permissible but it will need to qualify use of any overlapping conflict.
        // `CSemanticAnalyzerImpl::AnalyzeMacroCall_Using ()` uses a similar test mechanism.
        // [Note that `IsSubtype()` also matches for same type.]
        if (SemanticTypeUtils::IsSubtype(ExistingDef->GetType(), NewType))
        {
            // Type being added already exists - do not add and return conflicting context
            return ExistingDef;
        }
    }

    _UsingInstances.Add(UsingContext);

    return nullptr;
}

void CScope::ResolvedDefnsAppend(SResolvedDefinitionArray* ResolvedDefns, const SmallDefinitionArray& Definitions)
{
    ResolvedDefns->Reserve(ResolvedDefns->Num() + Definitions.Num());
    for (CDefinition* Definition : Definitions)
    {
        ResolvedDefns->Emplace(Definition);
    }
}

void CScope::ResolvedDefnsAppendWithContext(SResolvedDefinitionArray* ResolvedDefns, const SmallDefinitionArray& Definitions, const CDataDefinition* Context)
{
    ResolvedDefns->Reserve(ResolvedDefns->Num() + Definitions.Num());
    for (CDefinition* Definition : Definitions)
    {
        ResolvedDefns->Emplace(Definition, Context);
    }
}

SResolvedDefinitionArray CScope::ResolveDefinition(const CSymbol& Name, const SQualifier& Qualifier, const CAstPackage* ContextPackage) const
{
    ULANG_ASSERTF(!Name.IsNull(), "Null names are reserved for anonymous variables");
    VisitStampType VisitStamp = GenerateNewVisitStamp();
    SResolvedDefinitionArray Result;

    // NOTE: (yiliang.siew) For `(local:)`, we first figure out which function's scope will act as the point where we
    // stop considering any futher definitions. If we're not resolving a `(local:)` definition, this has no effect.
    const CScope* LimitingScope = nullptr;
    if (Qualifier._Type == SQualifier::EType::Local)
    {
        // NOTE: (yiliang.siew) In order to account for the special case of parametric classes
        // (which is really just a normal class nested inside a function), we can't just stop the search
        // once we hit the first function scope found; we keep going up the hierarchy until we've exhausted all
        // functions.
        for (const CScope* Scope = this; Scope; Scope = Scope->GetParentScope())
        {
            if (Scope->GetKind() == CScope::EKind::Function)
            {
                LimitingScope = Scope->GetParentScope();
            }
        }
    }

    // Every time you call FindInstanceMember it should use a new visit stamp, which ensures that
    // it will resolve ambiguous references to instance members from different using{Instance}
    // statements to multiple definitions instead of only finding the first. To make that work
    // correctly, it needs to walk the scope hierarchy twice:
    //   1. Generate a new visit stamp
    //   2. Walk up the parent chain, calling FindDefinitions on each scope and its used scopes,
    //      passing in the visit stamp from step 1.
    //   3. Walk up the parent chain a second time, calling FindInstanceMember on each scope's used
    //      instances, using a new visit stamp each time (which is the default if you don't pass
    //      one in).

    // Traverse scope's parent chain to the root scope, trying to find definitions in each scope
    // and any `using` scopes.
    for (const CScope* Scope = this; Scope != LimitingScope; Scope = Scope->GetParentScope())
    {
        const CLogicalScope* LogicalScope = Scope->AsLogicalScopeNullable();
        if (LogicalScope && LogicalScope->TryMarkVisited(VisitStamp))
        {
            SmallDefinitionArray FoundDefinitions = LogicalScope->FindDefinitions(Name, EMemberOrigin::InheritedOrOriginal, Qualifier, ContextPackage, VisitStamp);
            ResolvedDefnsAppend(&Result, FoundDefinitions);
        }

        if (Qualifier._Type != SQualifier::EType::Local)
        {
            // Check each of the using declarations
            for (const CLogicalScope* Using : Scope->GetUsingScopes())
            {
                if (Using->TryMarkVisited(VisitStamp))
                {
                    ResolvedDefnsAppend(&Result, Using->FindDefinitions(Name, EMemberOrigin::InheritedOrOriginal, Qualifier, ContextPackage, VisitStamp));
                }
            }
        }
    }

    if (Qualifier._Type != SQualifier::EType::Local)
    {
        // Traverse scope's parent chain to the root scope, finding definitions in any `using` instances.
        for (const CScope* Scope = this; Scope; Scope = Scope->GetParentScope())
        {
            // Check each of the `using` instances
            for (const CDataDefinition* UsingContext : Scope->GetUsingInstances())
            {
                const CTypeBase* ContextResultType = UsingContext->GetType();
                const CNormalType& ContextNormalType = ContextResultType->GetNormalType();
                const CReferenceType* ContextReferenceType = ContextNormalType.AsNullable<CReferenceType>();
                const CNormalType* ContextNormalValueType;

                if (ContextReferenceType)
                {
                    ContextNormalValueType = &ContextReferenceType->PositiveValueType()->GetNormalType();
                }
                else
                {
                    ContextNormalValueType = &ContextNormalType;
                }

                // Note that each call to `FindInstanceMember()` uses a new visit stamp and it returns
                // an array - so if a match is not found it only appends an empty array
                ResolvedDefnsAppendWithContext(&Result, ContextNormalValueType->FindInstanceMember(Name, EMemberOrigin::InheritedOrOriginal, Qualifier), UsingContext);
            }
        }
    }

    return Result;
}

TSRef<CControlScope> CScope::CreateNestedControlScope(CSymbol Name)
{
    _NestedControlScopes.AddNew(this, _Program, Name);
    return _NestedControlScopes.Last();
}

TSRef<CTypeScope> CScope::CreateNestedTypeScope()
{
    _NestedTypeScopes.AddNew(*this);
    return _NestedTypeScopes.Last();
}

VisitStampType CScope::GenerateNewVisitStamp()
{
    static VisitStampType CurrentStamp = 0;
    return ++CurrentStamp;
}

namespace {
const CModule& GetRootModule(const CModule& Module)
{
    const CModule* Result = &Module;
    const CScope* Scope = &Module;
    for (;;)
    {
        const CScope* Parent = Scope->GetParentScope();
        if (!Parent)
        {
            return *Result;
        }
        if (Parent->GetKind() == CScope::EKind::Module)
        {
            Result = static_cast<const CModule*>(Parent);
        }
        Scope = Parent;
    }
}

template <typename TPredicate>
bool OrConstrained(const CModule& Module, TPredicate Predicate)
{
    if (uLang::Invoke(Predicate, Module))
    {
        return true;
    }
    const CDefinition* ConstrainedDefinition = Module.GetConstrainedDefinition();
    if (!ConstrainedDefinition)
    {
        return false;
    }
    return uLang::Invoke(Predicate, ConstrainedDefinition->AsChecked<CModule>());
}

bool CheckScopedAccessLevelHelper(const CDefinition& Definition, const SAccessLevel& DefinitionAccessLevel, const CModule& ReferenceModule)
{
    const CScope& DefinitionScope = Definition._EnclosingScope;
    
    // If the definition is scoped, then we need to check each of those to see if they can see the reference scope
    if (DefinitionAccessLevel._Kind == SAccessLevel::EKind::Scoped)
    {
        for (const CScope* Scope : DefinitionAccessLevel._Scopes)
        {
            if (!Scope)
            {
                continue;
            }
            if (OrConstrained(ReferenceModule, [=](auto&& Module) { return Module.IsSameOrChildOf(Scope); }))
            {
                return true;
            }
        }
    }

    // If the definition site is internal, but the reference site is scoped to the definition, then that's also ok
    // Walk up the parent scopes for the Definition and look for any scoped access levels. We want to know if any of those 
    // parents of the definition game access to the reference site
    for (const CScope* S = &DefinitionScope; S != nullptr; S = S->GetParentScope())
    {
        if (const CDefinition* ScopeDefinition = S->ScopeAsDefinition())
        {
            const SAccessLevel& ScopeAccessLevel = ScopeDefinition->DerivedAccessLevel();
            if (ScopeAccessLevel._Kind == SAccessLevel::EKind::Scoped)
            {
                for (const CScope* TestScope : ScopeAccessLevel._Scopes)
                {
                    if (!TestScope)
                    {
                        continue;
                    }
                    if (OrConstrained(ReferenceModule, [=](auto&& Module) { return Module.IsSameOrChildOf(TestScope); }))
                    {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}
}

bool CScope::CanAccess(const CDefinition& Definition, const SAccessLevel& DefinitionAccessLevel) const
{
    const CScope& DefinitionScope = Definition._EnclosingScope;

    const CDefinition* EnclosingDefinition = Definition.GetEnclosingDefinition();

    // Recursively check that the enclosing definition is accessible from this scope. This may be redundant, as
    // the caller will often have already checked accessibility of the enclosing scope. However, there are cases
    // where it isn't redundant: e.g. checking accessibility of an overriding definition, where the accessibility
    // of the scope containing the override will have already been checked by the caller, but not the scope
    // containing the overridden definition (Definition in this function).
    const uint32_t UploadedAtFNVersion = GetPackage()->_UploadedAtFNVersion;
    if (UploadedAtFNVersion >= 2810 && EnclosingDefinition && !EnclosingDefinition->IsAccessibleFrom(*this))
    {
        return false;
    }

    auto GetScopeClass = [](const CScope* Scope) -> const CClass*
    {
        const CClass* CallingClass = nullptr;
        const CScope* QueryingScope = Scope;
        while (QueryingScope)
        {
            if (QueryingScope->GetKind() == CScope::EKind::Class)
            {
                CallingClass = static_cast<const CClass*>(QueryingScope);
                break;
            }
            QueryingScope = QueryingScope->GetParentScope();
        }

        return CallingClass;
    };

    switch (DefinitionAccessLevel._Kind)
    {
    case SAccessLevel::EKind::Public:
        // Access is permitted anywhere
        // This case is present to be complete for switch
        return true;

    case SAccessLevel::EKind::Scoped:
    case SAccessLevel::EKind::Internal:
    {
        // for both internal and scoped, we may need to do some work to see if any parent scopes are scoped
        const CModule* ReferenceSiteModule = GetModule();
        if (!ReferenceSiteModule)
        {
            return false;
        }
        const CModule* DefinitionModule = DefinitionScope.GetModule();
        if (OrConstrained(*ReferenceSiteModule, [=](auto&& Module) { return Module.IsSameOrChildOf(DefinitionModule); }))
        {
            // ordinary internal rules are sufficient - reference is same or child of definition
            return true;
        }
        // we need to look at all parent scopes of the definition to see if any one granted access to the reference site
        return CheckScopedAccessLevelHelper(Definition, DefinitionAccessLevel, *ReferenceSiteModule);
    }
    case SAccessLevel::EKind::Protected:
    {
        const CClass* ReferencingClass = GetScopeClass(this);

        // Must be in same class or subclass
        return ReferencingClass
            && DefinitionScope.GetKind() == CScope::EKind::Class
            && ReferencingClass->IsClass(*static_cast<const CClass*>(&DefinitionScope));
    }
    case SAccessLevel::EKind::Private:
    {
        // Must be in same class
        return GetScopeClass(this) == &DefinitionScope;
    }
    case SAccessLevel::EKind::EpicInternal:
    {
        return CanAccessEpicInternal();
    }
    default:
        ULANG_UNREACHABLE();
    }
}

bool CScope::IsAuthoredByEpic() const
{
    CUTF8StringBuilder ScopePathBuilder;
    ScopePathBuilder.Append(GetScopePath('/', EPathMode::PrefixSeparator));
    ScopePathBuilder.Append('/');
    return uLang::AnyOf(GetProgram()._EpicInternalModulePrefixes, [&](const CUTF8String& EpicInternalModulePrefix) -> bool
    {
        return ScopePathBuilder.ToStringView().StartsWith(EpicInternalModulePrefix);
    });
}

bool CScope::CanAccessEpicInternal() const
{
    CAstPackage* Package = GetPackage();
    return (Package && Package->_VerseScope == EVerseScope::InternalUser) || IsAuthoredByEpic();
}

const char* CScope::KindToCString(CScope::EKind Kind)
{
#define CSCOPE_KINDTOCSTRING_CASE(K, S) case EKind::K: return S;

    switch (Kind)
    {
        CSCOPE_KINDTOCSTRING_CASE(Program, "program")
        CSCOPE_KINDTOCSTRING_CASE(CompatConstraintRoot, "compatibility constraint root")
        CSCOPE_KINDTOCSTRING_CASE(Module, "module")
        CSCOPE_KINDTOCSTRING_CASE(ModulePart, "module part")
        CSCOPE_KINDTOCSTRING_CASE(Snippet, "file")
        CSCOPE_KINDTOCSTRING_CASE(Class, "class")
        CSCOPE_KINDTOCSTRING_CASE(Function, "function")
        CSCOPE_KINDTOCSTRING_CASE(ControlScope, "control scope")
        CSCOPE_KINDTOCSTRING_CASE(Interface, "interface")
        CSCOPE_KINDTOCSTRING_CASE(Type, "type")
        CSCOPE_KINDTOCSTRING_CASE(Enumeration, "enumeration")
    default:
        ULANG_ERRORF("Could not convert CScope::EKind %d to string", Kind);
    }
#undef CSCOPE_KINDTOCSTRING_CASE

    return "";
}

//=======================================================================================
// CLogicalScope
//=======================================================================================

CLogicalScope::~CLogicalScope()
{
    for (const TSRef<CDefinition>& Definition : _Definitions)
    {
        ULANG_ASSERTF(Definition->GetRefCount() == 1, "Unexpectedly freeing %s scope while there's an external reference to its %s definition %s",
            CScope::KindToCString(GetKind()),
            DefinitionKindAsCString(Definition->GetKind()),
            Definition->AsNameCString());
    }
}

EIterateResult CLogicalScope::IterateRecurseLogicalScopes(const TFunction<EVisitResult(const CLogicalScope&)>& Functor) const
{
    // Invoke on this
    EVisitResult Result = Functor(*this);
    if (Result != EVisitResult::Continue)
    {
        return Result == EVisitResult::Stop ? EIterateResult::Stopped : EIterateResult::Completed;
    }

    // Then on all definitions.
    for (const CDefinition* Definition : _Definitions)
    {
        if (const CLogicalScope* DefinitionLogicalScope = Definition->DefinitionAsLogicalScopeNullable())
        {
            if (DefinitionLogicalScope->IterateRecurseLogicalScopes(Functor) == EIterateResult::Stopped)
            {
                return EIterateResult::Stopped;
            }
        }
    }

    return EIterateResult::Completed;
}

EIterateResult CLogicalScope::IterateRecurseLogicalScopes(TFunction<EVisitResult(const CLogicalScope&)>&& Functor) const
{
    return IterateRecurseLogicalScopes(Functor);
}

SmallDefinitionArray CLogicalScope::FindDefinitions(const CSymbol& Name, EMemberOrigin /*Origin*/, const SQualifier& Qualifier, const CAstPackage* ContextPackage, VisitStampType VisitStamp) const
{
    SmallDefinitionArray Result;

    ULANG_ASSERTF(!Name.IsNull(), "Null names are reserved for anonymous variables");

    for (CDefinition* Definition : _Definitions)
    {
        if (Definition->GetName() == Name)
        {
            if (Qualifier.IsUnspecified() || Qualifier == Definition->GetImplicitQualifier())
            {
                if (ContextPackage && !IsDefinitionAvailableAtVersion(*Definition, ContextPackage->_UploadedAtFNVersion, _Program))
                {
                    continue;
                }

                if (Definition->TryMarkOverriddenAndConstrainedDefinitionsVisited(VisitStamp))
                {
                    Result.Add(Definition);
                }
            }
        }
    }

    return Result;
}

void CLogicalScope::SetRevision(SemanticRevision Revision)
{
    ULANG_ENSUREF(Revision >= _CumulativeRevision, "Revision to be set must not be smaller than existing revisions.");

    _CumulativeRevision = Revision;
    if (_Parent)
    {
        const_cast<CLogicalScope&>(_Parent->GetLogicalScope()).SetRevision(Revision);
    }
}

SQualifier CLogicalScope::AsQualifier() const
{
    if (IsControlScope())
    {
        return SQualifier::Local();
    }
    else if (const CTypeBase* Scope = ScopeAsType())
    {
        return SQualifier::NominalType(*static_cast<const CNominalType*>(Scope));
    }
    else
    {
        return SQualifier::Unknown();
    }
}

} // namespace uLang
