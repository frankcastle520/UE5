// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/USDImportNode_v2.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothDataflowTools.h"
#include "ChaosClothAsset/ClothGeometryTools.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "ChaosClothAsset/USDImportNode.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dataflow/DataflowInputOutput.h"
#include "Dataflow/DataflowGraph.h"
#include "Dataflow/DataflowObject.h"
#include "Engine/StaticMesh.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "UnrealUSDWrapper.h"
#include "USDConversionUtils.h"
#include "USDProjectSettings.h"
#include "USDStageImportContext.h"
#include "USDStageImporter.h"
#include "USDStageImportOptions.h"
#include "USDTypesConversion.h"
#include "USDValueConversion.h"
#include "Chaos/CollectionPropertyFacade.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdRelationship.h"
#include "UsdWrappers/VtValue.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(USDImportNode_v2)

#define LOCTEXT_NAMESPACE "ChaosClothAssetUSDImportNode_v2"

namespace UE::Chaos::ClothAsset::Private
{
	static TArray<FSoftObjectPath> UsdClothOverrideMaterials_v2 {
		FSoftObjectPath(TEXT("/ChaosClothAsset/Materials/USDImportMaterial.USDImportMaterial")),
		FSoftObjectPath(TEXT("/ChaosClothAsset/Materials/USDImportTranslucentMaterial.USDImportTranslucentMaterial")),
		FSoftObjectPath(TEXT("/ChaosClothAsset/Materials/USDImportTwoSidedMaterial.USDImportTwoSidedMaterial")),
		FSoftObjectPath(TEXT("/ChaosClothAsset/Materials/USDImportTranslucentTwoSidedMaterial.USDImportTranslucentTwoSidedMaterial")),
		FSoftObjectPath(TEXT("/ChaosClothAsset/Materials/USDImportDisplayColorMaterial.USDImportDisplayColorMaterial")),
	};
	
	static void OverrideUsdImportMaterials_v2(const TArray<FSoftObjectPath>& Materials, TArray<FSoftObjectPath>* SavedValues = nullptr)
	{
		if (UUsdProjectSettings* UsdProjectSettings = GetMutableDefault<UUsdProjectSettings>())
		{
			// Check to see if we should save the existing values
			if (SavedValues)
			{
				SavedValues->Push(UsdProjectSettings->ReferencePreviewSurfaceMaterial);
				SavedValues->Push(UsdProjectSettings->ReferencePreviewSurfaceTranslucentMaterial);
				SavedValues->Push(UsdProjectSettings->ReferencePreviewSurfaceTwoSidedMaterial);
				SavedValues->Push(UsdProjectSettings->ReferencePreviewSurfaceTranslucentTwoSidedMaterial);
				SavedValues->Push(UsdProjectSettings->ReferenceDisplayColorMaterial);
			}
			UsdProjectSettings->ReferencePreviewSurfaceMaterial = Materials[0];
			UsdProjectSettings->ReferencePreviewSurfaceTranslucentMaterial = Materials[1];
			UsdProjectSettings->ReferencePreviewSurfaceTwoSidedMaterial = Materials[2];
			UsdProjectSettings->ReferencePreviewSurfaceTranslucentTwoSidedMaterial = Materials[3];
			UsdProjectSettings->ReferenceDisplayColorMaterial = Materials[4];
		}
	}

