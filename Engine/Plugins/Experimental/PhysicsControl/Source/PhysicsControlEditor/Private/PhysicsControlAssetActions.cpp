// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsControlAssetActions.h"
#include "PhysicsControlAsset.h"
#include "PhysicsControlAssetEditor.h"

//======================================================================================================================
UClass* FPhysicsControlAssetActions::GetSupportedClass() const
{
	return UPhysicsControlAsset::StaticClass();
}

//======================================================================================================================
FText FPhysicsControlAssetActions::GetName() const
{
	return INVTEXT("Physics Control Asset");
}

//======================================================================================================================
FColor FPhysicsControlAssetActions::GetTypeColor() const
{
	// Match the "standard" physics color - they tend to be variations around this value
	return FColor(255, 192, 128);
}

//======================================================================================================================
uint32 FPhysicsControlAssetActions::GetCategories()
{
	return EAssetTypeCategories::Physics;
}

//======================================================================================================================
void FPhysicsControlAssetActions::OpenAssetEditor(
	const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	const EToolkitMode::Type Mode = 
		EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (UObject* Object : InObjects)
	{
		if (UPhysicsControlAsset* Asset = Cast<UPhysicsControlAsset>(Object))
		{
			TSharedRef<FPhysicsControlAssetEditor> NewEditor(new FPhysicsControlAssetEditor());
			NewEditor->InitAssetEditor(Mode, EditWithinLevelEditor, Asset);
		}
	}
}
