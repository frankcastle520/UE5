// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_NiagaraAssetTagDefinitions.h"

#include "SDetailsDiff.h"

#define LOCTEXT_NAMESPACE "UAssetDefinition_NiagaraAssetTagDefinitions"

UAssetDefinition_NiagaraAssetTagDefinitions::UAssetDefinition_NiagaraAssetTagDefinitions()
{
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_NiagaraAssetTagDefinitions::GetAssetCategories() const
{
	static const auto Categories = { EAssetCategoryPaths::FX / NSLOCTEXT("Niagara", "NiagaraAssetSubMenu_Advanced", "Advanced")};
	return Categories;
}

EAssetCommandResult UAssetDefinition_NiagaraAssetTagDefinitions::PerformAssetDiff(const FAssetDiffArgs& DiffArgs) const
{
	if (DiffArgs.OldAsset == nullptr && DiffArgs.NewAsset == nullptr)
	{
		return EAssetCommandResult::Unhandled;
	}
	
	const TSharedRef<SDetailsDiff> DetailsDiff = SDetailsDiff::CreateDiffWindow(DiffArgs.OldAsset, DiffArgs.NewAsset, DiffArgs.OldRevision, DiffArgs.NewRevision, UNiagaraAssetTagDefinitions::StaticClass());
	// allow users to edit NewAsset if it's a local asset
	if (!FPackageName::IsTempPackage(DiffArgs.NewAsset->GetPackage()->GetName()))
	{
		DetailsDiff->SetOutputObject(DiffArgs.NewAsset);
	}
	return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE
