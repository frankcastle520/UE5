// Copyright Epic Games, Inc. All Rights Reserved.

#if !WITH_VERSE_BPVM || defined(__INTELLISENSE__)
#include "VerseVM/VVMNativeRef.h"
#include "UObject/EnumProperty.h"
#include "UObject/PropertyOptional.h"
#include "UObject/VerseStringProperty.h"
#include "UObject/VerseValueProperty.h"
#include "VerseVM/Inline/VVMCellInline.h"
#include "VerseVM/Inline/VVMVarInline.h"
#include "VerseVM/VVMNativeConverter.h"
#include "VerseVM/VVMVerseEnum.h"
#include "VerseVM/VVMVerseStruct.h"

namespace Verse
{

DEFINE_DERIVED_VCPPCLASSINFO(VNativeRef);
TGlobalTrivialEmergentTypePtr<&VNativeRef::StaticCppClassInfo> VNativeRef::GlobalTrivialEmergentType;

VValue VNativeRef::Get(FAllocationContext Context)
{
	if (UObject* Object = Base.Get().ExtractUObject())
	{
		V_DIE_UNLESS(Type == EType::FProperty);
		return Get(Context, Object, UProperty);
	}
	else if (VNativeStruct* Struct = Base.Get().DynamicCast<VNativeStruct>())
	{
		V_DIE_UNLESS(Type == EType::FProperty);
		return Get(Context, Struct->GetStruct(), UProperty);
	}
	else
	{
		VERSE_UNREACHABLE();
		return VValue();
	}
}

VValue VNativeRef::Get(FAllocationContext Context, void* Container, FProperty* Property)
{
	if (FEnumProperty* TrueProperty = CastField<FEnumProperty>(Property); TrueProperty && TrueProperty->GetEnum() == StaticEnum<EVerseTrue>())
	{
		EVerseTrue* NativeValue = TrueProperty->ContainerPtrToValuePtr<EVerseTrue>(Container);
		return FNativeConverter::ToVValue(Context, *NativeValue);
	}
	else if (FBoolProperty* LogicProperty = CastField<FBoolProperty>(Property))
	{
		bool* NativeValue = LogicProperty->ContainerPtrToValuePtr<bool>(Container);
		return FNativeConverter::ToVValue(Context, *NativeValue);
	}
	else if (FInt64Property* IntProperty = CastField<FInt64Property>(Property))
	{
		int64* NativeValue = IntProperty->ContainerPtrToValuePtr<int64>(Container);
		return FNativeConverter::ToVValue(Context, *NativeValue);
	}
	else if (FDoubleProperty* FloatProperty = CastField<FDoubleProperty>(Property))
	{
		double* NativeValue = FloatProperty->ContainerPtrToValuePtr<double>(Container);
		return FNativeConverter::ToVValue(Context, *NativeValue);
	}
	else if (FByteProperty* CharProperty = CastField<FByteProperty>(Property))
	{
		UTF8CHAR* NativeValue = CharProperty->ContainerPtrToValuePtr<UTF8CHAR>(Container);
		return FNativeConverter::ToVValue(Context, *NativeValue);
	}
	else if (FIntProperty* Char32Property = CastField<FIntProperty>(Property))
	{
		UTF32CHAR* NativeValue = Char32Property->ContainerPtrToValuePtr<UTF32CHAR>(Container);
		return FNativeConverter::ToVValue(Context, *NativeValue);
	}
	else if (FObjectProperty* ClassProperty = CastField<FObjectProperty>(Property))
	{
		TObjectPtr<UObject>* NativeValue = ClassProperty->ContainerPtrToValuePtr<TObjectPtr<UObject>>(Container);
		return FNativeConverter::ToVValue(Context, NativeValue->Get());
	}
	else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		void* NativeValue = StructProperty->ContainerPtrToValuePtr<void>(Container);
		UVerseStruct* UeStruct = CastChecked<UVerseStruct>(StructProperty->Struct);
		if (UeStruct->EmergentType)
		{ // It's a native struct
			VNativeStruct& Struct = VNativeStruct::NewUninitialized(Context, *UeStruct->EmergentType);
			StructProperty->CopyCompleteValue(Struct.GetStruct(), NativeValue);
			return Struct;
		}
		else
		{ // It's a tuple
			uint32 NumElements = 0;
			for (TFieldIterator<FProperty> Counter(UeStruct); Counter; ++Counter)
			{
				++NumElements;
			}
			TFieldIterator<FProperty> Iterator(UeStruct);
			// We assume here that the element initializer gets invoked in ascending index order
			return VArray::New(Context, NumElements, [Context, NativeValue, &Iterator](uint32 Index) {
				VValue ElementValue = VNativeRef::Get(Context, NativeValue, *Iterator);
				++Iterator;
				return ElementValue;
			});
		}
	}
	else if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper_InContainer NativeValue(ArrayProperty, Container);
		return VArray::New(Context, NativeValue.Num(), [Context, ArrayProperty, &NativeValue](uint32 Index) {
			return VNativeRef::Get(Context, NativeValue.GetElementPtr(Index), ArrayProperty->Inner);
		});
	}
	else if (FVerseStringProperty* StringProperty = CastField<FVerseStringProperty>(Property))
	{
		FNativeString* NativeValue = StringProperty->ContainerPtrToValuePtr<FNativeString>(Container);
		return FNativeConverter::ToVValue(Context, *NativeValue);
	}
	else if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		FScriptMapHelper_InContainer NativeValue(MapProperty, Container);

		TArray<TPair<VValue, VValue>> Pairs;
		Pairs.Reserve(NativeValue.Num());
		for (auto Pair = NativeValue.CreateIterator(); Pair; ++Pair)
		{
			void* Data = NativeValue.GetPairPtr(Pair);
			VValue Key = VNativeRef::Get(Context, Data, MapProperty->KeyProp);
			VValue Value = VNativeRef::Get(Context, Data, MapProperty->ValueProp);
			Pairs.Push({Key, Value});
		}

		return VMapBase::New<VMap>(Context, Pairs.Num(), [&Pairs](uint32 I) { return Pairs[I]; });
	}
	else if (FOptionalProperty* OptionProperty = CastField<FOptionalProperty>(Property))
	{
		void* NativeValue = OptionProperty->ContainerPtrToValuePtr<void>(Container);
		if (OptionProperty->IsSet(NativeValue))
		{
			return VOption::New(Context, VNativeRef::Get(Context, NativeValue, OptionProperty->GetValueProperty()));
		}
		else
		{
			return GlobalFalse();
		}
	}
	else
	{
		VERSE_UNREACHABLE();
		return VValue();
	}
}

