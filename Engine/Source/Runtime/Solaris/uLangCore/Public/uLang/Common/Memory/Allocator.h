// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Misc/Threading.h"
#include "uLang/Common/Templates/References.h"
#include "uLang/Common/Templates/Storage.h" // for TTypeCompatibleBytes<>
#include "uLang/Common/Memory/MemoryOps.h" // for RelocateConstructElements<>

namespace uLang
{

/// Id type for observer pointers
enum EObserverId : uint32_t
{
    ObserverId_Null = 0,
};

/// Compute a new CRC32 from a given CRC32 by rotating one bit
/// Due to the nature of CRCs, this will exactly iterate through all possible 32-bit values except for 0
ULANG_FORCEINLINE uint32_t RotateCRC32(uint32_t CRC)
{
    constexpr uint32_t ReversedPolynomial = 0xedb88320u; // The bit-reversed version of the famous 0x04c11db7 (posix etc.)
    uint32_t CRCShifted = CRC >> 1;
    return (CRC & 1) ? CRCShifted ^ ReversedPolynomial : CRCShifted;
}

/**
 * This allows smart pointers to free object memory they are holding on to
 * Passing the allocator itself to the free function allows allocation
 * from multiple instances of an allocator and returning memory to the
 * appropriate instance it was allocated from
 */
class CAllocatorInstance
{
public:
    using FAllocate   = void * (*)(const CAllocatorInstance *, size_t);
    using FReallocate = void * (*)(const CAllocatorInstance *, void *, size_t);
    using FDeallocate = void   (*)(const CAllocatorInstance *, void *);

    /// Use the memory address of this object to seed the observer id generator
    CAllocatorInstance(FAllocate Allocate, FReallocate Reallocate, FDeallocate Deallocate)
        : _Allocate(Allocate), _Reallocate(Reallocate), _Deallocate(Deallocate), _ObserverIdGenerator(EObserverId(uintptr_t(this) | 1)) {}

    ULANG_FORCEINLINE void * Allocate(size_t NumBytes) const { return (*_Allocate)(this, NumBytes); }
    ULANG_FORCEINLINE void * Reallocate(void * Memory, size_t NumBytes) const { return (*_Reallocate)(this, Memory, NumBytes); }
    ULANG_FORCEINLINE void   Deallocate(void * Memory) const { (*_Deallocate)(this, Memory); }

    ULANG_FORCEINLINE EObserverId GenerateObserverId() { return EObserverId(_ObserverIdGenerator = RotateCRC32(_ObserverIdGenerator)); }

private:
    FAllocate   _Allocate;
    FReallocate _Reallocate;
    FDeallocate _Deallocate;

    /// Id generator for observer pointers
    /// Kept here so it does not need to be thread safe
    uint32_t _ObserverIdGenerator;
};


/// Raw memory allocator that allocates memory from the global heap
class CHeapRawAllocator
{
public:
    ULANG_FORCEINLINE CHeapRawAllocator() {}
    ULANG_FORCEINLINE CHeapRawAllocator(EDefaultInit) {}

    ULANG_FORCEINLINE static void * Allocate(size_t NumBytes)                  { return GetSystemParams()._HeapMalloc(NumBytes); }
    ULANG_FORCEINLINE static void * Reallocate(void * Memory, size_t NumBytes) { return GetSystemParams()._HeapRealloc(Memory, NumBytes); }
    ULANG_FORCEINLINE static void   Deallocate(void * Memory)                  { GetSystemParams()._HeapFree(Memory); }

    ULANG_FORCEINLINE static EObserverId GenerateObserverId()
    {
        static volatile uint32_t ObserverIdGenerator(0xdeadbeefu);
        uint32_t CRC, NewCRC;
        do
        {
            CRC = ObserverIdGenerator;
            NewCRC = RotateCRC32(CRC);
        } while (CRC != InterlockedCompareExchange(&ObserverIdGenerator, NewCRC, CRC));
        return EObserverId(NewCRC);
    }

