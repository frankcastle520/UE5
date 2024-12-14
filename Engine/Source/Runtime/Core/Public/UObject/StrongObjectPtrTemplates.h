// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/EnableIf.h"
#include "Templates/PointerIsConvertibleFromTo.h"
#include "Templates/UniquePtr.h"
#include "UObject/StrongObjectPtrTemplatesFwd.h"

class UObject;

namespace UEStrongObjectPtr_Private
{
	struct FInternalReferenceCollectorReferencerNameProvider
	{
	};

	COREUOBJECT_API void ReleaseUObject(const UObject*);
}

/**
 * Take a ref-count on a UObject to prevent it from being GC'd while this guard is in scope.
 */
template <typename ObjectType, typename ReferencerNameProvider>
class TStrongObjectPtr
{
public:
	using ElementType = ObjectType;

	TStrongObjectPtr(TStrongObjectPtr&& InOther)
	{
		Reset();
		Object = InOther.Object;
		InOther.Object = nullptr;
	}

	TStrongObjectPtr& operator=(TStrongObjectPtr&& InOther)
	{
		if (this != &InOther)
		{
			Reset();
			Object = InOther.Object;
			InOther.Object = nullptr;
		}
		return *this;
	}

	FORCEINLINE_DEBUGGABLE ~TStrongObjectPtr()
	{
		Reset();
	}

	FORCEINLINE_DEBUGGABLE TStrongObjectPtr(TYPE_OF_NULLPTR = nullptr)
	{
		static_assert(TPointerIsConvertibleFromTo<ObjectType, const volatile UObject>::Value, "TStrongObjectPtr can only be constructed with UObject types");
	}

	FORCEINLINE_DEBUGGABLE explicit TStrongObjectPtr(ObjectType* InObject)
	{
		static_assert(TPointerIsConvertibleFromTo<ObjectType, const volatile UObject>::Value, "TStrongObjectPtr can only be constructed with UObject types");
		Reset(InObject);
	}

	FORCEINLINE_DEBUGGABLE TStrongObjectPtr(const TStrongObjectPtr& InOther)
	{
		Reset(InOther.Get());
	}

	template <
		typename OtherObjectType,
		typename OtherReferencerNameProvider
		UE_REQUIRES(std::is_convertible_v<OtherObjectType*, ObjectType*>)
	>
	FORCEINLINE_DEBUGGABLE TStrongObjectPtr(const TStrongObjectPtr<OtherObjectType, OtherReferencerNameProvider>& InOther)
	{
		Reset(InOther.Get());
	}

	FORCEINLINE_DEBUGGABLE TStrongObjectPtr& operator=(const TStrongObjectPtr& InOther)
	{
		Reset(InOther.Get());
		return *this;
	}

	template <
		typename OtherObjectType,
		typename OtherReferencerNameProvider
		UE_REQUIRES(std::is_convertible_v<OtherObjectType*, ObjectType*>)
	>
	FORCEINLINE_DEBUGGABLE TStrongObjectPtr& operator=(const TStrongObjectPtr<OtherObjectType, OtherReferencerNameProvider>& InOther)
	{
		Reset(InOther.Get());
		return *this;
	}

	FORCEINLINE_DEBUGGABLE ObjectType& operator*() const
	{
		check(IsValid());
		return *(ObjectType*)Get();
	}

	FORCEINLINE_DEBUGGABLE ObjectType* operator->() const
	{
		check(IsValid());
		return (ObjectType*)Get();
	}

	FORCEINLINE_DEBUGGABLE bool IsValid() const
	{
		return Object != nullptr;
	}

	FORCEINLINE_DEBUGGABLE explicit operator bool() const
	{
		return IsValid();
	}

	FORCEINLINE_DEBUGGABLE ObjectType* Get() const
	{
		return (ObjectType*)Object;
	}

	FORCEINLINE_DEBUGGABLE void Reset()
	{
		if (Object)
		{
			// UObject type is forward declared, ReleaseRef() is not known.
			// So move the implementation to the cpp file instead.
			UEStrongObjectPtr_Private::ReleaseUObject(Object);
			Object = nullptr;
		}
	}
	
private:
	template<class T, class TWeakObjectPtrBase> friend struct TWeakObjectPtr;

	// Attach an object without incrementing its ref-count.
	FORCEINLINE_DEBUGGABLE void Attach(ObjectType* InNewObject)
	{
		Reset();
		Object = InNewObject;
	}

