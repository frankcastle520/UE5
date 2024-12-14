// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !WITH_VERSE_BPVM || defined(__INTELLISENSE__)

#include "HAL/Platform.h"
#include "Templates/EnableIf.h"
#include "VerseVM/Inline/VVMClassInline.h"
#include "VerseVM/Inline/VVMIntInline.h"
#include "VerseVM/Inline/VVMMapInline.h"
#include "VerseVM/Inline/VVMMutableArrayInline.h"
#include "VerseVM/Inline/VVMNativeStructInline.h"
#include "VerseVM/Inline/VVMValueInline.h"
#include "VerseVM/VVMArray.h"
#include "VerseVM/VVMCell.h"
#include "VerseVM/VVMContext.h"
#include "VerseVM/VVMFalse.h"
#include "VerseVM/VVMFloat.h"
#include "VerseVM/VVMInt.h"
#include "VerseVM/VVMLog.h"
#include "VerseVM/VVMMap.h"
#include "VerseVM/VVMMutableArray.h"
#include "VerseVM/VVMNativeString.h"
#include "VerseVM/VVMNativeTuple.h"
#include "VerseVM/VVMOpResult.h"
#include "VerseVM/VVMOption.h"
#include "VerseVM/VVMUnreachable.h"
#include "VerseVM/VVMValue.h"

enum class EVerseFalse : uint8;
enum class EVerseTrue : uint8;
struct FVerseValue;
struct FVerseFunction;
template <typename FunctionType>
struct TVerseFunction;
template <typename InterfaceProxyType>
struct TInterfaceInstance;

namespace Verse
{
struct VClass;

template <typename NativeType>
struct TIsNativeStruct
{
	static constexpr bool Value = false;
};

// Assigned to a native implementation return value - a default-emplaceable version of TOptional<NativeType>.
template <typename NativeType, typename = void>
struct TToVValue
{
	TOptional<NativeType> Value;

	void Emplace() { Value.Emplace(); }

	TToVValue& operator=(NativeType Other)
	{
		Value = MoveTemp(Other);
		return *this;
	}
	TToVValue& operator=(TOptional<NativeType> Other)
	{
		Value = MoveTemp(Other);
		return *this;
	}

	operator TOptional<NativeType>&() { return Value; }
	NativeType& operator*() { return *Value; }
};

// Marshal class objects through a raw pointer.
template <typename ObjectType>
struct TToVValue<TNonNullPtr<ObjectType>>
{
	TOptional<ObjectType*> Value;

	void Emplace() { Value.Emplace(); }

	TToVValue& operator=(TNonNullPtr<ObjectType> Other)
	{
		Value = Other.Get();
		return *this;
	}
	TToVValue& operator=(TOptional<TNonNullPtr<ObjectType>> Other)
	{
		if (Other.IsSet())
		{
			Value = Other.GetValue().Get();
		}
		else
		{
			Value.Reset();
		}
		return *this;
	}
};

// Same for TInterfaceInstance
template <typename InterfaceProxyType>
struct TToVValue<TInterfaceInstance<InterfaceProxyType>>;

// Out parameter for FromVValue - a default-constructible version of NativeType.
template <typename NativeType, typename = void>
struct TFromVValue
{
	NativeType Value;
	NativeType GetValue() { return MoveTemp(Value); }
};

template <typename NativeType>
struct TFromVValue<TNonNullPtr<NativeType>>
{
	NativeType* Value;
	TNonNullPtr<NativeType> GetValue() { return Value; }
};

// Same for TInterfaceInstance
template <typename InterfaceProxyType>
struct TFromVValue<TInterfaceInstance<InterfaceProxyType>>;

template <typename NativeType>
struct TFromVValue<NativeType, typename TEnableIf<TIsNativeStruct<NativeType>::Value>::Type>
{
	const NativeType* Value;
	const NativeType& GetValue() { return *Value; }
};

// This class defines canonical conversion functions between two runtime value representations:
// VValue-based representation and C++/native-based representation
struct FNativeConverter
{
	// 1) Conversions from C++/native to VValue representation

	static VValue ToVValue(FAllocationContext Context, EVerseFalse)
	{
		// There shouldn't be a value of EVerseFalse
		VERSE_UNREACHABLE();
	}