    /// Any instance of this allocator is as good as any other
    bool operator == (const CHeapRawAllocator& Other) const { return true; }
    bool operator != (const CHeapRawAllocator& Other) const { return false; }
};

/// Raw memory allocator that keeps a pointer to an allocator instance which is used for allocation
class CInstancedRawAllocator
{
public:
    ULANG_FORCEINLINE CInstancedRawAllocator(CAllocatorInstance * AllocatorInstance) : _AllocatorInstance(AllocatorInstance) {}
    ULANG_FORCEINLINE CInstancedRawAllocator(EDefaultInit) : _AllocatorInstance(nullptr) {} // Requires explicit argument to prevent default initialization by simply passing no arguments

    ULANG_FORCEINLINE void * Allocate(size_t NumBytes) const { return _AllocatorInstance->Allocate(NumBytes); }
    ULANG_FORCEINLINE void * Reallocate(void * Memory, size_t NumBytes) { return _AllocatorInstance->Reallocate(Memory, NumBytes); }
    ULANG_FORCEINLINE void   Deallocate(void * Memory) const { _AllocatorInstance->Deallocate(Memory); }

    ULANG_FORCEINLINE EObserverId GenerateObserverId() const { return _AllocatorInstance->GenerateObserverId(); }

    CAllocatorInstance * _AllocatorInstance;
};

}

// Operators new and delete must be defined outside of any namespace

// Deliberately NOT implementing new[] and delete[] here
ULANG_FORCEINLINE void * operator new(size_t NumBytes, const ::uLang::CHeapRawAllocator & Allocator) { return Allocator.Allocate(NumBytes); }
// Unfortunately, C++ does not have a clean syntax to invoke custom delete operators so this is a bit useless...
ULANG_FORCEINLINE void operator delete(void * Memory, const ::uLang::CHeapRawAllocator & Allocator) { Allocator.Deallocate(Memory); }

// Deliberately NOT implementing new[] and delete[] here
ULANG_FORCEINLINE void * operator new(size_t NumBytes, const ::uLang::CInstancedRawAllocator & Allocator) { return Allocator.Allocate(NumBytes); }
// Unfortunately, C++ does not have a clean syntax to invoke custom delete operators so this is a bit useless...
ULANG_FORCEINLINE void operator delete(void * Memory, const ::uLang::CInstancedRawAllocator & Allocator) { Allocator.Deallocate(Memory); }

namespace uLang
{

enum
{
    // Default allocator alignment. If the default is specified, the allocator applies to engine rules.
    // Blocks >= 16 bytes will be 16-byte-aligned, Blocks < 16 will be 8-byte aligned. If the allocator does
    // not support allocation alignment, the alignment will be ignored.
    DEFAULT_ALIGNMENT = 0,

