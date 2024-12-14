// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once 

#include "ChaosClothAsset/ImportFilePath.h"
#include "Dataflow/DataflowTerminalNode.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Misc/SecureHash.h"
#include "USDImportNode.generated.h"

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5
namespace Dataflow = UE::Dataflow;
#else
namespace UE_DEPRECATED(5.5, "Use UE::Dataflow instead.") Dataflow {}
#endif

/** Import a USD file from a third party garment construction software. */
USTRUCT(Meta = (DataflowCloth, Deprecated = "5.5"))
struct UE_DEPRECATED(5.5, "Use the newer version of this node instead.") FChaosClothAssetUSDImportNode : public FDataflowTerminalNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FChaosClothAssetUSDImportNode, "USDImport", "Cloth", "Cloth USD Import")
	DATAFLOW_NODE_RENDER_TYPE("SurfaceRender", FName("FClothCollection"), "Collection")

public:
	UPROPERTY(meta = (DataflowOutput))
	FManagedArrayCollection Collection;

	/** Name of the USD file to import. */
	UPROPERTY(EditAnywhere, Category = "USD Import", Meta = (DisplayName = "USD File"))
	FChaosClothAssetImportFilePath UsdFile;

	FChaosClothAssetUSDImportNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

private:
	friend struct FChaosClothAssetUSDImportNode_v2;  // For ImportFromFile

	static bool ImportFromFile(
		const FString& UsdPath,
		const FString& AssetPath,
		const bool bImportSimMesh,
		const TSharedRef<FManagedArrayCollection>&OutClothCollection,
		FString& OutPackagePath,
		class FText& OutErrorText);

	//~ Begin FDataflowNode interface
	virtual void SetAssetValue(TObjectPtr<UObject> Asset, UE::Dataflow::FContext& Context) const override;
	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;
	virtual void Serialize(FArchive& Archive) override;
	//~ End FDataflowNode interface

	bool ImportFromCache(const TSharedRef<FManagedArrayCollection>& ClothCollection, class FText& OutErrorText) const;
	void UpdateImportedAssets();

	/** Content folder where all the USD assets are imported. */
	UPROPERTY(VisibleAnywhere, Category = "USD Import")
	FString PackagePath;

	/** List of all the dependent assets created from the USD import process. */
	UPROPERTY(VisibleAnywhere, Category = "USD Import")
	TArray<TObjectPtr<UObject>> ImportedAssets;

	FMD5Hash FileHash;
	FManagedArrayCollection CollectionCache;  // Content cache for data that hasn't got a USD schema yet
};
