// Copyright Epic Games, Inc. All Rights Reserved.

#include "SVariablesView.h"

#include "AnimNextAssetWorkspaceAssetUserData.h"
#include "AnimNextRigVMAsset.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "SAssetDropTarget.h"
#include "SceneOutlinerPublicTypes.h"
#include "SceneOutlinerSourceControlColumn.h"
#include "ScopedTransaction.h"
#include "SPositiveActionButton.h"
#include "SSceneOutliner.h"
#include "Framework/Commands/GenericCommands.h"
#include "Outliner/VariablesOutlinerColumns.h"
#include "Outliner/VariablesOutlinerMode.h"
#include "Outliner/VariablesOutlinerEntryItem.h"
#include "Variables/SAddVariablesDialog.h"
#include "AnimNextRigVMAsset.h"
#include "IWorkspaceEditor.h"
#include "WorkspaceAssetRegistryInfo.h"
#include "Outliner/VariablesOutlinerAssetItem.h"

#define LOCTEXT_NAMESPACE "SVariablesView"

namespace UE::AnimNext::Editor
{

const FLazyName VariablesTabName("VariablesTab");

void SVariablesOutliner::Construct(const FArguments& InArgs, const FSceneOutlinerInitializationOptions& InitOptions)
{
	AddVariablesButton = SNew(SPositiveActionButton)
			.OnClicked(this, &SVariablesOutliner::HandleAddVariablesClicked)
			.Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
			.Text(LOCTEXT("AddVariablesButton", "Add"))
			.ToolTipText(LOCTEXT("AddVariablesButtonTooltip", "Adds variables to assets.\nIf multiple assets are selected, then variables will be added to each.\nIf no assets are selected and there are multiple assets, variables will be added to all assets."));

	SSceneOutliner::Construct(InArgs, InitOptions);
}

void SVariablesOutliner::CustomAddToToolbar(TSharedPtr<class SHorizontalBox> Toolbar)
{
	Toolbar->AddSlot()
	.VAlign(VAlign_Center)
	.AutoWidth()
	.Padding(4.f, 0.f, 0.f, 0.f)
	[
		AddVariablesButton.ToSharedRef()
	];
}

FReply SVariablesOutliner::HandleAddVariablesClicked()
{
	TArray<UAnimNextRigVMAssetEditorData*> AssetsToAddTo;
	if(Assets.Num() == 1)
	{
		UAnimNextRigVMAsset* Asset = Assets[0].Get();
		if(Asset == nullptr)
		{
			FReply::Unhandled();
		}

		UAnimNextRigVMAssetEditorData* EditorData = UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(Asset);
		if(EditorData == nullptr)
		{
			FReply::Unhandled();
		}

		AssetsToAddTo.Add(EditorData);
	}
	else
	{
		// Add to multiple selected
		TArray<FSceneOutlinerTreeItemPtr> SelectedItems = GetSelectedItems();
		for(const FSceneOutlinerTreeItemPtr& Item : SelectedItems)
		{
			const FVariablesOutlinerAssetItem* AssetItem = Item->CastTo<FVariablesOutlinerAssetItem>();
			if (AssetItem == nullptr)
			{
				continue;
			}

			UAnimNextRigVMAsset* Asset = AssetItem->SoftAsset.Get();
			if(Asset == nullptr)
			{
				continue;
			}

			UAnimNextRigVMAssetEditorData* EditorData = UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(Asset);
			if(EditorData == nullptr)
			{
				continue;
			}
			
			AssetsToAddTo.Add(EditorData);
		}

		// No selected asset items, so add to all asset items
		if(AssetsToAddTo.Num() == 0)
		{
			for(const TSoftObjectPtr<UAnimNextRigVMAsset>& SoftAsset : Assets)
			{
				UAnimNextRigVMAsset* Asset = SoftAsset.Get();
				if(Asset == nullptr)
				{
					continue;
				}

				UAnimNextRigVMAssetEditorData* EditorData = UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(Asset);
				if(EditorData == nullptr)
				{
					continue;
				}

				AssetsToAddTo.Add(EditorData);
			}
		}
	}

	if(AssetsToAddTo.Num() == 0)
	{
		return FReply::Unhandled();
	}

	TSharedRef<SAddVariablesDialog> AddVariablesDialog =
		SNew(SAddVariablesDialog, AssetsToAddTo);

	TArray<FVariableToAdd> VariablesToAdd;
	TArray<FDataInterfaceToAdd> DataInterfacesToAdd;
	if(AddVariablesDialog->ShowModal(VariablesToAdd, DataInterfacesToAdd))
	{
		FScopedTransaction Transaction(FText::Format(LOCTEXT("AddVariablesFormat", "Add {0}|plural(one=variable, other=variables)"), (DataInterfacesToAdd.Num() + VariablesToAdd.Num()) * AssetsToAddTo.Num()));
		for(UAnimNextRigVMAssetEditorData* EditorData : AssetsToAddTo)
		{
			for (const FVariableToAdd& VariableToAdd : VariablesToAdd)
			{
				EditorData->AddVariable(VariableToAdd.Name, VariableToAdd.Type);
			}

			for (const FDataInterfaceToAdd& DataInterfaceToAdd : DataInterfacesToAdd)
			{
				EditorData->AddDataInterface(DataInterfaceToAdd.DataInterface);
			}
		}
	}

	return FReply::Handled();
}

void SVariablesOutliner::SetAssets(TConstArrayView<TSoftObjectPtr<UAnimNextRigVMAsset>> InAssets)
{
	for(const TSoftObjectPtr<UAnimNextRigVMAsset>& CurrentSoftAsset : Assets)
	{
		if(UAnimNextRigVMAsset* CurrentAsset = CurrentSoftAsset.Get())
		{
			UAnimNextRigVMAssetEditorData* EditorData = UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(CurrentAsset);
			if(EditorData != nullptr)
			{
				EditorData->ModifiedDelegate.RemoveAll(this);
			}
		}
	}

	Assets = InAssets;

	for(const TSoftObjectPtr<UAnimNextRigVMAsset>& NewSoftAsset : Assets)
	{
		if(UAnimNextRigVMAsset* NewAsset = NewSoftAsset.Get())
		{
			UAnimNextRigVMAssetEditorData* EditorData = UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(NewAsset);
			if(EditorData != nullptr)
			{
				EditorData->ModifiedDelegate.AddSP(this, &SVariablesOutliner::OnEditorDataModified);
			}
		}
	}

	FullRefresh();
}

void SVariablesOutliner::HandleAssetLoaded(const FSoftObjectPath& InSoftObjectPath, UAnimNextRigVMAsset* InAsset)
{
	if(Assets.Contains(TSoftObjectPtr<UAnimNextRigVMAsset>(InAsset)))
	{
		// Bind for any modification callbacks
		UAnimNextRigVMAssetEditorData* EditorData = UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(InAsset);
		if(EditorData != nullptr)
		{
			EditorData->ModifiedDelegate.AddSP(this, &SVariablesOutliner::OnEditorDataModified);
		}

		FullRefresh();
	}
}

void SVariablesOutliner::OnEditorDataModified(UAnimNextRigVMAssetEditorData* InEditorData, EAnimNextEditorDataNotifType InType, UObject* InSubject)
{
	ensure(Assets.Contains(TSoftObjectPtr<UAnimNextRigVMAsset>(UncookedOnly::FUtils::GetAsset<UAnimNextRigVMAsset>(InEditorData))));

	switch(InType)
	{
	case EAnimNextEditorDataNotifType::UndoRedo:
	case EAnimNextEditorDataNotifType::EntryAdded:
	case EAnimNextEditorDataNotifType::EntryRemoved:
		FullRefresh();
		break;
	default:
		break;
	}
}

bool SVariablesOutliner::IsEnabled() const
{
	return Assets.Num() > 0;
}

void SVariablesView::Construct(const FArguments& InArgs, TSharedRef<UE::Workspace::IWorkspaceEditor> InWorkspaceEditor)
{
	InWorkspaceEditor->OnOutlinerSelectionChanged().AddSP(this, &SVariablesView::HandleWorkspaceOutlinerSelectionChanged);

	FSceneOutlinerInitializationOptions InitOptions;
	InitOptions.OutlinerIdentifier = TEXT("AnimNextVariablesOutliner");
	InitOptions.bShowHeaderRow = true;
	InitOptions.ColumnMap.Add(FSceneOutlinerBuiltInColumnTypes::Label(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 0, FCreateSceneOutlinerColumn(), false, 0.5f));
	InitOptions.ColumnMap.Add(FVariablesOutlinerTypeColumn::GetID(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 10, FCreateSceneOutlinerColumn::CreateLambda([](ISceneOutliner& InSceneOutliner) { return MakeShared<FVariablesOutlinerTypeColumn>(InSceneOutliner); }), false));
	InitOptions.ColumnMap.Add(FVariablesOutlinerValueColumn::GetID(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 20, FCreateSceneOutlinerColumn::CreateLambda([](ISceneOutliner& InSceneOutliner) { return MakeShared<FVariablesOutlinerValueColumn>(InSceneOutliner); }), true, 0.5f));
	InitOptions.ColumnMap.Add(FSceneOutlinerSourceControlColumn::GetID(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 30, FCreateSceneOutlinerColumn::CreateLambda([](ISceneOutliner& InSceneOutliner) { return MakeShared<FSceneOutlinerSourceControlColumn>(InSceneOutliner); }), true));
	InitOptions.ColumnMap.Add(FVariablesOutlinerAccessSpecifierColumn::GetID(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 40, FCreateSceneOutlinerColumn::CreateLambda([](ISceneOutliner& InSceneOutliner) { return MakeShared<FVariablesOutlinerAccessSpecifierColumn>(InSceneOutliner); }), false));
	InitOptions.ModeFactory = FCreateSceneOutlinerMode::CreateLambda([this, WeakWorkspaceEditor=InWorkspaceEditor.ToWeakPtr()](SSceneOutliner* InOutliner) { return new FVariablesOutlinerMode(static_cast<SVariablesOutliner*>(InOutliner), WeakWorkspaceEditor.Pin().ToSharedRef()); });

	VariablesOutliner = SNew(SVariablesOutliner, InitOptions);
	VariablesOutliner->SetEnabled(MakeAttributeSP(VariablesOutliner.Get(), &SVariablesOutliner::IsEnabled));

	TArray<FWorkspaceOutlinerItemExport> SelectedExports;
	InWorkspaceEditor->GetOutlinerSelection(SelectedExports);
	HandleWorkspaceOutlinerSelectionChanged(SelectedExports);

	ChildSlot
	[
		VariablesOutliner.ToSharedRef()
	];
}

void SVariablesView::HandleWorkspaceOutlinerSelectionChanged(TConstArrayView<FWorkspaceOutlinerItemExport> InExports)
{
	TArray<TSoftObjectPtr<UAnimNextRigVMAsset>> Assets;
	TArray<FSoftObjectPath> AssetsToAsyncLoad;
	for(const FWorkspaceOutlinerItemExport& Export : InExports)
	{
		if(Export.GetAssetPath().IsAsset() && Export.GetData().GetScriptStruct()->IsChildOf(FAnimNextRigVMAssetOutlinerData::StaticStruct()))
		{
			Assets.Add(TSoftObjectPtr<UAnimNextRigVMAsset>(Export.GetAssetPath()));
			UAnimNextRigVMAsset* ResolvedAsset = Cast<UAnimNextRigVMAsset>(Export.GetAssetPath().ResolveObject());
			if(ResolvedAsset == nullptr)
			{
				AssetsToAsyncLoad.Add(Export.GetAssetPath());
			}
		}
		else if(Export.GetData().GetScriptStruct()->IsChildOf(FAnimNextAssetEntryOutlinerData::StaticStruct()))
		{
			// TODO: As we are not showing references in the workspace yet, we are just traversing the outer here and showing the containing asset's
			// variables. Eventually we would want to recurse up to the root of the export hierarchy, which will entail changing the signature of
			// FOnOutlinerSelectionChanged to include parent items somehow, or adding parent query functionality to the workspace API
			const FAnimNextAssetEntryOutlinerData& EntryData = Export.GetData().Get<FAnimNextAssetEntryOutlinerData>();
			if(EntryData.Entry)
			{
				if(UAnimNextRigVMAsset* Asset = EntryData.Entry->GetTypedOuter<UAnimNextRigVMAsset>())
				{
					Assets.Add(TSoftObjectPtr<UAnimNextRigVMAsset>(Asset));
				}
			}
		}
	}

	VariablesOutliner->SetAssets(Assets);

	// Try async load any missing assets
	for(const FSoftObjectPath& AssetPath : AssetsToAsyncLoad)
	{
		AssetPath.LoadAsync(FLoadSoftObjectPathAsyncDelegate::CreateLambda([WeakVariablesOutliner = TWeakPtr<SVariablesOutliner>(VariablesOutliner)](const FSoftObjectPath& InSoftObjectPath, UObject* InObject)
		{
			UAnimNextRigVMAsset* Asset = Cast<UAnimNextRigVMAsset>(InObject);
			if(Asset == nullptr)
			{
				return;
			}

			TSharedPtr<SVariablesOutliner> PinnedVariablesOutliner = WeakVariablesOutliner.Pin();
			if(!PinnedVariablesOutliner.IsValid())
			{
				return;
			}

			PinnedVariablesOutliner->HandleAssetLoaded(InSoftObjectPath, Asset);
		}));
	}
}

FAnimNextVariablesTabSummoner::FAnimNextVariablesTabSummoner(TSharedPtr<UE::Workspace::IWorkspaceEditor> InHostingApp)
	: FWorkflowTabFactory(VariablesTabName, StaticCastSharedPtr<FAssetEditorToolkit>(InHostingApp))
{
	TabLabel = LOCTEXT("AnimNextVariablesTabLabel", "Variables");
	TabIcon = FSlateIcon("EditorStyle", "LevelEditor.Tabs.Outliner");
	ViewMenuDescription = LOCTEXT("AnimNextVariablesTabMenuDescription", "Variables");
	ViewMenuTooltip = LOCTEXT("AnimNextVariablesTabToolTip", "Shows the Variables tab.");
	bIsSingleton = true;

	VariablesView = SNew(SVariablesView, InHostingApp.ToSharedRef());

	TArray<FWorkspaceOutlinerItemExport> Exports;
	if(InHostingApp->GetOutlinerSelection(Exports))
	{
		VariablesView->HandleWorkspaceOutlinerSelectionChanged(Exports);
	}
}

TSharedRef<SWidget> FAnimNextVariablesTabSummoner::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	return VariablesView.ToSharedRef();
}

FText FAnimNextVariablesTabSummoner::GetTabToolTipText(const FWorkflowTabSpawnInfo& Info) const
{
	return ViewMenuTooltip;
}

}

#undef LOCTEXT_NAMESPACE