	static VValue ToVValue(FAllocationContext Context, EVerseTrue)
	{
		return GlobalFalse();
	}

	static VValue ToVValue(FAllocationContext Context, bool Logic)
	{
		return VValue::FromBool(Logic);
	}

	static VValue ToVValue(FAllocationContext Context, int64 Number)
	{
		return VValue(VInt(Context, Number));
	}

	static VValue ToVValue(FAllocationContext Context, double Number)
	{
		return VValue(VFloat(Number));
	}

	static VValue ToVValue(FAllocationContext Context, const FNativeString& String)
	{
		return VValue(VArray::New(Context, String));
	}

	static VValue ToVValue(FAllocationContext Context, UTF8CHAR Char)
	{
		return VValue::Char(Char);
	}

	static VValue ToVValue(FAllocationContext Context, UTF32CHAR Char32)
	{
		return VValue::Char32(Char32);
	}

	template <class ObjectType>
	static VValue ToVValue(FAllocationContext Context, ObjectType* Object)
	{
		// ObjectType must be a subclass of UObject, but it may be incomplete.
		UObject* Obj = reinterpret_cast<UObject*>(Object);
		check(Obj->IsValidLowLevel());
		return VValue(Obj);
	}
	template <class ObjectType>
	static VValue ToVValue(FAllocationContext Context, TNonNullPtr<ObjectType> Object)
	{
		return ToVValue(Context, Object.Get());
	}

	template <class InterfaceProxyType>
	static VValue ToVValue(FAllocationContext Context, TInterfaceInstance<InterfaceProxyType> Object);

	template <class StructType, typename = typename TEnableIf<TIsNativeStruct<typename TDecay<StructType>::Type>::Value>::Type>
	static VValue ToVValue(FAllocationContext Context, StructType&& Struct)
	{
		return StaticVClass<typename TDecay<StructType>::Type>().NewNativeStruct(Context, Forward<StructType>(Struct));
	}

	template <typename... ElementTypes>
	static VValue ToVValue(FAllocationContext Context, const TNativeTuple<ElementTypes...>& Tuple)
	{
		return ToVValue(Context, Tuple, std::make_index_sequence<sizeof...(ElementTypes)>());
	}

	template <typename... ElementTypes, size_t... Indices>
	static VValue ToVValue(FAllocationContext Context, const TNativeTuple<ElementTypes...>& Tuple, std::index_sequence<Indices...>)
	{
		return VArray::New(Context, {ToVValue(Context, Tuple.template Get<Indices>())...});
	}

	template <class ElementType>
	static VValue ToVValue(FAllocationContext Context, const TArray<ElementType>& Array)
	{
		VArray& NewArray = VArray::New(Context, Array.Num(), [Context, &Array](uint32 Index) {
			return ToVValue(Context, Array[Index]);
		});
		return NewArray;
	}

	template <class KeyType, class ValueType>
	static VValue ToVValue(FAllocationContext Context, const TMap<KeyType, ValueType>& Map)
	{
		TArray<TPair<VValue, VValue>> Pairs;
		Pairs.Reserve(Map.Num());
		for (auto Pair : Map)
		{
			Pairs.Push({ToVValue(Context, Pair.Key), ToVValue(Context, Pair.Value)});
		}

		return VMapBase::New<VMap>(Context, Pairs.Num(), [&](uint32 I) { return Pairs[I]; });
	}

	template <typename ValueType>
	static VValue ToVValue(FAllocationContext Context, const ::TOptional<ValueType>& Optional)
	{
		if (Optional.IsSet())
		{
			return VOption::New(Context, ToVValue(Context, Optional.GetValue()));
		}
		else
		{
			return GlobalFalse();
		}
	}

	static VValue ToVValue(FAllocationContext Context, const FVerseValue& Value);
	static VValue ToVValue(FAllocationContext Context, const FVerseFunction& Function);

	// 2) Conversions from VValue to C++/native representation

	static FOpResult FromVValue(FAllocationContext Context, const VValue Value, TFromVValue<EVerseTrue>& OutNative)
	{
		V_REQUIRE_CONCRETE(Value);
		V_DIE_UNLESS(Value == GlobalFalse());
		return {FOpResult::Return};
	}

