// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DetailColumnSizeData.h"
#include "Engine/EngineTypes.h"
#include "IDetailPropertyRow.h"
#include "IDetailTreeNode.h"
#include "MaterialEditor/MaterialEditorInstanceConstant.h"
#include "MaterialPropertyHelpers.h"
#include "Materials/Material.h"
#include "PropertyCustomizationHelpers.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

class IPropertyHandle;
class SMaterialSubstrateTree;
class SMaterialLayersFunctionsInstanceWrapper;
class UDEditorParameterValue;
class UMaterialEditorInstanceConstant;
struct FRecursiveCreateWidgetsContext;

typedef TSharedPtr<FSortedParamData> FSortedParamDataPtr;
class SMaterialSubstrateTree : public STreeView<FSortedParamDataPtr>
{
	friend class SMaterialSubstrateTreeItem;
public:
	SLATE_BEGIN_ARGS(SMaterialSubstrateTree)
		: _InMaterialEditorInstance(nullptr)
		, _InGenerator(nullptr)
	{}

	SLATE_ARGUMENT(UMaterialEditorParameters*, InMaterialEditorInstance)
	SLATE_ARGUMENT(SMaterialLayersFunctionsInstanceWrapper*, InWrapper)
	SLATE_ARGUMENT(TSharedPtr<class IPropertyRowGenerator>, InGenerator)
	SLATE_ARGUMENT(FGetShowHiddenParameters, InShowHiddenDelegate)
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);
	TSharedRef< ITableRow > OnGenerateRowMaterialLayersFunctionsTreeView(TSharedPtr<FSortedParamData> Item, const TSharedRef< STableViewBase >& OwnerTable);
	void OnGetChildrenMaterialLayersFunctionsTreeView(TSharedPtr<FSortedParamData> InParent, TArray< TSharedPtr<FSortedParamData> >& OutChildren);
	void OnExpansionChanged(TSharedPtr<FSortedParamData> Item, bool bIsExpanded);
	void OnSelectionChangedMaterialSubstrateView(TSharedPtr<FSortedParamData> InSelectedItem, ESelectInfo::Type SelectInfo);
	void SetParentsExpansionState();

	TSharedPtr<SWidget> CreateContextMenu();
	void ShowHiddenValues(bool& bShowHiddenParameters) { bShowHiddenParameters = true; }
	TWeakObjectPtr<class UDEditorParameterValue> FunctionParameter;
	struct FMaterialLayersFunctions* FunctionInstance;
	TSharedPtr<IPropertyHandle> FunctionInstanceHandle;
	void RefreshOnAssetChange(const struct FAssetData& InAssetData, int32 InNodeId, EMaterialParameterAssociation MaterialType);
	void ResetAssetToDefault(TSharedPtr<FSortedParamData> InData);
	
	void AddRootNodeLayer() { AddNodeLayer(); }
	void AddNodeLayer(int32 InParent = -1);
	void RemoveNodeLayer(int32 InNodeId);

	FReply UnlinkLayer(int32 Index);
	FReply RelinkLayersToParent();
	EVisibility GetUnlinkLayerVisibility(int32 Index) const;
	EVisibility GetRelinkLayersToParentVisibility() const;
	FReply ToggleLayerVisibility(int32 Index);
	bool IsLayerVisible(int32 Index) const;

	TSharedPtr<class FAssetThumbnailPool> GetTreeThumbnailPool();

	/** Object that stores all of the possible parameters we can edit */
	UMaterialEditorParameters* MaterialEditorInstance;

	/** Builds the custom parameter groups category */
	void CreateGroupsWidget();
	using FNodeId = int32;
	void RecursiveCreateWidgets(struct FRecursiveCreateWidgetsContext* Context, FNodeId InNodeId, TArray<TSharedPtr<FSortedParamData>>& InParentContainer, bool GenerateChildren);


	SMaterialLayersFunctionsInstanceWrapper* GetWrapper() { return Wrapper; }

	TSharedPtr<IDetailTreeNode> FindParameterGroupsNode(TSharedPtr<IPropertyRowGenerator> PropertyRowGenerator);

	TSharedRef<SWidget> CreateThumbnailWidget(EMaterialParameterAssociation InAssociation, int32 InIndex, float InThumbnailSize);
	void UpdateThumbnailMaterial(TEnumAsByte<EMaterialParameterAssociation> InAssociation, int32 InIndex, bool bAlterBlendIndex = false);
	FReply OnThumbnailDoubleClick(const FGeometry& Geometry, const FPointerEvent& MouseEvent, EMaterialParameterAssociation InAssociation, int32 InIndex);
	bool IsOverriddenExpression(class UDEditorParameterValue* Parameter, int32 InIndex);
	bool IsOverriddenExpression(TObjectPtr<UDEditorParameterValue> Parameter, int32 InIndex);
	
	FGetShowHiddenParameters GetShowHiddenDelegate() const;