FOpResult VNativeRef::Set(FAllocationContext Context, VValue Value)
{
	if (UObject* Object = Base.Get().ExtractUObject())
	{
		V_DIE_UNLESS(Type == EType::FProperty);
		return Set<true>(Context, Object, Object, UProperty, Value);
	}
	else if (VNativeStruct* Struct = Base.Get().DynamicCast<VNativeStruct>())
	{
		V_DIE_UNLESS(Type == EType::FProperty);
		return Set<true>(Context, Struct, Struct->GetStruct(), UProperty, Value);
	}
	else
	{
		VERSE_UNREACHABLE();
	}
}

FOpResult VNativeRef::SetNonTransactionally(FAllocationContext Context, VValue Value)
{
	if (UObject* Object = Base.Get().ExtractUObject())
	{
		V_DIE_UNLESS(Type == EType::FProperty);
		return Set<false>(Context, nullptr, Object, UProperty, Value);
	}
	else if (VNativeStruct* Struct = Base.Get().DynamicCast<VNativeStruct>())
	{
		V_DIE_UNLESS(Type == EType::FProperty);
		return Set<false>(Context, nullptr, Struct->GetStruct(), UProperty, Value);
	}
	else
	{
		VERSE_UNREACHABLE();
	}
}

template FOpResult VNativeRef::Set<true>(FAllocationContext Context, UObject* Base, void* Container, FProperty* Property, VValue Value);
template FOpResult VNativeRef::Set<true>(FAllocationContext Context, VNativeStruct* Base, void* Container, FProperty* Property, VValue Value);
template FOpResult VNativeRef::Set<false>(FAllocationContext Context, std::nullptr_t Base, void* Container, FProperty* Property, VValue Value);

#define OP_RESULT_HELPER(Result)          \
	if (Result.Kind != FOpResult::Return) \
	{                                     \
		return Result;                    \
	}

