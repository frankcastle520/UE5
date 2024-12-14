// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Compiler Public API

#pragma once

#include "uLang/Common/Algo/Cases.h"
#include "uLang/Common/Containers/Array.h"
#include "uLang/Common/Containers/Function.h"
#include "uLang/Common/Containers/RangeView.h"
#include "uLang/Common/Containers/Set.h"
#include "uLang/Common/Containers/UniquePointerArray.h"
#include "uLang/Common/Text/Named.h"
#include "uLang/Common/Text/UTF8StringBuilder.h"
#include "uLang/Semantics/Effects.h"
#include "uLang/Semantics/MemberOrigin.h"
#include "uLang/Semantics/Revision.h"
#include "uLang/Semantics/SmallDefinitionArray.h"
#include "uLang/Semantics/VisitStamp.h"
#include <cmath> // isnan

namespace uLang
{

// Forward Declaration
struct SQualifier;
class CSymbol;
class CClass;
class CInterface;
class CDefinition;
class CFunctionType;
class CAstPackage;
class CExpressionBase;
class CFlowType;
class CFunction;
class CNamedType;
class CNormalType;
class CNominalType;
class CSemanticProgram;
class CTypeVariable;
class CTupleType;
class CUnknownType;
class CAliasType;

// NOTE: (YiLiangSiew) Currently, Visual Verse relies on the numerical values of these enumerations. If you
// change this, be sure to update `BaseVisualVerseSettings.ini` as well.
// Ensure to update uLangToolchainDependencies.natvis if the numerical values of these enumerations are changed.
#define VERSE_ENUM_SEMANTIC_TYPE_KINDS(v) \
    v(Unknown, CUnknownType)\
    v(False, CFalseType)                   /* false, the type containing no possible values */ \
    v(True, CTrueType)                     /* true, the type containing one possible value: false */ \
    v(Void, CVoidType)                     /* void, a functor that maps any value to false */ \
    v(Any, CAnyType)                       /* any, the top type that contains all possible values */ \
    v(Comparable, CComparableType)         /* comparable, top type of all comparable types */ \
    v(Logic, CLogicType)                   /* logic */ \
    v(Int, CIntType)                       /* int */ \
    v(Rational, CRationalType)             /* rational */ \
    v(Float, CFloatType)                   /* float */ \
    v(Char8, CChar8Type)                   /* char/char8 */ \
    v(Char32, CChar32Type)                 /* char32 */ \
    v(Path, CPathType)                     /* path */ \
    v(Range, CRangeType)                   /* an internal type of ranges */ \
    v(Type, CTypeType)                     /* type, the type of types */ \
    v(Class, CClass) \
    v(Module, CModule) \
    v(Enumeration, CEnumeration) \
    v(Array, CArrayType) \
    v(Generator, CGeneratorType) \
    v(Map, CMapType) \
    v(Pointer, CPointerType) \
    v(Reference, CReferenceType) \
    v(Option, COptionType) \
    v(Interface, CInterface) \
    v(Tuple, CTupleType) \
    v(Function, CFunctionType) \
    v(Variable, CTypeVariable) \
    v(Named, CNamedType) \
    v(Persistable, CPersistableType)


enum class ETypeKind : uint8_t
{
#define VISIT_KIND(Name, CppType) Name,
    VERSE_ENUM_SEMANTIC_TYPE_KINDS(VISIT_KIND)
#undef VISIT_KIND
};
VERSECOMPILER_API const char* TypeKindAsCString(ETypeKind Type);

enum class ETypeSyntaxPrecedence : uint8_t
{
    Min = 0,
    List = 0,       // a,b or a;b
    Definition = 1, // a:=b or a:b
    Comparison = 2, // a<=b
    To = 3,         // a->b
    Call = 4,       // a()
};

// Characterizes whether a type is comparable and hashable, just comparable, or incomparable.
// The comparable and hashable vs just comparable distinction is necessary as a temporary limitation of the FProperty-based implementation,
// which doesn't implement hashing for all the types it implements comparison for.
enum class EComparability : uint8_t
{
    Incomparable,
    Comparable,
    ComparableAndHashable
};

/// Base class for all types
class VERSECOMPILER_API CTypeBase
{
public:
    CTypeBase(CSemanticProgram& Program): _Program(Program) {}
    virtual ~CTypeBase() {}

    CSemanticProgram& GetProgram() const { return _Program; }

    /** Normalizes generic types to head-normal types. */
    virtual const CNormalType& GetNormalType() const = 0;

    virtual CNamedType* AsNamedType() { return nullptr; }

    virtual const CNamedType* AsNamedType() const { return nullptr; }

    virtual CFlowType* AsFlowType() { return nullptr; }

    virtual const CFlowType* AsFlowType() const { return nullptr; }

    virtual const CAliasType* AsAliasType() const { return nullptr; }

    virtual bool CanBeCustomAccessorDataType() const = 0;

    /**
     * Converts this class into its source code string equivalent, for use in error messages.
     * This is essentially a disassembly of the internal language data-structures into source code.
     * @Notes: The code generated may not round-trip to an equivalent CTypeBase.
     **/
    CUTF8String AsCode(ETypeSyntaxPrecedence OuterPrecedence = ETypeSyntaxPrecedence::Min) const
    {
        TArray<const CFlowType*> VisitedFlowTypes;
        return AsCodeRecursive(OuterPrecedence, VisitedFlowTypes, false);
    }

    virtual CUTF8String AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const = 0;

    /**
    * As above, but if it's a tuple then doesn't write the enclosing parenthesis, and : before the type.
    **/
    CUTF8String AsParamsCode(ETypeSyntaxPrecedence OuterPrecedence = ETypeSyntaxPrecedence::Min) const
    {
        TArray<const CFlowType*> VisitedFlowTypes;
        return AsParamsCode(OuterPrecedence, VisitedFlowTypes, true);
    }

