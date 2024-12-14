// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ContainersFwd.h"
#include "Delegates/IDelegateInstance.h"

class UToolMenu;
struct FOnGenerateGlobalRowExtensionArgs;
struct FPropertyRowExtensionButton;

class FCustomDetailsViewRowExtensions
{
public:
	static FCustomDetailsViewRowExtensions& Get();

	~FCustomDetailsViewRowExtensions();

	void RegisterRowExtensions();

	void UnregisterRowExtensions();

private:
	FDelegateHandle RowExtensionHandle;

	static void HandleCreatePropertyRowExtension(const FOnGenerateGlobalRowExtensionArgs& InArgs, TArray<FPropertyRowExtensionButton>& OutExtensions);

	static void FillPropertyRightClickMenu(UToolMenu* InToolMenu);
};
