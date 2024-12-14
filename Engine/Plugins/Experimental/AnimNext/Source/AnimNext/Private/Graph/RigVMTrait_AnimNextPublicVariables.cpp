// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/RigVMTrait_AnimNextPublicVariables.h"
#include "AnimNextRigVMAsset.h"
#include "Logging/StructuredLog.h"
#if WITH_EDITOR
#include "RigVMModel/RigVMController.h"
#include "RigVMModel/RigVMPin.h"
#endif

#if WITH_EDITOR
FString FRigVMTrait_AnimNextPublicVariables::GetDisplayName() const
{
	TStringBuilder<256> StringBuilder;
	StringBuilder.Appendf(TEXT("Variables: %s"), Asset ? *Asset->GetFName().ToString() : TEXT("None"));
	return StringBuilder.ToString();
}

void FRigVMTrait_AnimNextPublicVariables::GetProgrammaticPins(URigVMController* InController, int32 InParentPinIndex, const FString& InDefaultValue, FRigVMPinInfoArray& OutPinArray) const
{
	if (Asset == nullptr)
	{
		return;
	}

	const FInstancedPropertyBag& UserDefaults = Asset->GetPublicVariableDefaults();
	if(!UserDefaults.IsValid())
	{
		return;
	}

	FInstancedPropertyBag Defaults;
	Defaults.InitializeFromBagStruct(UserDefaults.GetPropertyBagStruct());

	const TFunction<ERigVMPinDefaultValueType(const FName&)> DefaultValueTypeGetter = [&UserDefaults, &Defaults](const FName& InPropertyName)
	{
		if(const FProperty* Property = UserDefaults.GetPropertyBagStruct()->FindPropertyByName(InPropertyName))
		{
			if(Property->Identical_InContainer(UserDefaults.GetValue().GetMemory(), Defaults.GetValue().GetMemory()))
			{
				return ERigVMPinDefaultValueType::Unset;
			}
			return ERigVMPinDefaultValueType::Override;
		}
		return ERigVMPinDefaultValueType::AutoDetect;
	};

	OutPinArray.AddPins(const_cast<UPropertyBag*>(UserDefaults.GetPropertyBagStruct()), InController, ERigVMPinDirection::Input, InParentPinIndex, DefaultValueTypeGetter, UserDefaults.GetValue().GetMemory(), true);
}

bool FRigVMTrait_AnimNextPublicVariables::ShouldCreatePinForProperty(const FProperty* InProperty) const
{
	if(!Super::ShouldCreatePinForProperty(InProperty))
	{
		return false;
	}
	return
		InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FRigVMTrait_AnimNextPublicVariables, Asset) ||
		InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(FRigVMTrait_AnimNextPublicVariables, VariableNames) ||
		VariableNames.Contains(InProperty->GetFName());
}
#endif

namespace UE::AnimNext
{

const UAnimNextDataInterface* FPublicVariablesTraitToDataInterfaceHostAdapter::GetDataInterface() const
{
	return Trait.Asset;
}

uint8* FPublicVariablesTraitToDataInterfaceHostAdapter::GetMemoryForVariable(int32 InVariableIndex, FName InVariableName, const FProperty* InVariableProperty) const
{
	// Note we dont use InVariableIndex here as we may not have bound all variables in an interface

	int32 TraitVariableIndex = Trait.VariableNames.Find(InVariableName);
	if(TraitVariableIndex == INDEX_NONE)
	{
		// Variable not bound here
		return nullptr;
	}

	if(!TraitScope.GetAdditionalMemoryHandles().IsValidIndex(TraitVariableIndex))
	{
		// Memory handle is out of bounds
		// If this ensure fires then we have a mismatch between the variable names and the compiled memory handles, indicating a bug with the
		// compilation of trait additional memory handles (programmatic pins)
		ensure(false);
		return nullptr;
	}

	const FRigVMMemoryHandle& MemoryHandle = TraitScope.GetAdditionalMemoryHandles()[TraitVariableIndex];
	if(InVariableProperty->GetClass() != MemoryHandle.GetProperty()->GetClass())
	{
		UE_LOGFMT(LogAnimation, Error, "FPublicVariablesTraitToDataInterfaceHostAdapter::GetMemoryForVariable: Mismatched variable types: {Name}:{Type} vs {OtherType}", InVariableName, InVariableProperty->GetFName(), MemoryHandle.GetProperty()->GetFName());
		return nullptr;
	}

	return const_cast<uint8*>(MemoryHandle.GetData());
}

}