    virtual CUTF8String AsParamsCode(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool WithColon) const
    {
        CUTF8StringBuilder DestCode;
        if (WithColon)
        {
            DestCode.Append(":");
        }
        DestCode.Append(AsCodeRecursive(OuterPrecedence, VisitedFlowTypes, false));
        return DestCode.MoveToString();
    }

    /**
     * As above, but sacrifices readability to ensure that it will not collide with other CTypes.
     */
    CUTF8String AsLinkableCode() const
    {
        TArray<const CFlowType*> VisitedFlowTypes;
        return AsCodeRecursive(ETypeSyntaxPrecedence::Min, VisitedFlowTypes, true);
    }

private:
    friend class CSemanticProgram;
    CSemanticProgram& _Program;
    mutable TURefArray<CTupleType> _TupleTypesStartingWithThisType;
    mutable TURefArray<CFunctionType> _FunctionTypesWithThisParameterType;
};

/// A normal type: a head normal form of types where the head is not a parametric type instantiation.
class VERSECOMPILER_API CNormalType : public CTypeBase
{
public:

    CNormalType(ETypeKind Kind, CSemanticProgram& Program): CTypeBase(Program), _Kind(Kind) {}

    ETypeKind GetKind() const { return _Kind; }

    template<typename TType>
    TType& AsChecked() { ULANG_ASSERTF(IsA<TType>(), "Failed to cast Type."); return *static_cast<TType*>(this); }

    template<typename TType>
    TType const& AsChecked() const { ULANG_ASSERTF(IsA<TType>(), "Failed to cast Type."); return *static_cast<const TType*>(this); }

    template<typename TType>
    bool IsA() const { return _Kind == TType::StaticTypeKind; }

    template<typename TType>
    TType* AsNullable() { return IsA<TType>() ? static_cast<TType*>(this) : nullptr; }

    template<typename TType>
    TType const* AsNullable() const { return IsA<TType>() ? static_cast<TType const*>(this) : nullptr; }

    /**
     * If this type is a CReferenceType, this gets the non-reference
     * value type, otherwise returns itself
     **/
    virtual const CTypeBase* GetReferenceValueType() const { return this; }
    
    /**
     * Gets the innermost type of this type
     **/
    virtual const CTypeBase* GetInnerType() const { return this; }

    /** @return `this` as a `CNominalType`, or `nullptr` if `this` is not a `CNominalType` **/
    virtual const CNominalType* AsNominalType() const { return nullptr; }

    /** Returns whether this type is comparable for equality */
    virtual EComparability GetComparability() const { return EComparability::Incomparable; }

    /** Returns whether this type is `persistable` */
    virtual bool IsPersistable() const { return false; }
    
    /**
     * Look up a member in this type.
     */
    SmallDefinitionArray FindInstanceMember(const CSymbol& MemberName, EMemberOrigin Origin, const SQualifier& Qualifier, const CAstPackage* ContextPackage = nullptr) const;
    virtual SmallDefinitionArray FindInstanceMember(const CSymbol& MemberName, EMemberOrigin Origin, const SQualifier& Qualifier, const CAstPackage* ContextPackage, VisitStampType VisitStamp) const { return {}; }
    SmallDefinitionArray FindTypeMember(const CSymbol& MemberName, EMemberOrigin Origin, const SQualifier& Qualifier) const;
    virtual SmallDefinitionArray FindTypeMember(const CSymbol& MemberName, EMemberOrigin Origin, const SQualifier& Qualifier, VisitStampType VisitStamp) const { return {}; }

    // CTypeBase interface.
    virtual const CNormalType& GetNormalType() const override { return *this; }

private:
    const ETypeKind _Kind;
};

// Global type: used for various kinds of types of which there are one per program: false, unit, void, any.
template<ETypeKind Kind>
class CGlobalType : public CNormalType
{
public:
    static const ETypeKind StaticTypeKind = Kind;

    // CTypeBase interface.
    virtual CUTF8String AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const override
    {
        if      (Kind == ETypeKind::False)          { return "false";       }
        else if (Kind == ETypeKind::True)           { return "true";        }
        else if (Kind == ETypeKind::Void)           { return "void";        }
        else if (Kind == ETypeKind::Any)            { return "any";         }
        else if (Kind == ETypeKind::Comparable)     { return "comparable";  }
        else if (Kind == ETypeKind::Logic)          { return "logic";       }
        else if (Kind == ETypeKind::Rational)       { return "rational";    }
        else if (Kind == ETypeKind::Char8)          { return "char";        }
        else if (Kind == ETypeKind::Char32)         { return "char32";      }
        else if (Kind == ETypeKind::Path)           { return "path";        }
        else if (Kind == ETypeKind::Range)          { return "$range";      }
        else if (Kind == ETypeKind::Persistable)    { return "persistable"; }
        else { ULANG_UNREACHABLE(); }
    }

    // CNormalType interface.
    virtual EComparability GetComparability() const override
    {
        if (Kind == ETypeKind::Comparable
            || Kind == ETypeKind::Logic
            || Kind == ETypeKind::Char8
            || Kind == ETypeKind::Char32
            || Kind == ETypeKind::False)
        {
            return EComparability::ComparableAndHashable;
        }
        else if (Kind == ETypeKind::True || Kind == ETypeKind::Void)
        {
            return EComparability::Comparable;
        }
        else
        {
            return EComparability::Incomparable;
        }
    }

