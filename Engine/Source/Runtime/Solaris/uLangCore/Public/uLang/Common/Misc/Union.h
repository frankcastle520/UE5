// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Common.h"
#include "uLang/Common/Templates/References.h"
#include "uLang/Common/Templates/Storage.h"


//
// Macros that can be used to specify multiple template parameters in a macro parameter.
// This is necessary to prevent the macro parsing from interpreting the template parameter
// delimiting comma as a macro parameter delimiter.
// 

#define TEMPLATE_PARAMETERS2(X,Y) X,Y

namespace uLang {

/** Used to disambiguate methods that are overloaded for all possible subtypes of a TUnion where the subtypes may not be distinct. */
template<uint32_t>
struct TDisambiguater
{
    TDisambiguater() {}
};


class FNull
{
public:

    bool operator==(const FNull&) const
    {
        return true;
    }

    bool operator!=(const FNull&) const
    {
        return false;
    }
};


/**
 * Represents a type which is the union of several other types; i.e. it can have a value whose type is of any the union's subtypes.
 * This differs from C union types by being type-safe, and supporting non-trivial data types as subtypes.
 * Since a value for the union must be of a single subtype, the union stores potential values of different subtypes in overlapped memory, and keeps track of which one is currently valid.
 */
template<typename TypeA, typename TypeB = FNull, typename TypeC = FNull, typename TypeD = FNull, typename TypeE = FNull, typename TypeF = FNull>
class TUnion
{
public:

    /** Default constructor. */
    TUnion() //-V730
        : CurrentSubtypeIndex(uint8_t(-1))
    { }

    /** Initialization constructor. */
    explicit TUnion(typename TCallTraits<TypeA>::ParamType InValue, TDisambiguater<0> Disambiguater = TDisambiguater<0>())
        : CurrentSubtypeIndex(uint8_t(-1))
    {
        SetSubtype<TypeA>(InValue);
    }

    /** Initialization constructor. */
    explicit TUnion(typename TCallTraits<TypeB>::ParamType InValue, TDisambiguater<1> Disambiguater = TDisambiguater<1>())
        : CurrentSubtypeIndex(uint8_t(-1))
    {
        SetSubtype<TypeB>(InValue);
    }

    /** Initialization constructor. */
    explicit TUnion(typename TCallTraits<TypeC>::ParamType InValue, TDisambiguater<2> Disambiguater = TDisambiguater<2>())
        : CurrentSubtypeIndex(uint8_t(-1))
    {
        SetSubtype<TypeC>(InValue);
    }

    /** Initialization constructor. */
    explicit TUnion(typename TCallTraits<TypeD>::ParamType InValue, TDisambiguater<3> Disambiguater = TDisambiguater<3>())
        : CurrentSubtypeIndex(uint8_t(-1))
    {
        SetSubtype<TypeD>(InValue);
    }

    /** Initialization constructor. */
    explicit TUnion(typename TCallTraits<TypeE>::ParamType InValue, TDisambiguater<4> Disambiguater = TDisambiguater<4>())
        : CurrentSubtypeIndex(uint8_t(-1))
    {
        SetSubtype<TypeE>(InValue);
    }

    /** Initialization constructor. */
    explicit TUnion(typename TCallTraits<TypeF>::ParamType InValue, TDisambiguater<5> Disambiguater = TDisambiguater<5>())
        : CurrentSubtypeIndex(uint8_t(-1))
    {
        SetSubtype<TypeF>(InValue);
    }

    /** Copy constructor. */
    TUnion(const TUnion& Other)
        : CurrentSubtypeIndex(uint8_t(-1))
    {
        *this = Other;
    }

    /** Destructor. */
    ~TUnion()
    {
        // Destruct any subtype value the union may have.
        Reset();
    }

    /** @return True if the union's value is of the given subtype. */
    template<typename Subtype>
    bool HasSubtype() const
    {
        // Determine the subtype's index and reference.
        int32_t SubtypeIndex;
        const Subtype* SubtypeValuePointer;
        GetSubtypeIndexAndReference<Subtype, const Subtype*>(*this, SubtypeIndex, SubtypeValuePointer);

        return CurrentSubtypeIndex == SubtypeIndex;
    }

