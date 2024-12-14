// Copyright Epic Games, Inc. All Rights Reserved.

#include "Entries/AnimNextDataInterfaceEntry.h"

#include "UncookedOnlyUtils.h"
#include "DataInterface/AnimNextDataInterface.h"
#include "DataInterface/AnimNextDataInterface_EditorData.h"
#include "Entries/AnimNextVariableEntry.h"
#include "Logging/StructuredLog.h"
#include "Param/ParamType.h"

#define LOCTEXT_NAMESPACE "AnimNextDataInterfaceEntry"

void UAnimNextDataInterfaceEntry::Initialize(UAnimNextRigVMAssetEditorData* InEditorData)
{
	Super::Initialize(InEditorData);

	if(DataInterface)
	{
		UAnimNextDataInterface_EditorData* DataInterfaceEditorData = UE::AnimNext::UncookedOnly::FUtils::GetEditorData<UAnimNextDataInterface_EditorData>(DataInterface.Get());
		DataInterfaceEditorData->ModifiedDelegate.AddUObject(this, &UAnimNextDataInterfaceEntry::HandleDataInterfaceModified);
	}
}

FName UAnimNextDataInterfaceEntry::GetEntryName() const
{
	return DataInterface ? DataInterface->GetFName() : NAME_None;
}

FText UAnimNextDataInterfaceEntry::GetDisplayName() const
{
	return DataInterface ? FText::FromName(DataInterface->GetFName()) : LOCTEXT("InvalidDataInterface", "Invalid Data Interface");
}

FText UAnimNextDataInterfaceEntry::GetDisplayNameTooltip() const
{
	return DataInterface ? FText::FromName(DataInterface->GetFName()) : LOCTEXT("InvalidDataInterfaceTooltip", "Invalid or deleted Data Interface");
}

void UAnimNextDataInterfaceEntry::SetDataInterface(UAnimNextDataInterface* InDataInterface, bool bSetupUndoRedo)
{
	check(InDataInterface != nullptr);

	if(bSetupUndoRedo)
	{
		Modify();
	}

	DataInterface = InDataInterface;
	DataInterfacePath = FSoftObjectPath(DataInterface);
	ValueOverrides.Reset();
}

UAnimNextDataInterface* UAnimNextDataInterfaceEntry::GetDataInterface() const
{
	return DataInterface;
}

FSoftObjectPath UAnimNextDataInterfaceEntry::GetDataInterfacePath() const
{
	return DataInterfacePath;
}

bool UAnimNextDataInterfaceEntry::SetValueOverrideToDefault(FName InName, bool bSetupUndoRedo)
{
	const FProperty* DefaultValueProperty = nullptr;
	TConstArrayView<uint8> DefaultValue;
	if(!GetDefaultValueRecursive(InName, DefaultValueProperty, DefaultValue))
	{
		// No value, so cannot compare with default
		UE_LOGFMT(LogAnimation, Error, "UAnimNextDataInterfaceEntry::SetValueOverrideToDefault: Could not find a default value for variable {Name}", InName);
		return false;
	}

	return SetValueOverride(InName, FAnimNextParamType::FromProperty(DefaultValueProperty), DefaultValue, bSetupUndoRedo);
}