	static void ImportStaticMeshesFromUsdStage(const UE::FUsdStage& UsdStage, const FString& UsdFilePath, const FString& PackagePath)
	{
		// Import recognised assets
		FUsdStageImportContext ImportContext;

		const TObjectPtr<UUsdStageImportOptions>& ImportOptions = ImportContext.ImportOptions;
		{
			check(ImportOptions);
			// Data to import
			ImportOptions->bImportActors = false;
			ImportOptions->bImportGeometry = true;
			ImportOptions->bImportSkeletalAnimations = false;
			ImportOptions->bImportLevelSequences = false;
			ImportOptions->bImportMaterials = true;
			ImportOptions->bImportGroomAssets = false;
			ImportOptions->bImportOnlyUsedMaterials = true;
			// Prims to import
			ImportOptions->PrimsToImport = TArray<FString>{ TEXT("/") };
			// USD options
			ImportOptions->PurposesToImport = (int32)(EUsdPurpose::Render | EUsdPurpose::Guide);
			ImportOptions->NaniteTriangleThreshold = TNumericLimits<int32>::Max();  // Don't enable Nanite
			ImportOptions->RenderContextToImport = NAME_None;
			ImportOptions->MaterialPurpose = NAME_None;  // *UnrealIdentifiers::MaterialPreviewPurpose ???
			ImportOptions->RootMotionHandling = EUsdRootMotionHandling::NoAdditionalRootMotion;
			ImportOptions->SubdivisionLevel = 0;
			ImportOptions->bOverrideStageOptions = false;
			ImportOptions->bImportAtSpecificTimeCode = false;
			ImportOptions->ImportTimeCode = 0.f;
			// Groom
			ImportOptions->GroomInterpolationSettings = TArray<FHairGroupsInterpolation>();
			// Collision
			ImportOptions->ExistingActorPolicy = EReplaceActorPolicy::Replace;
			ImportOptions->ExistingAssetPolicy = EReplaceAssetPolicy::Replace;
			// Processing
			ImportOptions->bPrimPathFolderStructure = false;
			ImportOptions->KindsToCollapse = (int32)EUsdDefaultKind::Component;
			ImportOptions->bMergeIdenticalMaterialSlots = true;
			ImportOptions->bInterpretLODs = false;
		}

		constexpr bool bIsAutomated = true;
		constexpr bool bIsReimport = false;
		constexpr bool bAllowActorImport = false;

		ImportContext.Stage = UsdStage;  // Set the stage first to prevent re-opening it in the Init function
		ImportContext.Init(TEXT(""), UsdFilePath, PackagePath, RF_NoFlags, bIsAutomated, bIsReimport, bAllowActorImport);

		TArray<FSoftObjectPath> OriginalUsdMaterials;
		// Override the project settings to point the USD importer to cloth specific parent materials.
		// This is because we want the materials to import into UEFN and the default USD ones
		// use operations that are not allowed.
		OverrideUsdImportMaterials_v2(UsdClothOverrideMaterials_v2, &OriginalUsdMaterials);

		UUsdStageImporter UsdStageImporter;
		UsdStageImporter.ImportFromFile(ImportContext);

		// Restore Original USD Materials
		OverrideUsdImportMaterials_v2(OriginalUsdMaterials);
	}

	static UE::FUsdPrim FindClothPrim(const UE::FUsdPrim& RootPrim)
	{
		for (UE::FUsdPrim& ChildPrim : RootPrim.GetChildren())
		{
			static const FName ClothRootAPI(TEXT("ClothRootAPI"));
			if (ChildPrim.HasAPI(ClothRootAPI))
			{
				return ChildPrim;
			}
		}
		return UE::FUsdPrim();
	}

	static UE::FUsdPrim FindSimMeshPrim(const UE::FUsdPrim& ClothPrim)
	{
		for (UE::FUsdPrim& ClothChildPrim : ClothPrim.GetChildren())
		{
			if (ClothChildPrim.IsA(TEXT("Mesh")))
			{
				static const FName SimMeshDataAPI(TEXT("SimMeshDataAPI"));
				if (ClothChildPrim.HasAPI(SimMeshDataAPI))
				{
					// Check that the sim mesh has at least one valid geomsubset patern
					for (UE::FUsdPrim& SimMeshChildPrim : ClothChildPrim.GetChildren())
					{
						static const FName SimPatternAPI(TEXT("SimPatternAPI"));
						if (SimMeshChildPrim.IsA(TEXT("GeomSubset")) && SimMeshChildPrim.HasAPI(SimPatternAPI))
						{
							return ClothChildPrim;
						}
					}
				}
			}
		}
		return UE::FUsdPrim();
	}