	static FOpResult FromVValue(FAllocationContext Context, const VValue Value, TFromVValue<bool>& OutNative)
	{
		V_REQUIRE_CONCRETE(Value);
		V_DIE_UNLESS(Value.IsLogic());
		OutNative.Value = Value.AsBool();
		return {FOpResult::Return};
	}

	static FOpResult FromVValue(FAllocationContext Context, const VValue Value, TFromVValue<int64>& OutNative)
	{
		V_REQUIRE_CONCRETE(Value);
		V_DIE_UNLESS(Value.IsInt());
		VInt ValueHelper(Value);
		V_RUNTIME_ERROR_IF(!ValueHelper.IsInt64(), Context, "Value exceeds the range of a 64 bit integer.");
		OutNative.Value = ValueHelper.AsInt64();
		return {FOpResult::Return};
	}

	static FOpResult FromVValue(FAllocationContext Context, const VValue Value, TFromVValue<double>& OutNative)
	{
		V_REQUIRE_CONCRETE(Value);
		V_DIE_UNLESS(Value.IsFloat());
		OutNative.Value = Value.AsFloat().AsDouble();
		return {FOpResult::Return};
	}

	static FOpResult FromVValue(FAllocationContext Context, const VValue Value, TFromVValue<FNativeString>& OutNative)
	{
		V_REQUIRE_CONCRETE(Value);
		V_DIE_UNLESS(Value.IsCellOfType<VArrayBase>());
		// TODO: Error on invalid UTF-8 and interior nuls.
		OutNative.Value = Value.StaticCast<VArrayBase>().AsStringView();
		return {FOpResult::Return};
	}

	static FOpResult FromVValue(FAllocationContext Context, const VValue Value, TFromVValue<UTF8CHAR>& OutNative)
	{
		V_REQUIRE_CONCRETE(Value);
		V_DIE_UNLESS(Value.IsChar());
		OutNative.Value = Value.AsChar();
		return {FOpResult::Return};
	}

	static FOpResult FromVValue(FAllocationContext Context, const VValue Value, TFromVValue<UTF32CHAR>& OutNative)
	{
		V_REQUIRE_CONCRETE(Value);
		V_DIE_UNLESS(Value.IsChar32());
		OutNative.Value = Value.AsChar32();
		return {FOpResult::Return};
	}

	template <class ObjectType>
	static FOpResult FromVValue(FAllocationContext Context, const VValue Value, TFromVValue<TNonNullPtr<ObjectType>>& OutNative)
	{
		V_REQUIRE_CONCRETE(Value);
		V_DIE_UNLESS(Value.IsUObject());
		// ObjectType must be a subclass of UObject, but it may be incomplete.
		OutNative.Value = reinterpret_cast<ObjectType*>(Value.AsUObject());
		return {FOpResult::Return};
	}

	template <class InterfaceProxyType>
	static FOpResult FromVValue(FAllocationContext Context, const VValue Value, TFromVValue<TInterfaceInstance<InterfaceProxyType>>& OutNative);

	template <class StructType, typename = typename TEnableIf<TIsNativeStruct<StructType>::Value>::Type>
	static FOpResult FromVValue(FAllocationContext Context, const VValue Value, TFromVValue<StructType>& OutNative)
	{
		V_REQUIRE_CONCRETE(Value);
		OutNative.Value = &Value.StaticCast<VNativeStruct>().GetStruct<StructType>();
		return {FOpResult::Return};
	}

	template <typename... ElementTypes>
	static FOpResult FromVValue(FAllocationContext Context, const VValue Value, TFromVValue<TNativeTuple<ElementTypes...>>& OutNative)
	{
		return FromVValue(Context, Value, OutNative, std::make_index_sequence<sizeof...(ElementTypes)>());
	}

