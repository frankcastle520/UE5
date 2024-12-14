// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextEntryAssetDefinitions.h"

#define LOCTEXT_NAMESPACE "AnimNextAssetDefinitions"

FText UAssetDefinition_AnimNextVariableEntry::GetObjectDisplayNameText(UObject* Object) const
{
	UAnimNextRigVMAssetEntry* Parameter = CastChecked<UAnimNextRigVMAssetEntry>(Object);
	return Parameter->GetDisplayName();
}

FText UAssetDefinition_AnimNextAnimationGraphEntry::GetObjectDisplayNameText(UObject* Object) const
{
	UAnimNextRigVMAssetEntry* Parameter = CastChecked<UAnimNextRigVMAssetEntry>(Object);
	return Parameter->GetDisplayName();
}

FText UAssetDefinition_AnimNextEventGraphEntry::GetObjectDisplayNameText(UObject* Object) const
{
	UAnimNextRigVMAssetEntry* Variable = CastChecked<UAnimNextRigVMAssetEntry>(Object);
	return Variable->GetDisplayName();
}

FText UAssetDefinition_AnimNextDataInterfaceEntry::GetObjectDisplayNameText(UObject* Object) const
{
	UAnimNextDataInterfaceEntry* DataInterface = CastChecked<UAnimNextDataInterfaceEntry>(Object);
	return DataInterface->GetDisplayName();
}

#undef LOCTEXT_NAMESPACE