	static UE::FUsdPrim FindRenderMeshPrim(const UE::FUsdPrim& ClothPrim)
	{
		for (UE::FUsdPrim& ClothChildPrim : ClothPrim.GetChildren())
		{
			if (ClothChildPrim.IsA(TEXT("Mesh")))
			{
				// Look for all GeomSubsets to see if this is a suitable render mesh prim
				bool bIsRenderMesh = false;
				for (UE::FUsdPrim& RenderMeshChildPrim : ClothChildPrim.GetChildren())
				{
					static const FName RenderPatternAPI(TEXT("RenderPatternAPI"));
					if (RenderMeshChildPrim.IsA(TEXT("GeomSubset")) && RenderMeshChildPrim.HasAPI(RenderPatternAPI))
					{
						return ClothChildPrim;
					}
				}
			}
		}
		return UE::FUsdPrim();
	}

	static FVector2f GetSimMeshUVScale(const UE::FUsdPrim& SimMeshPrim)
	{
		FVector2f UVScale(1.f);
		const UE::FUsdAttribute RestPositionScaleAttr = SimMeshPrim.GetAttribute(TEXT("restPositionScale"));
		if (RestPositionScaleAttr.HasValue() && RestPositionScaleAttr.GetTypeName() == TEXT("float2"))
		{
			UE::FVtValue Value;
			RestPositionScaleAttr.Get(Value);
			UsdUtils::FConvertedVtValue ConvertedVtValue;
			if (UsdToUnreal::ConvertValue(Value, ConvertedVtValue) &&
				!ConvertedVtValue.bIsArrayValued && !ConvertedVtValue.bIsEmpty &&
				ConvertedVtValue.Entries.Num() == 1 && ConvertedVtValue.Entries[0].Num() == 2 && ConvertedVtValue.Entries[0][0].IsType<float>())
			{
				UVScale = FVector2f(
					ConvertedVtValue.Entries[0][0].Get<float>(),
					ConvertedVtValue.Entries[0][1].Get<float>());
			}
		}
		return UVScale;
	}
}  // End namespace Private

FChaosClothAssetUSDImportNode_v2::FChaosClothAssetUSDImportNode_v2(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
	, UsdFile(
		FDataflowFunctionProperty::FDelegate::CreateLambda([this, OwningObject = InParam.OwningObject](UE::Dataflow::FContext& /*Context*/)
			{
				const FString AssetPath = OwningObject ? OwningObject->GetPackage()->GetPathName() : FString();
				FText ErrorText;
				if (!ImportUsdFile_Schemaless(UsdFile.FilePath, AssetPath, ErrorText) &&
					!ImportUsdFile(UsdFile.FilePath, AssetPath, ErrorText))
				{
					UE::Chaos::ClothAsset::FClothDataflowTools::LogAndToastWarning(*this,
						LOCTEXT("FailedToImportUsdFileHeadline", "Failed to import USD file from file."),
						FText::Format(LOCTEXT("FailedToImportUsdDetails", "Error while importing USD cloth from file '{0}':\n{1}"), FText::FromString(UsdFile.FilePath), ErrorText));
				}
			}))
	, ReimportUsdFile(
		FDataflowFunctionProperty::FDelegate::CreateLambda([this](UE::Dataflow::FContext& Context)
			{
				UsdFile.Execute(Context);
			}))
	, ReloadSimStaticMesh(
		FDataflowFunctionProperty::FDelegate::CreateLambda([this](UE::Dataflow::FContext& /*Context*/)
			{
				const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>(MoveTemp(Collection));
				FText ErrorText;
				if (!ImportSimStaticMesh(ClothCollection, ErrorText))
				{
					UE::Chaos::ClothAsset::FClothDataflowTools::LogAndToastWarning(*this,
						LOCTEXT("FailedToImportSimMeshHeadline", "Failed to reload the simulation static mesh."),
						FText::Format(LOCTEXT("FailedToImportSimMeshDetails", "Error while re-importing the simulation mesh from static mesh '{0}':\n{1}"), FText::FromString(ImportedSimStaticMesh->GetName()), ErrorText));
				}
				Collection = MoveTemp(*ClothCollection);
			}))
	, ReloadRenderStaticMesh(
		FDataflowFunctionProperty::FDelegate::CreateLambda([this](UE::Dataflow::FContext& /*Context*/)
			{
				const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>(MoveTemp(Collection));
				FText ErrorText;
				if (!ImportRenderStaticMesh(ClothCollection, ErrorText))
				{
					UE::Chaos::ClothAsset::FClothDataflowTools::LogAndToastWarning(*this,
						LOCTEXT("FailedToImportRenderMeshHeadline", "Failed to reload the render static mesh."),
						FText::Format(LOCTEXT("FailedToImportRenderMeshDetails", "Error while re-importing the render mesh from static mesh '{0}':\n{1}"), FText::FromString(ImportedRenderStaticMesh->GetName()), ErrorText));
				}
				Collection = MoveTemp(*ClothCollection);
			}))
{
	using namespace UE::Chaos::ClothAsset;

	// Initialize to a valid collection
	const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>(MoveTemp(Collection));
	FCollectionClothFacade(ClothCollection).DefineSchema();
	Collection = MoveTemp(*ClothCollection);

	// Register connections
	RegisterOutputConnection(&Collection);
}

void FChaosClothAssetUSDImportNode_v2::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	using namespace UE::Chaos::ClothAsset;

	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		SetValue(Context, Collection, &Collection);
	}
}