	template <typename... ElementTypes, size_t... Indices>
	static FOpResult FromVValue(FAllocationContext Context, const VValue Value, TFromVValue<TNativeTuple<ElementTypes...>>& OutNative, std::index_sequence<Indices...>)
	{
		V_REQUIRE_CONCRETE(Value);
		const VArrayBase& Array = Value.StaticCast<VArrayBase>();
		FOpResult Result{FOpResult::Return};
		auto ElementFromVValue = [&](const VValue ElementValue, auto& OutNativeElement) {
			// Only convert this element if the previous element successfully converted
			if (Result.Kind == FOpResult::Return)
			{
				TFromVValue<typename TRemoveReference<decltype(OutNativeElement)>::Type> NativeElement;
				Result = FromVValue(Context, ElementValue, NativeElement);
				if (Result.Kind == FOpResult::Return)
				{
					OutNativeElement = NativeElement.GetValue();
				}
			}
		};
		(ElementFromVValue(Array.GetValue(Indices), OutNative.Value.template Get<Indices>()), ...);
		return Result;
	}

	template <class ElementType>
	static FOpResult FromVValue(FAllocationContext Context, const VValue Value, TFromVValue<TArray<ElementType>>& OutNative)
	{
		V_REQUIRE_CONCRETE(Value);
		V_DIE_UNLESS(Value.IsCellOfType<VArrayBase>());
		const VArrayBase& Array = Value.StaticCast<VArrayBase>();
		ensure(OutNative.Value.IsEmpty());
		OutNative.Value.Reserve(Array.Num());
		for (const VValue Element : Array)
		{
			TFromVValue<ElementType> NativeElement;
			FOpResult ElementOpResult = FromVValue(Context, Element, NativeElement);
			if (ElementOpResult.Kind != FOpResult::Return)
			{
				return ElementOpResult;
			}
			OutNative.Value.Add(NativeElement.GetValue());
		}
		return {FOpResult::Return};
	}

	template <class KeyType, class ValueType>
	static FOpResult FromVValue(FAllocationContext Context, const VValue Value, TFromVValue<TMap<KeyType, ValueType>>& OutNative)
	{
		V_REQUIRE_CONCRETE(Value);
		V_DIE_UNLESS(Value.IsCellOfType<VMap>());
		const VMap& Map = Value.StaticCast<VMap>();
		ensure(OutNative.Value.IsEmpty());
		OutNative.Value.Reserve(Map.Num());
		for (const TPair<VValue, VValue> Pair : Map)
		{
			TFromVValue<KeyType> NativeKey;
			FOpResult KeyOpResult = FromVValue(Context, Pair.Key, NativeKey);
			if (KeyOpResult.Kind != FOpResult::Return)
			{
				return KeyOpResult;
			}
			TFromVValue<ValueType> NativeValue;
			FOpResult ValueOpResult = FromVValue(Context, Pair.Value, NativeValue);
			if (ValueOpResult.Kind != FOpResult::Return)
			{
				return ValueOpResult;
			}
			OutNative.Value.Add(NativeKey.GetValue(), NativeValue.GetValue());
		}
		return {FOpResult::Return};
	}

	template <typename ValueType>
	static FOpResult FromVValue(FAllocationContext Context, const VValue Value, TFromVValue<TOptional<ValueType>>& OutNative)
	{
		V_REQUIRE_CONCRETE(Value);
		ensure(!OutNative.Value.IsSet());
		if (VOption* Option = Value.DynamicCast<VOption>())
		{
			TFromVValue<ValueType> NativeValue;
			FOpResult ValueOpResult = FromVValue(Context, Option->GetValue(), NativeValue);
			if (ValueOpResult.Kind != FOpResult::Return)
			{
				return ValueOpResult;
			}
			OutNative.Value.Emplace(NativeValue.GetValue());
		}
		else
		{
			V_DIE_UNLESS(Value == GlobalFalse());
		}
		return {FOpResult::Return};
	}

	static FOpResult FromVValue(FAllocationContext Context, const VValue Value, TFromVValue<FVerseValue>& OutNative);

	template <typename ReturnType, typename... ParamTypes>
	static FOpResult FromVValue(FAllocationContext Context, const VValue Value, TFromVValue<TVerseFunction<ReturnType(ParamTypes...)>>& OutNative);
};

} // namespace Verse

#endif // !WITH_VERSE_BPVM || defined(__INTELLISENSE__)
