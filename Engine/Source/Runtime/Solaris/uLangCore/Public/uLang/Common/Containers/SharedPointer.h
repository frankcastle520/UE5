// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Common.h"
#include "uLang/Common/Memory/Allocator.h"
#include "uLang/Common/Templates/Conditionals.h"
#include "uLang/Common/Templates/References.h"

namespace uLang
{

template<class ObjectType, bool AllowNull, class AllocatorType, typename... AllocatorArgsType> class TSPtrG;
template<class ObjectType, bool AllowNull, class AllocatorType, typename... AllocatorArgsType> class TSPtrArrayG;
template<class ObjectType, bool AllowNull, class KeyType, class AllocatorType, typename... AllocatorArgsType> class TSPtrSetG;

/**
 * Mixin super/base class for objects that need to be reference counted.
 *
 * As a mixin it avoids the (minor) speed cost of virtual function calls and the
 * virtual table memory cost (1 pointer).  It uses the coding technique known as
 * "mix-in from above"/"Curiously Recurring Template Pattern".
 */
class ULANGCORE_API CSharedMix
{
public:

    CSharedMix() : _RefCount(0) {}
	virtual ~CSharedMix();

    CSharedMix(const CSharedMix & Other) = delete; // Do not copy
    CSharedMix & operator=(const CSharedMix & Other) = delete; // Do not assign

    uint32_t GetRefCount() const { return _RefCount; }

protected:
    template<class ObjectType>
    static TSPtrG<ObjectType, false, CHeapRawAllocator> SharedThis(ObjectType* This)
    {
        return TSPtrG<ObjectType, false, CHeapRawAllocator>(This, CHeapRawAllocator());
    }

    template<class ObjectType>
    static TSPtrG<const ObjectType, false, CHeapRawAllocator> SharedThis(const ObjectType* This)
    {
        return TSPtrG<const ObjectType, false, CHeapRawAllocator>(This, CHeapRawAllocator());
    }

private:

    template<class ObjectType, bool AllowNull, class AllocatorType, typename... AllocatorArgsType> friend class TSPtrG;
    template<class ObjectType, bool AllowNull, class AllocatorType, typename... AllocatorArgsType> friend class TSPtrArrayG;
    template<class ObjectType, bool AllowNull, class KeyType, class AllocatorType, typename... AllocatorArgsType> friend class TSPtrSetG;



    void    Reference() const { ++_RefCount; }
    bool    Dereference() const;

    /// Number of references to this object
    mutable uint32_t _RefCount;

};

/**
 * TSPtr is a convenience class - it wraps around a pointer to an object
 * that is a subclass of CSharedMix [or any class that has the methods:
 * Reference() & Dereference()] and acts just like a regular pointer except that
 * it automatically references and dereferences the object as needed.
 * The AllocatorType must provide the methods void * Allocate(size_t) and void Deallocate(void *)
 */
template<class ObjectType, bool AllowNull, class AllocatorType, typename... AllocatorArgsType>
class TSPtrG
{
public:

    // Construction

    ULANG_FORCEINLINE TSPtrG(NullPtrType NullPtr = nullptr) : _Object(nullptr), _Allocator(DefaultInit), _ReleaseFunc(nullptr) { static_assert(AllowNull, "Cannot default construct a shared reference, as it is not allowed to be null."); }
    ULANG_FORCEINLINE TSPtrG(const TSPtrG & Other) : _Object(Other._Object), _Allocator(Other._Allocator) { EnableRelease(); if (Other._Object) { Other._Object->Reference(); } }
    template<class OtherObjectType, bool OtherAllowNull, typename = typename TEnableIf<TPointerIsConvertibleFromTo<OtherObjectType, ObjectType>::Value && (AllowNull || !OtherAllowNull)>::Type>
    ULANG_FORCEINLINE TSPtrG(const TSPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> & Other) : _Object(Other._Object), _Allocator(Other._Allocator) { EnableRelease(); if (Other._Object) { Other._Object->Reference(); } }
    ULANG_FORCEINLINE TSPtrG(TSPtrG && Other) : _Object(Other._Object), _Allocator(Other._Allocator) { EnableRelease(); Other._Object = nullptr; } // Note: We null here even when OtherAllowNull==false since 99% likely will be destructed immediately after anyway
    template<class OtherObjectType, bool OtherAllowNull, typename = typename TEnableIf<TPointerIsConvertibleFromTo<OtherObjectType, ObjectType>::Value && (AllowNull || !OtherAllowNull)>::Type>
    ULANG_FORCEINLINE TSPtrG(TSPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> && Other) : _Object(Other._Object), _Allocator(Other._Allocator) { EnableRelease(); Other._Object = nullptr; } // Note: We null here even when OtherAllowNull==false since 99% likely will be destructed immediately after anyway 
    ULANG_FORCEINLINE ~TSPtrG() { if (_Object) Release(); }

