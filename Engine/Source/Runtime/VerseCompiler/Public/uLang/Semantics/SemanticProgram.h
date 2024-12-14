// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Containers/UniquePointer.h"
#include "uLang/Common/Containers/UniquePointerArray.h"
#include "uLang/Common/Containers/UniquePointerSet.h"
#include "uLang/Common/Containers/Map.h"
#include "uLang/Semantics/Definition.h"
#include "uLang/Semantics/MemberOrigin.h"
#include "uLang/Semantics/SemanticClass.h"
#include "uLang/Semantics/SemanticInterface.h"
#include "uLang/Semantics/SmallDefinitionArray.h"
#include "uLang/Semantics/UnknownType.h"
#include "uLang/Semantics/VisitStamp.h"

namespace uLang
{
class CAstModule;
class CModulePart;

struct SEffectDescriptor
{
    SEffectSet _EffectSet;                           // If we get this effect-set...
    SEffectSet _RescindFromDefault;                  // ...we trim these from the default
    uLang::TArray<const CClass*> _MutualExclusions;  // ...but this effect set is not allowed with these others
    bool _AllowInDecomposition = true;               // When decomposing an effect set, allow this class to be used. Needed when deprecating an effect.
};

struct SDecompositionMapping
{
    SEffectSet Effects;
    const CClass* Class;
};

/**
 * Encapsulates a reference to an AST/IR package
 **/
class CAstPackageRef
{
public:
    void SetAstPackage(CAstPackage* AstPackage) { ULANG_ASSERTF(!_IrPackage, "Called AST function when IR available"); _AstPackage = AstPackage; }
    CAstPackage* GetAstPackage() const { ULANG_ASSERTF(!_IrPackage, "Called AST function when IR available"); return _AstPackage; }
    void SetIrPackage(CAstPackage* IrPackage) { _IrPackage = IrPackage; }
    CAstPackage* GetIrPackage() const { return _IrPackage ? _IrPackage : _AstPackage; }

private:
    CAstPackage* _AstPackage{ nullptr };
    CAstPackage* _IrPackage{ nullptr };
};

/**
 * Semantically represents a module
 **/
class VERSECOMPILER_API CModule : public CDefinition, public CNominalType, public CLogicalScope, public CAstPackageRef
{
public:
    static const ETypeKind StaticTypeKind = ETypeKind::Module;
    static const CDefinition::EKind StaticDefinitionKind = CDefinition::EKind::Module;
    using PartArray = TUPtrArrayG<CModulePart, false, TInlineElementAllocator<1>>;

    CModule(const CSymbol& Name, CScope& EnclosingScope);

    // Handling of partial module definitions
    bool HasParts() const { return _Parts.IsFilled(); }
    const PartArray& GetParts() const { return _Parts; }
    CModulePart& CreatePart(CScope* ParentScope, bool bExplicitDefinition);
    bool IsExplicitDefinition() const;

    // CScope interface
    virtual CSymbol GetScopeName() const override { return CNamed::GetName(); }
    virtual const CTypeBase* ScopeAsType() const override { return this; }
    virtual const CDefinition* ScopeAsDefinition() const override { return this; }

    // CLogicalScope interface
    virtual SmallDefinitionArray FindDefinitions(
        const CSymbol& Name,
        EMemberOrigin Origin = EMemberOrigin::InheritedOrOriginal,
        const SQualifier& Qualifier = SQualifier::Unknown(),
        const CAstPackage* ContextPackage = nullptr,
        VisitStampType VisitStamp = GenerateNewVisitStamp()) const override;

    // CTypeBase interface.
    virtual SmallDefinitionArray FindInstanceMember(const CSymbol& Name, EMemberOrigin Origin, const SQualifier& Qualifier, const CAstPackage* ContextPackage = nullptr, VisitStampType VisitStamp = CScope::GenerateNewVisitStamp()) const override;
    virtual bool CanBeCustomAccessorDataType() const override { return false; };

    // CNominalType interface.
    virtual const CDefinition* Definition() const override { return this; }