bool UAnimNextDataInterfaceEntry::SetValueOverride(FName InName, const FAnimNextParamType& InType, TConstArrayView<uint8> InValue, bool bSetupUndoRedo)
{
	check(InName != NAME_None);
	check(InType.IsValid());
	check(InValue.GetData() != nullptr);

	if(bSetupUndoRedo)
	{
		Modify();
	}

	const FPropertyBagPropertyDesc* Desc = ValueOverrides.FindPropertyDescByName(InName);
	if(Desc == nullptr)
	{
		ValueOverrides.AddContainerProperty(InName, InType.GetContainerType(), InType.GetValueType(), InType.GetValueTypeObject());
		Desc = ValueOverrides.FindPropertyDescByName(InName);
	}

	if(Desc == nullptr)
	{
		UE_LOGFMT(LogAnimation, Error, "UAnimNextDataInterfaceEntry::SetValueOverride: Failed to add value override to property bag for {Name}", InName);
		return false;
	}

	// Check type matches
	FAnimNextParamType FoundType(Desc->ValueType, Desc->ContainerTypes.GetFirstContainerType(), Desc->ValueTypeObject);
	if(FoundType != InType)
	{
		UE_LOGFMT(LogAnimation, Error, "UAnimNextDataInterfaceEntry::SetValueOverride: Failed to add value override of the correct type to property bag for {Name}", InName);
		return false;
	}
	
	check(Desc->CachedProperty);
	if(Desc->CachedProperty->GetElementSize() != InValue.Num())
	{
		UE_LOGFMT(LogAnimation, Error, "UAnimNextDataInterfaceEntry::SetValueOverride: Mismatched buffer sizes (%d vs %d)", Desc->CachedProperty->GetElementSize(), InValue.Num());
		return false;
	}

	uint8* DestPtr = Desc->CachedProperty->ContainerPtrToValuePtr<uint8>(ValueOverrides.GetMutableValue().GetMemory());
	const uint8* SrcPtr = InValue.GetData();
	Desc->CachedProperty->CopyCompleteValue(DestPtr, SrcPtr);

	BroadcastModified(EAnimNextEditorDataNotifType::VariableDefaultValueChanged);

	return true;
}

bool UAnimNextDataInterfaceEntry::ClearValueOverride(FName InName, bool bSetupUndoRedo)
{
	if(bSetupUndoRedo)
	{
		Modify();
	}

	const FPropertyBagPropertyDesc* Desc = ValueOverrides.FindPropertyDescByName(InName);
	if(Desc == nullptr)
	{
		UE_LOGFMT(LogAnimation, Error, "Failed to clear value override in property bag");
		return false;
	}

	ValueOverrides.RemovePropertyByName(InName);

	BroadcastModified(EAnimNextEditorDataNotifType::VariableDefaultValueChanged);

	return true;
}

const UAnimNextDataInterfaceEntry* UAnimNextDataInterfaceEntry::FindOverrideRecursiveHelper(TFunctionRef<bool(const UAnimNextDataInterfaceEntry*)> InPredicate) const
{
	if(DataInterface == nullptr)
	{
		return nullptr;
	}

	if(InPredicate(this))
	{
		return this;
	}

	UAnimNextDataInterface_EditorData* EditorData = UE::AnimNext::UncookedOnly::FUtils::GetEditorData<UAnimNextDataInterface_EditorData>(DataInterface.Get());
	for(const UAnimNextRigVMAssetEntry* Entry : EditorData->Entries)
	{
		if(const UAnimNextDataInterfaceEntry* DataInterfaceEntry = Cast<UAnimNextDataInterfaceEntry>(Entry))
		{
			if(InPredicate(DataInterfaceEntry))
			{
				return DataInterfaceEntry;
			}
			else
			{
				return DataInterfaceEntry->FindOverrideRecursiveHelper(InPredicate);
			}
		}
	}

	return nullptr;
}

EAnimNextDataInterfaceValueOverrideStatus UAnimNextDataInterfaceEntry::FindOverrideStatusRecursiveHelper(TFunctionRef<bool(const UAnimNextDataInterfaceEntry*)> InPredicate) const
{
	const UAnimNextDataInterfaceEntry* OverridingEntry = FindOverrideRecursiveHelper(InPredicate);
	if(OverridingEntry == this)
	{
		return EAnimNextDataInterfaceValueOverrideStatus::OverriddenInThisAsset;
	}

	if(OverridingEntry != nullptr)
	{
		return EAnimNextDataInterfaceValueOverrideStatus::OverriddenInParentAsset;
	}

	return EAnimNextDataInterfaceValueOverrideStatus::NotOverridden;
}

