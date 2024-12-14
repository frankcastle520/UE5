// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMTextureSetContentBrowserIntegration.h"

FDMTextureSetContentBrowserIntegration::FOnPopulateMenu FDMTextureSetContentBrowserIntegration::PopulateMenuDelegate;

TMulticastDelegate<void(FMenuBuilder&, const TArray<FAssetData>&)>::RegistrationType& FDMTextureSetContentBrowserIntegration::GetPopulateExtenderDelegate()
{
	return PopulateMenuDelegate;
}