    // Minimum allocator alignment
    MIN_ALIGNMENT = 8,
};

/**
* TODO: Implement for special allocators such as binned allocators
*/
ULANG_FORCEINLINE size_t DefaultQuantizeSize(size_t Count, uint32_t Alignment)
{
    return Count;
}

ULANG_FORCEINLINE int32_t DefaultCalculateSlackShrink(int32_t NumElements, int32_t NumAllocatedElements, size_t BytesPerElement, bool bAllowQuantize, uint32_t Alignment = DEFAULT_ALIGNMENT)
{
    int32_t Retval;
    ULANG_ASSERTF(NumElements < NumAllocatedElements, "Invalid shrink parameters.");

    // If the container has too much slack, shrink it to exactly fit the number of elements.
    const uint32_t CurrentSlackElements = NumAllocatedElements - NumElements;
    const size_t CurrentSlackBytes = (NumAllocatedElements - NumElements)*BytesPerElement;
    const bool bTooManySlackBytes = CurrentSlackBytes >= 16384;
    const bool bTooManySlackElements = 3 * NumElements < 2 * NumAllocatedElements;
    if ((bTooManySlackBytes || bTooManySlackElements) && (CurrentSlackElements > 64 || !NumElements)) //  hard coded 64 :-(
    {
        Retval = NumElements;
        if (Retval > 0)
        {
            if (bAllowQuantize)
            {
                Retval = int32_t(DefaultQuantizeSize(Retval * BytesPerElement, Alignment) / BytesPerElement);
            }
        }
    }
    else
    {
        Retval = NumAllocatedElements;
    }

    return Retval;
}

ULANG_FORCEINLINE int32_t DefaultCalculateSlackGrow(int32_t NumElements, int32_t NumAllocatedElements, size_t BytesPerElement, bool bAllowQuantize, uint32_t Alignment = DEFAULT_ALIGNMENT)
{
#if ULANG_AGGRESSIVE_MEMORY_SAVING
    const size_t FirstGrow = 1;
    const size_t ConstantGrow = 0;
#else
    const size_t FirstGrow = 4;
    const size_t ConstantGrow = 16;
#endif

    int32_t Retval;
    ULANG_ASSERTF(NumElements > NumAllocatedElements && NumElements > 0, "Invalid grow parameters.");

    size_t Grow = FirstGrow; // this is the amount for the first alloc
    if (NumAllocatedElements || size_t(NumElements) > Grow)
    {
        // Allocate slack for the array proportional to its size.
        Grow = size_t(NumElements) + 3 * size_t(NumElements) / 8 + ConstantGrow;
    }
    if (bAllowQuantize)
    {
        Retval = int32_t(DefaultQuantizeSize(Grow * BytesPerElement, Alignment) / BytesPerElement);
    }
    else
    {
        Retval = int32_t(Grow);
    }
    // NumElements and MaxElements are stored in 32 bit signed integers so we must be careful not to overflow here.
    if (NumElements > Retval)
    {
        Retval = INT32_MAX;
    }

    return Retval;
}

ULANG_FORCEINLINE int32_t DefaultCalculateSlackReserve(int32_t NumElements, size_t BytesPerElement, bool bAllowQuantize, uint32_t Alignment = DEFAULT_ALIGNMENT)
{
    int32_t Retval = NumElements;
    ULANG_ASSERTF(NumElements > 0, "Invalid reserve parameters.");
    if (bAllowQuantize)
    {
        Retval = int32_t(DefaultQuantizeSize(size_t(Retval) * size_t(BytesPerElement), Alignment) / BytesPerElement);
        // NumElements and MaxElements are stored in 32 bit signed integers so we must be careful not to overflow here.
        if (NumElements > Retval)
        {
            Retval = INT32_MAX;
        }
    }

    return Retval;
}

/** A type which is used to represent a script type that is unknown at compile time. */
struct SScriptContainerElement
{
};

template <typename AllocatorType>
struct TAllocatorTraitsBase
{
    enum { SupportsMove    = false };
    enum { IsZeroConstruct = false };
};

template <typename AllocatorType>
struct TAllocatorTraits : TAllocatorTraitsBase<AllocatorType>
{
};


/** The indirect allocation policy always allocates the elements indirectly. */
template<class InRawAllocatorType, typename... AllocatorArgsType>
class TDefaultElementAllocator
{
public:

    using RawAllocatorType = InRawAllocatorType;

    enum { NeedsElementType = false };
    enum { RequireRangeCheck = true };

    /**
     * A class that may be used when NeedsElementType=false is specified.
     * If NeedsElementType=true, then this must be present but will not be used, and so can simply be a typedef to void
     */
    class ForAnyElementType
    {
    public:
        /** Default constructor. */
        ULANG_FORCEINLINE ForAnyElementType(EDefaultInit) : _Data(nullptr), _RawAllocator(DefaultInit) {}

        /** Constructor with given allocator */
        ULANG_FORCEINLINE ForAnyElementType(const RawAllocatorType & Allocator) : _Data(nullptr), _RawAllocator(Allocator) {}
        ULANG_FORCEINLINE ForAnyElementType(AllocatorArgsType... AllocatorArgs) : _Data(nullptr), _RawAllocator(AllocatorArgs...) {}

