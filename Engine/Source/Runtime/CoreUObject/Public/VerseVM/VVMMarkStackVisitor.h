// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"
#include "VVMMarkStack.h"
#include "VVMRestValue.h"

namespace Verse
{

struct FMarkStackVisitor
{
	UE_NONCOPYABLE(FMarkStackVisitor);

	static constexpr bool bIsAbstractVisitor = false;

	// No need to save the string when using the mark stack visitor
	struct ConsumeElementName
	{
		ConsumeElementName(const TCHAR*)
		{
		}
	};

	FMarkStackVisitor(FMarkStack& InMarkStack)
		: MarkStack(InMarkStack)
	{
	}

	FORCEINLINE bool IsMarked(const VCell* InCell, ConsumeElementName ElementName)
	{
		return FHeap::IsMarked(InCell);
	}

	void VisitNonNull(const VCell* InCell, ConsumeElementName ElementName)
	{
		MarkStack.MarkNonNull(InCell);
	}

	void VisitNonNull(const UObject* InObject, ConsumeElementName ElementName)
	{
		MarkStack.MarkNonNull(InObject);
	}

	void VisitAuxNonNull(const void* InAux, ConsumeElementName ElementName)
	{
		MarkStack.MarkAuxNonNull(InAux);
	}

	FORCEINLINE void VisitEmergentType(const VCell* InEmergentType)
	{
		VisitNonNull(InEmergentType, TEXT("EmergentType"));
	}

	FORCEINLINE void Visit(const VCell* InCell, ConsumeElementName ElementName)
	{
		if (InCell != nullptr)
		{
			VisitNonNull(InCell, ElementName);
		}
	}

	FORCEINLINE void Visit(const UObject* InObject, ConsumeElementName ElementName)
	{
		if (InObject != nullptr)
		{
			VisitNonNull(InObject, ElementName);
		}
	}

	FORCEINLINE void VisitAux(const void* Aux, ConsumeElementName ElementName)
	{
		if (Aux != nullptr)
		{
			VisitAuxNonNull(Aux, ElementName);
		}
	}

	FORCEINLINE void Visit(VFloat, ConsumeElementName)
	{
	}

	FORCEINLINE void Visit(VValue Value, ConsumeElementName ElementName)
	{
		if (VCell* Cell = Value.ExtractCell())
		{
			Visit(Cell, ElementName);
		}
		else if (Value.IsUObject())
		{
			Visit(Value.AsUObject(), ElementName);
		}
	}

	FORCEINLINE void Visit(const VRestValue& Value, ConsumeElementName ElementName)
	{
		Value.Visit(*this, TEXT(""));
	}

	// Null visitors that are only used by the abstract visitor
	FORCEINLINE void Visit(bool bValue, ConsumeElementName ElementName)
	{
	}

	FORCEINLINE void Visit(const FAnsiStringView Value, ConsumeElementName ElementName)
	{
	}

	FORCEINLINE void Visit(const FWideStringView Value, ConsumeElementName ElementName)
	{
	}

	FORCEINLINE void Visit(const FUtf8StringView Value, ConsumeElementName ElementName)
	{
	}

	// NOTE: The Value parameter can not be passed by value.
	template <typename T>
	FORCEINLINE void Visit(const TWriteBarrier<T>& Value, ConsumeElementName ElementName)
	{
		Visit(Value.Get(), ElementName);
	}

	template <typename TVisitBody>
	FORCEINLINE void VisitClass(FUtf8StringView, TVisitBody VisitBody)
	{
		VisitBody();
	}

	template <typename TVisitBody>
	FORCEINLINE void VisitFunction(FUtf8StringView, TVisitBody VisitBody)
	{
		VisitBody();
	}

	template <typename TVisitBody>
	FORCEINLINE void VisitConstrainedInt(TVisitBody VisitBody)
	{
		VisitBody();
	}

	template <typename TVisitBody>
	FORCEINLINE void VisitConstrainedFloat(TVisitBody VisitBody)
	{
		VisitBody();
	}

	template <typename T>
	void Visit(T Begin, T End);

	// Arrays
	template <typename ElementType, typename AllocatorType>
	void Visit(const TArray<ElementType, AllocatorType>& Values, ConsumeElementName ElementName);

	// Sets
	template <typename ElementType, typename KeyFuncs, typename Allocator>
	void Visit(const TSet<ElementType, KeyFuncs, Allocator>& Values, ConsumeElementName ElementName);

	// Maps
	template <typename KeyType, typename ValueType, typename SetAllocator, typename KeyFuncs>
	void Visit(const TMap<KeyType, ValueType, SetAllocator, KeyFuncs>& Values, ConsumeElementName ElementName);

	void ReportNativeBytes(size_t Bytes)
	{
		MarkStack.ReportNativeBytes(Bytes);
	}

private:
	FMarkStack& MarkStack;
};

// Helper method used by the container methods that allow for template specialization of types
template <typename ValueType>
void Visit(FMarkStackVisitor& Visitor, const ValueType& Value, FMarkStackVisitor::ConsumeElementName ElementName);

} // namespace Verse
#endif // WITH_VERSE_VM