bool UAnimNextDataInterfaceEntry::GetValueOverride(FName InName, const FProperty*& OutProperty, TConstArrayView<uint8>& OutValue) const
{
	const FPropertyBagPropertyDesc* Desc = ValueOverrides.FindPropertyDescByName(InName);
	if(Desc == nullptr)
	{
		return false;
	}

	check(Desc->CachedProperty);
	OutProperty = Desc->CachedProperty;
	OutValue = TConstArrayView<uint8>(Desc->CachedProperty->ContainerPtrToValuePtr<uint8>(ValueOverrides.GetValue().GetMemory()), Desc->CachedProperty->GetElementSize());
	return true;
}

bool UAnimNextDataInterfaceEntry::GetValueOverride(FName InName, FAnimNextParamType& OutType, const FProperty*& OutProperty, TConstArrayView<uint8>& OutValue) const
{
	const FPropertyBagPropertyDesc* Desc = ValueOverrides.FindPropertyDescByName(InName);
	if(Desc == nullptr)
	{
		return false;
	}

	check(Desc->CachedProperty);
	OutType = FAnimNextParamType(Desc->ValueType, Desc->ContainerTypes.GetFirstContainerType(), Desc->ValueTypeObject);
	OutProperty = Desc->CachedProperty;
	OutValue = TConstArrayView<uint8>(Desc->CachedProperty->ContainerPtrToValuePtr<uint8>(ValueOverrides.GetValue().GetMemory()), Desc->CachedProperty->GetElementSize());
	return true;
}

EAnimNextDataInterfaceValueOverrideStatus UAnimNextDataInterfaceEntry::FindValueOverrideRecursive(FName InName, const FProperty*& OutProperty, TConstArrayView<uint8>& OutValue) const
{
	auto CheckOverride = [&InName, &OutValue, &OutProperty](const UAnimNextDataInterfaceEntry* InInterfaceEntry) -> bool
	{
		return InInterfaceEntry->GetValueOverride(InName, OutProperty, OutValue);
	};

	return FindOverrideStatusRecursiveHelper(CheckOverride);
}

EAnimNextDataInterfaceValueOverrideStatus UAnimNextDataInterfaceEntry::FindValueOverrideRecursive(FName InName, FAnimNextParamType& OutType, const FProperty*& OutProperty, TConstArrayView<uint8>& OutValue) const
{
	auto CheckOverride = [&InName, &OutType, &OutValue, &OutProperty](const UAnimNextDataInterfaceEntry* InInterfaceEntry) -> bool
	{
		return InInterfaceEntry->GetValueOverride(InName, OutType, OutProperty, OutValue);
	};

	return FindOverrideStatusRecursiveHelper(CheckOverride);
}

bool UAnimNextDataInterfaceEntry::HasValueOverride(FName InName, FAnimNextParamType& OutType) const
{
	const FPropertyBagPropertyDesc* Desc = ValueOverrides.FindPropertyDescByName(InName);
	if(Desc == nullptr)
	{
		return false;
	}

	OutType = FAnimNextParamType(Desc->ValueType, Desc->ContainerTypes.GetFirstContainerType(), Desc->ValueTypeObject);
	return true;
}

bool UAnimNextDataInterfaceEntry::HasValueOverride(FName InName) const
{
	const FPropertyBagPropertyDesc* Desc = ValueOverrides.FindPropertyDescByName(InName);
	return Desc != nullptr;
}

bool UAnimNextDataInterfaceEntry::GetDefaultValueRecursive(FName InName, const FProperty*& OutProperty, TConstArrayView<uint8>& OutValue) const
{
	auto CheckOverride = [this, &InName, &OutValue, &OutProperty](const UAnimNextDataInterfaceEntry* InInterfaceEntry) -> bool
	{
		// Skip 'this' when looking for overrides
		bool bHasOverride = this != InInterfaceEntry && InInterfaceEntry->GetValueOverride(InName, OutProperty, OutValue);
		if(!bHasOverride && InInterfaceEntry->DataInterface)
		{
			// No override, so see if this data interface holds a value
			UAnimNextDataInterface_EditorData* EditorData = UE::AnimNext::UncookedOnly::FUtils::GetEditorData<UAnimNextDataInterface_EditorData>(InInterfaceEntry->DataInterface.Get());
			if(UAnimNextVariableEntry* VariableEntry = Cast<UAnimNextVariableEntry>(EditorData->FindEntry(InName)))
			{
				VariableEntry->GetDefaultValue(OutProperty, OutValue);
			}
		}
		return bHasOverride;
	};

	(void)FindOverrideStatusRecursiveHelper(CheckOverride);
	return (!OutValue.IsEmpty() && OutProperty != nullptr);
}