        /**
         * Moves the state of another allocator into this one.
         * Assumes that the allocator is currently empty, i.e. memory may be allocated but any existing elements have already been destructed (if necessary).
         * @param Other - The allocator to move the state from.  This allocator should be left in a valid empty state.
         */
        ULANG_FORCEINLINE void MoveToEmpty(ForAnyElementType& Other)
        {
            ULANG_ASSERTF(this != &Other, "Must not move data onto itself.");

            if (_Data)
            {
                _RawAllocator.Deallocate(_Data);
            }

            _Data       = Other._Data;
            _RawAllocator  = Other._RawAllocator;
            Other._Data = nullptr;
        }

        /** Destructor. */
        ULANG_FORCEINLINE ~ForAnyElementType()
        {
            if (_Data)
            {
                _RawAllocator.Deallocate(_Data);
            }
        }

        /** Accesses the container's current data. */
        ULANG_FORCEINLINE SScriptContainerElement* GetAllocation() const
        {
            return _Data;
        }

        /** Accesses the container's raw allocator. */
        ULANG_FORCEINLINE const RawAllocatorType & GetRawAllocator() const
        {
            return _RawAllocator;
        }

        /**
         * Resizes the container's allocation.
         * @param PreviousNumElements - The number of elements that were stored in the previous allocation.
         * @param NumElements - The number of elements to allocate space for.
         * @param NumBytesPerElement - The number of bytes/element.
         */
        ULANG_FORCEINLINE void ResizeAllocation(int32_t PreviousNumElements, int32_t NumElements, size_t NumBytesPerElement)
        {
            // Avoid calling FMemory::Realloc( nullptr, 0 ) as ANSI C mandates returning a valid pointer which is not what we want.
            if (_Data || NumElements)
            {
                //ULANG_ASSERTF(((uint64_t)NumElements*(uint64_t)ElementTypeInfo.GetSize() < (uint64_t)INT_MAX));
                size_t NumBytes = NumElements * NumBytesPerElement;
                _Data = _Data
                    ? (SScriptContainerElement*)_RawAllocator.Reallocate( _Data, NumBytes )
                    : (SScriptContainerElement*)_RawAllocator.Allocate( NumBytes );
            }
        }

        /**
         * Calculates the amount of slack to allocate for an array that has just grown or shrunk to a given number of elements.
         * @param NumElements - The number of elements to allocate space for.
         * @param CurrentNumSlackElements - The current number of elements allocated.
         * @param NumBytesPerElement - The number of bytes/element.
         */
        ULANG_FORCEINLINE int32_t CalculateSlackReserve(int32_t NumElements, int32_t NumBytesPerElement) const
        {
            return DefaultCalculateSlackReserve(NumElements, NumBytesPerElement, true);
        }

        /**
        * Calculates the amount of slack to allocate for an array that has just shrunk to a given number of elements.
        * @param NumElements - The number of elements to allocate space for.
        * @param CurrentNumSlackElements - The current number of elements allocated.
        * @param NumBytesPerElement - The number of bytes/element.
        */
        ULANG_FORCEINLINE int32_t CalculateSlackShrink(int32_t NumElements, int32_t NumAllocatedElements, int32_t NumBytesPerElement) const
        {
            return DefaultCalculateSlackShrink(NumElements, NumAllocatedElements, NumBytesPerElement, true);
        }

        /**
        * Calculates the amount of slack to allocate for an array that has just grown to a given number of elements.
        * @param NumElements - The number of elements to allocate space for.
        * @param CurrentNumSlackElements - The current number of elements allocated.
        * @param NumBytesPerElement - The number of bytes/element.
        */
        ULANG_FORCEINLINE int32_t CalculateSlackGrow(int32_t NumElements, int32_t NumAllocatedElements, int32_t NumBytesPerElement) const
        {
            return DefaultCalculateSlackGrow(NumElements, NumAllocatedElements, NumBytesPerElement, true);
        }

        ULANG_FORCEINLINE size_t GetAllocatedSize(int32_t NumAllocatedElements, size_t NumBytesPerElement) const
        {
            return NumAllocatedElements * NumBytesPerElement;
        }

        ULANG_FORCEINLINE bool HasAllocation()
        {
            return !!_Data;
        }