protected:

	void ShowSubParameters(TSharedPtr<FSortedParamData> ParentParameter);

private:
	TArray<TSharedPtr<FSortedParamData>> LayerProperties;
	
	TArray<FUnsortedParamData> NonLayerProperties;
	
	FDetailColumnSizeData ColumnSizeData;
	
	SMaterialLayersFunctionsInstanceWrapper* Wrapper;
	
	TSharedPtr<class IPropertyRowGenerator> Generator;
	
	bool bLayerIsolated;
	
	/** Delegate to call to determine if hidden parameters should be shown */
	FGetShowHiddenParameters ShowHiddenDelegate;
};

class SMaterialSubstrateTreeItem : public STableRow< FSortedParamDataPtr >, public IDraggableItem
{
public:

	SLATE_BEGIN_ARGS(SMaterialSubstrateTreeItem)
		: _StackParameterData(nullptr),
		_MaterialEditorInstance(nullptr),
		_InTree(nullptr),
		_Padding( FMargin(0) )
	{}

	/** The item content. */
	SLATE_ARGUMENT(FSortedParamDataPtr, StackParameterData)
	SLATE_ARGUMENT(UMaterialEditorParameters*, MaterialEditorInstance)
	SLATE_ARGUMENT(SMaterialSubstrateTree*, InTree)
	SLATE_ATTRIBUTE( FMargin, Padding )
	SLATE_END_ARGS()

	bool bIsBeingDragged = false;

private:
	bool bIsHoveredDragTarget = false;


	FString GetCurvePath(class UDEditorScalarParameterValue* Parameter) const;
	const FSlateBrush* GetBorderImage() const;

	FSlateColor GetOuterBackgroundColor(TSharedPtr<FSortedParamData> InParamData) const;
public:

	void RefreshOnRowChange(const FAssetData& AssetData, SMaterialSubstrateTree* InTree);
	bool GetFilterState(SMaterialSubstrateTree* InTree, TSharedPtr<FSortedParamData> InStackData) const;
	void FilterClicked(const ECheckBoxState NewCheckedState, SMaterialSubstrateTree* InTree, TSharedPtr<FSortedParamData> InStackData);
	ECheckBoxState GetFilterChecked(SMaterialSubstrateTree* InTree, TSharedPtr<FSortedParamData> InStackData) const;
	FText GetLayerName() const;
	FText GetLayerDesc() const;
	void OnNameChanged(const FText& InText, ETextCommit::Type CommitInfo);
	
	FReply ToggleLayerVisibility();
	bool IsLayerVisible() const;

	FReply UnlinkLayer();
	EVisibility GetUnlinkLayerVisibility() const;

	TOptional<EItemDropZone> CanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, FSortedParamDataPtr Item);
	FReply OnAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, FSortedParamDataPtr TargetItem);
	void OnLayerDragEnter(const FDragDropEvent& DragDropEvent) override
	{
		//if (StackParameterData->ParameterInfo.Index != 0)
		{
			bIsHoveredDragTarget = true;
		}
	}

	void OnLayerDragLeave(const FDragDropEvent& DragDropEvent) override
	{
		bIsHoveredDragTarget = false;
	}

	void OnLayerDragDetected() override
	{
		bIsBeingDragged = true;
	}

	FReply OnLayerDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, FSortedParamDataPtr TargetItem);
	void OnOverrideParameter(bool NewValue, class UDEditorParameterValue* Parameter);
	void OnOverrideParameter(bool NewValue, TObjectPtr<UDEditorParameterValue> Parameter);

	/**
	* Construct the widget
	*
	* @param InArgs   A declaration from which to construct the widget
	*/
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView);
	int32 OnPaint( const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const override;
	/** The node info to build the tree view row from. */
	
	FSortedParamDataPtr StackParameterData;

	SMaterialSubstrateTree* Tree;

	UMaterialEditorParameters* MaterialEditorInstance;

	FString GetInstancePath(SMaterialSubstrateTree* InTree) const;
};
