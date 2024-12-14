// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SceneOutlinerPublicTypes.h"

namespace SceneOutliner
{
	// Class to hold common functionality needed by the Outliner and helpful to modules creating Outliner instances
	class SCENEOUTLINER_API FSceneOutlinerHelpers
	{
	public:
		UE_DEPRECATED(5.5, "FSceneOutlinerHelpers::GetExternalPackageName has been deprecated implement/use ISceneOutlinerTreeItem::GetPackageName instead")
		static FString GetExternalPackageName(const ISceneOutlinerTreeItem& TreeItem);
		
		UE_DEPRECATED(5.5, "FSceneOutlinerHelpers::GetExternalPackage has been deprecated implement/use ISceneOutlinerTreeItem::GetPackageName instead")
		static UPackage* GetExternalPackage(const ISceneOutlinerTreeItem& TreeItem);
		
		static TSharedPtr<SWidget> GetClassHyperlink(UObject* InObject);
		
		static void PopulateExtraSearchStrings(const ISceneOutlinerTreeItem& TreeItem, TArray< FString >& OutSearchStrings);
	};
};
