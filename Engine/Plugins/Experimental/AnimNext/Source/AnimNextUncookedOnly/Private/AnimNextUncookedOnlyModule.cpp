// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextUncookedOnlyModule.h"

#include "AnimNextAssetWorkspaceAssetUserData.h"
#include "UncookedOnlyUtils.h"
#include "Engine/Blueprint.h"
#include "Modules/ModuleManager.h"
#include "UObject/AssetRegistryTagsContext.h"
#include "MessageLogModule.h"
#include "Variables/UniversalObjectLocatorBindingType.h"

#define LOCTEXT_NAMESPACE "AnimNextUncookedOnlyModule"

namespace UE::AnimNext::UncookedOnly
{

void FModule::StartupModule()
{
	RegisterVariableBindingType("/Script/AnimNextUncookedOnly.AnimNextUniversalObjectLocatorBindingData", MakeShared<FUniversalObjectLocatorBindingType>());

	// Register the compilation log (hidden from the main log set, it is displayed in the workspace editor)
	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	FMessageLogInitializationOptions LogInitOptions;
	LogInitOptions.bShowInLogWindow = false;
	LogInitOptions.MaxPageCount = 10;
	MessageLogModule.RegisterLogListing("AnimNextCompilerResults", LOCTEXT("CompilerResults", "AnimNext Compiler Results"), LogInitOptions);
}

void FModule::ShutdownModule()
{
	if(FMessageLogModule* MessageLogModule = FModuleManager::GetModulePtr<FMessageLogModule>("MessageLog"))
	{
		MessageLogModule->UnregisterLogListing("AnimNextCompilerResults");
	}

	UnregisterVariableBindingType("/Script/AnimNextUncookedOnly.AnimNextUniversalObjectLocatorBindingData");
}

void FModule::RegisterVariableBindingType(FName InStructName, TSharedPtr<IVariableBindingType> InType)
{
	VariableBindingTypes.Add(InStructName, InType);
}

void FModule::UnregisterVariableBindingType(FName InStructName)
{
	VariableBindingTypes.Remove(InStructName);
}

TSharedPtr<IVariableBindingType> FModule::FindVariableBindingType(const UScriptStruct* InStruct) const
{
	return VariableBindingTypes.FindRef(*InStruct->GetPathName());
}

}

#undef LOCTEXT_NAMESPACE 

IMPLEMENT_MODULE(UE::AnimNext::UncookedOnly::FModule, AnimNextUncookedOnly);