    /** If the union's current value is of the given subtype, sets the union's value to a NULL value. */
    template<typename Subtype>
    void ResetSubtype()
    {
        // Determine the subtype's index and reference.
        int32_t SubtypeIndex;
        Subtype* SubtypeValuePointer;
        GetSubtypeIndexAndReference<Subtype, Subtype*>(*this, SubtypeIndex, SubtypeValuePointer);

        // Only reset the value if it is of the specified subtype.
        if (CurrentSubtypeIndex == SubtypeIndex)
        {
            CurrentSubtypeIndex = uint8_t(-1);

            // Destruct the subtype.
            SubtypeValuePointer->~Subtype();
        }
    }

    /** @return A reference to the union's value of the given subtype.  May only be called if the union's HasSubtype()==true for the given subtype. */
    template<typename Subtype>
    const Subtype& GetSubtype() const
    {
        // Determine the subtype's index and reference.
        int32_t SubtypeIndex;
        const Subtype* SubtypeValuePointer;
        GetSubtypeIndexAndReference<Subtype, const Subtype*>(*this, SubtypeIndex, SubtypeValuePointer);

        // Validate that the union has a value of the requested subtype.
        ULANG_ASSERTF(CurrentSubtypeIndex == SubtypeIndex, "Union is not of this type");

        return *SubtypeValuePointer;
    }

    /** @return A reference to the union's value of the given subtype.  May only be called if the union's HasSubtype()==true for the given subtype. */
    template<typename Subtype>
    Subtype& GetSubtype()
    {
        // Determine the subtype's index and reference.
        int32_t SubtypeIndex;
        Subtype* SubtypeValuePointer;
        GetSubtypeIndexAndReference<Subtype, Subtype*>(*this, SubtypeIndex, SubtypeValuePointer);

        // Validate that the union has a value of the requested subtype.
        ULANG_ASSERTF(CurrentSubtypeIndex == SubtypeIndex, "Union is not of this type");

        return *SubtypeValuePointer;
    }

    /** Replaces the value of the union with a value of the given subtype. */
    template<typename Subtype>
    Subtype* SetSubtype(typename TCallTraits<Subtype>::ParamType NewValue)
    {
        int32_t SubtypeIndex;
        Subtype* SubtypeValuePointer;
        GetSubtypeIndexAndReference<Subtype, Subtype*>(*this, SubtypeIndex, SubtypeValuePointer);

        Reset();

        new(SubtypeValuePointer) Subtype(NewValue);

        CurrentSubtypeIndex = (uint8_t)SubtypeIndex;
        return SubtypeValuePointer;
    }

    /** @return The index corresponding to the type currently stored in this union; useful for writing switches and indexing into tables. */
    uint8_t GetCurrentSubtypeIndex() const
    {
        return CurrentSubtypeIndex;
    }

    /** Sets the union's value to NULL. */
    void Reset()
    {
        switch (CurrentSubtypeIndex)
        {
        case uint8_t(-1): break;
        case 0: ResetSubtype<TypeA>(); break;
        case 1: ResetSubtype<TypeB>(); break;
        case 2: ResetSubtype<TypeC>(); break;
        case 3: ResetSubtype<TypeD>(); break;
        case 4: ResetSubtype<TypeE>(); break;
        case 5: ResetSubtype<TypeF>(); break;
        default: FatalErrorUndefinedSubtype(); break;
        };
    }

    TUnion& operator=(const TUnion& Other)
    {
        // Copy the value of the appropriate subtype from the other union
        switch (Other.CurrentSubtypeIndex)
        {
        case uint8_t(-1): break;
        case 0: SetSubtype<TypeA>(Other.GetSubtype<TypeA>()); break;
        case 1: SetSubtype<TypeB>(Other.GetSubtype<TypeB>()); break;
        case 2: SetSubtype<TypeC>(Other.GetSubtype<TypeC>()); break;
        case 3: SetSubtype<TypeD>(Other.GetSubtype<TypeD>()); break;
        case 4: SetSubtype<TypeE>(Other.GetSubtype<TypeE>()); break;
        case 5: SetSubtype<TypeF>(Other.GetSubtype<TypeF>()); break;
        default: FatalErrorUndefinedSubtype(); break;
        };

        return *this;
    }

