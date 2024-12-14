// Copyright Epic Games, Inc. All Rights Reserved.

#include "Inputs/BuilderInputManager.h"
#include "BuilderCommandCreationManager.h"
#include "Framework/Commands/UICommandInfo.h"

FBuilderInputManager& FBuilderInputManager::Get()
{
	static FBuilderInputManager Manager;
	return Manager;
}

void FBuilderInputManager::Initialize()
{
	FBuilderCommandCreationManager::Register();
}

void FBuilderInputManager::Shutdown()
{
	FBuilderCommandCreationManager::Unregister();
}

const FBuilderCommandCreationManager& FBuilderInputManager::GetCommandManager()
{
	static const FBuilderCommandCreationManager& CommandManager = FBuilderCommandCreationManager::Get();
	return CommandManager;
}