    template<typename... CtorArgsType>
    ULANG_FORCEINLINE TSPtrG& SetNew(CtorArgsType&&... CtorArgs) { EnableRelease(); if (_Object) { Release(); } ObjectType * Object = new(_Allocator) ObjectType(uLang::ForwardArg<CtorArgsType>(CtorArgs)...); ULANG_ASSERT(Object); Object->Reference(); _Object = Object; return *this; }
    template<typename... CtorArgsType>
    ULANG_FORCEINLINE static TSPtrG New(AllocatorArgsType&&... AllocatorArgs, CtorArgsType&&... CtorArgs) { AllocatorType Allocator(AllocatorArgs...); ObjectType* Object = new(Allocator) ObjectType(uLang::ForwardArg<CtorArgsType>(CtorArgs)...); ULANG_ASSERT(Object); return TSPtrG(Object, uLang::Move(Allocator)); }

    // Assignment

    ULANG_FORCEINLINE TSPtrG & operator=(NullPtrType) { static_assert(AllowNull, "Cannot assign null to shared reference, as it is not allowed to be null."); if (_Object) { Release(); } _Object = nullptr; return *this; }
    ULANG_FORCEINLINE TSPtrG & operator=(const TSPtrG & Other) { return AssignCopy(Other); }
    template<class OtherObjectType, bool OtherAllowNull, typename = typename TEnableIf<TPointerIsConvertibleFromTo<OtherObjectType, ObjectType>::Value && (AllowNull || !OtherAllowNull)>::Type>
    ULANG_FORCEINLINE TSPtrG & operator=(const TSPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> & Other) { return AssignCopy(Other); }
    ULANG_FORCEINLINE TSPtrG & operator=(TSPtrG && Other) { return AssignMove(ForwardArg<TSPtrG>(Other)); }
    template<class OtherObjectType, bool OtherAllowNull, typename = typename TEnableIf<TPointerIsConvertibleFromTo<OtherObjectType, ObjectType>::Value>::Type>
    ULANG_FORCEINLINE TSPtrG & operator=(TSPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> && Other) { return AssignMove(uLang::ForwardArg<TSPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...>>(Other)); }

    // Conversion methods

    ULANG_FORCEINLINE operator ObjectType*() const { return _Object; }
    ULANG_FORCEINLINE ObjectType &          operator*() const { ULANG_ASSERT(_Object); return *_Object; }
    ULANG_FORCEINLINE ObjectType *          operator->() const { ULANG_ASSERT(AllowNull || _Object); return _Object; }
    ULANG_FORCEINLINE ObjectType *          Get() const { ULANG_ASSERT(AllowNull || _Object); return _Object; }
    ULANG_FORCEINLINE const AllocatorType & GetAllocator() const { return _Allocator; }
    ULANG_FORCEINLINE void                  Reset() { if (_Object) { Release(); _Object = nullptr; } }

    ULANG_FORCEINLINE TSPtrG<ObjectType, false, AllocatorType, AllocatorArgsType...>& AsRef()&
    {
        static_assert(AllowNull, "Unnecessary conversion!");
        ULANG_ASSERTF(_Object, "Converting null pointer to reference!");
        return *reinterpret_cast<TSPtrG<ObjectType, false, AllocatorType, AllocatorArgsType...> *>(this);
    }
    ULANG_FORCEINLINE TSPtrG<ObjectType, false, AllocatorType, AllocatorArgsType...> AsRef()&&
    {
        static_assert(AllowNull, "Unnecessary conversion!");
        ULANG_ASSERTF(_Object, "Converting null pointer to reference!");
        return *reinterpret_cast<TSPtrG<ObjectType, false, AllocatorType, AllocatorArgsType...> *>(this);
    }
    ULANG_FORCEINLINE const TSPtrG<ObjectType, false, AllocatorType, AllocatorArgsType...>& AsRef() const&
    {
        static_assert(AllowNull, "Unnecessary conversion!");
        ULANG_ASSERTF(_Object, "Converting null pointer to reference!");
        return *reinterpret_cast<const TSPtrG<ObjectType, false, AllocatorType, AllocatorArgsType...> *>(this);
    }