    // CDefinition interface.
    virtual const CLogicalScope* DefinitionAsLogicalScopeNullable() const override { return this; }
    void SetAstNode(CExprModuleDefinition* AstNode) { CDefinition::SetAstNode(AstNode); }
    CExprModuleDefinition* GetAstNode() const { return static_cast<CExprModuleDefinition*>(CDefinition::GetAstNode()); }
    void SetIrNode(CExprModuleDefinition* AstNode) { CDefinition::SetIrNode(AstNode); }
    CExprModuleDefinition* GetIrNode(bool bForce = false) const { return static_cast<CExprModuleDefinition*>(CDefinition::GetIrNode(bForce)); }

    void MarkPersistenceCompatConstraint() const;

    virtual bool IsPersistenceCompatConstraint() const override { return _bPersistenceCompatConstraint; }

private:
    // The partial modules that make up this module (in 99% of cases there will be just one)
    PartArray _Parts;

    mutable bool _bPersistenceCompatConstraint{ false };
};

/**
 * Semantically represents a partial module
 * aka a module definition either via vmodule file or module macro
 * Always is directly parented to a CModule
 **/
class VERSECOMPILER_API CModulePart : public CScope, public TAstNodeRef<CExprModuleDefinition>, public CAstPackageRef
{
public:
    CModulePart(CModule& Module, CScope* ParentScope, bool bExplicitDefinition, CSemanticProgram& Program) : CScope(EKind::ModulePart, ParentScope, Program), _Module(Module), _bExplicitDefinition(bExplicitDefinition) {}

    // This statically overrides CScope::GetModule() for efficiency, calling either override will return the same result
    const CModule* GetModule() const { return &_Module; }
    CModule* GetModule() { return &_Module; }
    bool IsExplicitDefinition() const { return _bExplicitDefinition; }

    // CScope interface
    virtual CSymbol GetScopeName() const override { return _Module.GetName(); }
    virtual const CTypeBase* ScopeAsType() const override { return &_Module; }
    virtual const CDefinition* ScopeAsDefinition() const override { return &_Module; }

    virtual const CLogicalScope* AsLogicalScopeNullable() const override { return &_Module; }
    virtual CLogicalScope* AsLogicalScopeNullable() override { return &_Module; }

private:
    CModule& _Module; // The module this part belongs to
    bool _bExplicitDefinition; // True for definition via module macro, false for definition via directory or VersePath component
};

/**
 * Semantically represents a snippet
 **/
class CSnippet : public CScope, public CNamed
{
public:
    CSnippet(const CSymbol& Path, CScope* ParentScope, CSemanticProgram& Program) : CScope(EKind::Snippet, ParentScope, Program), CNamed(Path) {}

    // CScope interface
    virtual CSymbol GetScopeName() const override { return CNamed::GetName(); }
};

/**
 * Container structure for the various pre-defined, intrinsic symbols.
 */
class VERSECOMPILER_API CIntrinsicSymbols
{
public:
    void Initialize(CSymbolTable&);

    CSymbol GetArithmeticOpName(CExprBinaryArithmetic::EOp) const;

    CSymbol GetComparisonOpName(CExprComparison::EOp) const;

    CSymbol GetAssignmentOpName(CExprAssignment::EOp) const;

    CUTF8String MakeExtensionFieldOpName(CSymbol FieldName) const;
    CUTF8StringView StripExtensionFieldOpName(CSymbol FieldName) const;

    bool IsOperatorOpName(CSymbol) const;

    bool IsPrefixOpName(CSymbol) const;

    bool IsPostfixOpName(CSymbol) const;

    CSymbol _OpNameNegate;
    CSymbol _OpNameAdd;
    CSymbol _OpNameSub;
    CSymbol _OpNameMul;
    CSymbol _OpNameDiv;
    CSymbol _OpNameLess;
    CSymbol _OpNameLessEqual;
    CSymbol _OpNameGreater;
    CSymbol _OpNameGreaterEqual;
    CSymbol _OpNameEqual;
    CSymbol _OpNameNotEqual;
    CSymbol _OpNameAddRMW;
    CSymbol _OpNameSubRMW;
    CSymbol _OpNameMulRMW;
    CSymbol _OpNameDivRMW;
    CSymbol _OpNameCall;
    CSymbol _OpNameQuery;