    virtual bool IsPersistable() const override
    {
        return Kind == Cases<
            ETypeKind::Void,
            ETypeKind::Logic,
            ETypeKind::Char8,
            ETypeKind::Char32,
            ETypeKind::Persistable>;
    }

    virtual bool CanBeCustomAccessorDataType() const override
    {
        return Kind == Cases<
            ETypeKind::Logic,
            ETypeKind::Rational,
            ETypeKind::Char8,
            ETypeKind::Char32
            >;
    }

private:
    CGlobalType(CSemanticProgram& Program): CNormalType(StaticTypeKind, Program) {}
    
    friend class CSemanticProgram;
};

using CFalseType = CGlobalType<ETypeKind::False>;
using CTrueType = CGlobalType<ETypeKind::True>;
using CVoidType = CGlobalType<ETypeKind::Void>;
using CAnyType = CGlobalType<ETypeKind::Any>;
using CComparableType = CGlobalType<ETypeKind::Comparable>;
using CLogicType = CGlobalType<ETypeKind::Logic>;
using CRationalType = CGlobalType<ETypeKind::Rational>;
using CChar8Type = CGlobalType<ETypeKind::Char8>;
using CChar32Type = CGlobalType<ETypeKind::Char32>;
using CPathType = CGlobalType<ETypeKind::Path>;
using CRangeType = CGlobalType<ETypeKind::Range>;
using CPersistableType = CGlobalType<ETypeKind::Persistable>;

class VERSECOMPILER_API CTypeType : public CNormalType
{
public:
    static const ETypeKind StaticTypeKind = ETypeKind::Type;

    CTypeType(CSemanticProgram& Program, const CTypeBase* NegativeType, const CTypeBase* PositiveType)
        : CNormalType(StaticTypeKind, Program)
        , _NegativeType(NegativeType)
        , _PositiveType(PositiveType)
    {
    }

    virtual CUTF8String AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const override;

    virtual SmallDefinitionArray FindInstanceMember(
        const CSymbol& MemberName,
        EMemberOrigin Origin,
        const SQualifier& Qualifier,
        const CAstPackage* ContextPackage,
        VisitStampType VisitStamp) const override
    {
        return PositiveType()->GetNormalType().FindTypeMember(MemberName, Origin, Qualifier, VisitStamp);
    }

    const CTypeBase* NegativeType() const { return _NegativeType; }

    const CTypeBase* PositiveType() const { return _PositiveType; }

    struct Key
    {
        const CTypeBase* NegativeType;
        const CTypeBase* PositiveType;

        friend bool operator==(const Key& Left, const Key& Right)
        {
            return
                Left.NegativeType == Right.NegativeType &&
                Left.PositiveType == Right.PositiveType;
        }

        friend bool operator!=(const Key& Left, const Key& Right)
        {
            return
                Left.NegativeType != Right.NegativeType ||
                Left.PositiveType != Right.PositiveType;
        }

        friend bool operator<(const Key& Left, const Key& Right)
        {
            if (Left.NegativeType < Right.NegativeType)
            {
                return true;
            }
            if (Right.NegativeType < Left.NegativeType)
            {
                return false;
            }
            return Left.PositiveType < Right.PositiveType;
        }
    };

    operator Key() const
    {
        return {_NegativeType, _PositiveType};
    }

    virtual bool CanBeCustomAccessorDataType() const override { return false; }

private:
    const CTypeBase* _NegativeType;
    const CTypeBase* _PositiveType;
};

/// Class defining instance and class objects
class VERSECOMPILER_API CNominalType : public CNormalType
{
public:
    CNominalType(ETypeKind Kind, CSemanticProgram& Program): CNormalType(Kind, Program) {} 

    virtual const CDefinition* Definition() const = 0;

    // CTypeBase interface.
    virtual CUTF8String AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const override;

    // CNormalType interface.
    virtual const CNominalType* AsNominalType() const override  { return this; }

};

class VERSECOMPILER_API CInvariantValueType : public CNormalType
{
public:
    CInvariantValueType(ETypeKind Kind, CSemanticProgram& Program, const CTypeBase* NegativeValueType, const CTypeBase* PositiveValueType)
        : CNormalType(Kind, Program)
        , _NegativeValueType(NegativeValueType)
        , _PositiveValueType(PositiveValueType)
    {
    }

    const CTypeBase* NegativeValueType() const { return _NegativeValueType; }

    const CTypeBase* PositiveValueType() const { return _PositiveValueType; }

    // CNormalType interface.
    virtual const CTypeBase* GetInnerType() const override { return PositiveValueType()->GetNormalType().GetInnerType(); }

    struct Key
    {
        const CTypeBase* NegativeValueType;
        const CTypeBase* PositiveValueType;

        friend bool operator==(const Key& Left, const Key& Right)
        {
            return
                Left.NegativeValueType == Right.NegativeValueType &&
                Left.PositiveValueType == Right.PositiveValueType;
        }

        friend bool operator!=(const Key& Left, const Key& Right)
        {
            return
                Left.NegativeValueType != Right.NegativeValueType ||
                Left.PositiveValueType != Right.PositiveValueType;
        }

        friend bool operator<(const Key& Left, const Key& Right)
        {
            if (Left.NegativeValueType < Right.NegativeValueType)
            {
                return true;
            }
            if (Right.NegativeValueType < Left.NegativeValueType)
            {
                return false;
            }
            return Left.PositiveValueType < Right.PositiveValueType;
        }
    };

    operator Key() const
    {
        return {_NegativeValueType, _PositiveValueType};
    }

protected:
    const CTypeBase* _NegativeValueType;
    const CTypeBase* _PositiveValueType;
};

/**
 * Represents a pointer to mutable inner type.
 **/
class VERSECOMPILER_API CPointerType : public CInvariantValueType
{
public:
    static const ETypeKind StaticTypeKind = ETypeKind::Pointer;

