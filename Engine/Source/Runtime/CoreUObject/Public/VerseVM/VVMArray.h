// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VVMArrayBase.h"
#include "VVMEmergentTypeCreator.h"
#include "VVMGlobalTrivialEmergentTypePtr.h"
#include "VVMType.h"
#include "VVMTypeCreator.h"
#include "VVMUniqueCreator.h"

namespace Verse
{

struct VInt;

// Array, fix number of elements, each with its own type
// No type information for the parts here.
struct VTypeArray : VType
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VType);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	uint32 Size;

	static VTypeArray* New(FAllocationContext Context, uint32 S)
	{
		return new (Context.AllocateFastCell(sizeof(VTypeArray))) VTypeArray(Context, S);
	}
	static bool Equals(const VType& Type, uint32 S)
	{
		if (Type.IsA<VTypeArray>())
		{
			const VTypeArray& Other = Type.StaticCast<VTypeArray>();
			return Other.Size == S;
		}
		return false;
	}

	uint32 Num() const
	{
		return Size;
	}

private:
	explicit VTypeArray(FAllocationContext& Context, uint32 S)
		: VType(Context, &GlobalTrivialEmergentType.Get(Context))
		, Size(S)
	{
	}
};

struct VArray : VArrayBase
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VArrayBase);

	static VArray& Concat(FRunningContext Context, VArrayBase& Lhs, VArrayBase& Rhs);

	static VArray& New(FAllocationContext Context, uint32 NumValues, EArrayType ArrayType)
	{
		return *new (Context.AllocateFastCell(sizeof(VArray))) VArray(Context, NumValues, ArrayType);
	}

	static VArray& New(FAllocationContext Context, std::initializer_list<VValue> InitList)
	{
		return *new (Context.AllocateFastCell(sizeof(VArray))) VArray(Context, InitList);
	}

	template <typename InitIndexFunc, typename = std::enable_if_t<std::is_same_v<VValue, std::invoke_result_t<InitIndexFunc, uint32>>>>
	static VArray& New(FAllocationContext Context, uint32 NumValues, InitIndexFunc&& InitFunc)
	{
		return *new (Context.AllocateFastCell(sizeof(VArray))) VArray(Context, NumValues, InitFunc);
	}

	static VArray& New(FAllocationContext Context, FUtf8StringView String)
	{
		return *new (Context.AllocateFastCell(sizeof(VArray))) VArray(Context, String,
			VEmergentTypeCreator::GetOrCreate(Context, VTypeCreator::GetOrCreate<VTypeArray>(Context, String.Len()), &StaticCppClassInfo));
	}

	static void SerializeImpl(VArray*& This, FAllocationContext Context, FAbstractVisitor& Visitor) { Serialize(This, Context, Visitor); }

private:
	friend struct VMutableArray;
	VArray(FAllocationContext Context, uint32 InNumValues, EArrayType ArrayType)
		: VArrayBase(Context, InNumValues, ArrayType, VEmergentTypeCreator::GetOrCreate(Context, VTypeCreator::GetOrCreate<VTypeArray>(Context, InNumValues), &StaticCppClassInfo)) {}

	VArray(FAllocationContext Context, std::initializer_list<VValue> InitList)
		: VArrayBase(Context, InitList, VEmergentTypeCreator::GetOrCreate(Context, VTypeCreator::GetOrCreate<VTypeArray>(Context, static_cast<uint32>(InitList.size())), &StaticCppClassInfo)) {}

	template <typename InitIndexFunc, typename = std::enable_if_t<std::is_same_v<VValue, std::invoke_result_t<InitIndexFunc, uint32>>>>
	VArray(FAllocationContext Context, uint32 InNumValues, InitIndexFunc&& InitFunc)
		: VArrayBase(Context, InNumValues, InitFunc, VEmergentTypeCreator::GetOrCreate(Context, VTypeCreator::GetOrCreate<VTypeArray>(Context, InNumValues), &StaticCppClassInfo)) {}

protected:
	VArray(FAllocationContext Context, FUtf8StringView String, VEmergentType* Type)
		: VArrayBase(Context, String, Type) {}
};

} // namespace Verse
#endif // WITH_VERSE_VM