    CSymbol _FuncNameAbs;
    CSymbol _FuncNameCeil;
    CSymbol _FuncNameFloor;
    CSymbol _FuncNameWeakMap;
    CSymbol _FuncNameFitsInPlayerMap;

    CSymbol _FieldNameLength;

    CSymbol _Wildcard; // `_`

    CSymbol _Inf;
    CSymbol _NaN;

    // @available symbols
    CSymbol _MinUploadedAtFNVersion;

private:
    CUTF8StringView _OperatorOpNamePrefix;
    CUTF8StringView _PrefixOpNamePrefix;
    CUTF8StringView _PostfixOpNamePrefix;
    CUTF8StringView _OpNameSuffix;
    CUTF8StringView _ExtensionFieldPrefix;
    CUTF8StringView _ExtensionFieldSuffix;
};

/**
 * Serves as an alternate root scope for definitions in packages that define compatibility constraints.
 **/
class VERSECOMPILER_API CCompatConstraintRoot : public CSharedMix, public CLogicalScope
{
public:
    CCompatConstraintRoot(CSemanticProgram& Program)
        : CLogicalScope(CScope::EKind::CompatConstraintRoot, nullptr, Program)
    {}

    // CScope interface
    virtual CSymbol GetScopeName() const override { return GetSymbols()->AddChecked("CompatConstraintRoot"); }

    virtual SmallDefinitionArray FindDefinitions(const CSymbol& Name, EMemberOrigin, const SQualifier& Qualifier, const CAstPackage* ContextPackage, VisitStampType) const override;
};

/**
 * Stores whole parsed semantic hierarchy/infrastructure
 **/
class VERSECOMPILER_API CSemanticProgram : public CSharedMix, public CLogicalScope
{
public:
// Public data members

    // An optional root module for a compatible ancestor of the current code.
    TSPtr<CCompatConstraintRoot> _GeneralCompatConstraintRoot;
    TSPtr<CCompatConstraintRoot> _PersistenceCompatConstraintRoot;
    TSPtr<CCompatConstraintRoot> _PersistenceSoftCompatConstraintRoot;

    // The notional package that is created to contain the built-in definitions.
    TSPtr<CAstPackage> _BuiltInPackage;

    // The /Verse.org/Verse module.
    CModule* _VerseModule{nullptr};

    // Global types for the program.
    CFalseType          _falseType         {*this};
    CTrueType           _trueType          {*this};
    CVoidType           _voidType          {*this};
    CAnyType            _anyType           {*this};
    CComparableType     _comparableType    {*this};
    CPersistableType    _persistableType   {*this};
    CLogicType          _logicType         {*this};
    CRationalType       _rationalType      {*this};
    CChar8Type          _char8Type         {*this};
    CChar32Type         _char32Type        {*this};
    CPathType           _pathType          {*this};
    CRangeType          _rangeType         {*this};

    // Non-globalTypes that have an alias
    const CTypeType* _typeType{nullptr};
    const CIntType* _intType{nullptr};
    const CFloatType* _floatType{nullptr};

    CTypeAlias* _falseAlias{nullptr};
    CTypeAlias* _trueAlias{nullptr};
    CTypeAlias* _voidAlias{nullptr};
    CTypeAlias* _anyAlias{nullptr};
    CTypeAlias* _comparableAlias{nullptr};
    CTypeAlias* _logicAlias{nullptr};
    CTypeAlias* _intAlias{nullptr};
    CTypeAlias* _rationalAlias{nullptr};
    CTypeAlias* _floatAlias{nullptr};
    CTypeAlias* _char8Alias{nullptr};
    CTypeAlias* _char32Alias{nullptr};
    CTypeAlias* _stringAlias{nullptr};
    CTypeAlias* _typeAlias{nullptr};

