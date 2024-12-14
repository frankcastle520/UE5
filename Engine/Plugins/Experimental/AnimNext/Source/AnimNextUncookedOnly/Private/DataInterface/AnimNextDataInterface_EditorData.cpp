// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataInterface/AnimNextDataInterface_EditorData.h"

#include "Entries/AnimNextDataInterfaceEntry.h"
#include "Entries/AnimNextVariableEntry.h"

TConstArrayView<TSubclassOf<UAnimNextRigVMAssetEntry>> UAnimNextDataInterface_EditorData::GetEntryClasses() const
{
	static const TSubclassOf<UAnimNextRigVMAssetEntry> Classes[] =
	{
		UAnimNextVariableEntry::StaticClass(),
		UAnimNextDataInterfaceEntry::StaticClass(),
	};

	return Classes;
}

void UAnimNextDataInterface_EditorData::CustomizeNewAssetEntry(UAnimNextRigVMAssetEntry* InNewEntry) const
{
	UAnimNextVariableEntry* VariableEntry = Cast<UAnimNextVariableEntry>(InNewEntry);
	if(VariableEntry == nullptr)
	{
		return;
	}
	
	const bool bIsDataInterface = ExactCast<UAnimNextDataInterface_EditorData>(this) != nullptr;
	if(!bIsDataInterface)
	{
		return;
	}

	// Force all data interface variables to be public
	VariableEntry->SetExportAccessSpecifier(EAnimNextExportAccessSpecifier::Public, false);
}