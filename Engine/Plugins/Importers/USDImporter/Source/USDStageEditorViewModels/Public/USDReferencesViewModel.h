// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

#include "UsdWrappers/ForwardDeclarations.h"
#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdStage.h"

class USDSTAGEEDITORVIEWMODELS_API FUsdReference : public TSharedFromThis<FUsdReference>
{
public:
	FString AssetPath;
	FString PrimPath;
	bool bIntroducedInLocalLayerStack = true;
	bool bIsPayload = false;
};

class USDSTAGEEDITORVIEWMODELS_API FUsdReferencesViewModel
{
public:
	void UpdateReferences(const UE::FUsdStageWeak& UsdStage, const TCHAR* PrimPath);
	void RemoveReference(const TSharedPtr<FUsdReference>& Reference);
	void ReloadReference(const TSharedPtr<FUsdReference>& Reference);

public:
	UE::FUsdStageWeak UsdStage;
	UE::FSdfPath PrimPath;
	TArray<TSharedPtr<FUsdReference>> References;
};
