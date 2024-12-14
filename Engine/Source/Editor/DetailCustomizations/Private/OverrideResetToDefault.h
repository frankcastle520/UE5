// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailPropertyRow.h"
#include "Layout/Visibility.h"
#include "IPropertyTypeCustomization.h"
#include "PropertyHandle.h"
#include "UObject/PropertyAccessUtil.h"

/**
 * Class template aiming to override ResetToDefault methods by comparing properties with an internal static DefaultObject.
 * 
 * This behavior was originally written for settings UI and does not properly support inheritance (resetting a parent class's
 * struct member to default and have it propagate to loaded children). If a struct wants to override ResetToDefault behavior
 * using this class and must support the outer-object being a parent class, OnResetToDefault should be revisited to properly
 * reset archetype instances as well.
 */
template<typename UStructType>
class TOverrideResetToDefaultWithStaticUStruct 
{
public:
	/** Called by the UI to show/hide the reset widgets */
	static bool IsResetToDefaultVisible(TSharedPtr<IPropertyHandle> InPropertyHandle);
	
	/** Reset to default triggered in UI */
	static void OnResetToDefault(TSharedPtr<IPropertyHandle> InPropertyHandle);

protected:
	/** Adds callbacks used by the UI to determine if a property can be reset and reset it. */
	static void AddResetToDefaultOverrides(IDetailPropertyRow& InDetailPropertyRow);

	static const UStructType DefaultObject;
};

template<typename UStructType>
const UStructType TOverrideResetToDefaultWithStaticUStruct<UStructType>::DefaultObject;

template<typename UStructType>
bool TOverrideResetToDefaultWithStaticUStruct<UStructType>::IsResetToDefaultVisible(TSharedPtr<IPropertyHandle> InPropertyHandle)
{
	FProperty*		Property = InPropertyHandle->GetProperty();
	const void*		DefaultValuePtr = nullptr;
	void*			ValuePtr = nullptr;

	check(Property != nullptr);

	DefaultValuePtr = Property->ContainerPtrToValuePtr<void>(&DefaultObject);
	InPropertyHandle->GetValueData(ValuePtr);

	if ((DefaultValuePtr != nullptr) && (ValuePtr != nullptr))
	{
		return !Property->Identical(DefaultValuePtr, ValuePtr);
	}

	return false;
}

template<typename UStructType>
void TOverrideResetToDefaultWithStaticUStruct<UStructType>::OnResetToDefault(TSharedPtr<IPropertyHandle> InPropertyHandle)
{
	FProperty*	Property = InPropertyHandle->GetProperty();
	const void* DefaultValuePtr = nullptr;
	void*		ValuePtr = nullptr;
	
	check(Property != nullptr);

	DefaultValuePtr = Property->ContainerPtrToValuePtr<void>(&DefaultObject);
	InPropertyHandle->GetValueData(ValuePtr);

	if ((DefaultValuePtr != nullptr) && (ValuePtr != nullptr))
	{
		// SetPropertyValue_DirectSingle when called on an archetype can propagate to instances. 
		// See class comments: this ResetToDefault override was not written for contexts involving 
		// inheritance, so pass in empty archetype instances list.
		TArray<void*> NoArchetypeInsts;
		PropertyAccessUtil::SetPropertyValue_DirectSingle(Property, DefaultValuePtr, Property, ValuePtr, NoArchetypeInsts, 0, Property->HasAnyFlags(RF_ArchetypeObject), []() { return nullptr; });
	}
}

template<typename UStructType>
void TOverrideResetToDefaultWithStaticUStruct<UStructType>::AddResetToDefaultOverrides(IDetailPropertyRow& InDetailPropertyRow)
{
	InDetailPropertyRow.OverrideResetToDefault(FResetToDefaultOverride::Create(FIsResetToDefaultVisible::CreateStatic(&TOverrideResetToDefaultWithStaticUStruct<UStructType>::IsResetToDefaultVisible),
																			   FResetToDefaultHandler::CreateStatic(&TOverrideResetToDefaultWithStaticUStruct<UStructType>::OnResetToDefault),
																			   true));
}
