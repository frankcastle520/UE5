// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/SelectionGroupCustomization.h"
#include "ChaosClothAsset/CollectionClothFacade.h"

namespace UE::Chaos::ClothAsset
{
	TSharedRef<IPropertyTypeCustomization> FSelectionGroupCustomization::MakeInstance()
	{
		return MakeShareable(new FSelectionGroupCustomization);
	}

	TArray<FName> FSelectionGroupCustomization::GetTargetGroupNames(const FManagedArrayCollection& /*Collection*/) const
	{
		return FCollectionClothFacade::GetValidClothCollectionGroupName();
	}
}  // End namespace UE::Chaos::ClothAsset
