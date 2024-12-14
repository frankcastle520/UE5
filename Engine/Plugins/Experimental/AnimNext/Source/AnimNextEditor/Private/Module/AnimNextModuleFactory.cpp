// Copyright Epic Games, Inc. All Rights Reserved.

#include "Module/AnimNextModuleFactory.h"
#include "Module/AnimNextModule.h"
#include "Module/AnimNextModule_EditorData.h"
#include "UncookedOnlyUtils.h"
#include "Module/RigUnit_AnimNextModuleEvents.h"

UAnimNextModuleFactory::UAnimNextModuleFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UAnimNextModule::StaticClass();
}

bool UAnimNextModuleFactory::ConfigureProperties()
{
	return true;
}

UObject* UAnimNextModuleFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	EObjectFlags FlagsToUse = Flags | RF_Public | RF_Standalone | RF_Transactional | RF_LoadCompleted;
	if(InParent == GetTransientPackage())
	{
		FlagsToUse &= ~RF_Standalone;
	}

	UAnimNextModule* NewModule = NewObject<UAnimNextModule>(InParent, Class, Name, FlagsToUse);

	// Create internal editor data
	UAnimNextModule_EditorData* EditorData = NewObject<UAnimNextModule_EditorData>(NewModule, TEXT("EditorData"), RF_Transactional);
	NewModule->EditorData = EditorData;
	EditorData->Initialize(/*bRecompileVM*/false);

	EditorData->AddEventGraph(FRigUnit_AnimNextPrePhysicsEvent::EventName, FRigUnit_AnimNextPrePhysicsEvent::StaticStruct());

	// Compile the initial skeleton
	EditorData->RecompileVM();
	check(!EditorData->bErrorsDuringCompilation);

	return NewModule;
}