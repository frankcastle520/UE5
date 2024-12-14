// Copyright Epic Games, Inc. All Rights Reserved.

#include "Experimental/ContentBrowserExtensionUtils.h"
#include "ContentBrowserDataSubsystem.h"
#include "ContentBrowserUtils.h"
#include "CollectionViewUtils.h"
#include "ContentBrowserItemPath.h"
#include "IContentBrowserDataModule.h"

namespace UE::Editor::ContentBrowser::ExtensionUtils
{
	TOptional<FLinearColor> GetFolderColor(const FName& FolderPath)
	{
		const FName VirtualPath = IContentBrowserDataModule::Get().GetSubsystem()->ConvertInternalPathToVirtual(FolderPath);

		FName CollectionName;
		ECollectionShareType::Type CollectionFolderShareType = ECollectionShareType::CST_All;

		if(ContentBrowserUtils::IsCollectionPath(VirtualPath.ToString(), &CollectionName, &CollectionFolderShareType))
		{
			if(TOptional<FLinearColor> Color = CollectionViewUtils::GetCustomColor(CollectionName, CollectionFolderShareType))
			{
				return Color;
			}
		}
		if (TOptional<FLinearColor> Color = ContentBrowserUtils::GetPathColor(FolderPath.ToString()))
		{
			return Color.GetValue();
		}

		return TOptional<FLinearColor>();
	}

	void SetFolderColor(const FName& FolderPath, const FLinearColor& FolderColor)
	{
		ContentBrowserUtils::SetPathColor(FolderPath.ToString(), FolderColor);
	}
}