    private:
        ForAnyElementType(const ForAnyElementType &) = delete;
        ForAnyElementType& operator=(const ForAnyElementType &) = delete;

        /** A pointer to the container's elements. */
        SScriptContainerElement * _Data;

        /** How to allocate/deallocate the data
         * This allocator can be 0 in size */
        RawAllocatorType _RawAllocator;
    };

    /**
     * A class that may be used when NeedsElementType=true is specified.
     * If NeedsElementType=false, then this must be present but will not be used, and so can simply be a typedef to void
     */
    template<typename ElementType>
    class ForElementType : public ForAnyElementType
    {
    public:

        ULANG_FORCEINLINE ForElementType(EDefaultInit) : ForAnyElementType(DefaultInit) {}
        ULANG_FORCEINLINE ForElementType(const RawAllocatorType & Allocator) : ForAnyElementType(Allocator) {}
        ULANG_FORCEINLINE ForElementType(AllocatorArgsType&&... AllocatorArgs) : ForAnyElementType(uLang::ForwardArg<AllocatorArgsType>(AllocatorArgs)...) {}

        ULANG_FORCEINLINE ElementType* GetAllocation() const
        {
            return (ElementType*)ForAnyElementType::GetAllocation();
        }
    };
};

template<class RawAllocatorType>
struct TAllocatorTraits<TDefaultElementAllocator<RawAllocatorType>> : TAllocatorTraitsBase<TDefaultElementAllocator<RawAllocatorType>>
{
    enum { SupportsMove = true };
    enum { IsZeroConstruct = true };
};

/**
 * The inline allocation policy allocates up to a specified number of elements in the same allocation as the container.
 * Any allocation needed beyond that causes all data to be moved into an indirect allocation.
 */
template <uint32_t NumInlineElements, typename SecondaryAllocator = TDefaultElementAllocator<CHeapRawAllocator>, typename... AllocatorArgsType>
class TInlineElementAllocator
{
public:

    using RawAllocatorType = typename SecondaryAllocator::RawAllocatorType;

    enum { NeedsElementType = true };
    enum { RequireRangeCheck = true };

    template<typename ElementType>
    class ForElementType
    {
    public:

        /** Default constructor. */
        ForElementType(EDefaultInit)
        {
        }

        /** Constructor with given allocator */
        ULANG_FORCEINLINE ForElementType(const RawAllocatorType& Allocator) : _SecondaryData(Allocator) {} //-V730
        ULANG_FORCEINLINE ForElementType(AllocatorArgsType... AllocatorArgs) : _SecondaryData(AllocatorArgs...) {} //-V730

        /**
         * Moves the state of another allocator into this one.
         * Assumes that the allocator is currently empty, i.e. memory may be allocated but any existing elements have already been destructed (if necessary).
         * @param Other - The allocator to move the state from.  This allocator should be left in a valid empty state.
         */
        ULANG_FORCEINLINE void MoveToEmpty(ForElementType& Other)
        {
            ULANG_ASSERTF(this != &Other, "Must not move data onto itself.");

            if (!Other._SecondaryData.GetAllocation())
            {
                // Relocate objects from other inline storage only if it was stored inline in Other
                RelocateConstructElements<ElementType>((void*)InlineData, Other.GetInlineElements(), NumInlineElements);
            }

            // Move secondary storage in any case.
            // This will move secondary storage if it exists but will also handle the case where secondary storage is used in Other but not in *this.
            _SecondaryData.MoveToEmpty(Other._SecondaryData);
        }

        ULANG_FORCEINLINE ElementType* GetAllocation() const
        {
            return _SecondaryData.GetAllocation() ? _SecondaryData.GetAllocation() : GetInlineElements();
        }

        /** Accesses the container's raw allocator. */
        ULANG_FORCEINLINE const RawAllocatorType& GetRawAllocator() const
        {
            return _SecondaryData.GetRawAllocator();
        }