    // Task class
    const CFunction* GetTaskFunction() const;
    const CClass* GetTaskClass() const;
    const CTypeBase* InstantiateTaskType(const CTypeBase* TypeArgument);

    // Attribute classes
    CClass* _attributeClass{nullptr};
    CClass* _abstractClass{nullptr};     // <abstract> - class: cannot be instantiated - only its subclasses
    CClass* _finalClass{nullptr};        // <final> - class: cannot be used as superclass; routine: cannot be overridden
    CClass* _concreteClass{nullptr};     // <concrete> - class: all properties, including superclasses, must have initializers.
    CClass* _uniqueClass{nullptr};       // <unique> - class: comparable with only the same instances equal
    CClass* _intrinsicClass{nullptr};    // <intrinsic> - implementation is generated inline by backend
    CClass* _nativeClass{nullptr};       // <native> - native body/implementation in C++
    CClass* _nativeCallClass{nullptr};   // <native_callable> - script function which can be called from C++

    CClass* _suspendsClass{nullptr};     // <suspends> - a durational, coroutine function
    CClass* _decidesClass{nullptr};      // <decides> - failure effect
    CClass* _variesClassDeprecated{nullptr}; // <varies> - impure meta-effect: non-referentially transparent
    CClass* _computesClass{nullptr};     // <computes> - pure meta-effect: referentially transparent
    CClass* _convergesClass{nullptr};    // <converges> - converges meta-effect: function is guaranteed to return in finite time
    CClass* _transactsClass{nullptr};    // <transacts> - equivalent to computes+reads+writes+allocates
    CClass* _readsClass{nullptr};        // <reads> - pointer reading. Part of transacts.
    CClass* _writesClass{nullptr};       // <writes> - pointer writing. Part of transacts.
    CClass* _allocatesClass{nullptr};    // <allocates> - adds some finer granularity on <varies> for unique classes and mutable values

    CClass* _constructorClass{nullptr};  // <constructor> - this function is a constructor for the result class
    CClass* _overrideClass{nullptr};     // <override> - this function overrides an inherited function
    CClass* _publicClass{nullptr};       // <public> - member which can be accessed from anywhere 
    CClass* _privateClass{nullptr};      // <private> - member which can only be accessed by the owning class
    CClass* _protectedClass{nullptr};    // <protected> - member which can only be accessed by the owning class and sub-classes
    CClass* _internalClass{nullptr};     // <internal> - member which can only be accessed by the current module
    CClass* _scopedClass{ nullptr };     // <scoped> - member which can only be accessed by a code-specified list of modules
    CClass* _epicInternalClass{nullptr}; // <epic_internal> - member which can only be accessed in Epic code
    CClass* _localizes{nullptr};         // <localizes> - data definition or function that should resolve to a 'message' at runtime
    CClass* _ignore_unreachable{nullptr};// @ignore_unreachable - unreachable code that can be ignored
    CClass* _deprecatedClass{nullptr};   // @deprecated - this definition is deprecated
    CClass* _experimentalClass{nullptr}; // @experimental - this definition is experimental
    CClass* _persistentClass{nullptr};   // <persistent> - specify a `module_scoped_var_weak_map_key`'s related value is persistent
    CClass* _persistableClass{nullptr};  // <persistable> - `struct` may be stored in a module-scoped `var` with `persistent` key
    CClass* _moduleScopedVarWeakMapKeyClass{nullptr}; // <module_scoped_var_weak_map_key> - allow use as module-scoped `var` `weak_map` key
    CClass* _getterClass{nullptr};
    CClass* _setterClass{nullptr};

