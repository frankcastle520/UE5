// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VVMArray.h"
#include "VVMArrayBase.h"
#include "VVMEmergentTypeCreator.h"
#include "VVMGlobalTrivialEmergentTypePtr.h"

namespace Verse
{
struct FOpResult;

struct VMutableArray : VArrayBase
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VArrayBase);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

public:
	void Reset(FAllocationContext Context);

	void AddValue(FAllocationContext Context, VValue Value);

	template <typename T>
	void Append(FAllocationContext Context, VArrayBase& Array);
	void Append(FAllocationContext Context, VArrayBase& Array);

	static VMutableArray& Concat(FAllocationContext Context, VArrayBase& Lhs, VArrayBase& Rhs);

	void InPlaceMakeImmutable(FAllocationContext Context)
	{
		static_assert(std::is_base_of_v<VArrayBase, VArray>);
		static_assert(sizeof(VArray) == sizeof(VArrayBase));
		SetEmergentType(Context, VEmergentTypeCreator::GetOrCreate(Context, VTypeCreator::GetOrCreate<VTypeArray>(Context, Num()), &VArray::StaticCppClassInfo));
	}

	static VMutableArray& New(FAllocationContext Context, uint32 NumValues, uint32 InitialCapacity, EArrayType ArrayType)
	{
		return *new (Context.AllocateFastCell(sizeof(VMutableArray))) VMutableArray(Context, NumValues, InitialCapacity, ArrayType);
	}

	static VMutableArray& New(FAllocationContext Context, uint32 NumValues, EArrayType ArrayType)
	{
		return VMutableArray::New(Context, NumValues, NumValues, ArrayType);
	}

	static VMutableArray& New(FAllocationContext Context, std::initializer_list<VValue> InitList)
	{
		return *new (Context.AllocateFastCell(sizeof(VMutableArray))) VMutableArray(Context, InitList);
	}

	template <typename InitIndexFunc, typename = std::enable_if_t<std::is_same_v<VValue, std::invoke_result_t<InitIndexFunc, uint32>>>>
	static VMutableArray& New(FAllocationContext Context, uint32 InNumValues, InitIndexFunc&& InitFunc)
	{
		return *new (Context.AllocateFastCell(sizeof(VMutableArray))) VMutableArray(Context, InNumValues, InitFunc);
	}

	static VMutableArray& New(FAllocationContext Context, FUtf8StringView String)
	{
		return *new (Context.AllocateFastCell(sizeof(VMutableArray))) VMutableArray(Context, String);
	}

	static VMutableArray& New(FAllocationContext Context)
	{
		return VMutableArray::New(Context, 0, 0, EArrayType::None);
	}

	static void SerializeImpl(VMutableArray*& This, FAllocationContext Context, FAbstractVisitor& Visitor) { Serialize(This, Context, Visitor); }

	COREUOBJECT_API VValue FreezeImpl(FAllocationContext Context);

private:
	VMutableArray(FAllocationContext Context, uint32 NumValues, uint32 InitialCapacity, EArrayType ArrayType)
		: VArrayBase(Context, NumValues, InitialCapacity, ArrayType, &GlobalTrivialEmergentType.Get(Context))
	{
		V_DIE_UNLESS(InitialCapacity >= NumValues);
	}

	VMutableArray(FAllocationContext Context, std::initializer_list<VValue> InitList)
		: VArrayBase(Context, InitList, &GlobalTrivialEmergentType.Get(Context))
	{
	}

	template <typename InitIndexFunc, typename = std::enable_if_t<std::is_same_v<VValue, std::invoke_result_t<InitIndexFunc, uint32>>>>
	VMutableArray(FAllocationContext Context, uint32 InNumValues, InitIndexFunc&& InitFunc)
		: VArrayBase(Context, InNumValues, InitFunc, &GlobalTrivialEmergentType.Get(Context))
	{
	}

	VMutableArray(FAllocationContext Context, FUtf8StringView String)
		: VArrayBase(Context, String, &GlobalTrivialEmergentType.Get(Context))
	{
	}
};

} // namespace Verse
#endif // WITH_VERSE_VM
