// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

#include "UI/Utils/DMWidgetSlot.h"

class FDetailColumnSizeData;
class ICustomDetailsViewItem;
class SDMMaterialEditor;
class SDMMaterialSlotLayerView;
class SHorizontalBox;
class SVerticalBox;
class UDMMaterialLayerObject;
class UDMMaterialSlot;
class UDMMaterialValueFloat1;
class UDMTextureSet;
class UMaterialFunctionInterface;
class UTexture;
struct FDMMaterialLayerReference;

class SDMMaterialSlotEditor : public SCompoundWidget
{
	SLATE_DECLARE_WIDGET(SDMMaterialSlotEditor, SCompoundWidget)

	SLATE_BEGIN_ARGS(SDMMaterialSlotEditor) {}
	SLATE_END_ARGS()

public:
	virtual ~SDMMaterialSlotEditor() override;

	void Construct(const FArguments& InArgs, const TSharedRef<SDMMaterialEditor>& InEditorWidget, UDMMaterialSlot* InSlot);

	void ValidateSlots();

	TSharedPtr<SDMMaterialEditor> GetEditorWidget() const;

	UDMMaterialSlot* GetSlot() const;

	/** Actions */
	void ClearSelection();

	bool CanAddNewLayer() const;
	void AddNewLayer();

	bool CanInsertNewLayer() const;
	void InsertNewLayer();

	bool CanCopySelectedLayer() const;
	void CopySelectedLayer();

	bool CanCutSelectedLayer() const;
	void CutSelectedLayer();

	bool CanPasteLayer() const;
	void PasteLayer();

	bool CanDuplicateSelectedLayer() const;
	void DuplicateSelectedLayer();

	bool CanDeleteSelectedLayer() const;
	void DeleteSelectedLayer();

	bool SelectLayer_CanExecute(int32 InIndex) const;
	void SelectLayer_Execute(int32 InIndex);

	bool SetOpacity_CanExecute();
	void SetOpacity_Execute(float InOpacity);

	/** Slots */
	TSharedRef<SDMMaterialSlotLayerView> GetLayerView() const;

	void InvalidateSlotSettings();

	void InvalidateLayerView();

	void InvalidateLayerSettings();

protected:
	TWeakPtr<SDMMaterialEditor> EditorWidgetWeak;
	TWeakObjectPtr<UDMMaterialSlot> MaterialSlotWeak;
	bool bIsDynamic;

	TDMWidgetSlot<SWidget> ContentSlot;
	TDMWidgetSlot<SWidget> SlotSettingsSlot;
	TDMWidgetSlot<SDMMaterialSlotLayerView> LayerViewSlot;
	TDMWidgetSlot<SWidget> LayerSettingsSlot;

	TWeakObjectPtr<UDMMaterialValueFloat1> LayerOpacityValueWeak;
	TSharedPtr<ICustomDetailsViewItem> LayerOpacityItem;

	TSharedRef<SWidget> CreateSlot_Container();

	TSharedRef<SWidget> CreateSlot_SlotSettings();

	TSharedRef<SWidget> CreateSlot_LayerOpacity();

	TSharedRef<SDMMaterialSlotLayerView> CreateSlot_LayerView();

	TSharedRef<SWidget> CreateSlot_LayerSettings();

	void OnSlotLayersUpdated(UDMMaterialSlot* InSlot);

	void OnSlotPropertiesUpdated(UDMMaterialSlot* InSlot);

	void OnLayerSelected(const TSharedRef<SDMMaterialSlotLayerView>& InLayerView, const TSharedPtr<FDMMaterialLayerReference>& InLayerReference);

	FText GetLayerButtonsDescription() const;
	TSharedRef<SWidget> GetLayerButtonsMenuContent();

	bool GetLayerCanAddEffect() const;
	TSharedRef<SWidget> GetLayerEffectsMenuContent();

	bool GetLayerRowsButtonsCanDuplicate() const;
	FReply OnLayerRowButtonsDuplicateClicked();

	bool GetLayerRowsButtonsCanRemove() const;
	FReply OnLayerRowButtonsRemoveClicked();

	/** Drag and drop. */
	bool OnAreAssetsAcceptableForDrop(TArrayView<FAssetData> InAssets);

	void OnAssetsDropped(const FDragDropEvent& InDragDropEvent, TArrayView<FAssetData> InAssets);

	void HandleDrop_Texture(UTexture* InTexture);

	void HandleDrop_CreateTextureSet(const TArray<FAssetData>& InTextureAssets);

	void HandleDrop_TextureSet(UDMTextureSet* InTextureSet);

	void HandleDrop_MaterialFunction(UMaterialFunctionInterface* InMaterialFunction);

	bool IsValidLayerDropForDelete(TSharedPtr<FDragDropOperation> InDragDropOperation);

	bool CanDropLayerForDelete(TSharedPtr<FDragDropOperation> InDragDropOperation);

	FReply OnLayerDroppedForDelete(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent);
};