    /** Equality comparison. */
    bool operator==(const TUnion& Other) const
    {
        if (CurrentSubtypeIndex == Other.CurrentSubtypeIndex)
        {
            switch (CurrentSubtypeIndex)
            {
            case uint8_t(-1): return true;
            case 0: return GetSubtype<TypeA>() == Other.GetSubtype<TypeA>(); break;
            case 1: return GetSubtype<TypeB>() == Other.GetSubtype<TypeB>(); break;
            case 2: return GetSubtype<TypeC>() == Other.GetSubtype<TypeC>(); break;
            case 3: return GetSubtype<TypeD>() == Other.GetSubtype<TypeD>(); break;
            case 4: return GetSubtype<TypeE>() == Other.GetSubtype<TypeE>(); break;
            case 5: return GetSubtype<TypeF>() == Other.GetSubtype<TypeF>(); break;
            default: FatalErrorUndefinedSubtype(); break;
            };
        }

        return false;
    }

private:

    /** The potential values for each subtype of the union. */
    union
    {
        TTypeCompatibleBytes<TypeA> A;
        TTypeCompatibleBytes<TypeB> B;
        TTypeCompatibleBytes<TypeC> C;
        TTypeCompatibleBytes<TypeD> D;
        TTypeCompatibleBytes<TypeE> E;
        TTypeCompatibleBytes<TypeF> F;
    } Values;

    /** The index of the subtype that the union's current value is of. */
    uint8_t CurrentSubtypeIndex;

    /** Sets the union's value to a default value of the given subtype. */
    template<typename Subtype>
    Subtype& InitSubtype()
    {
        Subtype* NewSubtype = &GetSubtype<Subtype>();
        return *new(NewSubtype) Subtype;
    }

    /** Determines the index and reference to the potential value for the given union subtype. */
    template<typename Subtype, typename PointerType>
    static void GetSubtypeIndexAndReference(
        const TUnion& Union,
        int32_t& OutIndex,
        PointerType& OutValuePointer
    )
    {
        if (TAreTypesEqual<TypeA, Subtype>::Value)
        {
            OutIndex = 0;
            OutValuePointer = (PointerType)&Union.Values.A;
        }
        else if (TAreTypesEqual<TypeB, Subtype>::Value)
        {
            OutIndex = 1;
            OutValuePointer = (PointerType)&Union.Values.B;
        }
        else if (TAreTypesEqual<TypeC, Subtype>::Value)
        {
            OutIndex = 2;
            OutValuePointer = (PointerType)&Union.Values.C;
        }
        else if (TAreTypesEqual<TypeD, Subtype>::Value)
        {
            OutIndex = 3;
            OutValuePointer = (PointerType)&Union.Values.D;
        }
        else if (TAreTypesEqual<TypeE, Subtype>::Value)
        {
            OutIndex = 4;
            OutValuePointer = (PointerType)&Union.Values.E;
        }
        else if (TAreTypesEqual<TypeF, Subtype>::Value)
        {
            OutIndex = 5;
            OutValuePointer = (PointerType)&Union.Values.F;
        }
        else
        {
            static_assert(
                TAreTypesEqual<TEMPLATE_PARAMETERS2(TypeA, Subtype)>::Value ||
                TAreTypesEqual<TEMPLATE_PARAMETERS2(TypeB, Subtype)>::Value ||
                TAreTypesEqual<TEMPLATE_PARAMETERS2(TypeC, Subtype)>::Value ||
                TAreTypesEqual<TEMPLATE_PARAMETERS2(TypeD, Subtype)>::Value ||
                TAreTypesEqual<TEMPLATE_PARAMETERS2(TypeE, Subtype)>::Value ||
                TAreTypesEqual<TEMPLATE_PARAMETERS2(TypeF, Subtype)>::Value,
                "Type is not subtype of union.");
            OutIndex = uint8_t(-1);
            OutValuePointer = NULL;
        }
    }

    static void FatalErrorUndefinedSubtype()
    {
        ULANG_ERRORF("Unrecognized TUnion subtype");
    }
};


}