        ULANG_FORCEINLINE void ResizeAllocation(int32_t PreviousNumElements, int32_t NumElements, size_t NumBytesPerElement)
        {
            // Check if the new allocation will fit in the inline data area.
            if (uint32_t(NumElements) <= NumInlineElements)
            {
                // If the old allocation wasn't in the inline data area, relocate it into the inline data area.
                if (_SecondaryData.GetAllocation())
                {
                    RelocateConstructElements<ElementType>((void*)InlineData, (ElementType*)_SecondaryData.GetAllocation(), PreviousNumElements);

                    // Free the old indirect allocation.
                    _SecondaryData.ResizeAllocation(0, 0, NumBytesPerElement);
                }
            }
            else
            {
                if (!_SecondaryData.GetAllocation())
                {
                    // Allocate new indirect memory for the data.
                    _SecondaryData.ResizeAllocation(0, NumElements, NumBytesPerElement);

                    // Move the data out of the inline data area into the new allocation.
                    RelocateConstructElements<ElementType>((void*)_SecondaryData.GetAllocation(), GetInlineElements(), PreviousNumElements);
                }
                else
                {
                    // Reallocate the indirect data for the new size.
                    _SecondaryData.ResizeAllocation(PreviousNumElements, NumElements, NumBytesPerElement);
                }
            }
        }

        ULANG_FORCEINLINE int32_t CalculateSlackReserve(int32_t NumElements, int32_t NumBytesPerElement) const
        {
            // If the elements use less space than the inline allocation, only use the inline allocation as slack.
            return uint32_t(NumElements) <= NumInlineElements ?
                NumInlineElements :
                _SecondaryData.CalculateSlackReserve(NumElements, NumBytesPerElement);
        }
        ULANG_FORCEINLINE int32_t CalculateSlackShrink(int32_t NumElements, int32_t NumAllocatedElements, int32_t NumBytesPerElement) const
        {
            // If the elements use less space than the inline allocation, only use the inline allocation as slack.
            return uint32_t(NumElements) <= NumInlineElements ?
                NumInlineElements :
                _SecondaryData.CalculateSlackShrink(NumElements, NumAllocatedElements, NumBytesPerElement);
        }
        ULANG_FORCEINLINE int32_t CalculateSlackGrow(int32_t NumElements, int32_t NumAllocatedElements, int32_t NumBytesPerElement) const
        {
            // If the elements use less space than the inline allocation, only use the inline allocation as slack.
            return uint32_t(NumElements) <= NumInlineElements ?
                NumInlineElements :
                _SecondaryData.CalculateSlackGrow(NumElements, NumAllocatedElements, NumBytesPerElement);
        }

        ULANG_FORCEINLINE size_t GetAllocatedSize(int32_t NumAllocatedElements, size_t NumBytesPerElement) const
        {
            if (uint32_t(NumAllocatedElements) > NumInlineElements)
            {
                return _SecondaryData.GetAllocatedSize(NumAllocatedElements, NumBytesPerElement);
            }
            return 0;
        }

        ULANG_FORCEINLINE bool HasAllocation() const
        {
            return _SecondaryData.HasAllocation();
        }

        uint32_t GetInitialCapacity() const
        {
            return NumInlineElements;
        }

    private:
        ForElementType(const ForElementType&);
        ForElementType& operator=(const ForElementType&);

        /** The data is stored in this array if less than NumInlineElements is needed. */
        TTypeCompatibleBytes<ElementType> InlineData[NumInlineElements];

        /** The data is allocated through the indirect allocation policy if more than NumInlineElements is needed. */
        typename SecondaryAllocator::template ForElementType<ElementType> _SecondaryData;

        /** @return the base of the aligned inline element data */
        ElementType* GetInlineElements() const
        {
            return (ElementType*)InlineData;
        }
    };

    typedef void ForAnyElementType;
};

template <uint32_t NumInlineElements, typename SecondaryAllocator>
struct TAllocatorTraits<TInlineElementAllocator<NumInlineElements, SecondaryAllocator>> : TAllocatorTraitsBase<TInlineElementAllocator<NumInlineElements, SecondaryAllocator>>
{
    enum { SupportsMove = TAllocatorTraits<SecondaryAllocator>::SupportsMove };
};

}