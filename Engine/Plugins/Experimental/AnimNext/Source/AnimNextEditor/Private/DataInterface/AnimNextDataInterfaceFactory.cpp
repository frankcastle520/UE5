// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataInterface/AnimNextDataInterfaceFactory.h"
#include "DataInterface/AnimNextDataInterface.h"
#include "DataInterface/AnimNextDataInterface_EditorData.h"
#include "Module/RigUnit_AnimNextModuleEvents.h"

UAnimNextDataInterfaceFactory::UAnimNextDataInterfaceFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UAnimNextDataInterface::StaticClass();
}

bool UAnimNextDataInterfaceFactory::ConfigureProperties()
{
	return true;
}

UObject* UAnimNextDataInterfaceFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	EObjectFlags FlagsToUse = Flags | RF_Public | RF_Standalone | RF_Transactional | RF_LoadCompleted;
	if(InParent == GetTransientPackage())
	{
		FlagsToUse &= ~RF_Standalone;
	}

	UAnimNextDataInterface* NewDataInterface = NewObject<UAnimNextDataInterface>(InParent, Class, Name, FlagsToUse);

	// Create internal editor data
	UAnimNextDataInterface_EditorData* EditorData = NewObject<UAnimNextDataInterface_EditorData>(NewDataInterface, TEXT("EditorData"), RF_Transactional);
	NewDataInterface->EditorData = EditorData;
	EditorData->Initialize(/*bRecompileVM*/false);

	// Compile the initial skeleton
	EditorData->RecompileVM();
	check(!EditorData->bErrorsDuringCompilation);

	return NewDataInterface;
}