    CPointerType(CSemanticProgram&, const CTypeBase* NegativeValueType, const CTypeBase* PositiveValueType);

    // CTypeBase interface.
    virtual CUTF8String AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const override
    {
        return CUTF8String(
            "^%s",
            PositiveValueType()->AsCodeRecursive(ETypeSyntaxPrecedence::Call, VisitedFlowTypes, bLinkable).AsCString());
    }

    virtual bool CanBeCustomAccessorDataType() const override { return false; }
};

/**
 * Represents a reference to (possibly mutable) inner type.
 **/
class VERSECOMPILER_API CReferenceType : public CInvariantValueType
{
public:
    static const ETypeKind StaticTypeKind = ETypeKind::Reference;

    CReferenceType(CSemanticProgram&, const CTypeBase* NegativeValueType, const CTypeBase* PositiveValueType);

    // CNormalType interface.
    virtual const CTypeBase* GetReferenceValueType() const override { return PositiveValueType(); }

    // CTypeBase interface.
    virtual CUTF8String AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const override
    {
        return CUTF8String(
            "ref %s",
            PositiveValueType()->AsCodeRecursive(ETypeSyntaxPrecedence::Call, VisitedFlowTypes, bLinkable).AsCString());
    }

    virtual bool CanBeCustomAccessorDataType() const override { return false; };
};

/**
 * Abstract type that has an additional sub-type representing a value (ex: List)
 **/
class VERSECOMPILER_API CValueType : public CNormalType
{
public:

    CValueType(ETypeKind Kind, CSemanticProgram& Program, const CTypeBase* ValueType) : CNormalType(Kind, Program), _ValueType(ValueType) {}

    // CNormalType interface.
    virtual const CTypeBase* GetInnerType() const override { return _ValueType->GetNormalType().GetInnerType(); }

    // Needed for map insertion
    operator CTypeBase const* () const { return _ValueType; }

protected:
    const CTypeBase* _ValueType;
};

/**
 * Option type
 */
class VERSECOMPILER_API COptionType : public CValueType
{
public:
    static const ETypeKind StaticTypeKind = ETypeKind::Option;

    COptionType(CSemanticProgram& Program, const CTypeBase* ValueType);

    const CTypeBase* GetValueType() const { return _ValueType; }

    // CTypeBase interface.
    virtual CUTF8String AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const override
    {
        return CUTF8String("?%s", _ValueType->AsCodeRecursive(ETypeSyntaxPrecedence::Call, VisitedFlowTypes, bLinkable).AsCString());
    }

    virtual bool CanBeCustomAccessorDataType() const override { return GetValueType()->GetNormalType().CanBeCustomAccessorDataType(); }

    // CNormalType interface.
    virtual EComparability GetComparability() const override { return _ValueType->GetNormalType().GetComparability(); }
    virtual bool IsPersistable() const override { return _ValueType->GetNormalType().IsPersistable(); }
};

/**
 * A parametric type of arrays with a specific element type: []t where t:type
 **/
class VERSECOMPILER_API CArrayType : public CValueType
{
public:
    static const ETypeKind StaticTypeKind = ETypeKind::Array;

    CArrayType(CSemanticProgram& Program, const CTypeBase* ElementType)
    : CValueType(ETypeKind::Array, Program, ElementType)
    {}

    const CTypeBase* GetElementType() const { return _ValueType; }
    
    // Returns whether the type is string, i.e. []char8.
    bool IsStringType() const
    {
        return GetElementType()->GetNormalType().IsA<CChar8Type>();
    }

    // CTypeBase interface.
    virtual CUTF8String AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const override
    {
        return CUTF8String("[]%s", GetElementType()->AsCodeRecursive(ETypeSyntaxPrecedence::Call, VisitedFlowTypes, bLinkable).AsCString());
    }
    virtual bool CanBeCustomAccessorDataType() const override { return _ValueType->CanBeCustomAccessorDataType(); }

    // CNormalType interface.
    virtual EComparability GetComparability() const override
    {
        if (IsStringType())
        {
            // if the element type is char8, this is supported because our current backend
            // uses FVerseStringProperty for that instead of FArrayProperty
            return EComparability::ComparableAndHashable;
        }
        else if (GetElementType()->GetNormalType().GetComparability() != EComparability::Incomparable)
        {
            // FArrayProperty doesn't support hashing. See SOL-2126.
            return EComparability::Comparable;
        }
        else
        {
            return EComparability::Incomparable;
        }
    }

    virtual bool IsPersistable() const override
    {
        return GetElementType()->GetNormalType().IsPersistable();
    }
};

/**
 * A parametric type of generators with a specific element type: generator(t) where t:type
 **/
class VERSECOMPILER_API CGeneratorType : public CValueType
{
public:
    static const ETypeKind StaticTypeKind = ETypeKind::Generator;

    CGeneratorType(CSemanticProgram& Program, const CTypeBase* ElementType)
        : CValueType(ETypeKind::Generator, Program, ElementType)
    {}

    const CTypeBase* GetElementType() const { return _ValueType; }

    // CTypeBase interface.
    virtual CUTF8String AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const override
    {
        return CUTF8String("generator(%s)", GetElementType()->AsCodeRecursive(ETypeSyntaxPrecedence::Call, VisitedFlowTypes, bLinkable).AsCString());
    }
    virtual bool CanBeCustomAccessorDataType() const override { return false; }
};

/**
 * A parametric type of maps with specific key and value types: [t]u where t&u:type
 */
class VERSECOMPILER_API CMapType : public CNormalType
{
public:
    static const ETypeKind StaticTypeKind = ETypeKind::Map;

