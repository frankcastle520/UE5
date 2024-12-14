// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMMarkStackVisitor.h"

namespace Verse
{

template <typename ValueType>
void Visit(FMarkStackVisitor& Visitor, const ValueType& Value, FMarkStackVisitor::ConsumeElementName ElementName)
{
	Visitor.Visit(Value, ElementName);
}

template <typename T>
FORCEINLINE void FMarkStackVisitor::Visit(T Begin, T End)
{
	for (; Begin != End; ++Begin)
	{
		::Verse::Visit(*this, *Begin, TEXT(""));
	}
}

// Arrays
template <typename ElementType, typename AllocatorType>
FORCEINLINE void FMarkStackVisitor::Visit(const TArray<ElementType, AllocatorType>& Values, ConsumeElementName ElementName)
{
	Visit(Values.begin(), Values.end());
}

// Sets
template <typename ElementType, typename KeyFuncs, typename Allocator>
FORCEINLINE void FMarkStackVisitor::Visit(const TSet<ElementType, KeyFuncs, Allocator>& Values, ConsumeElementName ElementName)
{
	for (const auto& Value : Values)
	{
		::Verse::Visit(*this, Value, ElementName);
	}
}

// Maps
template <typename KeyType, typename ValueType, typename SetAllocator, typename KeyFuncs>
FORCEINLINE void FMarkStackVisitor::Visit(const TMap<KeyType, ValueType, SetAllocator, KeyFuncs>& Values, ConsumeElementName ElementName)
{
	for (const auto& Kvp : Values)
	{
		::Verse::Visit(*this, Kvp.Key, TEXT("Key"));
		::Verse::Visit(*this, Kvp.Value, TEXT("Value"));
	}
}

} // namespace Verse
#endif // WITH_VERSE_VM