namespace
{
template <bool bTransactional, typename BaseType, typename FunctionType>
FOpResult WriteImpl(FAllocationContext Context, BaseType Root, FunctionType F)
{
	if constexpr (bTransactional)
	{
		if constexpr (!std::is_same_v<BaseType, std::nullptr_t>)
		{
			Context.CurrentTransaction()->AddRoot(Context, Root);
		}

		AutoRTFM::EContextStatus Status = AutoRTFM::Close(F);
		V_RUNTIME_ERROR_IF(Status != AutoRTFM::EContextStatus::OnTrack, Context, "Closed write to native field did not yield AutoRTFM::EContextStatus::OnTrack");
	}
	else
	{
		F();
	}

	return {FOpResult::Return};
}

template <bool bTransactional, typename BaseType, typename ValueType, typename PropertyType>
FOpResult SetImpl(FAllocationContext Context, BaseType Base, void* Container, PropertyType* Property, VValue Value)
{
	TFromVValue<ValueType> NativeValue;
	FOpResult Result = FNativeConverter::FromVValue(Context, Value, NativeValue);
	OP_RESULT_HELPER(Result);

	return WriteImpl<bTransactional>(Context, Base, [Property, Container, &NativeValue] {
		ValueType* ValuePtr = Property->template ContainerPtrToValuePtr<ValueType>(Container);
		*ValuePtr = NativeValue.GetValue();
	});
}
} // namespace

