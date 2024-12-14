// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SkeletalMeshSkeletonWidget.h"

#include "IContentBrowserSingleton.h"
#include "Elements/Framework/TypedElementAttributeBinding.h"
#include "Internationalization/Text.h"
#include "TedsAssetDataColumns.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/ObjectMacros.h"
#include "Widgets/Input/SHyperlink.h"

namespace UE::SkeletalMeshSkeletonWidget::Private
{
	
	FString GetAssetName(const FString& RawPath)
	{
		return FSoftObjectPath(RawPath).GetAssetName();
	}
	
	void NavigateToAsset(IEditorDataStorageProvider* DataStorage, UE::Editor::DataStorage::RowHandle Row)
	{
		if (FItemStringAttributeColumn_Experimental* SkeletonAttributeColumn =
				DataStorage->GetColumn<FItemStringAttributeColumn_Experimental>(Row, FName("Skeleton")))
		{
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			TArray<FAssetData> Assets;
			Assets.Add(AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(SkeletonAttributeColumn->Value)));
			IContentBrowserSingleton::Get().SyncBrowserToAssets(Assets);
		}
	}
}

void USkeletalMeshSkeletonWidgetFactory::RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage,
														IEditorDataStorageUiProvider& DataStorageUi) const
{
	using namespace UE::Editor::DataStorage::Queries;
	DataStorageUi.RegisterWidgetFactory<FSkeletalMeshSkeletonWidgetConstructor>(TEXT("General.Cell"), TColumn<FItemStringAttributeColumn_Experimental>("Skeleton"));
}

FSkeletalMeshSkeletonWidgetConstructor::FSkeletalMeshSkeletonWidgetConstructor()
	: FSimpleWidgetConstructor(StaticStruct())
{
}

TSharedPtr<SWidget> FSkeletalMeshSkeletonWidgetConstructor::CreateWidget(IEditorDataStorageProvider* DataStorage,
	IEditorDataStorageUiProvider* DataStorageUi, UE::Editor::DataStorage::RowHandle TargetRow, UE::Editor::DataStorage::RowHandle WidgetRow,
	const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	UE::Editor::DataStorage::FAttributeBinder Binder(TargetRow, DataStorage);

	return SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SHyperlink)
				.Text(Binder.BindData(&FItemStringAttributeColumn_Experimental::Value, [](const FString& SkeletonPath)
				{
					return FText::FromString(UE::SkeletalMeshSkeletonWidget::Private::GetAssetName(SkeletonPath));
				}, FString(), FName("Skeleton")))
				.ToolTipText(Binder.BindText(&FItemStringAttributeColumn_Experimental::Value, FName("Skeleton")))
				.OnNavigate_Static(&UE::SkeletalMeshSkeletonWidget::Private::NavigateToAsset, DataStorage, TargetRow)
				.Style(FAppStyle::Get(), "Common.GotoBlueprintHyperlink")
			];
}
