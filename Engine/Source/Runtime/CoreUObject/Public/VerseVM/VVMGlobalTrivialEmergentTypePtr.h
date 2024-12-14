// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VVMEmergentType.h"
#include "VVMGlobalHeapRoot.h"
#include "VVMLazyInitialized.h"
#include "VVMMarkStack.h"
#include "VVMWriteBarrier.h"

namespace Verse
{

struct FGlobalTrivialEmergentTypePtrRoot : FGlobalHeapRoot
{
	FGlobalTrivialEmergentTypePtrRoot(FAccessContext Context, VEmergentType* Type)
		: EmergentType(Context, Type)
	{
	}

	COREUOBJECT_API void Visit(FAbstractVisitor& Visitor) override;
	COREUOBJECT_API void Visit(FMarkStackVisitor& Visitor) override;

	TWriteBarrier<VEmergentType> EmergentType;

private:
	template <typename TVisitor>
	FORCEINLINE void VisitImpl(TVisitor&);
};

struct FGlobalTrivialEmergentTypePtr
{
	FGlobalTrivialEmergentTypePtr() = default;

protected:
	VEmergentType& Get(FAllocationContext Context, VCppClassInfo* ClassInfo)
	{
		VEmergentType* Result = EmergentType.load(std::memory_order_relaxed);
		std::atomic_signal_fence(std::memory_order_seq_cst);
		if (Result)
		{
			return *Result;
		}
		else
		{
			return Create(Context, ClassInfo);
		}
	}

	COREUOBJECT_API VEmergentType& Create(FAllocationContext Context, VCppClassInfo* ClassInfo);

	std::atomic<VEmergentType*> EmergentType = nullptr;
};

template <VCppClassInfo* ClassInfo>
struct TGlobalTrivialEmergentTypePtr : public FGlobalTrivialEmergentTypePtr
{
	TGlobalTrivialEmergentTypePtr() = default;

	VEmergentType& Get(FAllocationContext Context)
	{
		return FGlobalTrivialEmergentTypePtr::Get(Context, ClassInfo);
	}
};

} // namespace Verse
#endif // WITH_VERSE_VM
