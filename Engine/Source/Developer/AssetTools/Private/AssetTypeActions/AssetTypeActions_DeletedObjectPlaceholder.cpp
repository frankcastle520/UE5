// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions_DeletedObjectPlaceholder.h"

#if WITH_EDITOR

FString FAssetTypeActions_DeletedObjectPlaceholder::GetObjectDisplayName(UObject* InObject) const
{
	if (UDeletedObjectPlaceholder* Object = Cast<UDeletedObjectPlaceholder>(InObject))
	{
		return Object->GetDisplayName();
	}
	return FAssetTypeActions_Base::GetObjectDisplayName(InObject);
}

#endif