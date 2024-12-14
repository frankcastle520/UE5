// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TedsOutlinerMode.h"
#include "SceneOutlinerFwd.h"
#include "Elements/Common/TypedElementHandles.h"

DECLARE_DELEGATE_OneParam(FOnTedsRowSelected, UE::Editor::DataStorage::RowHandle RowHandle);
DECLARE_DELEGATE_RetVal_OneParam(bool, FOnShouldFilterTedsRow, const UE::Editor::DataStorage::RowHandle RowHandle);

/*
* Picking mode for TEDs Scene Outliner Widgets. Based off of FActorPickingMode
*/
class TEDSPROPERTYEDITOR_API FTedsRowPickingMode : public UE::Editor::Outliner::FTedsOutlinerMode
{
public:
	FTedsRowPickingMode(const UE::Editor::Outliner::FTedsOutlinerParams& Params, FOnSceneOutlinerItemPicked OnItemPickedDelegate);

	virtual ~FTedsRowPickingMode() = default;
public:
	virtual void OnItemSelectionChanged(FSceneOutlinerTreeItemPtr Item, ESelectInfo::Type SelectionType, const FSceneOutlinerItemSelection& Selection) override;

	/** Allow the user to commit their selection by pressing enter if it is valid */
	virtual void OnFilterTextCommited(FSceneOutlinerItemSelection& Selection, ETextCommit::Type CommitType) override;

	virtual bool ShowViewButton() const override { return false; }
private:
	FOnSceneOutlinerItemPicked OnItemPicked;
};