void FChaosClothAssetUSDImportNode_v2::Serialize(FArchive& Ar)
{
	using namespace UE::Chaos::ClothAsset;

	if (Ar.IsLoading() && !Ar.IsTransacting())
	{
		// Make sure to always have a valid cloth collection on reload, some new attributes could be missing from the cached collection
		// Must be executed before ImportRenderStaticMesh below, and after serializing the collection above, and even if the serialized version hasn't changed
		const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>(MoveTemp(Collection));
		FCollectionClothFacade ClothFacade(ClothCollection);
		if (!ClothFacade.IsValid())
		{
			ClothFacade.DefineSchema();
		}
		Collection = MoveTemp(*ClothCollection);
	}
}

// V1 of the USD importer (schemaless)
bool FChaosClothAssetUSDImportNode_v2::ImportUsdFile_Schemaless(const FString& UsdFilePath, const FString& AssetPath, FText& OutErrorText)
{
	using namespace UE::Chaos::ClothAsset;

	ImportedRenderStaticMesh = nullptr;
	ImportedSimStaticMesh = nullptr;
	ImportedUVScale = { 1.f, 1.f };
	ImportedAssets.Reset();

	// Temporary borrow the collection to make the shared ref
	const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>(MoveTemp(Collection));
	ON_SCOPE_EXIT{ Collection = MoveTemp(*ClothCollection); };

	const float NumSteps = bImportRenderMesh ? 2.f : 1.f;
	FScopedSlowTask SlowTask(NumSteps, LOCTEXT("ImportingUSDFile", "Importing USD file..."));

	SlowTask.EnterProgressFrame(1.f, LOCTEXT("CreatingAssets", "Creating assets and importing simulation mesh..."));
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FChaosClothAssetUSDImportNode::ImportFromFile(UsdFilePath, AssetPath, bImportSimMesh, ClothCollection, PackagePath, OutErrorText);
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	static const FString SchemalessSimStaticMeshName = TEXT("");
	static const FString SchemalessRenderStaticMeshName = TEXT("SM_Mesh");
	UpdateImportedAssets(SchemalessSimStaticMeshName, SchemalessRenderStaticMeshName);

	// Add the render mesh to the collection, since it wasn't originally cached in the collection in the first importer
	if (bImportRenderMesh)
	{
		SlowTask.EnterProgressFrame(1.f, LOCTEXT("ImportingRenderMesh", "Importing render mesh..."));
		if (!ImportRenderStaticMesh(ClothCollection, OutErrorText))
		{
			return false;
		}
	}
	return true;
}