	// Detach the current owned object without decrementing its ref-count.
	FORCEINLINE_DEBUGGABLE ObjectType* Detach()
	{
		ObjectType* DetachedObject = Get();
		Object = nullptr;
		return DetachedObject;
	}

public:
	FORCEINLINE_DEBUGGABLE void Reset(ObjectType* InNewObject)
	{
		if (InNewObject)
		{
			if (Object == InNewObject)
			{
				return;
			}

			if (Object)
			{
				// UObject type is forward declared, ReleaseRef() is not known.
				// So move the implementation to the cpp file instead.
				UEStrongObjectPtr_Private::ReleaseUObject(Object);
			}
			InNewObject->AddRef();
			Object = InNewObject;
		}
		else
		{
			Reset();
		}
	}

	[[nodiscard]] FORCEINLINE_DEBUGGABLE friend uint32 GetTypeHash(const TStrongObjectPtr& InStrongObjectPtr)
	{
		return GetTypeHash(InStrongObjectPtr.Get());
	}

private:
	// Store as UObject to allow forward declarations without having to fully resolve ObjectType before construction.
	// This is required because the destructor calls Reset, which need to be fully resolved at declaration.
	const UObject* Object{ nullptr };

	[[nodiscard]] friend FORCEINLINE bool operator==(const TStrongObjectPtr& InLHS, const TStrongObjectPtr& InRHS)
	{
		return InLHS.Get() == InRHS.Get();
	}

	[[nodiscard]] friend FORCEINLINE bool operator==(const TStrongObjectPtr& InLHS, TYPE_OF_NULLPTR)
	{
		return !InLHS.IsValid();
	}

	[[nodiscard]] friend FORCEINLINE bool operator==(TYPE_OF_NULLPTR, const TStrongObjectPtr& InRHS)
	{
		return !InRHS.IsValid();
	}

	template <
		typename RHSObjectType,
		typename RHSReferencerNameProvider
		UE_REQUIRES(UE_REQUIRES_EXPR(std::declval<ObjectType*>() == std::declval<RHSObjectType*>()))
	>
	[[nodiscard]] friend FORCEINLINE bool operator==(const TStrongObjectPtr& InLHS, const TStrongObjectPtr<RHSObjectType, RHSReferencerNameProvider>& InRHS)
	{
		return InLHS.Get() == InRHS.Get();
	}

	template <
		typename LHSObjectType,
		typename LHSReferencerNameProvider
		UE_REQUIRES(UE_REQUIRES_EXPR(std::declval<LHSObjectType*>() == std::declval<ObjectType*>()))
	>
	[[nodiscard]] friend FORCEINLINE bool operator==(const TStrongObjectPtr<LHSObjectType, LHSReferencerNameProvider>& InLHS, const TStrongObjectPtr& InRHS)
	{
		return InLHS.Get() == InRHS.Get();
	}

#if !PLATFORM_COMPILER_HAS_GENERATED_COMPARISON_OPERATORS
	[[nodiscard]] friend FORCEINLINE bool operator!=(const TStrongObjectPtr& InLHS, const TStrongObjectPtr& InRHS)
	{
		return InLHS.Get() != InRHS.Get();
	}

	[[nodiscard]] friend FORCEINLINE bool operator!=(const TStrongObjectPtr& InLHS, TYPE_OF_NULLPTR)
	{
		return InLHS.IsValid();
	}

	[[nodiscard]] friend FORCEINLINE bool operator!=(TYPE_OF_NULLPTR, const TStrongObjectPtr& InRHS)
	{
		return InRHS.IsValid();
	}

	template <
		typename RHSObjectType,
		typename RHSReferencerNameProvider
		UE_REQUIRES(UE_REQUIRES_EXPR(std::declval<ObjectType*>() == std::declval<RHSObjectType*>()))
	>
	[[nodiscard]] friend FORCEINLINE bool operator!=(const TStrongObjectPtr& InLHS, const TStrongObjectPtr<RHSObjectType, RHSReferencerNameProvider>& InRHS)
	{
		return InLHS.Get() != InRHS.Get();
	}

	template <
		typename LHSObjectType,
		typename LHSReferencerNameProvider
		UE_REQUIRES(UE_REQUIRES_EXPR(std::declval<LHSObjectType*>() == std::declval<ObjectType*>()))
	>
	[[nodiscard]] friend FORCEINLINE bool operator!=(const TStrongObjectPtr<LHSObjectType, LHSReferencerNameProvider>& InLHS, const TStrongObjectPtr& InRHS)
	{
		return InLHS.Get() != InRHS.Get();
	}
#endif
};
