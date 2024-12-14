// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMContext.h"
#include "VerseVM/VVMRestValue.h"
#include "VerseVM/VVMWriteBarrier.h"

namespace Verse
{
struct VFrame;

struct VReturnSlot
{
	enum class EReturnKind : uint8
	{
		RestValue,
		Value,
	};

	template <typename ReturnSlotType>
	VReturnSlot(FAllocationContext Context, ReturnSlotType ReturnSlot)
	{
		Set(Context, ReturnSlot);
	}

	VValue Get(FAllocationContext Context)
	{
		if (Kind == EReturnKind::RestValue)
		{
			if (RestValue)
			{
				return RestValue->Get(Context);
			}
			else
			{
				return VValue();
			}
		}
		else
		{
			return Value.Get();
		}
	}

	void Set(FAllocationContext Context, VRestValue* InRestValue)
	{
		RestValue = InRestValue;
		Kind = EReturnKind::RestValue;
	}

	void Set(FAllocationContext Context, VValue InValue)
	{
		Value.Set(Context, InValue);
		Kind = EReturnKind::Value;
	}

	template <typename TVisitor>
	FORCEINLINE void Visit(TVisitor& Visitor)
	{
		Visitor.Visit(EffectToken, TEXT("ReturnEffectToken"));
		if (Kind == EReturnKind::Value)
		{
			Visitor.Visit(Value, TEXT("ReturnSlot"));
		}
	}

	friend class FInterpreter;
	friend struct VFrame;

private:
	VRestValue EffectToken{0};
	union
	{
		// This points into Frame or the C++ stack, so we don't need to tell GC about it.
		VRestValue* RestValue;
		TWriteBarrier<VValue> Value;
	};
	EReturnKind Kind;
};
} // namespace Verse

#endif