// V2 of the USD importer (using cloth schema)
bool FChaosClothAssetUSDImportNode_v2::ImportUsdFile(const FString& UsdFilePath, const FString& AssetPath, FText& OutErrorText)
{
#if USE_USD_SDK
	using namespace UE::Chaos::ClothAsset;

	// Reset collection
	Collection.Reset();
	PackagePath = FString();
	ImportedRenderStaticMesh = nullptr;
	ImportedSimStaticMesh = nullptr;
	ImportedUVScale = { 1.f, 1.f };
	ImportedAssets.Reset();

	// Temporary borrow the collection to make the shared ref
	const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>(MoveTemp(Collection));
	ON_SCOPE_EXIT{ Collection = MoveTemp(*ClothCollection); };

	FCollectionClothFacade ClothFacade(ClothCollection);
	ClothFacade.DefineSchema();

	// Empty file
	if (UsdFilePath.IsEmpty())
	{
		return true;
	}

	// Start slow task
	const float NumSteps = 1.f + (bImportSimMesh ? bImportRenderMesh ? 2.f : 1.f : bImportRenderMesh ? 1.f : 0.f);
	FScopedSlowTask SlowTask(NumSteps, LOCTEXT("ImportingUSDFile", "Importing USD file..."));
	SlowTask.MakeDialogDelayed(1.f);

	// Open stage
	constexpr bool bUseStageCache = false;  // Reload from disk, not from cache
	constexpr EUsdInitialLoadSet UsdInitialLoadSet = EUsdInitialLoadSet::LoadAll;  // TODO: Ideally we should only use LoadNone to start with and load what's needed once the Schema is defined

	const UE::FUsdStage UsdStage = UnrealUSDWrapper::OpenStage(*UsdFilePath, UsdInitialLoadSet, bUseStageCache);
	if (!UsdStage)
	{
		OutErrorText = LOCTEXT("CantCreateNewStage", "Failed to open the specified USD file.");
		return false;
	}

	// Find the cloth prim
	const UE::FUsdPrim ClothPrim = Private::FindClothPrim(UsdStage.GetPseudoRoot());
	if (!ClothPrim)
	{
		OutErrorText = LOCTEXT("CantFindClothRootAPI", "Can't find a cloth root inside the specified USD file.");
		return false;
	}

	// Find SimMesh and Render Mesh prims
	const UE::FUsdPrim SimMeshPrim = Private::FindSimMeshPrim(ClothPrim);
	const UE::FUsdPrim RenderMeshPrim = Private::FindRenderMeshPrim(ClothPrim);
	if (!SimMeshPrim && !RenderMeshPrim)
	{
		OutErrorText = LOCTEXT("CantFindMeshPrims", "Can't find a sim mesh or render mesh prim with valid pattern data.");
		return false;
	}

	// Read UVScale attribute
	ImportedUVScale = Private::GetSimMeshUVScale(SimMeshPrim);

	// Update import location
	const uint32 UsdPathHash = GetTypeHash(UsdFile.FilePath);  // Path hash to store all import from the same file/same path to the same content folder
	const FString UsdFileName = SlugStringForValidName(FPaths::GetBaseFilename(UsdFile.FilePath));
	const FString PackageName = FString::Printf(TEXT("%s_%08X"), *UsdFileName, UsdPathHash);
	PackagePath = FPaths::Combine(AssetPath + TEXT("_Import"), PackageName);

	// Import the stage
	SlowTask.EnterProgressFrame(1.f);
	Private::ImportStaticMeshesFromUsdStage(UsdStage, UsdFilePath, PackagePath);

	// Fill up the asset list from the imported USD assets
	const FString SimMeshName = SimMeshPrim ? FString::Printf(TEXT("SM_%s"), *SimMeshPrim.GetName().ToString()) : FString();
	const FString RenderMeshName = RenderMeshPrim ? FString::Printf(TEXT("SM_%s"), *RenderMeshPrim.GetName().ToString()) : FString();
	UpdateImportedAssets(SimMeshName, RenderMeshName);

	// Import sim mesh from the static mesh
	if (bImportSimMesh)
	{
		SlowTask.EnterProgressFrame(1.f);
		if (!ImportSimStaticMesh(ClothCollection, OutErrorText))
		{
			return false;
		}
	}

	// Import render mesh from the static mesh
	if (bImportRenderMesh)
	{
		SlowTask.EnterProgressFrame(1.f);
		if (!ImportRenderStaticMesh(ClothCollection, OutErrorText))
		{
			return false;
		}
	}

	return true;

#else  // #if USE_USD_SDK

	OutErrorText = LOCTEXT("NoUsdSdk", "The ChaosClothAssetDataflowNodes module has been compiled without the USD SDK enabled.");
	return false;

#endif  // #else #if USE_USD_SDK
}

