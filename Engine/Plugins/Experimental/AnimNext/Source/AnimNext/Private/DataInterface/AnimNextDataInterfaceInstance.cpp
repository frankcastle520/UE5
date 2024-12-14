// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataInterface/AnimNextDataInterfaceInstance.h"
#include "Logging/StructuredLog.h"
#include "Param/ParamUtils.h"

namespace UE::AnimNext::Private
{
	// Utility functions for type conversion, adapted from UE::StructUtils::Private in PropertyBag.cpp

	bool CanCastTo(const UStruct* From, const UStruct* To)
	{
		return From != nullptr && To != nullptr && From->IsChildOf(To);
	}

	EPropertyBagResult GetPropertyAsInt64(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, int64& OutValue)
	{
		check(Address != nullptr);
		check(InProperty != nullptr);

		switch(InValueType)
		{
		case EPropertyBagPropertyType::Bool:
			{
				const FBoolProperty* Property = CastFieldChecked<FBoolProperty>(InProperty);
				OutValue = Property->GetPropertyValue(Address) ? 1 : 0;
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Byte:
			{
				const FByteProperty* Property = CastFieldChecked<FByteProperty>(InProperty);
				OutValue = Property->GetPropertyValue(Address);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Int32:
			{
				const FIntProperty* Property = CastFieldChecked<FIntProperty>(InProperty);
				OutValue = Property->GetPropertyValue(Address);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::UInt32:
			{
				const FUInt32Property* Property = CastFieldChecked<FUInt32Property>(InProperty);
				OutValue = static_cast<uint32>(Property->GetPropertyValue(Address));
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Int64:
			{
				const FInt64Property* Property = CastFieldChecked<FInt64Property>(InProperty);
				OutValue = Property->GetPropertyValue(Address);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::UInt64:
			{
				const FUInt64Property* Property = CastFieldChecked<FUInt64Property>(InProperty);
				OutValue = static_cast<int64>(Property->GetPropertyValue(Address));
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Float:
			{
				const FFloatProperty* Property = CastFieldChecked<FFloatProperty>(InProperty);
				OutValue = static_cast<int64>(Property->GetPropertyValue(Address));
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Double:
			{
				const FDoubleProperty* Property = CastFieldChecked<FDoubleProperty>(InProperty);
				OutValue = static_cast<int64>(Property->GetPropertyValue(Address));
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Enum:
			{
				const FEnumProperty* EnumProperty = CastFieldChecked<FEnumProperty>(InProperty);
				const FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty();
				check(UnderlyingProperty);
				OutValue = UnderlyingProperty->GetSignedIntPropertyValue(Address);
				return EPropertyBagResult::Success;
			}
		default:
			return EPropertyBagResult::TypeMismatch;
		}
	}

	EPropertyBagResult GetPropertyAsUInt64(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, uint64& OutValue)
	{
		check(Address != nullptr);
		check(InProperty != nullptr);

		switch (InValueType)
		{
		case EPropertyBagPropertyType::Bool:
		{
			const FBoolProperty* Property = CastFieldChecked<FBoolProperty>(InProperty);
			OutValue = Property->GetPropertyValue(Address) ? 1 : 0;
			return EPropertyBagResult::Success;
		}
		case EPropertyBagPropertyType::Byte:
		{
			const FByteProperty* Property = CastFieldChecked<FByteProperty>(InProperty);
			OutValue = Property->GetPropertyValue(Address);
			return EPropertyBagResult::Success;
		}
		case EPropertyBagPropertyType::Int32:
		{
			const FIntProperty* Property = CastFieldChecked<FIntProperty>(InProperty);
			OutValue = static_cast<uint32>(Property->GetPropertyValue(Address));
			return EPropertyBagResult::Success;
		}
		case EPropertyBagPropertyType::UInt32:
		{
			const FUInt32Property* Property = CastFieldChecked<FUInt32Property>(InProperty);
			OutValue = Property->GetPropertyValue(Address);
			return EPropertyBagResult::Success;
		}
		case EPropertyBagPropertyType::Int64:
		{
			const FInt64Property* Property = CastFieldChecked<FInt64Property>(InProperty);
			OutValue = static_cast<uint64>(Property->GetPropertyValue(Address));
			return EPropertyBagResult::Success;
		}
		case EPropertyBagPropertyType::UInt64:
		{
			const FUInt64Property* Property = CastFieldChecked<FUInt64Property>(InProperty);
			OutValue = Property->GetPropertyValue(Address);
			return EPropertyBagResult::Success;
		}
		case EPropertyBagPropertyType::Float:
		{
			const FFloatProperty* Property = CastFieldChecked<FFloatProperty>(InProperty);
			OutValue = static_cast<uint64>(Property->GetPropertyValue(Address));
			return EPropertyBagResult::Success;
		}
		case EPropertyBagPropertyType::Double:
		{
			const FDoubleProperty* Property = CastFieldChecked<FDoubleProperty>(InProperty);
			OutValue = static_cast<uint64>(Property->GetPropertyValue(Address));
			return EPropertyBagResult::Success;
		}
		case EPropertyBagPropertyType::Enum:
		{
			const FEnumProperty* EnumProperty = CastFieldChecked<FEnumProperty>(InProperty);
			const FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty();
			check(UnderlyingProperty);
			OutValue = UnderlyingProperty->GetUnsignedIntPropertyValue(Address);
			return EPropertyBagResult::Success;
		}
		default:
			return EPropertyBagResult::TypeMismatch;
		}
	}

	EPropertyBagResult GetPropertyAsDouble(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, double& OutValue)
	{
		check(Address != nullptr);
		check(InProperty != nullptr);

		switch(InValueType)
		{
		case EPropertyBagPropertyType::Bool:
			{
				const FBoolProperty* Property = CastFieldChecked<FBoolProperty>(InProperty);
				OutValue = Property->GetPropertyValue(Address) ? 1.0 : 0.0;
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Byte:
			{
				const FByteProperty* Property = CastFieldChecked<FByteProperty>(InProperty);
				OutValue = Property->GetPropertyValue(Address);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Int32:
			{
				const FIntProperty* Property = CastFieldChecked<FIntProperty>(InProperty);
				OutValue = Property->GetPropertyValue(Address);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::UInt32:
			{
				const FUInt32Property* Property = CastFieldChecked<FUInt32Property>(InProperty);
				OutValue = Property->GetPropertyValue(Address);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Int64:
			{
				const FInt64Property* Property = CastFieldChecked<FInt64Property>(InProperty);
				OutValue = static_cast<double>(Property->GetPropertyValue(Address));
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::UInt64:
			{
				const FUInt64Property* Property = CastFieldChecked<FUInt64Property>(InProperty);
				OutValue = static_cast<double>(Property->GetPropertyValue(Address));
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Float:
			{
				const FFloatProperty* Property = CastFieldChecked<FFloatProperty>(InProperty);
				OutValue = Property->GetPropertyValue(Address);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Double:
			{
				const FDoubleProperty* Property = CastFieldChecked<FDoubleProperty>(InProperty);
				OutValue = Property->GetPropertyValue(Address);
				return EPropertyBagResult::Success;
			}
		case EPropertyBagPropertyType::Enum:
			{
				const FEnumProperty* EnumProperty = CastFieldChecked<FEnumProperty>(InProperty);
				const FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty();
				check(UnderlyingProperty);
				OutValue = static_cast<double>(UnderlyingProperty->GetSignedIntPropertyValue(Address));
				return EPropertyBagResult::Success;
			}
		default:
			return EPropertyBagResult::TypeMismatch;
		}
	}

	// Generic property getter. Used for FName, FString, FText. 
	template<typename T, typename PropT>
	EPropertyBagResult GetPropertyValue(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, T& OutValue)
	{
		check(Address != nullptr);
		check(InProperty != nullptr);

		if (!InProperty->IsA<PropT>())
		{
			return EPropertyBagResult::TypeMismatch;
		}
		
		const PropT* Property = CastFieldChecked<PropT>(InProperty);
		OutValue = Property->GetPropertyValue(Address);

		return EPropertyBagResult::Success;
	}

	EPropertyBagResult GetPropertyValueAsEnum(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, const UEnum* RequestedEnum, uint8& OutValue)
	{
		check(Address != nullptr);
		check(InProperty != nullptr);

		if (InValueType != EPropertyBagPropertyType::Enum)
		{
			return EPropertyBagResult::TypeMismatch;
		}

		const FEnumProperty* EnumProperty = CastFieldChecked<FEnumProperty>(InProperty);
		const FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty();
		check(UnderlyingProperty);
	
		if (RequestedEnum != EnumProperty->GetEnum())
		{
			return EPropertyBagResult::TypeMismatch;
		}
	
		OutValue = static_cast<uint8>(UnderlyingProperty->GetUnsignedIntPropertyValue(Address));

		return EPropertyBagResult::Success;
	}

	EPropertyBagResult GetPropertyValueAsStruct(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, const UScriptStruct* RequestedStruct, uint8* OutValue)
	{
		check(Address != nullptr);
		check(InProperty != nullptr);

		if (InValueType != EPropertyBagPropertyType::Struct)
		{
			return EPropertyBagResult::TypeMismatch;
		}

		const FStructProperty* StructProperty = CastFieldChecked<FStructProperty>(InProperty);
		check(StructProperty->Struct);

		if (RequestedStruct != nullptr && CanCastTo(StructProperty->Struct, RequestedStruct) == false)
		{
			return EPropertyBagResult::TypeMismatch;
		}

		StructProperty->CopyCompleteValue(OutValue, Address);

		return EPropertyBagResult::Success;
	}

	EPropertyBagResult GetPropertyValueAsObject(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, const UClass* RequestedClass,  UObject*& OutValue)
	{
		check(Address != nullptr);
		check(InProperty != nullptr);

		if (InValueType != EPropertyBagPropertyType::Object
			&& InValueType != EPropertyBagPropertyType::SoftObject
			&& InValueType != EPropertyBagPropertyType::Class
			&& InValueType != EPropertyBagPropertyType::SoftClass)
		{
			return EPropertyBagResult::TypeMismatch;
		}

		const FObjectPropertyBase* ObjectProperty = CastFieldChecked<FObjectPropertyBase>(InProperty);
		check(ObjectProperty->PropertyClass);

		if (RequestedClass != nullptr && CanCastTo(ObjectProperty->PropertyClass, RequestedClass) == false)
		{
			return EPropertyBagResult::TypeMismatch;
		}
	
		OutValue = ObjectProperty->GetObjectPropertyValue(Address);

		return EPropertyBagResult::Success;
	}

	EPropertyBagResult GetPropertyValueAsSoftPath(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, FSoftObjectPath& OutValue)
	{
		check(Address != nullptr);
		check(InProperty != nullptr);

		if (InValueType != EPropertyBagPropertyType::SoftObject
			&& InValueType != EPropertyBagPropertyType::SoftClass)
		{
			return EPropertyBagResult::TypeMismatch;
		}

		const FSoftObjectProperty* SoftObjectProperty = CastFieldChecked<FSoftObjectProperty>(InProperty);
		check(SoftObjectProperty->PropertyClass);

		OutValue = SoftObjectProperty->GetPropertyValue(Address).ToSoftObjectPath();

		return EPropertyBagResult::Success;
	}

	EPropertyBagResult GetValueBool(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, bool& OutValue)
	{
		int64 ReturnValue = 0;
		const EPropertyBagResult Result = GetPropertyAsInt64(InProperty, InValueType, Address, ReturnValue);
		if (Result == EPropertyBagResult::Success)
		{
			OutValue = ReturnValue != 0;
		}
		return Result;
	}

	EPropertyBagResult GetValueByte(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, uint8& OutValue)
	{
		uint64 ReturnValue = 0;
		const EPropertyBagResult Result = GetPropertyAsUInt64(InProperty, InValueType, Address, ReturnValue);
		if (Result == EPropertyBagResult::Success)
		{
			OutValue = static_cast<uint8>(ReturnValue);
		}
		return Result;
	}

	EPropertyBagResult GetValueInt32(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, int32& OutValue)
	{
		int64 ReturnValue = 0;
		const EPropertyBagResult Result = GetPropertyAsInt64(InProperty, InValueType, Address, ReturnValue);
		if (Result == EPropertyBagResult::Success)
		{
			OutValue = static_cast<int32>(ReturnValue);
		}
		return Result;
	}

	EPropertyBagResult GetValueUInt32(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, uint32& OutValue)
	{
		uint64 ReturnValue = 0;
		const EPropertyBagResult Result = GetPropertyAsUInt64(InProperty, InValueType, Address, ReturnValue);
		if (Result == EPropertyBagResult::Success)
		{
			OutValue = static_cast<uint32>(ReturnValue);
		}
		return Result;
	}

	EPropertyBagResult GetValueInt64(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, int64& OutValue)
	{
		return GetPropertyAsInt64(InProperty, InValueType, Address, OutValue);
	}

	EPropertyBagResult GetValueUInt64(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, uint64& OutValue)
	{
		return GetPropertyAsUInt64(InProperty, InValueType, Address, OutValue);
	}

	EPropertyBagResult GetValueFloat(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, float& OutValue)
	{
		double ReturnValue = 0;
		const EPropertyBagResult Result = GetPropertyAsDouble(InProperty, InValueType, Address, ReturnValue);
		if (Result == EPropertyBagResult::Success)
		{
			OutValue = static_cast<float>(ReturnValue);
		}
		return Result;
	}

	EPropertyBagResult GetValueDouble(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, double& OutValue)
	{
		return GetPropertyAsDouble(InProperty, InValueType, Address, OutValue);
	}

	EPropertyBagResult GetValueName(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, FName& OutValue)
	{
		return GetPropertyValue<FName, FNameProperty>(InProperty, InValueType, Address, OutValue);
	}

	EPropertyBagResult GetValueString(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, FString& OutValue)
	{
		return GetPropertyValue<FString, FStrProperty>(InProperty, InValueType, Address, OutValue);
	}

	EPropertyBagResult GetValueText(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, FText& OutValue)
	{
		return GetPropertyValue<FText, FTextProperty>(InProperty, InValueType, Address, OutValue);
	}

	EPropertyBagResult GetValueEnum(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, const UEnum* RequestedEnum, uint8& OutValue)
	{
		return GetPropertyValueAsEnum(InProperty, InValueType, Address, RequestedEnum, OutValue);
	}

	EPropertyBagResult GetValueStruct(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, const UScriptStruct* RequestedStruct, uint8* OutValue)
	{
		return GetPropertyValueAsStruct(InProperty, InValueType, Address, RequestedStruct, OutValue);
	}

	EPropertyBagResult GetValueObject(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, const UClass* RequestedClass, UObject*& OutValue)
	{
		return GetPropertyValueAsObject(InProperty, InValueType, Address, RequestedClass, OutValue);
	}

	EPropertyBagResult GetValueClass(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, const UClass* RequestedClass, UClass*& OutValue)
	{
		UObject* ReturnValue = nullptr;
		const EPropertyBagResult Result = GetPropertyValueAsObject(InProperty, InValueType, Address, nullptr, ReturnValue);
		if (Result != EPropertyBagResult::Success)
		{
			return Result;
		}
		UClass* Class = Cast<UClass>(ReturnValue);
		if (Class == nullptr && ReturnValue != nullptr)
		{
			return EPropertyBagResult::TypeMismatch;
		}
		if(CanCastTo(Class, RequestedClass) == false)
		{
			return EPropertyBagResult::TypeMismatch;
		}
		OutValue = Class;
		return Result;
	}

	EPropertyBagResult GetValueSoftPath(const FProperty* InProperty, EPropertyBagPropertyType InValueType, const void* Address, FSoftObjectPath& OutValue)
	{
		return GetPropertyValueAsSoftPath(InProperty, InValueType, Address, OutValue);
	}

	EPropertyBagResult GetVariableFromMismatchedValueType(const FProperty* InProperty, const FAnimNextParamType& InSrcType, const FAnimNextParamType& InDestType, const uint8* InAddress, uint8* OutResult)
	{
		check(InSrcType != InDestType);	// Function assumes that types are mismatched

		switch(InDestType.GetValueType())
		{
		case EPropertyBagPropertyType::Bool:
			return GetValueBool(InProperty, InSrcType.GetValueType(), InAddress, *reinterpret_cast<bool*>(OutResult));
		case EPropertyBagPropertyType::Byte:
			return GetValueByte(InProperty, InSrcType.GetValueType(), InAddress, *OutResult);
		case EPropertyBagPropertyType::Int32:
			return GetValueInt32(InProperty, InSrcType.GetValueType(), InAddress, *reinterpret_cast<int32*>(OutResult));
		case EPropertyBagPropertyType::Int64:
			return GetValueInt64(InProperty, InSrcType.GetValueType(), InAddress, *reinterpret_cast<int64*>(OutResult));
		case EPropertyBagPropertyType::Float:
			return GetValueFloat(InProperty, InSrcType.GetValueType(), InAddress, *reinterpret_cast<float*>(OutResult));
		case EPropertyBagPropertyType::Double:
			return GetValueDouble(InProperty, InSrcType.GetValueType(), InAddress, *reinterpret_cast<double*>(OutResult));
		case EPropertyBagPropertyType::Name:
			return GetValueName(InProperty, InSrcType.GetValueType(), InAddress, *reinterpret_cast<FName*>(OutResult));
		case EPropertyBagPropertyType::String:
			return GetValueString(InProperty, InSrcType.GetValueType(), InAddress, *reinterpret_cast<FString*>(OutResult));
		case EPropertyBagPropertyType::Text:
			return GetValueText(InProperty, InSrcType.GetValueType(), InAddress, *reinterpret_cast<FText*>(OutResult));
		case EPropertyBagPropertyType::Enum:
			return GetValueEnum(InProperty, InSrcType.GetValueType(), InAddress, CastChecked<UEnum>(InDestType.GetValueTypeObject()), *OutResult);
		case EPropertyBagPropertyType::Struct:
			return GetValueStruct(InProperty, InSrcType.GetValueType(), InAddress, CastChecked<UScriptStruct>(InDestType.GetValueTypeObject()), OutResult);
		case EPropertyBagPropertyType::Object:
			return GetValueObject(InProperty, InSrcType.GetValueType(), InAddress, CastChecked<UClass>(InDestType.GetValueTypeObject()), *reinterpret_cast<UObject**>(OutResult));
		case EPropertyBagPropertyType::SoftObject:
			return GetValueSoftPath(InProperty, InSrcType.GetValueType(), InAddress, *reinterpret_cast<FSoftObjectPath*>(OutResult));
		case EPropertyBagPropertyType::Class:
			return GetValueClass(InProperty, InSrcType.GetValueType(), InAddress, CastChecked<UClass>(InDestType.GetValueTypeObject()), *reinterpret_cast<UClass**>(OutResult));
		case EPropertyBagPropertyType::SoftClass:
			return GetValueSoftPath(InProperty, InSrcType.GetValueType(), InAddress, *reinterpret_cast<FSoftObjectPath*>(OutResult));
		case EPropertyBagPropertyType::UInt32:
			return GetValueUInt32(InProperty, InSrcType.GetValueType(), InAddress, *reinterpret_cast<uint32*>(OutResult));
		case EPropertyBagPropertyType::UInt64:
			return GetValueUInt64(InProperty, InSrcType.GetValueType(), InAddress, *reinterpret_cast<uint64*>(OutResult));
		default:
			return EPropertyBagResult::TypeMismatch;
		}
	}

	EPropertyBagResult GetVariableFromMismatchedArrayType(const FProperty* InProperty, const FAnimNextParamType& InSrcType, const FAnimNextParamType& InDestType, const uint8* InAddress, uint8* OutResult)
	{
		const FArrayProperty* ArrayProperty = CastFieldChecked<FArrayProperty>(InProperty);
		const FProperty* ElementProperty = ArrayProperty->Inner;
		FAnimNextParamType SrcValueType(InSrcType.GetValueType(), FAnimNextParamType::EContainerType::None, InSrcType.GetValueTypeObject());
		FAnimNextParamType DestValueType(InDestType.GetValueType(), FAnimNextParamType::EContainerType::None, InDestType.GetValueTypeObject());
		check(SrcValueType != DestValueType);	// Function assumes that types are mismatched

		const TScriptArray<FHeapAllocator>* SrcArray = reinterpret_cast<const TScriptArray<FHeapAllocator>*>(InAddress);
		TScriptArray<FHeapAllocator>* DestArray = reinterpret_cast<TScriptArray<FHeapAllocator>*>(OutResult);

		const int32 NumElements = SrcArray->Num();
		const size_t SrcValueTypeSize = InSrcType.GetValueTypeSize();
		const size_t DestValueTypeSize = InDestType.GetValueTypeSize();
		const size_t DestValueTypeAlignment = InDestType.GetValueTypeSize();

		// Reallocate dest array
		DestArray->SetNumUninitialized(NumElements, DestValueTypeSize, DestValueTypeAlignment);

		// Perform per-element conversion
		bool bSucceeded = true;
		const uint8* SrcElement = static_cast<const uint8*>(SrcArray->GetData());
		uint8* DestElement = static_cast<uint8*>(DestArray->GetData());
		for (int32 Index = 0; Index < NumElements; ++Index)
		{
			bSucceeded &= (GetVariableFromMismatchedValueType(ElementProperty, SrcValueType, DestValueType, SrcElement, DestElement) == EPropertyBagResult::Success);
			SrcElement += SrcValueTypeSize;
			DestElement += DestValueTypeSize;
		}

		return bSucceeded ? EPropertyBagResult::Success : EPropertyBagResult::TypeMismatch;
	}

	EPropertyBagResult GetVariableFromMismatchedType(const FPropertyBagPropertyDesc* InDesc, const FAnimNextParamType& InSrcType, const FAnimNextParamType& InDestType, const uint8* InAddress, uint8* OutResult)
	{
		switch(InSrcType.GetContainerType())
		{
		case EPropertyBagContainerType::None:
			if(InDestType.GetContainerType() == EPropertyBagContainerType::None)
			{
				return GetVariableFromMismatchedValueType(InDesc->CachedProperty, InSrcType, InDestType, InAddress, OutResult);
			}
			return EPropertyBagResult::TypeMismatch;
		case EPropertyBagContainerType::Array:
			if(InDestType.GetContainerType() == EPropertyBagContainerType::Array)
			{
				return GetVariableFromMismatchedArrayType(InDesc->CachedProperty, InSrcType, InDestType, InAddress, OutResult);
			}
			return EPropertyBagResult::TypeMismatch;
		default:
			return EPropertyBagResult::TypeMismatch;
		}
	}
}

EPropertyBagResult FAnimNextDataInterfaceInstance::GetVariableInternal(FName InVariableName, const FAnimNextParamType& InType, TArrayView<uint8> OutResult) const
{
	using namespace UE::AnimNext;
	
	const UPropertyBag* PropertyBag = Variables.GetPropertyBagStruct();
	if(PropertyBag == nullptr)
	{
		return EPropertyBagResult::PropertyNotFound;
	}

	const FPropertyBagPropertyDesc* Desc = PropertyBag->FindPropertyDescByName(InVariableName);
	if(Desc == nullptr)
	{
		return EPropertyBagResult::PropertyNotFound;
	}

	const int32 DescIndex = Desc - &PropertyBag->GetPropertyDescs()[0];
	const uint8* Memory = ExtendedExecuteContext.ExternalVariableRuntimeData[DescIndex].Memory;

	const FAnimNextParamType InternalType(Desc->ValueType, Desc->ContainerTypes.GetFirstContainerType(), Desc->ValueTypeObject);
	if(InternalType != InType)
	{
		return Private::GetVariableFromMismatchedType(Desc, InternalType, InType, Memory, OutResult.GetData());
	}

	Desc->CachedProperty->CopyCompleteValue(OutResult.GetData(), Memory);
	return EPropertyBagResult::Success;
}

uint8* FAnimNextDataInterfaceInstance::GetMemoryForVariable(int32 InVariableIndex, FName InVariableName, const FProperty* InVariableProperty) const
{
	const UPropertyBag* PropertyBag = Variables.GetPropertyBagStruct();
	if(PropertyBag == nullptr)
	{
		return nullptr;
	}

	if(!PropertyBag->GetPropertyDescs().IsValidIndex(InVariableIndex))
	{
		UE_LOGFMT(LogAnimation, Error, "FAnimNextDataInterfaceInstance::GetMemoryForVariable: Variable index {Index} out of range ({Range})", InVariableIndex, PropertyBag->GetPropertyDescs().Num());
		return nullptr;
	}

	if(!ExtendedExecuteContext.ExternalVariableRuntimeData.IsValidIndex(InVariableIndex))
	{
		UE_LOGFMT(LogAnimation, Error, "FAnimNextDataInterfaceInstance::GetMemoryForVariable: External variable index {Index} out of range ({Range})", InVariableIndex, ExtendedExecuteContext.ExternalVariableRuntimeData.Num());
		return nullptr;
	}

	check(ExtendedExecuteContext.ExternalVariableRuntimeData.Num() == PropertyBag->GetPropertyDescs().Num());

	const FPropertyBagPropertyDesc& Desc = PropertyBag->GetPropertyDescs()[InVariableIndex];
	if(Desc.Name != InVariableName)
	{
		UE_LOGFMT(LogAnimation, Error, "FAnimNextDataInterfaceInstance::GetMemoryForVariable: Mismatched variable names: {Name} vs {OtherName} in '{Host}'", Desc.Name, InVariableName, GetDataInterfaceName());
		return nullptr;
	}

	if(Desc.CachedProperty->GetClass() != InVariableProperty->GetClass())
	{
		UE_LOGFMT(LogAnimation, Error, "FAnimNextDataInterfaceInstance::GetMemoryForVariable: Mismatched variable types: {Name}:{Type} vs {OtherType} in '{Host}'", Desc.Name, Desc.CachedProperty->GetFName(), InVariableProperty->GetFName(), GetDataInterfaceName());
		return nullptr;
	}

	return ExtendedExecuteContext.ExternalVariableRuntimeData[InVariableIndex].Memory;
}
