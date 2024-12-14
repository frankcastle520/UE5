// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextStateTreeWorkspaceAssetUserData.h"

#include "AnimNextStateTree.h"
#include "StateTreeEditorData.h"
#include "AnimNextStateTreeWorkspaceExports.h"
#include "UObject/AssetRegistryTagsContext.h"

void UAnimNextStateTreeWorkspaceAssetUserData::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	FWorkspaceOutlinerItemExports OutlinerExports;

	if (UAnimNextStateTree* AnimStateTree = CastChecked<UAnimNextStateTree>(GetOuter()))
	{		
		const FName AnimStateTreeIdentifier = AnimStateTree->GetFName();
		
		// StateTree asset export
		UStateTree* StateTree = AnimStateTree->StateTree;
		FWorkspaceOutlinerItemExport& RootAssetExport = OutlinerExports.Exports.Add_GetRef(FWorkspaceOutlinerItemExport(AnimStateTreeIdentifier, GetOuter()));			
		RootAssetExport.GetData().InitializeAsScriptStruct(FAnimNextStateTreeOutlinerData::StaticStruct());
		RootAssetExport.GetData().GetMutable<FAnimNextStateTreeOutlinerData>().Asset = AnimStateTree;

		if (const UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData)) 
		{
			// Export each state as individual item as well (parented according to hierarchy)
			TMap<UStateTreeState*, FWorkspaceOutlinerItemExport> ParentExports;
			EditorData->VisitHierarchy([&OutlinerExports, RootAssetExport, &ParentExports, EditorData](UStateTreeState& State, const UStateTreeState* ParentState) -> EStateTreeVisitor
			{
				FWorkspaceOutlinerItemExport& StateExport = OutlinerExports.Exports.Add_GetRef(
				ParentState == nullptr ? FWorkspaceOutlinerItemExport(State.Name, RootAssetExport)
					: FWorkspaceOutlinerItemExport(State.Name, ParentExports.FindChecked(ParentState))
				);

				ParentExports.Add(&State, StateExport);

				StateExport.GetData().InitializeAsScriptStruct(FAnimNextStateTreeStateOutlinerData::StaticStruct());
				FAnimNextStateTreeStateOutlinerData& AssetData = StateExport.GetData().GetMutable<FAnimNextStateTreeStateOutlinerData>();
				AssetData.StateName = State.Name;
				AssetData.StateId = State.ID;
				AssetData.bIsLeafState = State.Children.IsEmpty(); 
				AssetData.Type = State.Type;
				AssetData.SelectionBehavior = State.SelectionBehavior;

				const FStateTreeEditorColor* StateTreeEditorColor = EditorData->FindColor(State.ColorRef);

				AssetData.Color = StateTreeEditorColor ? FSlateColor(StateTreeEditorColor->Color) : FSlateColor::UseForeground();
				
				return EStateTreeVisitor::Continue;
			});
		}

		{
			FString TagValue;
			FWorkspaceOutlinerItemExports::StaticStruct()->ExportText(TagValue, &OutlinerExports, nullptr, nullptr, PPF_None, nullptr);
			Context.AddTag(UObject::FAssetRegistryTag(UE::Workspace::ExportsWorkspaceItemsRegistryTag, TagValue, UObject::FAssetRegistryTag::TT_Hidden));
		}
	}
}
