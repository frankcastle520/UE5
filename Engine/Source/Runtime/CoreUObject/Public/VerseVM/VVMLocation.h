// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "HAL/Platform.h"

#include "VerseVM/VVMAbstractVisitor.h"

namespace Verse
{
struct FAllocationContext;
struct VUniqueString;

struct FLocation
{
	explicit FLocation(uint32 Line)
		: Line(Line)
	{
	}

	friend bool operator==(FLocation Left, FLocation Right)
	{
		return Left.Line == Right.Line;
	}

	friend bool operator!=(FLocation Left, FLocation Right)
	{
		return Left.Line != Right.Line;
	}

	friend uint32 GetTypeHash(const FLocation& Location)
	{
		return ::GetTypeHash(Location.Line);
	}

	uint32 Line;

private:
	friend void Visit<FLocation>(FAbstractVisitor& Visitor, FLocation&, const TCHAR* ElementName);
};

inline FLocation EmptyLocation()
{
	return FLocation{0};
}

template <>
inline void Visit(FAbstractVisitor& Visitor, FLocation& Value, const TCHAR* ElementName)
{
	Visitor.VisitObject(ElementName, [&Visitor, &Value] {
		Visitor.Visit(Value.Line, TEXT("Line"));
	});
}
} // namespace Verse

#endif