    template<class OtherObjectType, typename = typename TEnableIf<TPointerIsStaticCastableFromTo<OtherObjectType, ObjectType>::Value>::Type>
    ULANG_FORCEINLINE TSPtrG<OtherObjectType, AllowNull, AllocatorType, AllocatorArgsType...>& As() { return *reinterpret_cast<TSPtrG<OtherObjectType, AllowNull, AllocatorType, AllocatorArgsType...> *>(this); }
    template<class OtherObjectType, typename = typename TEnableIf<TPointerIsStaticCastableFromTo<OtherObjectType, ObjectType>::Value>::Type>
    ULANG_FORCEINLINE const TSPtrG<OtherObjectType, AllowNull, AllocatorType, AllocatorArgsType...>& As() const { return *reinterpret_cast<const TSPtrG<OtherObjectType, AllowNull, AllocatorType, AllocatorArgsType...> *>(this); }

    // Comparison operators

    ULANG_FORCEINLINE bool operator==(NullPtrType)  const { return _Object == nullptr; }
    ULANG_FORCEINLINE bool operator!=(NullPtrType)  const { return _Object != nullptr; }
    ULANG_FORCEINLINE bool operator==(const TSPtrG & Other)  const { return _Object == Other._Object; }
    template<class OtherObjectType, bool OtherAllowNull>
    ULANG_FORCEINLINE bool operator==(const TSPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> & Other)  const { return _Object == Other._Object; }
    template<class OtherObjectType, bool OtherAllowNull>
    ULANG_FORCEINLINE bool operator==(OtherObjectType * Object) const { return _Object == Object; }
    ULANG_FORCEINLINE bool operator!=(const TSPtrG & Other)  const { return _Object != Other._Object; }
    template<class OtherObjectType, bool OtherAllowNull>
    ULANG_FORCEINLINE bool operator!=(const TSPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> & Other)  const { return _Object != Other._Object; }
    template<class OtherObjectType, bool OtherAllowNull>
    ULANG_FORCEINLINE bool operator!=(OtherObjectType * Object) const { return _Object != Object; }
    ULANG_FORCEINLINE bool operator< (const TSPtrG & Other)  const { return _Object < Other._Object; }
    template<class OtherObjectType, bool OtherAllowNull>
    ULANG_FORCEINLINE bool operator< (const TSPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> & Other)  const { return _Object < Other._Object; }
    template<class OtherObjectType, bool OtherAllowNull>
    ULANG_FORCEINLINE bool operator< (OtherObjectType * Object) const { return _Object < Object; }
    ULANG_FORCEINLINE bool operator> (const TSPtrG & Other)  const { return _Object > Other._Object; }
    template<class OtherObjectType, bool OtherAllowNull>
    ULANG_FORCEINLINE bool operator> (const TSPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> & Other)  const { return _Object > Other._Object; }
    template<class OtherObjectType, bool OtherAllowNull>
    ULANG_FORCEINLINE bool operator> (OtherObjectType * Object) const { return _Object > Object; }

    // Validation methods

    ULANG_FORCEINLINE operator bool() { return (_Object != nullptr); }
    ULANG_FORCEINLINE operator bool() const { return (_Object != nullptr); }
    ULANG_FORCEINLINE bool operator!() const { return (_Object == nullptr); }
    ULANG_FORCEINLINE bool IsValid() const { return (_Object != nullptr); }

    // Needed to avoid ambiguous function resolution between
    // uLang and UnrealTemplate.h versions of Swap
    ULANG_FORCEINLINE friend void Swap(TSPtrG& A, TSPtrG& B) { uLang::Swap(A, B); }

protected:

    template<class OtherObjectType, bool OtherAllowNull, class OtherAllocatorType, typename... OtherAllocatorArgsType> friend class TSPtrG;
    template<class OtherObjectType, bool OtherAllowNull, class OtherAllocatorType, typename... OtherAllocatorArgsType> friend class TSPtrArrayG;
    template<class OtherObjectType, bool OtherAllowNull, class OtherKeyType, class OtherAllocatorType, typename... OtherAllocatorArgsType> friend class TSPtrSetG;


    // This is needed for CSharedMix to support SharedThis().
    // `return SharedThis(this)` is an equivalent of `return this` for Shared Pointers.
    friend class CSharedMix;

    ULANG_FORCEINLINE TSPtrG(ObjectType * Object, const AllocatorType & Allocator)
        : _Object(Object)
        , _Allocator(Allocator)
        , _ReleaseFunc(nullptr)
    {
        if (Object)
        {
            Object->Reference();
            EnableRelease();
        }
    }

    template<class OtherObjectType, bool OtherAllowNull>
    ULANG_FORCEINLINE TSPtrG & AssignCopy(const TSPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> & Other)
    {
        if (_Object != Other._Object)
        {
            if (Other._Object)
            {
                Other._Object->Reference();
                EnableRelease();
            }

            if (_Object)
            {
                Release();
            }

            _Object = Other._Object;
            _Allocator = Other._Allocator;
        }

        return *this;
    }

    template<class OtherObjectType, bool OtherAllowNull>
    ULANG_FORCEINLINE TSPtrG & AssignMove(TSPtrG<OtherObjectType, OtherAllowNull, AllocatorType, AllocatorArgsType...> && Other)
    {
        EnableRelease();

        if (_Object)
        {
            Release();
        }

        _Object = Other._Object;
        _Allocator = Other._Allocator;
        Other._Object = nullptr;

        return *this;
    }

    /// Set the release function pointer to a valid value
    ULANG_FORCEINLINE void EnableRelease()
    {
        _ReleaseFunc = [](ObjectType * Object, const AllocatorType & Allocator)
        {
            if (Object->Dereference())
            {
                // No references left: Delete the object
                Object->~ObjectType();
                Allocator.Deallocate(Object);
            }
        };
    }

    /// Let go of our object
    ULANG_FORCEINLINE void Release()
    {
        // Call the function set by EnableRelease() above
        (_ReleaseFunc)(_Object, _Allocator);
    }

    /// Pointer to original object
    ObjectType * _Object;

    /// How to deallocate the object
    /// This allocator can be 0 in size
    AllocatorType _Allocator;

    /// Indirection to keep knowledge about ObjectType out of default constructor and destructor
    /// so that TSPtrG can be forward declared with an incomplete ObjectType argument
    /// The price we pay is 8 more bytes of memory, indirect function call on each release, 
    /// and that we have to (re-)initialize this function pointer in all methods that can set the pointer to something non-null
    using ReleaseFuncType = void(*)(ObjectType *, const AllocatorType &);
    ReleaseFuncType _ReleaseFunc;
};

/// Shared pointer that allocates object on the heap
template<class ObjectType>
using TSPtr = TSPtrG<ObjectType, true, CHeapRawAllocator>;

/// Shared reference that allocates object on the heap
template<class ObjectType>
using TSRef = TSPtrG<ObjectType, false, CHeapRawAllocator>;

/// Shared pointer that allocates object using a given allocator instance
template<class ObjectType>
using TSPtrA = TSPtrG<ObjectType, true, CInstancedRawAllocator, CAllocatorInstance *>;

/// Shared reference that allocates object using a given allocator instance
template<class ObjectType>
using TSRefA = TSPtrG<ObjectType, false, CInstancedRawAllocator, CAllocatorInstance *>;

//=======================================================================================
// CSharedMix Inline Methods
//=======================================================================================

/**
 * Decrements the reference count to this object
 * and if the reference count becomes 0 return true
 */
ULANG_FORCEINLINE bool CSharedMix::Dereference() const
{
    ULANG_ASSERTF(_RefCount > 0, "Tried to dereference an object that has no references!");
    return (--_RefCount == 0);
}

}