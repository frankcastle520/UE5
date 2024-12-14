// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetRegistry/AssetData.h"
#include "Containers/ContainersFwd.h"
#include "Delegates/Delegate.h"
#include "Delegates/DelegateCombinations.h"
#include "Delegates/IDelegateInstance.h"

class FMenuBuilder;

class FDMTextureSetContentBrowserIntegration
{
public:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnPopulateMenu, FMenuBuilder& InMenuBuilder,
		const TArray<FAssetData>& InSelectedAssets)

	DYNAMICMATERIALTEXTURESETEDITOR_API static FOnPopulateMenu::RegistrationType& GetPopulateExtenderDelegate();

protected:
	static FOnPopulateMenu PopulateMenuDelegate;
};
