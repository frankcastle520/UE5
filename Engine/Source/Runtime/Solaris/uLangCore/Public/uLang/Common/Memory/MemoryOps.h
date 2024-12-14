// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <string.h>
#include "uLang/Common/Templates/TypeTraits.h"

namespace uLang
{

namespace Private
{
    template <typename DestinationElementType, typename SourceElementType>
    struct TCanBitwiseRelocate
    {
        enum
        {
            Value =
                TOr<
                    TAreTypesEqual<DestinationElementType, SourceElementType>,
                    TAnd<
                        TIsBitwiseConstructible<DestinationElementType, SourceElementType>,
                        TIsTriviallyDestructible<SourceElementType>
                    >
                >::Value
        };
    };
}

/**
 * Default constructs a range of items in memory.
 *
 * @param   Elements    The address of the first memory location to construct at.
 * @param   Count       The number of elements to destruct.
 */
template <typename ElementType>
ULANG_FORCEINLINE typename TEnableIf<!TIsZeroConstructType<ElementType>::Value>::Type DefaultConstructElements(void* Address, int32_t Count)
{
    ElementType* Element = (ElementType*)Address;
    while (Count > 0)
    {
        new (Element) ElementType;
        ++Element;
        --Count;
    }
}


template <typename ElementType>
ULANG_FORCEINLINE typename TEnableIf<TIsZeroConstructType<ElementType>::Value>::Type DefaultConstructElements(void* Elements, int32_t Count)
{
    if (Count)
    {
        memset(Elements, 0, sizeof(ElementType) * Count);
    }
}


/**
 * Destructs a single item in memory.
 *
 * @param   Elements    A pointer to the item to destruct.
 *
 * @note: This function is optimized for values of T, and so will not dynamically dispatch destructor calls if T's destructor is virtual.
 */
template <typename ElementType>
ULANG_FORCEINLINE typename TEnableIf<!TIsTriviallyDestructible<ElementType>::Value>::Type DestructElement(ElementType* Element)
{
    // We need a typedef here because VC won't compile the destructor call below if ElementType itself has a member called ElementType
    using DestructItemsElementTypeTypedef = ElementType;

    Element->DestructItemsElementTypeTypedef::~DestructItemsElementTypeTypedef();
}


template <typename ElementType>
ULANG_FORCEINLINE typename TEnableIf<TIsTriviallyDestructible<ElementType>::Value>::Type DestructElement(ElementType* Element)
{
}


/**
 * Destructs a range of items in memory.
 *
 * @param   Elements    A pointer to the first item to destruct.
 * @param   Count       The number of elements to destruct.
 *
 * @note: This function is optimized for values of T, and so will not dynamically dispatch destructor calls if T's destructor is virtual.
 */
template <typename ElementType>
ULANG_FORCEINLINE typename TEnableIf<!TIsTriviallyDestructible<ElementType>::Value>::Type DestructElements(ElementType* Element, int32_t Count)
{
    while (Count > 0)
    {
        // We need a typedef here because VC won't compile the destructor call below if ElementType itself has a member called ElementType
        using DestructItemsElementTypeTypedef = ElementType;

        Element->DestructItemsElementTypeTypedef::~DestructItemsElementTypeTypedef();
        ++Element;
        --Count;
    }
}


template <typename ElementType>
ULANG_FORCEINLINE typename TEnableIf<TIsTriviallyDestructible<ElementType>::Value>::Type DestructElements(ElementType* Elements, int32_t Count)
{
}


/**
 * Constructs a range of items into memory from a set of arguments.  The arguments come from an another array.
 *
 * @param   Dest        The memory location to start copying into.
 * @param   Source      A pointer to the first argument to pass to the constructor.
 * @param   Count       The number of elements to copy.
 */
template <typename DestinationElementType, typename SourceElementType>
ULANG_FORCEINLINE typename TEnableIf<!TIsBitwiseConstructible<DestinationElementType, SourceElementType>::Value>::Type ConstructElements(void* Dest, const SourceElementType* Source, int32_t Count)
{
    while (Count > 0)
    {
        new (Dest) DestinationElementType(*Source);
        ++(DestinationElementType*&)Dest;
        ++Source;
        --Count;
    }
}


template <typename DestinationElementType, typename SourceElementType>
ULANG_FORCEINLINE typename TEnableIf<TIsBitwiseConstructible<DestinationElementType, SourceElementType>::Value>::Type ConstructElements(void* Dest, const SourceElementType* Source, int32_t Count)
{
    if (Count)
    {
        memcpy(Dest, Source, sizeof(SourceElementType) * Count);
    }
}


/**
 * Relocates a range of items to a new memory location as a new type. This is a so-called 'destructive move' for which
 * there is no single operation in C++ but which can be implemented very efficiently in general.
 *
 * @param   Dest        The memory location to relocate to.
 * @param   Source      A pointer to the first item to relocate.
 * @param   Count       The number of elements to relocate.
 */
template <typename DestinationElementType, typename SourceElementType>
ULANG_FORCEINLINE typename TEnableIf<!Private::TCanBitwiseRelocate<DestinationElementType, SourceElementType>::Value>::Type RelocateConstructElements(void* Dest, const SourceElementType* Source, int32_t Count)
{
    while (Count > 0)
    {
        // We need a typedef here because VC won't compile the destructor call below if SourceElementType itself has a member called SourceElementType
        using RelocateConstructItemsElementTypeTypedef = SourceElementType;

        new (Dest) DestinationElementType(*Source);
        ++(DestinationElementType*&)Dest;
        (Source++)->RelocateConstructItemsElementTypeTypedef::~RelocateConstructItemsElementTypeTypedef();
        --Count;
    }
}

template <typename DestinationElementType, typename SourceElementType>
ULANG_FORCEINLINE typename TEnableIf<Private::TCanBitwiseRelocate<DestinationElementType, SourceElementType>::Value>::Type RelocateConstructElements(void* Dest, const SourceElementType* Source, int32_t Count)
{
    /* All existing UE containers seem to assume trivial relocatability (i.e. memcpy'able) of their members,
     * so we're going to assume that this is safe here.  However, it's not generally possible to assume this
     * in general as objects which contain pointers/references to themselves are not safe to be trivially
     * relocated.
     *
     * However, it is not yet possible to automatically infer this at compile time, so we can't enable
     * different (i.e. safer) implementations anyway. */
    if (Count)
    {
        memmove(Dest, Source, sizeof(SourceElementType) * Count);
    }
}


template <typename ElementType>
ULANG_FORCEINLINE typename TEnableIf<TTypeTraits<ElementType>::IsBytewiseComparable, bool>::Type CompareElements(const ElementType* A, const ElementType* B, int32_t Count)
{
    if (Count)
    {
        return !memcmp(A, B, sizeof(ElementType) * Count);
    }

    return true;
}


template <typename ElementType>
ULANG_FORCEINLINE typename TEnableIf<!TTypeTraits<ElementType>::IsBytewiseComparable, bool>::Type CompareElements(const ElementType* A, const ElementType* B, int32_t Count)
{
    while (Count > 0)
    {
        if (!(*A == *B))
        {
            return false;
        }

        ++A;
        ++B;
        --Count;
    }

    return true;
}

}