bool UAnimNextDataInterfaceEntry::HasValueOverrideNotMatchingDefault(FName InName) const
{
	const FProperty* OverrideProperty = nullptr;
	TConstArrayView<uint8> OverrideValue;
	if(!GetValueOverride(InName, OverrideProperty, OverrideValue))
	{
		// No override, so cant have a default-matching override
		return false;
	}

	check(OverrideProperty != nullptr);
	check(!OverrideValue.IsEmpty());

	const FProperty* BaseProperty = nullptr;
	TConstArrayView<uint8> BaseValue;
	if(!GetDefaultValueRecursive(InName, BaseProperty, BaseValue))
	{
		// No value, so cannot compare with default
		return false;
	}

	check(BaseProperty != nullptr);
	check(!BaseValue.IsEmpty());

	if(BaseProperty->GetClass() != OverrideProperty->GetClass()) 
	{
		// Properties differ, cannot compare
		// If the ensure hits here then we have somehow ended up with different types in implementing/base interfaces, so the workflow that got us to
		// this point needs edge cases handling better
		ensure(false);
		return false;
	}

	return !BaseProperty->Identical(OverrideValue.GetData(), BaseValue.GetData());
}

EAnimNextDataInterfaceValueOverrideStatus UAnimNextDataInterfaceEntry::GetValueOverrideStatusRecursive(FName InName) const
{
	auto CheckOverride = [&InName](const UAnimNextDataInterfaceEntry* InInterfaceEntry) -> bool
	{
		return InInterfaceEntry->HasValueOverride(InName);
	};

	return FindOverrideStatusRecursiveHelper(CheckOverride);
}

EAnimNextDataInterfaceValueOverrideStatus UAnimNextDataInterfaceEntry::FindValueOverridePropertyBagRecursive(FName InName, FInstancedPropertyBag*& OutPropertyBag) const
{
	auto CheckOverride = [&InName, &OutPropertyBag](const UAnimNextDataInterfaceEntry* InInterfaceEntry) -> bool
	{
		const FPropertyBagPropertyDesc* Desc = InInterfaceEntry->ValueOverrides.FindPropertyDescByName(InName);
		if(Desc == nullptr)
		{
			return false;
		}

		OutPropertyBag = const_cast<FInstancedPropertyBag*>(&InInterfaceEntry->ValueOverrides);
		return true;
	};

	return FindOverrideStatusRecursiveHelper(CheckOverride);
}

void UAnimNextDataInterfaceEntry::HandleDataInterfaceModified(UAnimNextRigVMAssetEditorData* InEditorData, EAnimNextEditorDataNotifType InType, UObject* InSubject)
{
	switch(InType)
	{
	case EAnimNextEditorDataNotifType::UndoRedo:
	case EAnimNextEditorDataNotifType::EntryAdded:
	case EAnimNextEditorDataNotifType::EntryRemoved:
	case EAnimNextEditorDataNotifType::EntryRenamed:
	case EAnimNextEditorDataNotifType::EntryAccessSpecifierChanged:
	case EAnimNextEditorDataNotifType::VariableDefaultValueChanged:
		if(UAnimNextRigVMAssetEditorData* EditorData = GetTypedOuter<UAnimNextRigVMAssetEditorData>())
		{
			EditorData->RecompileVM();
		}
		break;
	default:
		break;
	}
}

#undef LOCTEXT_NAMESPACE