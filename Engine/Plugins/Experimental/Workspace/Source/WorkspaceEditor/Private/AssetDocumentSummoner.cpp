// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDocumentSummoner.h"

#include "WorkspaceEditor.h"
#include "AssetDefinitionRegistry.h"
#include "ClassIconFinder.h"
#include "StructUtils/InstancedStruct.h"
#include "WorkspaceEditorModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SSpacer.h"
#include "WorkspaceDocumentState.h"
#include "SWorkspaceTabWrapper.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "AssetDocumentSummoner"

namespace UE::Workspace
{

FAssetDocumentSummoner::FAssetDocumentSummoner(FName InIdentifier, TSharedPtr<FWorkspaceEditor> InHostingApp, bool bInAllowUnsupportedClasses /*= false*/)
	: FDocumentTabFactoryForObjects<UObject>(InIdentifier, InHostingApp)
	, HostingAppPtr(InHostingApp)
	, bAllowUnsupportedClasses(bInAllowUnsupportedClasses)
{
}

void FAssetDocumentSummoner::SetAllowedClassPaths(TConstArrayView<FTopLevelAssetPath> InAllowedClassPaths)
{
	AllowedClassPaths = InAllowedClassPaths;
}

void FAssetDocumentSummoner::OnTabActivated(TSharedPtr<SDockTab> Tab) const
{
	TSharedRef<SWorkspaceTabWrapper> TabWrapper = StaticCastSharedRef<SWorkspaceTabWrapper>(Tab->GetContent());
	if (UObject* DocumentAsset = TabWrapper->GetDocumentObject().Get())
	{
		HostingAppPtr.Pin()->SetFocussedAsset(DocumentAsset);
	}
}

void FAssetDocumentSummoner::OnTabBackgrounded(TSharedPtr<SDockTab> Tab) const
{
}

void FAssetDocumentSummoner::OnTabRefreshed(TSharedPtr<SDockTab> Tab) const
{
}

void FAssetDocumentSummoner::SaveState(TSharedPtr<SDockTab> Tab, TSharedPtr<FTabPayload> Payload) const
{
	if(TSharedPtr<FWorkspaceEditor> WorkspaceEditor = HostingAppPtr.Pin())
	{
		UObject* Object = Payload->IsValid() ? FTabPayload_UObject::CastChecked<UObject>(Payload) : nullptr;
		if(Object)
		{
			TSharedRef<SWorkspaceTabWrapper> TabWrapper = StaticCastSharedRef<SWorkspaceTabWrapper>(Tab->GetContent());
			FWorkspaceEditorModule& WorkspaceEditorModule = FModuleManager::LoadModuleChecked<FWorkspaceEditorModule>("WorkspaceEditor");
			const FObjectDocumentArgs* DocumentArgs = WorkspaceEditorModule.FindObjectDocumentType(Object);
			if(DocumentArgs && DocumentArgs->OnGetDocumentState.IsBound())
			{
				WorkspaceEditor->RecordDocumentState(DocumentArgs->OnGetDocumentState.Execute(FWorkspaceEditorContext(WorkspaceEditor.ToSharedRef(), Object), TabWrapper->GetContent()));
			}
			else
			{
				WorkspaceEditor->RecordDocumentState(TInstancedStruct<FWorkspaceDocumentState>::Make(Object));
			}
		}
	}
}

TAttribute<FText> FAssetDocumentSummoner::ConstructTabNameForObject(UObject* DocumentID) const
{
	TSharedPtr<FWorkspaceEditor> WorkspaceEditor = HostingAppPtr.Pin();
	if(!WorkspaceEditor.IsValid() || DocumentID == nullptr)
	{
		return LOCTEXT("NoneObjectName", "None");
	}

	FWorkspaceEditorModule& WorkspaceEditorModule = FModuleManager::LoadModuleChecked<FWorkspaceEditorModule>("WorkspaceEditor");
	if(const FObjectDocumentArgs* DocumentArgs = WorkspaceEditorModule.FindObjectDocumentType(DocumentID))
	{
		if(DocumentArgs->OnGetTabName.IsBound())
		{
			return DocumentArgs->OnGetTabName.Execute(FWorkspaceEditorContext(WorkspaceEditor.ToSharedRef(), DocumentID));
		}
	}

	return MakeAttributeLambda([WeakObject = TWeakObjectPtr<UObject>(DocumentID)]()
	{
		if(UObject* Object = WeakObject.Get())
		{
			return FText::FromName(Object->GetFName());
		}

		return LOCTEXT("UnknownObjectName", "Unknown");
	});
}

bool FAssetDocumentSummoner::IsPayloadSupported(TSharedRef<FTabPayload> Payload) const
{
	UObject* Object = Payload->IsValid() ? FTabPayload_UObject::CastChecked<UObject>(Payload) : nullptr;
	if(Object)
	{
		FWorkspaceEditorModule& WorkspaceEditorModule = FModuleManager::LoadModuleChecked<FWorkspaceEditorModule>("WorkspaceEditor");
		if(const FObjectDocumentArgs* DocumentArgs = WorkspaceEditorModule.FindObjectDocumentType(Object))
		{
			return AllowedClassPaths.Contains(Object->GetClass()->GetClassPathName()) || bAllowUnsupportedClasses;
		}
	}

	return false;
}

TAttribute<FText> FAssetDocumentSummoner::ConstructTabLabelSuffix(const FWorkflowTabSpawnInfo& Info) const
{
	UObject* Object = Info.Payload->IsValid() ? FTabPayload_UObject::CastChecked<UObject>(Info.Payload) : nullptr;
	if(Object)
	{
		return MakeAttributeLambda([WeakObject = TWeakObjectPtr<UObject>(Object)]()
		{
			if(UObject* Object = WeakObject.Get())
			{
				if(Object->GetPackage()->IsDirty())
				{
					return LOCTEXT("TabSuffixAsterisk", "*");
				}
			}

			return FText::GetEmpty();
		});
	}

	return FText::GetEmpty();
}

TSharedRef<SWidget> FAssetDocumentSummoner::CreateTabBodyForObject(const FWorkflowTabSpawnInfo& Info, UObject* DocumentID) const
{
	TSharedPtr<SWidget> TabContent = SNullWidget::NullWidget;

	const TSharedPtr<FWorkspaceEditor> WorkspaceEditor = HostingAppPtr.Pin();
	check(WorkspaceEditor.IsValid());
	
	const FWorkspaceEditorModule& WorkspaceEditorModule = FModuleManager::LoadModuleChecked<FWorkspaceEditorModule>("WorkspaceEditor");
	if(const FObjectDocumentArgs* DocumentArgs = WorkspaceEditorModule.FindObjectDocumentType(DocumentID))
	{
		if(DocumentArgs->OnMakeDocumentWidget.IsBound())
		{
			TabContent = DocumentArgs->OnMakeDocumentWidget.Execute(FWorkspaceEditorContext(WorkspaceEditor.ToSharedRef(), DocumentID));
		}
	}

	return SNew(SWorkspaceTabWrapper, Info.TabInfo, WorkspaceEditor, DocumentID)
	[
		TabContent.ToSharedRef()
	];
}

const FSlateBrush* FAssetDocumentSummoner::GetTabIconForObject(const FWorkflowTabSpawnInfo& Info, UObject* DocumentID) const
{
	if(TSharedPtr<FWorkspaceEditor> WorkspaceEditor = HostingAppPtr.Pin())
	{
		FWorkspaceEditorModule& WorkspaceEditorModule = FModuleManager::LoadModuleChecked<FWorkspaceEditorModule>("WorkspaceEditor");
		if(const FObjectDocumentArgs* DocumentArgs = WorkspaceEditorModule.FindObjectDocumentType(DocumentID))
		{
			if(DocumentArgs->OnGetTabIcon.IsBound())
			{
				return DocumentArgs->OnGetTabIcon.Execute(FWorkspaceEditorContext(WorkspaceEditor.ToSharedRef(), DocumentID));
			}
		}

		if (UAssetDefinitionRegistry* AssetDefinitionRegistry = UAssetDefinitionRegistry::Get())
		{
			const FAssetData AssetData(DocumentID);
			if(const UAssetDefinition* AssetDefinition = AssetDefinitionRegistry->GetAssetDefinitionForAsset(AssetData))
			{
				const FSlateBrush* ThumbnailBrush = AssetDefinition->GetThumbnailBrush(AssetData, AssetData.AssetClassPath.GetAssetName());
				if(ThumbnailBrush == nullptr)
				{
					return FClassIconFinder::FindThumbnailForClass(DocumentID->GetClass(), NAME_None);
				}
				return ThumbnailBrush;
			}
		}
	}
	return nullptr;
}

}

#undef LOCTEXT_NAMESPACE