void FChaosClothAssetUSDImportNode_v2::UpdateImportedAssets(const FString& SimMeshName, const FString& RenderMeshName)
{
	ImportedSimStaticMesh = nullptr;
	ImportedRenderStaticMesh = nullptr;
	ImportedAssets.Reset();

	if (!PackagePath.IsEmpty())
	{
		TArray<FAssetData> AssetData;

		const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

		constexpr bool bRecursive = true;
		constexpr bool bIncludeOnlyOnDiskAssets = false;
		AssetRegistryModule.Get().GetAssetsByPath(FName(*PackagePath), AssetData, bRecursive, bIncludeOnlyOnDiskAssets);

		ImportedAssets.Reserve(AssetData.Num());

		// Find sim mesh and render mesh (static meshes) dependencies
		for (const FAssetData& AssetDatum : AssetData)
		{
			if (AssetDatum.IsUAsset() && AssetDatum.IsTopLevelAsset() && AssetDatum.GetClass() == UStaticMesh::StaticClass())  // IsUAsset returns false for redirects
			{
				if (AssetDatum.AssetName == SimMeshName)
				{
					ImportedSimStaticMesh = CastChecked<UStaticMesh>(AssetDatum.GetAsset());
					UE_LOG(LogChaosClothAssetDataflowNodes, Display, TEXT("Imported USD Sim Mesh %s, path: %s"), *AssetDatum.AssetName.ToString(), *AssetDatum.GetFullName());
				}
				else if (AssetDatum.AssetName == RenderMeshName)
				{
					ImportedRenderStaticMesh = CastChecked<UStaticMesh>(AssetDatum.GetAsset());
					UE_LOG(LogChaosClothAssetDataflowNodes, Display, TEXT("Imported USD Render Mesh %s, path: %s"), *AssetDatum.AssetName.ToString(), *AssetDatum.GetFullName());
				}
			}
		}

		// Find all other dependencies
		auto AddImportedAssetDependencies = [this, &AssetRegistryModule, &AssetData](const UObject* Object)
			{
				TQueue<const UObject*> ObjectsToVisit;
				ObjectsToVisit.Enqueue(Object);

				const UObject* VisitedObject;
				while (ObjectsToVisit.Dequeue(VisitedObject))
				{
					const FName PackageName(VisitedObject->GetPackage()->GetName());
					TArray<FName> Dependencies;
					AssetRegistryModule.Get().GetDependencies(PackageName, Dependencies);

					UE_LOG(LogChaosClothAssetDataflowNodes, Verbose, TEXT("Dependencies for Object %s - %s:"), *Object->GetName(), *PackageName.ToString());
					for (const FName& Dependency : Dependencies)
					{
						// Only add depedencies the USD import has created
						if (AssetData.FindByPredicate([&Dependency](const FAssetData& AssetDatum) { return AssetDatum.PackageName == Dependency; }))
						{
							UE_LOG(LogChaosClothAssetDataflowNodes, Verbose, TEXT("Found %s"), *Dependency.ToString());
							TArray<FAssetData> DependencyAssetData;
							AssetRegistryModule.Get().GetAssetsByPackageName(Dependency, DependencyAssetData);

							for (const FAssetData& DependencyAssetDatum : DependencyAssetData)
							{
								if (DependencyAssetDatum.IsUAsset() && DependencyAssetDatum.IsTopLevelAsset())  // IsUAsset returns false for redirects
								{
									const int32 Index = ImportedAssets.Emplace(DependencyAssetDatum.GetAsset());  // GetAsset does not handle redirects

									ObjectsToVisit.Enqueue(ImportedAssets[Index]);  // Visit this asset too
								}
							}
						}
					}
				}
			};

		if (ImportedSimStaticMesh)
		{
			AddImportedAssetDependencies(ImportedSimStaticMesh);
		}
		if (ImportedRenderStaticMesh)
		{
			AddImportedAssetDependencies(ImportedRenderStaticMesh);
		}

		for (const UObject* const Asset : ImportedAssets)
		{
			UE_LOG(LogChaosClothAssetDataflowNodes,
				Display,
				TEXT("Imported USD Object %s of type %s, path: %s"),
				*Asset->GetName(),
				*Asset->GetClass()->GetName(),
				*Asset->GetPathName());
		}
	}
}

