// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "Templates/TypeCompatibleBytes.h"
#include "VerseVM/VVMAbstractVisitor.h"

class UObject;

namespace Verse
{

inline FAbstractVisitor::FReferrerToken::FReferrerToken(VCell* Cell)
	: EncodedBits(BitCast<uint64>(Cell) | static_cast<uint64>(EReferrerType::Cell))
{
}

inline FAbstractVisitor::FReferrerToken::FReferrerToken(UObject* Object)
	: EncodedBits(BitCast<uint64>(Object) | static_cast<uint64>(EReferrerType::UObject))
{
}

inline FAbstractVisitor::EReferrerType FAbstractVisitor::FReferrerToken::GetType() const
{
	return static_cast<EReferrerType>((EncodedBits & EncodingBits));
}

inline bool FAbstractVisitor::FReferrerToken::IsCell() const
{
	return GetType() == EReferrerType::Cell;
}

inline VCell* FAbstractVisitor::FReferrerToken::AsCell() const
{
	checkSlow(IsCell());
	return BitCast<VCell*>(EncodedBits & ~EncodingBits);
}

inline bool FAbstractVisitor::FReferrerToken::IsUObject() const
{
	return GetType() == EReferrerType::UObject;
}

inline UObject* FAbstractVisitor::FReferrerToken::AsUObject() const
{
	checkSlow(IsUObject());
	return BitCast<UObject*>(EncodedBits & ~EncodingBits);
}

inline FAbstractVisitor::FReferrerContext::FReferrerContext(FAbstractVisitor& InVisitor, FReferrerToken InReferrer)
	: Visitor(InVisitor)
	, Referrer(InReferrer)
{
	Previous = Visitor.Context;
	Visitor.Context = this;
}

inline FAbstractVisitor::FReferrerContext::~FReferrerContext()
{
	Visitor.Context = Previous;
}

template <typename ValueType>
void Visit(FAbstractVisitor& Visitor, ValueType& Value, const TCHAR* ElementName)
{
	Visitor.Visit(Value, ElementName);
}

// Simple arrays
template <typename T>
FORCEINLINE void FAbstractVisitor::Visit(T Begin, T End)
{
	for (; Begin != End; ++Begin)
	{
		::Verse::Visit(*this, *Begin, TEXT(""));
	}
}

// Arrays
template <typename ElementType, typename AllocatorType>
FORCEINLINE void FAbstractVisitor::Visit(TArray<ElementType, AllocatorType>& Values, const TCHAR* ElementName)
{
	if (IsLoading())
	{
		uint64 ScratchNumElements = 0;
		BeginArray(ElementName, ScratchNumElements);
		Values.SetNum((typename TArray<ElementType, AllocatorType>::SizeType)ScratchNumElements);
		Visit(Values.begin(), Values.end());
		EndArray();
	}
	else
	{
		uint64 ScratchNumElements = Values.Num();
		BeginArray(ElementName, ScratchNumElements);
		Visit(Values.begin(), Values.end());
		EndArray();
	}
}

// Sets
template <typename ElementType, typename KeyFuncs, typename Allocator>
FORCEINLINE void FAbstractVisitor::Visit(const TSet<ElementType, KeyFuncs, Allocator>& Values, const TCHAR* ElementName)
{
	uint64 ScratchNumElements = Values.Num();
	BeginSet(ElementName, ScratchNumElements);
	for (const auto& Value : Values)
	{
		::Verse::Visit(*this, const_cast<ElementType&>(Value), TEXT(""));
	}
	EndSet();
}

// Maps
template <typename KeyType, typename ValueType, typename SetAllocator, typename KeyFuncs>
FORCEINLINE void FAbstractVisitor::Visit(TMap<KeyType, ValueType, SetAllocator, KeyFuncs>& Values, const TCHAR* ElementName)
{
	uint64 ScratchNumElements = Values.Num();
	BeginMap(ElementName, ScratchNumElements);
	for (auto& Kvp : Values)
	{
		VisitPair([this, &Kvp] {
			::Verse::Visit(*this, Kvp.Key, TEXT("Key"));
			::Verse::Visit(*this, Kvp.Value, TEXT("Value"));
		});
	}
	EndMap();
}

} // namespace Verse
#endif // WITH_VERSE_VM
