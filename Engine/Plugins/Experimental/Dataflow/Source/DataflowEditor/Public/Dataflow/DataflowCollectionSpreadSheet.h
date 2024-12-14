// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowView.h"

class UDataflowEditor;
class UPrimitiveComponent;
class SCollectionSpreadSheetWidget;

/**
*
* Class to handle the SelectionView widget
*
*/
class FDataflowCollectionSpreadSheet : public FDataflowNodeView
{
public:

	FDataflowCollectionSpreadSheet(TObjectPtr<UDataflowBaseContent> InContent = nullptr);
	~FDataflowCollectionSpreadSheet();

	virtual void SetSupportedOutputTypes() override;
	virtual void UpdateViewData() override;
	virtual void ConstructionViewSelectionChanged(const TArray<UPrimitiveComponent*>& InSelectedComponents) override {};

	void SetCollectionSpreadSheet(TSharedPtr<SCollectionSpreadSheetWidget>& InCollectionSpreadSheet);

private:
	TSharedPtr<SCollectionSpreadSheetWidget> CollectionSpreadSheet;

	FDelegateHandle OnPinnedDownChangedDelegateHandle;
	FDelegateHandle OnRefreshLockedChangedDelegateHandle;
};