    CClass* _attributeScopeAttribute{ nullptr };    // @attribscope_attribute - attribute which can be used with prefix @attr syntax
    CClass* _attributeScopeSpecifier{ nullptr };    // @attribscope_specifier - attribute which can be used with suffix <spec> syntax
    CClass* _attributeScopeModule{nullptr};         // @attribscope_module - attribute which can be used on the module definition scope
    CClass* _attributeScopeClass{nullptr};          // @attribscope_class - attribute which can be used on the class definition scope
    CClass* _attributeScopeStruct{nullptr};         // @attribscope_struct - attribute which can be used on the struct definition scope
    CClass* _attributeScopeData{nullptr};           // @attribscope_data - attribute which can be used on defining data members within a class
    CClass* _attributeScopeFunction{nullptr};       // @attribscope_function - attribute which can be used on functions
    CClass* _attributeScopeEnum{nullptr};           // @attribscope_enum - attribute which can be used on enumerations
    CClass* _attributeScopeEnumerator{nullptr};     // @attribscope_enumerator - attribute which can be used on a single enumeration value
    CClass* _attributeScopeAttributeClass{nullptr}; // @attribscope_attribclass - attribute which can be used on attribute classes
    CClass* _attributeScopeInterface{nullptr};      // @attribscope_interface - attribute which can be used on interfaces
    CClass* _attributeScopeIdentifier{nullptr};     // @attribscope_identifier - attribute which can be used on identifier expressions
    CClass* _attributeScopeExpression{nullptr};     // @attribscope_expression - attribute which can be used on expressions
    CClass* _attributeScopeClassMacro{nullptr};     // @attribscope_classmacro - attribute which can be used class<here>{}
    CClass* _attributeScopeStructMacro{nullptr};    // @attribscope_structmacro - attribute which can be used struct<here>{}
    CClass* _attributeScopeInterfaceMacro{nullptr}; // @attribscope_interfacemacro - attribute which can be used interface<here>{}
    CClass* _attributeScopeEnumMacro{nullptr};      // @attribscope_enummacro - attribute which can be used enum<here>{}
    CClass* _attributeScopeVar{nullptr};            // @attribscope_var - attribute which can be used var<here>
    CClass* _attributeScopeName{nullptr};           // @attribscope_name - attribute which can be used on names
    CClass* _attributeScopeEffect{nullptr};         // @attribscope_effect - attribute which can be used as an effect
    CClass* _attributeScopeTypeDefinition{nullptr}; // @attribscope_typedefinition - attribute which can be on type definitions
    CClass* _attributeScopeScopedDefinition{nullptr}; // @attribscope_scopeddefinition - attribute which can be on scoped access level definitions
    CClass* _customAttributeHandler{nullptr};       // @customattribhandler - attribute has a native custom handler to process

    CClass* _availableClass{nullptr};             // @available - this definition is only available in certain versions

    // Cached references to some attributes that are defined in code, but commonly interpreted by the compiler.
    template<typename DefinitionType>
    struct TCachedIntrinsicDefinition
    {
        TCachedIntrinsicDefinition(CSemanticProgram& Program, CUTF8StringView Path) : _Program(Program), _Path(Path) {}
        DefinitionType* Get() const
        {
            if (!_CachedValue.IsSet())
            {
                // If not found, cache a null result as well so we won't keep trying indefinitely to find it
                _CachedValue = _Program.FindDefinitionByVersePath<DefinitionType>(_Path);
            }
            return *_CachedValue;
        }
    private:
        CSemanticProgram& _Program;
        CUTF8String _Path;
        mutable TOptional<DefinitionType*> _CachedValue;
    };

    TCachedIntrinsicDefinition<CClassDefinition> _editable{*this,"/Verse.org/Simulation/editable"};
    TCachedIntrinsicDefinition<CClassDefinition> _editable_non_concrete{*this,"/Verse.org/Simulation/editable_non_concrete"};

    TCachedIntrinsicDefinition<CClassDefinition> _import_as_attribute{*this,"/Verse.org/Native/import_as_attribute"};
    TCachedIntrinsicDefinition<CFunction> _import_as{*this,"/Verse.org/Native/import_as"};

    TCachedIntrinsicDefinition<CClassDefinition> _doc_attribute{*this,"/Verse.org/Native/doc_attribute"};

    // Intrinsic functions.
    CFunction* _ComparableEqualOp{nullptr};
    CFunction* _ComparableNotEqualOp{nullptr};