    CMapType(CSemanticProgram& Program, const CTypeBase& KeyType, const CTypeBase& ValueType, bool bWeak)
        : CNormalType(ETypeKind::Map, Program)
        , _KeyType(&KeyType)
        , _ValueType(&ValueType)
        , _bWeak(bWeak)
    {
    }

    const CTypeBase* GetKeyType() const
    {
        return _KeyType;
    }

    const CTypeBase* GetValueType() const
    {
        return _ValueType;
    }

    bool IsWeak() const
    {
        return _bWeak;
    }

    virtual EComparability GetComparability() const override
    {
        if (!_bWeak &&
            GetKeyType()->GetNormalType().GetComparability() != EComparability::Incomparable &&
            GetValueType()->GetNormalType().GetComparability() != EComparability::Incomparable)
        {
            // FMapProperty doesn't support hashing. See SOL-2126
            return EComparability::Comparable;
        }
        else
        {
            return EComparability::Incomparable;
        }
    }

    virtual bool IsPersistable() const override
    {
        return
            !_bWeak &&
            GetKeyType()->GetNormalType().IsPersistable() &&
            GetValueType()->GetNormalType().IsPersistable();
    }

    // CTypeBase interface.
    virtual CUTF8String AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const override
    {
        if (_bWeak)
        {
            return CUTF8String(
                "weak_map(%s, %s)",
                GetKeyType()->AsCodeRecursive(ETypeSyntaxPrecedence::Min, VisitedFlowTypes, bLinkable).AsCString(),
                GetValueType()->AsCodeRecursive(ETypeSyntaxPrecedence::Min, VisitedFlowTypes, bLinkable).AsCString());
        }
        return CUTF8String(
            "[%s]%s",
            GetKeyType()->AsCodeRecursive(ETypeSyntaxPrecedence::Min, VisitedFlowTypes, bLinkable).AsCString(),
            GetValueType()->AsCodeRecursive(ETypeSyntaxPrecedence::Call, VisitedFlowTypes, bLinkable).AsCString());
    }

    virtual bool CanBeCustomAccessorDataType() const override { return false; }

    struct SKey
    {
        const CTypeBase* _KeyType;
        const CTypeBase* _ValueType;
        bool _bWeak;

        friend bool operator==(const SKey& Left, const SKey& Right)
        {
            return Left._KeyType == Right._KeyType && Left._ValueType == Right._ValueType && Left._bWeak == Right._bWeak;
        }

        friend bool operator!=(const SKey& Left, const SKey& Right)
        {
            return !(Left == Right);
        }

        friend bool operator<(const SKey& Left, const SKey& Right)
        {
            if (Left._KeyType < Right._KeyType)
            {
                return true;
            }
            if (Right._KeyType < Left._KeyType)
            {
                return false;
            }
            if (Left._ValueType < Right._ValueType)
            {
                return true;
            }
            if (Right._ValueType < Left._ValueType)
            {
                return false;
            }
            return Left._bWeak < Right._bWeak;
        }
    };

    operator SKey() const
    {
        return SKey{_KeyType, _ValueType, _bWeak};
    }

private:
    const CTypeBase* _KeyType;
    const CTypeBase* _ValueType;
    bool _bWeak;
};

class VERSECOMPILER_API CTupleType : public CNormalType
{
public:
    static const ETypeKind StaticTypeKind = ETypeKind::Tuple;

    using ElementArray = TArrayG<const CTypeBase*, TInlineElementAllocator<4>>;

    CTupleType(CSemanticProgram& Program, ElementArray&& Elements, int32_t FirstNamedIndex)
        : CNormalType(ETypeKind::Tuple, Program)
        , _Elements(Move(Elements))
        , _FirstNamedIndex(FirstNamedIndex)
        , _LastVisitStamp(0u)
    {}

    ULANG_FORCEINLINE bool TryMarkVisited(VisitStampType VisitStamp) const { if (_LastVisitStamp == VisitStamp) { return false; } else { _LastVisitStamp = VisitStamp; return true; } }

    ULANG_FORCEINLINE int32_t Num() const { return _Elements.Num(); }
    const CTypeBase* operator[](int32_t Index) const { return _Elements[Index]; }
    const ElementArray& GetElements() const { return _Elements; }
    ElementArray ElementsWithSortedNames() const;

    const CNamedType* FindNamedType(CSymbol Name) const;

    int32_t GetFirstNamedIndex() const { return _FirstNamedIndex; }
    int32_t NumNonNamedElements() const { return GetFirstNamedIndex(); }

    // CTypeBase interface.
    virtual CUTF8String AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const override;
    virtual CUTF8String AsParamsCode(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool WithColon) const override;
    CUTF8String AsParamsCode(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool WithColon, bool bLinkable) const;
    virtual bool CanBeCustomAccessorDataType() const override { return false; }

    // CNormalType interface.
    virtual EComparability GetComparability() const override;
    virtual bool IsPersistable() const override;

private:
    ElementArray _Elements;

    int32_t _FirstNamedIndex;

    // Used to detect reentrant visits to a tuple.
    mutable VisitStampType _LastVisitStamp;
};

class VERSECOMPILER_API CFunctionType : public CNormalType
{
public:
    static const ETypeKind StaticTypeKind = ETypeKind::Function;

    using ParamTypes = TRangeView<CTypeBase const* const*, CTypeBase const* const*>;

    CFunctionType(
        CSemanticProgram& Program,
        const CTypeBase& ParamsType,
        const CTypeBase& ReturnType,
        const SEffectSet Effects,
        TArray<const CTypeVariable*>&& TypeVariables = {},
        bool ImplicitlySpecialized = false)
        : CNormalType(ETypeKind::Function, Program)
        , _ParamsType(&ParamsType)
        , _ReturnType(ReturnType)
        , _Effects(Effects)
        , _TypeVariables(Move(TypeVariables))
        , _bImplicitlySpecialized(ImplicitlySpecialized)
    {}

