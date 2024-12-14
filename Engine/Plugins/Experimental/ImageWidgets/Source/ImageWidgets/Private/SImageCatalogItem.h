// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SImageCatalog.h"

namespace UE::ImageWidgets
{
	struct FImageCatalogItemData;

	/**
	 * Widget for a single item row in the catalog.
	 */
	class SImageCatalogItem : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SImageCatalogItem) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedPtr<FImageCatalogItemData>& InItemData);

	private:
		FText GetItemInfo() const;
		FText GetItemName() const;
		const FSlateBrush* GetItemThumbnail() const;
		FText GetItemToolTip() const;

		TSharedPtr<FImageCatalogItemData> ItemData;
	};
}
