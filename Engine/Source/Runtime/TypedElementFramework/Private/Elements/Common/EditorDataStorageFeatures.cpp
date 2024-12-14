// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Common/EditorDataStorageFeatures.h"

namespace UE::Editor::DataStorage
{
	const FName StorageFeatureName = TEXT("EditorDataStorage");
	const FName CompatibilityFeatureName = TEXT("EditorDataStorageCompatibility");
	const FName UiFeatureName = TEXT("EditorDataStorageUi");
	
	FSimpleMulticastDelegate& OnEditorDataStorageFeaturesEnabled()
	{
		static FSimpleMulticastDelegate OnTedsFeaturesEnabled;
		return OnTedsFeaturesEnabled;
	}

	bool AreEditorDataStorageFeaturesEnabled()
	{
		return IModularFeatures::Get().IsModularFeatureAvailable(StorageFeatureName)
			&& IModularFeatures::Get().IsModularFeatureAvailable(CompatibilityFeatureName)
			&& IModularFeatures::Get().IsModularFeatureAvailable(UiFeatureName);
	}
} // namespace UE::Editor::DataStorage
