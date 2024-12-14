// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IAvaTransitionModule.h"
#include "Templates/SharedPointer.h"

class FAvaTransitionSceneViewExtension;

class FAvaTransitionModule : public IAvaTransitionModule
{
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface

	//~ Begin IAvaTransitionModule
	virtual FOnValidateTransitionTree& GetOnValidateTransitionTree() override;
	//~ End IAvaTransitionModule

	void PostEngineInit();

	TSharedPtr<FAvaTransitionSceneViewExtension> TransitionSceneViewExtension;

	FOnValidateTransitionTree OnValidateStateTree;

	FDelegateHandle OnPostEngineInitHandle;
};
