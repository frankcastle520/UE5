// Copyright Epic Games, Inc. All Rights Reserved.

#include "Util/VCamCoreScriptingFunctionLibrary.h"

#include "IVCamCoreModule.h"
#include "Util/UnifiedActivationDelegateContainer.h"

void UVCamCoreScriptingFunctionLibrary::AddCanActivateOutputProviderDelegate(FCanChangeActiviationDynamicVCamDelegate Delegate)
{
	UE::VCamCore::IVCamCoreModule::Get().OnCanActivateOutputProvider().Add(MoveTemp(Delegate));
}

void UVCamCoreScriptingFunctionLibrary::RemoveCanActivateOutputProviderDelegate(UObject* Object)
{
	UE::VCamCore::IVCamCoreModule::Get().OnCanActivateOutputProvider().RemoveAll(Object);
}
