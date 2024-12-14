// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClothAssetFactory.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "Dataflow/DataflowObject.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Misc/PackageName.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ClothAssetFactory)

UChaosClothAssetFactory::UChaosClothAssetFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bEditorImport = true;
	bEditAfterNew = true;
	SupportedClass = UChaosClothAsset::StaticClass();
}

UObject* UChaosClothAssetFactory::FactoryCreateNew(UClass* Class, UObject* Parent, FName Name, EObjectFlags Flags, UObject* /*Context*/, FFeedbackContext* /*Warn*/)
{
	using namespace UE::Chaos::ClothAsset;

	UChaosClothAsset* const ClothAsset = NewObject<UChaosClothAsset>(Parent, Class, Name, Flags | RF_Transactional | RF_Public | RF_Standalone);
	ClothAsset->MarkPackageDirty();

	// Create a new Dataflow asset
	const FString DataflowPath = FPackageName::GetLongPackagePath(ClothAsset->GetOutermost()->GetName());
	const FString ClothAssetName = ClothAsset->GetName();
	const FString DataflowName = FString(TEXT("DF_")) + (ClothAssetName.StartsWith(TEXT("CA_")) ? ClothAssetName.RightChop(3) : ClothAssetName);
	const FString DataflowPackageName = FPaths::Combine(DataflowPath, DataflowName);

	UPackage* const DataflowPackage = CreatePackage(*DataflowPackageName);

	// Load the cloth template into the new Dataflow asset
	UDataflow* const Template = LoadObject<UDataflow>(DataflowPackage, TEXT("/ChaosClothAssetEditor/ClothAssetTemplate.ClothAssetTemplate"));
	UDataflow* const Dataflow = DuplicateObject(Template, DataflowPackage, FName(DataflowName));

	Dataflow->MarkPackageDirty();

	// Notify the asset registry
	FAssetRegistryModule::AssetCreated(Dataflow);

	// Set the Dataflow to the cloth asset
	ClothAsset->SetDataflow(Dataflow);

	return ClothAsset;
}

FString UChaosClothAssetFactory::GetDefaultNewAssetName() const
{
	return FString(TEXT("CA_NewChaosClothAsset"));
}