    CFunction* _IntNegateOp{nullptr};
    CFunction* _IntAddOp{nullptr};
    CFunction* _IntSubtractOp{nullptr};
    CFunction* _IntMultiplyOp{nullptr};
    CFunction* _IntDivideOp{nullptr};
    CFunction* _IntAddAssignOp{nullptr};
    CFunction* _IntSubtractAssignOp{nullptr};
    CFunction* _IntMultiplyAssignOp{nullptr};
    CFunction* _IntAbs{nullptr};

    CFunction* _IntGreaterOp{nullptr};
    CFunction* _IntGreaterEqualOp{nullptr};
    CFunction* _IntLessOp{nullptr};
    CFunction* _IntLessEqualOp{nullptr};

    CFunction* _RationalCeil{nullptr};
    CFunction* _RationalFloor{nullptr};

    CFunction* _FloatNegateOp{nullptr};
    CFunction* _FloatAddOp{nullptr};
    CFunction* _FloatSubtractOp{nullptr};
    CFunction* _FloatMultiplyOp{nullptr};
    CFunction* _FloatDivideOp{nullptr};
    CFunction* _FloatAddAssignOp{nullptr};
    CFunction* _FloatSubtractAssignOp{nullptr};
    CFunction* _FloatMultiplyAssignOp{nullptr};
    CFunction* _FloatDivideAssignOp{nullptr};
    CFunction* _FloatAbs{nullptr};

    CFunction* _IntMultiplyFloatOp{nullptr};
    CFunction* _FloatMultiplyIntOp{nullptr};
    
    CFunction* _FloatGreaterOp{nullptr};
    CFunction* _FloatGreaterEqualOp{nullptr};
    CFunction* _FloatLessOp{nullptr};
    CFunction* _FloatLessEqualOp{nullptr};

    CFunction* _ArrayAddOp{nullptr};
    CFunction* _ArrayAddAssignOp{nullptr};
    CFunction* _ArrayLength{nullptr};
    CFunction* _ArrayCallOp{nullptr};
    CFunction* _ArrayRefCallOp{nullptr};

    CFunction* _MapRefCallOp{nullptr};
    CFunction* _MapLength{nullptr};
    CFunction* _MapConcatenateMaps{nullptr};

    CFunction* _WeakMapCallOp{nullptr};
    CFunction* _WeakMapRefCallOp{nullptr};
    CFunction* _WeakMapOp{nullptr};

    CFunction* _LogicQueryOp{nullptr};

    CFunction* _OptionQueryOp{nullptr};

    CFunction* _FitsInPlayerMap{nullptr};

    CDataDefinition* _InfDefinition{nullptr};
    CDataDefinition* _NaNDefinition{nullptr};

    CFunction* _Getter{nullptr};
    CFunction* _Setter{nullptr};
    CFunction* _UnsafeCast{nullptr};

    // Quick access names
    CIntrinsicSymbols _IntrinsicSymbols;

    TArray<CUTF8String> _EpicInternalModulePrefixes;

    TSPtr<CAstProject> _AstProject;

private:
    TSPtr<CAstProject> _IrProject;
public:

    // The get-method is written to work even if no IR is generated.
    // This is to make it easier to run with and without IR, a useful feature while developing the IR.
    // It's temporary and won't work after IrNode is its own type distinct from AstNode. 
    TSPtr<CAstProject>& GetIrProject() { return _IrProject ? _IrProject : _AstProject; }
    void SetIrProject(const TSPtr<CAstProject>& project) { _IrProject = project; }

// Methods

    ULANG_FORCEINLINE CSemanticProgram()
        : CLogicalScope(CScope::EKind::Program, nullptr, *this)
    {}
    ULANG_FORCEINLINE ~CSemanticProgram()
    {
        // Destroy IR first. There are several asserts that fail if Ast is destroyed before IR.
        _IrProject.Reset();

        // Make sure the AST is deleted before any of the types to satisfy the assertions that check
        // that the type<->AST node links are cleaned up correctly.
        _AstProject.Reset();
    }

    void Initialize(TSPtr<CSymbolTable> Symbols = TSPtr<CSymbolTable>());

    ULANG_FORCEINLINE const TSPtr<CSymbolTable>& GetSymbols() const { return _Symbols; }

    CSnippet& GetOrCreateSnippet(const CSymbol& Path, CScope* ParentScope);
    CSnippet* FindSnippet(const CUTF8StringView& NameStr) const;

    CArrayType& GetOrCreateArrayType(const CTypeBase* ElementType);
    CGeneratorType& GetOrCreateGeneratorType(const CTypeBase* ElementType);
    CMapType& GetOrCreateMapType(const CTypeBase* KeyType, const CTypeBase* ValueType);
    CMapType& GetOrCreateWeakMapType(const CTypeBase& KeyType, const CTypeBase& ValueType);
    CMapType& GetOrCreateMapType(const CTypeBase& KeyType, const CTypeBase& ValueType, bool bWeak);
    CPointerType& GetOrCreatePointerType(const CTypeBase* NegativeValueType, const CTypeBase* PositiveValueType);
    CReferenceType& GetOrCreateReferenceType(const CTypeBase* NegativeValueType, const CTypeBase* PositiveValueType);
    COptionType& GetOrCreateOptionType(const CTypeBase* ValueType);
    CTypeType& GetOrCreateTypeType(const CTypeBase* NegativeType, const CTypeBase* PositiveType);
    CTypeType& GetOrCreateSubtypeType(const CTypeBase* PositiveType);
    CTupleType& GetOrCreateTupleType(CTupleType::ElementArray&& Elements);
    CTupleType& GetOrCreateTupleType(
        CTupleType::ElementArray&& Elements,
        int32_t FirstNamedIndex);
    CNamedType& GetOrCreateNamedType(
        CSymbol Name,
        const CTypeBase* ValueType,
        bool HasValue);
    const CFunctionType& GetOrCreateFunctionType(
        const CTypeBase& ParamsType,
        const CTypeBase& ReturnType,
        SEffectSet Effects = EffectSets::FunctionDefault,
        TArray<const CTypeVariable*> TypeVariables = {},
        bool ImplicitlySpecialized = false);

    const CIntType& GetOrCreateConstrainedIntType(CIntType::Constraint Min, CIntType::Constraint Max);
    const CFloatType& GetOrCreateConstrainedFloatType(double Min, double Max);

    CFlowType& CreateFlowType(ETypePolarity);
    CFlowType& CreateFlowType(ETypePolarity, const CTypeBase*);

    CFlowType& CreateNegativeFlowType() { return CreateFlowType(ETypePolarity::Negative, &_anyType); }

    CFlowType& CreatePositiveFlowType() { return CreateFlowType(ETypePolarity::Positive, &_falseType); }

    CInstantiatedClass& CreateInstantiatedClass(const CClass&, ETypePolarity, TArray<STypeVariableSubstitution>);

    CInstantiatedInterface& CreateInstantiatedInterface(const CInterface&, ETypePolarity, TArray<STypeVariableSubstitution>);

    struct SExplicitTypeParam
    {
        CDataDefinition* DataDefinition;
        CTypeVariable* TypeVariable;
        CTypeVariable* NegativeTypeVariable;
    };

    SExplicitTypeParam CreateExplicitTypeParam(
        CFunction*,
        CSymbol DataName,
        CSymbol TypeName,
        CSymbol NegativeTypeName,
        const CTypeType* Type);

    void AddStandardAccessLevelAttributes(CAttributable* NewAccessLevel) const;

    const CUnknownType* GetDefaultUnknownType() const { return _DefaultUnknownType.Get(); }

    // Find definition by Verse path
    template<class T>
    T* FindDefinitionByVersePath(CUTF8StringView VersePath) const
    {
        CDefinition* Definition = FindDefinitionByVersePathInternal(VersePath);
        return Definition ? Definition->AsNullable<T>() : nullptr;
    }

    /// Get next revision to use when creating new functions etc. 
    SemanticRevision GetNextRevision() const { return GetRevision() + 1; }

    /// Add common classes and bindings
    // TODO-Verse: This might be always done, though it may be done solely by converting a CSyntaxProgram
    void PopulateCoreAPI();

    //~ Begin CScope interface
    virtual CSymbol GetScopeName() const override { return CSymbol(); } // Program has no name
    //~ End CScope interface

    int32_t NextFunctionIndex()
    {
        return _NumFunctions++;
    }

    // Construct the effects descriptor table against this instance of the program's notion of the effect classes
    const SEffectDescriptor& FindEffectDescriptorChecked(const CClass* effectKey, uint32_t UploadedAtFNVersion = VerseFN::UploadedAtFNVersion::Latest) const;
    inline const TArray<const CClass*>& GetAllEffectClasses() const { return _AllEffectClasses; }
    
    TOptional<SEffectSet> ConvertEffectClassesToEffectSet(const TArray<const CClass*>& EffectClasses, const SEffectSet& DefaultEffectSet, uint32_t UploadedAtFNVersion = VerseFN::UploadedAtFNVersion::Latest) const;
    TOptional<TArray<const CClass*>> ConvertEffectSetToEffectClasses(const SEffectSet& EffectSet, const SEffectSet& DefaultEffectSet) const;

private:
    friend class CScope;
    friend class CLogicalScope;

    // Snippets
    TURefSet<CSnippet, CSymbol> _Snippets;

    // Array types
    TURefSet<CArrayType, const CTypeBase*> _ArrayTypes;

    // Generator types
    TURefSet<CGeneratorType, const CTypeBase*> _GeneratorTypes;

    // Map types
    TURefSet<CMapType, CMapType::SKey> _MapTypes;

    // Pointer types
    TURefSet<CPointerType, CPointerType::Key> _PointerTypes;

    // Reference types
    TURefSet<CReferenceType, CReferenceType::Key> _ReferenceTypes;

    // Option types
    TURefSet<COptionType, const CTypeBase*> _OptTypes;

    // Type types
    TURefSet<CTypeType, CTypeType::Key> _TypeTypes;

    // Tuple types
    CTupleType _EmptyTupleType{ *this, {}, 0 };

    // Named argument types
    TURefSet<CNamedType, CNamedType::Key> _NamedTypes;

    // Flow types
    TURefArray<CFlowType> _FlowTypes;

    // Ints constrained with the 'where' clause (the top int also happens to be retained here for easy hash-consing).
    TURefArray<CIntType> _ConstrainedIntTypes;

    // Floats constrained with the 'where' clause (the top float also happens to be retained here for easy hash-consing).
    TURefArray<CFloatType> _ConstrainedFloatTypes;

    // Instantiated classes
    TURefArray<CInstantiatedClass> _InstantiatedClasses;

    // Instantiated interfaces
    TURefArray<CInstantiatedInterface> _InstantiatedInterfaces;

    // Default unknown type
    TUPtr<CUnknownType> _DefaultUnknownType;

    /// Shared symbol table for this program. It can be the same table as other areas too
    /// though all structures storing a symbol in this program must use this table.
    TSPtr<CSymbolTable> _Symbols;

    // A cached reference to the task(t) function.
    mutable CFunction* _taskFunction{nullptr};

    int32_t _NumFunctions{0};

    // We choose between these effects tables based on the UploadedAtFNVersion. If we end up versioning this
    //  further, we should consider some more expandable structure to put them all in.
    TMap<const CClass*, SEffectDescriptor> _EffectDescriptorTable;
    TMap<const CClass*, SEffectDescriptor> _EffectDescriptorTable_Pre3100;

    TArray<const CClass*> _AllEffectClasses;
    TArray <SDecompositionMapping> _OrderedDecompositionData;

    bool bEffectsTablePopulated{ false };
    void PopulateEffectDescriptorTable();
    void ValidateEffectDescriptorTable(const TMap<const CClass*, SEffectDescriptor>& DescriptorTable) const;
    CDefinition* FindDefinitionByVersePathInternal(CUTF8StringView VersePath) const;
    const TMap<const CClass*, SEffectDescriptor>& GetEffectDescriptorTableForVersion(uint32_t UploadedAtFNVersion) const;
};

}  // namespace uLang