bool FChaosClothAssetUSDImportNode_v2::ImportSimStaticMesh(const TSharedRef<FManagedArrayCollection> ClothCollection, FText& OutErrorText)
{
	using namespace UE::Chaos::ClothAsset;

	FCollectionClothFacade ClothFacade(ClothCollection);
	check(ClothFacade.IsValid());  // The Cloth Collection schema must be valid at this point

	// Empty the current sim mesh
	FClothGeometryTools::DeleteSimMesh(ClothCollection);

	if (!ImportedSimStaticMesh)
	{
		return true;  // Nothing to import
	}

	// Init the static mesh attributes
	constexpr int32 LODIndex = 0;
	const FMeshDescription* const MeshDescription = ImportedSimStaticMesh->GetMeshDescription(LODIndex);
	check(MeshDescription);
	const FStaticMeshConstAttributes StaticMeshAttributes(*MeshDescription);

	if (!StaticMeshAttributes.GetVertexInstanceUVs().GetNumChannels())
	{
		OutErrorText = LOCTEXT("CantFindUVs", "Missing UV layer to initialize sim mesh data.");
		return false;
	}

	TArray<FVector2f> RestPositions2D;
	TArray<FVector3f> DrapedPositions3D;
	TArray<FIntVector3> TriangleToVertexIndex;

	// Retrieve 3D drapped positions
	DrapedPositions3D = StaticMeshAttributes.GetVertexPositions().GetRawArray();

	// Retrieve triangle indices and 2D rest positions
	RestPositions2D.SetNumZeroed(DrapedPositions3D.Num());

	const TConstArrayView<FVertexID> VertexInstanceVertexIndices = StaticMeshAttributes.GetVertexInstanceVertexIndices().GetRawArray();
	const TConstArrayView<FVertexInstanceID> TriangleVertexInstanceIndices = StaticMeshAttributes.GetTriangleVertexInstanceIndices().GetRawArray();
	const TConstArrayView<FVector2f> VertexInstanceUVs = StaticMeshAttributes.GetVertexInstanceUVs().GetRawArray();

	check(TriangleVertexInstanceIndices.Num() % 3 == 0)
		TriangleToVertexIndex.SetNumUninitialized(TriangleVertexInstanceIndices.Num() / 3);

	auto SetRestPositions2D = [&RestPositions2D, &VertexInstanceUVs](FVertexID VertexID, FVertexInstanceID VertexInstanceID) -> bool
		{
			if (RestPositions2D[VertexID] == FVector2f::Zero())
			{
				RestPositions2D[VertexID] = VertexInstanceUVs[VertexInstanceID];
			}
			else if (!RestPositions2D[VertexID].Equals(VertexInstanceUVs[VertexInstanceID]))
			{
				return false;
			}
			return true;
		};

	for (int32 TriangleIndex = 0; TriangleIndex < TriangleToVertexIndex.Num(); ++TriangleIndex)
	{
		const FVertexInstanceID VertexInstanceID0 = TriangleVertexInstanceIndices[TriangleIndex * 3];
		const FVertexInstanceID VertexInstanceID1 = TriangleVertexInstanceIndices[TriangleIndex * 3 + 1];
		const FVertexInstanceID VertexInstanceID2 = TriangleVertexInstanceIndices[TriangleIndex * 3 + 2];

		const FVertexID VertexID0 = VertexInstanceVertexIndices[VertexInstanceID0];
		const FVertexID VertexID1 = VertexInstanceVertexIndices[VertexInstanceID1];
		const FVertexID VertexID2 = VertexInstanceVertexIndices[VertexInstanceID2];

		TriangleToVertexIndex[TriangleIndex] = FIntVector3(VertexID0, VertexID1, VertexID2);

		if (!SetRestPositions2D(VertexID0, VertexInstanceID0) ||
			!SetRestPositions2D(VertexID1, VertexInstanceID1) ||
			!SetRestPositions2D(VertexID2, VertexInstanceID2))
		{
			OutErrorText = LOCTEXT("UsdSimMeshWelded", "The sim mesh has already been welded. This importer needs an unwelded sim mesh.");
			// TODO: unweld vertices, generate seams(?), and reindex all constraints
			return false;
		}
	}

	// Rescale the 2D mesh with the UV scale
	for (FVector2f& Pos : RestPositions2D)
	{
		Pos *= ImportedUVScale;
	}

	// Save pattern to the collection cache
	check(RestPositions2D.Num() == DrapedPositions3D.Num());
	const int32 VertexCount = RestPositions2D.Num();
	const int32 TriangleCount = TriangleToVertexIndex.Num();

	if (TriangleCount && VertexCount)
	{
		// Remove degenerated triangles
		TArray<FIntVector3> OutTriangleToVertexIndex;
		TArray<FVector2f> OutRestPositions2D;
		TArray<FVector3f> OutDrapedPositions3D;
		TArray<int32> OutIndices;

		bool bHasRepairedTriangles = FClothDataflowTools::RemoveDegenerateTriangles(
			TriangleToVertexIndex,
			RestPositions2D,
			DrapedPositions3D,
			OutTriangleToVertexIndex,
			OutRestPositions2D,
			OutDrapedPositions3D,
			OutIndices);

		// Remove duplicate triangles
		bHasRepairedTriangles = FClothDataflowTools::RemoveDuplicateTriangles(OutTriangleToVertexIndex) || bHasRepairedTriangles;

		// Add the new pattern
		const int32 SimPatternIndex = ClothFacade.AddSimPattern();
		FCollectionClothSimPatternFacade SimPattern = ClothFacade.GetSimPattern(SimPatternIndex);
		SimPattern.Initialize(OutRestPositions2D, OutDrapedPositions3D, OutTriangleToVertexIndex);
	}
	return true;
}