    const CTypeBase& GetParamsType() const                 { return *_ParamsType; }
    const CTypeBase& GetReturnType() const                 { return _ReturnType; }
    SEffectSet GetEffects() const                          { return _Effects; }
    bool ImplicitlySpecialized() const                     { return _bImplicitlySpecialized; }
    const TArray<const CTypeVariable*>& GetTypeVariables() const { return _TypeVariables; }

    static const CTypeBase* GetOrCreateParamType(CSemanticProgram&, CTupleType::ElementArray&& ParamTypes);

    static const CTypeBase* GetOrCreateParamType(CSemanticProgram&, CTupleType::ElementArray&& ParamTypes, int32_t FirstNamedIndex);

    static ParamTypes AsParamTypes(CTypeBase const* const& Type)
    {
        if (const CTupleType* TupleType = Type->GetNormalType().AsNullable<CTupleType>())
        {
            return ParamTypes(TupleType->GetElements());
        }
        return SingletonRangeView(Type);
    }

    ParamTypes GetParamTypes() const
    {
        return AsParamTypes(_ParamsType);
    }

    void BuildTypeVariableCode(CUTF8StringBuilder& Builder, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const;
    void BuildTypeVariableCode(CUTF8StringBuilder& Builder) const
    {
        TArray<const CFlowType*> VisitedFlowTypes;
        BuildTypeVariableCode(Builder, VisitedFlowTypes, false);
    }

    void BuildEffectAttributeCode(CUTF8StringBuilder& Builder) const;
    void BuildParameterBlockCode(CUTF8StringBuilder& Builder, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const;

    // CTypeBase interface.
    virtual CUTF8String AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const override;
    virtual bool CanBeCustomAccessorDataType() const override { return false; }

private:
    const CTypeBase* _ParamsType;
    const CTypeBase& _ReturnType;
    SEffectSet _Effects;
    TArray<const CTypeVariable*> _TypeVariables;
    bool _bImplicitlySpecialized;
};

enum class ETypePolarity : char { Negative, Positive };

inline ETypePolarity FlipPolarity(ETypePolarity Polarity)
{
    switch (Polarity)
    {
    case ETypePolarity::Negative: return ETypePolarity::Positive;
    case ETypePolarity::Positive: return ETypePolarity::Negative;
    default: ULANG_UNREACHABLE();
    }
}

class VERSECOMPILER_API CFlowType : public CTypeBase
{
public:
    CFlowType(CSemanticProgram& Program, ETypePolarity Polarity, const CTypeBase* Child)
        : CTypeBase(Program)
        , _Polarity(Polarity)
        , _Child(Child)
    {
    }

    ETypePolarity Polarity() const;

    const CTypeBase* GetChild() const;

    void SetChild(const CTypeBase*) const;

    TSet<const CFlowType*>& FlowEdges() const
    {
        return _FlowEdges;
    }

    void AddFlowEdge(const CFlowType*) const;

    void EmptyFlowEdges() const;

    virtual const CNormalType& GetNormalType() const override;

    virtual CFlowType* AsFlowType() override { return this; }

    virtual const CFlowType* AsFlowType() const override { return this; }

    virtual CUTF8String AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const override;

    virtual bool CanBeCustomAccessorDataType() const override { return false; }

private:
    ETypePolarity _Polarity;
    mutable const CTypeBase* _Child;
    mutable TSet<const CFlowType*> _FlowEdges;
};

struct VERSECOMPILER_API STypeVariableSubstitution
{
    const CTypeVariable* _TypeVariable;
    const CTypeBase* _NegativeType;
    const CTypeBase* _PositiveType;

    STypeVariableSubstitution(const CTypeVariable* TypeVariable, const CTypeBase* NegativeType, const CTypeBase* PositiveType)
        : _TypeVariable(TypeVariable)
        , _NegativeType(NegativeType)
        , _PositiveType(PositiveType)
    {
    }

    friend bool operator==(const STypeVariableSubstitution& Left, const STypeVariableSubstitution& Right)
    {
        return
            Left._TypeVariable == Right._TypeVariable &&
            Left._NegativeType == Right._NegativeType &&
            Left._PositiveType == Right._PositiveType;
    }
};

struct VERSECOMPILER_API SInstantiatedTypeVariable
{
    const CTypeBase* _NegativeType;
    const CTypeBase* _PositiveType;

    SInstantiatedTypeVariable(const CTypeBase* NegativeType, const CTypeBase* PositiveType)
        : _NegativeType(NegativeType)
        , _PositiveType(PositiveType)
    {
    }

    friend bool operator==(const SInstantiatedTypeVariable& Left, const SInstantiatedTypeVariable& Right)
    {
        return
            Left._NegativeType == Right._NegativeType &&
            Left._PositiveType == Right._PositiveType;
    }
};

/// Type representing an instantiation of some underlying type.  This is used to
/// lazily evaluate expensive type instantiation.
class VERSECOMPILER_API CInstantiatedType : public CTypeBase
{
    virtual const CNormalType& GetNormalType() const override
    {
        if (!_NormalType)
        {
            _NormalType = &CreateNormalType();
        }
        return *_NormalType;
    }

    // CTypeBase interface.
    virtual CUTF8String AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const override
    {
        return GetNormalType().AsCodeRecursive(OuterPrecedence, VisitedFlowTypes, bLinkable);
    }

protected:
    CInstantiatedType(CSemanticProgram& Program, ETypePolarity Polarity, TArray<STypeVariableSubstitution> Arguments)
        : CTypeBase(Program)
        , _Polarity(Polarity)
        , _Substitutions(Move(Arguments))
    {
    }

    virtual ~CInstantiatedType() = default;

    virtual const CNormalType& CreateNormalType() const = 0;

    ETypePolarity GetPolarity() const { return _Polarity; }

    const TArray<STypeVariableSubstitution>& GetSubstitutions() const { return _Substitutions; }

private:
    ETypePolarity _Polarity;

    TArray<STypeVariableSubstitution> _Substitutions;

    mutable const CNormalType* _NormalType = nullptr;
};

class VERSECOMPILER_API CNamedType : public CValueType
{
public:
    static constexpr ETypeKind StaticTypeKind = ETypeKind::Named;

    CNamedType(CSemanticProgram& Program, CSymbol Name, const CTypeBase* ValueType, bool HasDefault)
        : CValueType(ETypeKind::Named, Program, ValueType)
        , _Name(Name)
        , _HasValue(HasDefault)
    {
    }

    CSymbol GetName() const { return _Name; }

    const CTypeBase* GetValueType() const { return _ValueType; }

    bool HasValue() const { return _HasValue; }

    virtual CUTF8String AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const override;

    const CTupleType& ToTupleType() const;

    virtual CNamedType* AsNamedType() override { return this; }

    virtual const CNamedType* AsNamedType() const override { return this; }

    struct Key
    {
        CSymbol Name;
        const CTypeBase* ValueType;
        bool HasValue;

        friend bool operator==(const Key& Left, const Key& Right)
        {
            return
                Left.Name == Right.Name &&
                Left.ValueType == Right.ValueType &&
                Left.HasValue == Right.HasValue;
        }

        friend bool operator!=(const Key& Left, const Key& Right)
        {
            return
                Left.Name != Right.Name ||
                Left.ValueType != Right.ValueType ||
                Left.HasValue != Right.HasValue;
        }

        friend bool operator<(const Key& Left, const Key& Right)
        {
            if (Left.Name < Right.Name)
            {
                return true;
            }
            if (Right.Name < Left.Name)
            {
                return false;
            }
            if (Left.ValueType < Right.ValueType)
            {
                return true;
            }
            if (Right.ValueType < Left.ValueType)
            {
                return false;
            }
            return Left.HasValue < Right.HasValue;
        }
    };

    operator Key() const
    {
        return {_Name, _ValueType, _HasValue};
    }

    virtual bool CanBeCustomAccessorDataType() const override { return false; }

private:
    CSymbol _Name;
    bool _HasValue;
};

class VERSECOMPILER_API CIntType : public CNormalType
{
public:
    static constexpr ETypeKind StaticTypeKind = ETypeKind::Int;

    // We use this to support unconstrained integers
    struct Constraint : public TOptional<int64_t>
    {
        Constraint(int64_t Constraint)
            : TOptional<int64_t>(Constraint) {}

        friend bool operator<(const Constraint& lhs, const Constraint& rhs)
        {
            if (lhs == rhs)
            {
                return false;
            }
            if (!lhs) // LHS = unconstrained, RHS = constrained
            {
                return true;
            }
            if (!rhs) // LHS = constrained, RHS = unconstrained
            {
                return false;
            }

            return *lhs < *rhs;
        }

        friend bool operator<=(const Constraint& lhs, const Constraint& rhs) { return lhs == rhs || lhs < rhs; }
        friend bool operator>(const Constraint& lhs, const Constraint& rhs) { return rhs < lhs; }
        friend bool operator>=(const Constraint& lhs, const Constraint& rhs) { return lhs == rhs || rhs < lhs; }
    };

    CIntType(CSemanticProgram& Program, Constraint Min, Constraint Max)
        : CNormalType(StaticTypeKind, Program)
        , _MinInclusive(Min)
        , _MaxInclusive(Max)
    {
    }

    Constraint GetMin() const { return _MinInclusive; }
    Constraint GetMax() const { return _MaxInclusive; }

    bool IsInhabitable() const { return GetMin() <= GetMax(); }

    virtual CUTF8String AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const override;

    virtual EComparability GetComparability() const override { return EComparability::ComparableAndHashable; }

    virtual bool IsPersistable() const override { return true; }

    virtual bool CanBeCustomAccessorDataType() const override { return true; }

private:
    Constraint _MinInclusive;
    Constraint _MaxInclusive;
};

// the float type is a special form of CFloatType where _MaxInclusive is NaN. The only other way NaN can be encoded is for the NaN literal intrinsic.
// Given these constraints on NaN, we simpify our implementation by turning all doubles into a total order via CMath::FloatRanking.
class VERSECOMPILER_API CFloatType : public CNormalType
{
public:
    static constexpr ETypeKind StaticTypeKind = ETypeKind::Float;

    CFloatType(CSemanticProgram& Program, double Min, double Max, int64_t MinRanking, int64_t MaxRanking)
        : CNormalType(StaticTypeKind, Program)
        , _MinInclusive(Min)
        , _MaxInclusive(Max)
        , _MinRanking(MinRanking)
        , _MaxRanking(MaxRanking)
    {
    }

    double GetMin() const { return _MinInclusive; }
    double GetMax() const { return _MaxInclusive; }
    int64_t MinRanking() const { return _MinRanking; }
    int64_t MaxRanking() const { return _MaxRanking; }

    bool IsInhabitable() const { return _MinRanking <= _MaxRanking; }
    bool IsIntrinsicFloatType() const { return std::isnan(GetMax()) && GetMin() == -INFINITY; }

    // The only reason we preserve the "original" doubles is for AsCode.
    virtual CUTF8String AsCodeRecursive(ETypeSyntaxPrecedence OuterPrecedence, TArray<const CFlowType*>& VisitedFlowTypes, bool bLinkable) const override;

    virtual EComparability GetComparability() const override { return EComparability::ComparableAndHashable; }
    virtual bool IsPersistable() const override { return true; }

    virtual bool CanBeCustomAccessorDataType() const override { return true; }

private:
    double _MinInclusive;
    double _MaxInclusive;
    int64_t _MinRanking;
    int64_t _MaxRanking;
};

/**
 * Helper utilities for managing different types.
 **/
namespace SemanticTypeUtils
{
    VERSECOMPILER_API const CClass* AsSingleClass(const CNormalType& NegativeType, const CNormalType& PositiveType);

    VERSECOMPILER_API const CInterface* AsSingleInterface(const CNormalType& NegativeType, const CNormalType& PositiveType);

    VERSECOMPILER_API TArray<STypeVariableSubstitution> Instantiate(const TArray<const CTypeVariable*>& TypeVariables);

    VERSECOMPILER_API const CTypeBase* Substitute(const CTypeBase&, ETypePolarity Polarity, const TArray<STypeVariableSubstitution>& InstTypeVariables);

    VERSECOMPILER_API const CFunctionType* Instantiate(const CFunctionType*);

    /// Replace all types with equivalent representations with a single
    /// canonical type - the type used when emitting code.  In particular,
    /// type variables are erased.
    VERSECOMPILER_API const CTypeBase& Canonicalize(const CTypeBase&);

    VERSECOMPILER_API const CFunctionType& Canonicalize(const CFunctionType&);

    VERSECOMPILER_API const CTupleType& Canonicalize(const CTupleType&);

    VERSECOMPILER_API const CTypeBase& AsPolarity(const CTypeBase&, const TArray<SInstantiatedTypeVariable>&, ETypePolarity);
    VERSECOMPILER_API const CTypeBase& AsPositive(const CTypeBase&, const TArray<SInstantiatedTypeVariable>&);
    VERSECOMPILER_API const CTypeBase& AsNegative(const CTypeBase&, const TArray<SInstantiatedTypeVariable>&);

    /// Constrain `PositiveType1` to be a subtype of `NegativeType2`
    VERSECOMPILER_API bool Constrain(const CTypeBase* PositiveType1, const CTypeBase* NegativeType2);

    /// Determine if `PositiveType1` is a subtype of `PositiveType2`
    VERSECOMPILER_API bool IsSubtype(const CTypeBase* PositiveType1, const CTypeBase* PositiveType2);
    
    /// Determine if `PositiveType1` is equivalent to `PositiveType2`
    VERSECOMPILER_API bool IsEquivalent(const CTypeBase* PositiveType1, const CTypeBase* PositiveType2);

    /// Determine if argument `PositiveType1` is a match for parameter `NegativeType2`
    VERSECOMPILER_API bool Matches(const CTypeBase* PositiveType1, const CTypeBase* NegativeType2);

    VERSECOMPILER_API const CTypeBase& SkipIdentityFlowType(const CFlowType&, ETypePolarity);

    VERSECOMPILER_API const CTypeBase& SkipIdentityFlowType(const CTypeBase&, ETypePolarity);

    VERSECOMPILER_API const CTypeBase& SkipEmptyFlowType(const CTypeBase&);

    /// Compute the join of Type1 and Type2: the "least" type that contains all values contained by either Type1 or Type2.
    VERSECOMPILER_API const CTypeBase* Join(const CTypeBase* Type1, const CTypeBase* Type2);

    /// Compute the meet of Type1 and Type2: the "greatest" type that contains only values contained by both Type1 and Type2.
    VERSECOMPILER_API const CTypeBase* Meet(const CTypeBase* Type1, const CTypeBase* Type2);

    /// Determine whether there two types are distinct; i.e. that there are no values that are members of both types.
    VERSECOMPILER_API bool AreDomainsDistinct(const CTypeBase* DomainType1, const CTypeBase* DomainType2);

    VERSECOMPILER_API bool IsUnknownType(const CTypeBase* Type);

    VERSECOMPILER_API bool IsAttributeType(const CTypeBase* Type);

    VERSECOMPILER_API void VisitAllDefinitions(const CTypeBase* Type, const TFunction<void(const CDefinition&,const CSymbol&)>& Functor);

    /// Apply a function to each immediately possibly contained value's type.
    /// Note for `CFunctionType`, the function is not applied, as values of a
    /// given function type do not contain parameter or result values.
    VERSECOMPILER_API void ForEachDataType(const CTypeBase*, const TFunction<void(const CTypeBase*)>&);

    /// `ForEachDataType`, but recursive, and depth-first, top-down, including
    /// the immediately passed type.
    VERSECOMPILER_API void ForEachDataTypeRecursive(const CTypeBase*, const TFunction<void(const CTypeBase*)>&);

    // Returns whether the type is string, i.e. []char8.
    inline bool IsStringType(const CNormalType& NormalType)
    {
        return NormalType.IsA<CArrayType>()
            && NormalType.AsChecked<CArrayType>().IsStringType();
    }

    // Returns whether a type can be used with @editable.
    // An enum is used instead of a bool to make it possible to produce a more informative error message.
    enum class EIsEditable
    {
        Yes,
        NotEditableType,
        MissingConcreteOrEditable,
        MissingConcrete
    };
    VERSECOMPILER_API const char* IsEditableToCMessage(EIsEditable IsEditable);
    VERSECOMPILER_API EIsEditable IsEditableType(const uLang::CTypeBase* Type, bool bAllowNonConcreteClasses);
}


}  // namespace uLang
