// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextAssetWorkspaceAssetUserData.h"

#include "UncookedOnlyUtils.h"
#include "DataInterface/AnimNextDataInterface.h"
#include "Module/AnimNextModule.h"
#include "Module/AnimNextModule_EditorData.h"
#include "UObject/AssetRegistryTagsContext.h"

void UAnimNextAssetWorkspaceAssetUserData::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	Super::GetAssetRegistryTags(Context);

	FWorkspaceOutlinerItemExports Exports;
	{
		UAnimNextRigVMAsset* Asset = CastChecked<UAnimNextRigVMAsset>(GetOuter());
		const UAnimNextRigVMAssetEditorData* GraphEditorData = UE::AnimNext::UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(Asset);
		{
			FWorkspaceOutlinerItemExport& RootAssetExport = Exports.Exports.Add_GetRef(FWorkspaceOutlinerItemExport(Asset->GetFName(), Asset));

			if(UAnimNextModule* Module = Cast<UAnimNextModule>(Asset))
			{
				RootAssetExport.GetData().InitializeAsScriptStruct(FAnimNextModuleOutlinerData::StaticStruct());
			}
			else if(UAnimNextAnimationGraph* AnimationGraph = Cast<UAnimNextAnimationGraph>(Asset))
			{
				RootAssetExport.GetData().InitializeAsScriptStruct(FAnimNextAnimationGraphOutlinerData::StaticStruct());
			}
			else if(UAnimNextDataInterface* DataInterface = Cast<UAnimNextDataInterface>(Asset))
			{
				RootAssetExport.GetData().InitializeAsScriptStruct(FAnimNextDataInterfaceOutlinerData::StaticStruct());
			}
			else
			{
				RootAssetExport.GetData().InitializeAsScriptStruct(FAnimNextRigVMAssetOutlinerData::StaticStruct());
			}
			FAnimNextRigVMAssetOutlinerData& Data = RootAssetExport.GetData().GetMutable<FAnimNextRigVMAssetOutlinerData>();
			Data.Asset = Asset;
		}
	
		UE::AnimNext::UncookedOnly::FUtils::GetAssetOutlinerItems(GraphEditorData, Exports);
	}
	
	FString TagValue;
	FWorkspaceOutlinerItemExports::StaticStruct()->ExportText(TagValue, &Exports, nullptr, nullptr, PPF_None, nullptr);
	Context.AddTag(FAssetRegistryTag(UE::Workspace::ExportsWorkspaceItemsRegistryTag, TagValue, FAssetRegistryTag::TT_Hidden));
}