bool FChaosClothAssetUSDImportNode_v2::ImportRenderStaticMesh(const TSharedRef<FManagedArrayCollection> ClothCollection, FText& OutErrorText)
{
	using namespace UE::Chaos::ClothAsset;

	FCollectionClothFacade ClothFacade(ClothCollection);
	check(ClothFacade.IsValid());  // The Cloth Collection schema must be valid at this point

	// Empty the current render mesh
	FClothGeometryTools::DeleteRenderMesh(ClothCollection);

	if (!ImportedRenderStaticMesh)
	{
		return true;  // Nothing to import
	}

	// Import the LOD 0
	if (ImportedRenderStaticMesh->GetNumSourceModels())
	{
		constexpr int32 LODIndex = 0;
		const FMeshDescription* const MeshDescription = ImportedRenderStaticMesh->GetMeshDescription(LODIndex);
		const FMeshBuildSettings& BuildSettings = ImportedRenderStaticMesh->GetSourceModel(LODIndex).BuildSettings;
		const TArray<FStaticMaterial>& StaticMaterials = ImportedRenderStaticMesh->GetStaticMaterials();

		FSkeletalMeshLODModel SkeletalMeshModel;
		if (FClothDataflowTools::BuildSkeletalMeshModelFromMeshDescription(MeshDescription, BuildSettings, SkeletalMeshModel))
		{
			FStaticMeshConstAttributes MeshAttributes(*MeshDescription);
			TPolygonGroupAttributesConstRef<FName> MaterialSlotNames = MeshAttributes.GetPolygonGroupMaterialSlotNames();
			for (int32 SectionIndex = 0; SectionIndex < SkeletalMeshModel.Sections.Num(); ++SectionIndex)
			{
				// Section MaterialIndex refers to the polygon group index. Look up which material this corresponds with.
				const FName& MaterialSlotName = MaterialSlotNames[SkeletalMeshModel.Sections[SectionIndex].MaterialIndex];
				const int32 MaterialIndex = ImportedRenderStaticMesh->GetMaterialIndexFromImportedMaterialSlotName(MaterialSlotName);
				const FString RenderMaterialPathName = StaticMaterials.IsValidIndex(MaterialIndex) && StaticMaterials[MaterialIndex].MaterialInterface ?
					StaticMaterials[MaterialIndex].MaterialInterface->GetPathName() :
					FString();
				FClothDataflowTools::AddRenderPatternFromSkeletalMeshSection(ClothCollection, SkeletalMeshModel, SectionIndex, RenderMaterialPathName);
			}
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