template <bool bTransactional, typename BaseType>
FOpResult VNativeRef::Set(FAllocationContext Context, BaseType Base, void* Container, FProperty* Property, VValue Value)
{
	if (FEnumProperty* TrueProperty = CastField<FEnumProperty>(Property); TrueProperty && TrueProperty->GetEnum() == StaticEnum<EVerseTrue>())
	{
		return SetImpl<bTransactional, BaseType, EVerseTrue>(Context, Base, Container, TrueProperty, Value);
	}
	else if (FBoolProperty* LogicProperty = CastField<FBoolProperty>(Property))
	{
		return SetImpl<bTransactional, BaseType, bool>(Context, Base, Container, LogicProperty, Value);
	}
	if (FInt64Property* IntProperty = CastField<FInt64Property>(Property))
	{
		return SetImpl<bTransactional, BaseType, int64>(Context, Base, Container, IntProperty, Value);
	}
	else if (FDoubleProperty* FloatProperty = CastField<FDoubleProperty>(Property))
	{
		return SetImpl<bTransactional, BaseType, double>(Context, Base, Container, FloatProperty, Value);
	}
	else if (FByteProperty* CharProperty = CastField<FByteProperty>(Property))
	{
		return SetImpl<bTransactional, BaseType, UTF8CHAR>(Context, Base, Container, CharProperty, Value);
	}
	else if (FIntProperty* Char32Property = CastField<FIntProperty>(Property))
	{
		return SetImpl<bTransactional, BaseType, UTF32CHAR>(Context, Base, Container, Char32Property, Value);
	}
	else if (FObjectProperty* ClassProperty = CastField<FObjectProperty>(Property))
	{
		return SetImpl<bTransactional, BaseType, TNonNullPtr<UObject>>(Context, Base, Container, ClassProperty, Value);
	}
	else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		V_REQUIRE_CONCRETE(Value);

		UVerseStruct* UeStruct = CastChecked<UVerseStruct>(StructProperty->Struct);
		if (UeStruct->EmergentType)
		{ // It's a native struct
			V_DIE_UNLESS(Value.IsCellOfType<VNativeStruct>());
			VNativeStruct& Struct = Value.StaticCast<VNativeStruct>();
			checkSlow(VNativeStruct::GetUScriptStruct(*Struct.GetEmergentType()) == UeStruct);

			return WriteImpl<bTransactional>(Context, Base, [StructProperty, Container, &Struct] {
				void* ValuePtr = StructProperty->ContainerPtrToValuePtr<void>(Container);
				StructProperty->CopyCompleteValue(ValuePtr, Struct.GetStruct());
			});
		}
		else
		{ // It's a tuple
			V_DIE_UNLESS(Value.IsCellOfType<VArrayBase>());
			VArrayBase& Array = Value.StaticCast<VArrayBase>();
			// Unpack to temporary storage first
			TStringBuilderWithBuffer<char, 64> TempStorage;
			TempStorage.AddUninitialized(UeStruct->GetStructureSize()); // Uses heap memory if inline storage is exceeded
			FOpResult Result = WriteImpl<bTransactional>(Context, nullptr, [&] {
				StructProperty->InitializeValue(TempStorage.GetData());
			});
			OP_RESULT_HELPER(Result);
			TFieldIterator<FProperty> Iterator(StructProperty->Struct);
			for (int32 Index = 0; Index < Array.Num(); ++Index, ++Iterator)
			{
				FOpResult ElemResult = VNativeRef::Set<false>(Context, nullptr, TempStorage.GetData(), *Iterator, Array.GetValue(Index));
				OP_RESULT_HELPER(ElemResult);
			}
			// Upon success, copy temporary storage to final destination
			return WriteImpl<bTransactional>(Context, Base, [StructProperty, Container, &TempStorage] {
				void* ValuePtr = StructProperty->ContainerPtrToValuePtr<void>(Container);
				StructProperty->CopyCompleteValue(ValuePtr, TempStorage.GetData());
				StructProperty->DestroyValue(TempStorage.GetData());
			});
		}
	}
	else if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		V_REQUIRE_CONCRETE(Value);
		V_DIE_UNLESS(Value.IsCellOfType<VArrayBase>());
		VArrayBase& Array = Value.StaticCast<VArrayBase>();

		FScriptArray NativeValue;
		FScriptArrayHelper Helper(ArrayProperty, &NativeValue);
		FOpResult Result = WriteImpl<bTransactional>(Context, nullptr, [&] { Helper.EmptyAndAddValues(Array.Num()); });
		OP_RESULT_HELPER(Result);
		for (int32 Index = 0; Index < Array.Num(); Index++)
		{
			FOpResult ElemResult = VNativeRef::Set<false>(Context, nullptr, Helper.GetElementPtr(Index), ArrayProperty->Inner, Array.GetValue(Index));
			OP_RESULT_HELPER(ElemResult);
		}

		return WriteImpl<bTransactional>(Context, Base, [ArrayProperty, Container, &NativeValue] {
			FScriptArrayHelper_InContainer ValuePtr(ArrayProperty, Container);
			ValuePtr.MoveAssign(&NativeValue);
		});
	}
	else if (FVerseStringProperty* StringProperty = CastField<FVerseStringProperty>(Property))
	{
		return SetImpl<bTransactional, BaseType, FNativeString>(Context, Base, Container, StringProperty, Value);
	}
	else if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		V_REQUIRE_CONCRETE(Value);
		V_DIE_UNLESS(Value.IsCellOfType<VMapBase>());
		VMapBase& Map = Value.StaticCast<VMapBase>();

		FScriptMap NativeValue;
		FScriptMapHelper Helper(MapProperty, &NativeValue);
		FOpResult Result = WriteImpl<bTransactional>(Context, nullptr, [&] { Helper.EmptyValues(Map.Num()); });
		OP_RESULT_HELPER(Result);
		for (TPair<VValue, VValue> Pair : Map)
		{
			int32 Index = Helper.AddDefaultValue_Invalid_NeedsRehash();
			FOpResult KeyResult = VNativeRef::Set<false>(Context, nullptr, Helper.GetPairPtr(Index), Helper.GetKeyProperty(), Pair.Key);
			OP_RESULT_HELPER(KeyResult);
			FOpResult ValueResult = VNativeRef::Set<false>(Context, nullptr, Helper.GetPairPtr(Index), Helper.GetValueProperty(), Pair.Value);
			OP_RESULT_HELPER(ValueResult);
		}
		Helper.Rehash();

		return WriteImpl<bTransactional>(Context, Base, [MapProperty, Container, &NativeValue] {
			FScriptMapHelper_InContainer ValuePtr(MapProperty, Container);
			ValuePtr.MoveAssign(&NativeValue);
		});
	}
	else if (FOptionalProperty* OptionProperty = CastField<FOptionalProperty>(Property))
	{
		V_REQUIRE_CONCRETE(Value);

		if (VOption* Option = Value.DynamicCast<VOption>())
		{
			void* Data;
			FOpResult Result = WriteImpl<bTransactional>(Context, Base, [OptionProperty, Container, Value, &Data] {
				void* ValuePtr = OptionProperty->ContainerPtrToValuePtr<void>(Container);
				Data = OptionProperty->MarkSetAndGetInitializedValuePointerToReplace(ValuePtr);
			});
			OP_RESULT_HELPER(Result);

			return VNativeRef::Set<bTransactional>(Context, Base, Data, OptionProperty->GetValueProperty(), Option->GetValue());
		}
		else
		{
			V_DIE_UNLESS(Value == GlobalFalse());

			return WriteImpl<bTransactional>(Context, Base, [OptionProperty, Container] {
				void* ValuePtr = OptionProperty->ContainerPtrToValuePtr<void>(Container);
				OptionProperty->MarkUnset(ValuePtr);
			});
		}
	}
	else
	{
		VERSE_UNREACHABLE();
	}
}

#undef OP_RESULT_HELPER

VValue VNativeRef::FreezeImpl(FAllocationContext Context)
{
	return Get(Context);
}

template <typename TVisitor>
void VNativeRef::VisitReferencesImpl(TVisitor& Visitor)
{
	Visitor.Visit(Base, TEXT("Base"));
}

} // namespace Verse
#endif
