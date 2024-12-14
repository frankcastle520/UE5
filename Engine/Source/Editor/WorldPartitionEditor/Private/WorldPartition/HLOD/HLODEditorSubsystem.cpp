// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/HLOD/HLODEditorSubsystem.h"

#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/ActorPrimitiveColorHandler.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/FileHelper.h"
#include "StaticMeshResources.h"
#include "Subsystems/UnrealEditorSubsystem.h"
#include "UObject/UObjectIterator.h"
#include "WorldPartitionEditorModule.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/HLOD/HLODActorDesc.h"
#include "WorldPartition/HLOD/HLODEditorData.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/HLOD/HLODStats.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/WorldPartitionEditorSettings.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"

#include "PropertyPermissionList.h"

static TAutoConsoleVariable<bool> CVarHLODInEditorEnabled(
	TEXT("wp.Editor.HLOD.AllowShowingHLODsInEditor"),
	true,
	TEXT("Allow showing World Partition HLODs in the editor."));

#define LOCTEXT_NAMESPACE "HLODEditorSubsystem"

DEFINE_LOG_CATEGORY_STATIC(LogHLODEditorSubsystem, Log, All);

static FName NAME_HLODRelevantColorHandler(TEXT("HLODRelevantColorHandler"));
TMap<EHLODSettingsVisibility, UWorldPartitionHLODEditorSubsystem::FStructsPropertiesMap> UWorldPartitionHLODEditorSubsystem::StructsPropertiesVisibility;

UWorldPartitionHLODEditorSubsystem::UWorldPartitionHLODEditorSubsystem()
{
#if ENABLE_ACTOR_PRIMITIVE_COLOR_HANDLER
	if (HasAnyFlags(RF_ClassDefaultObject) && ExactCast<UWorldPartitionHLODEditorSubsystem>(this))
	{
		FActorPrimitiveColorHandler::Get().RegisterPrimitiveColorHandler(NAME_HLODRelevantColorHandler, LOCTEXT("HLODRelevantColor", "HLOD Relevant Color"), [](const UPrimitiveComponent* InPrimitiveComponent) -> FLinearColor
		{
			if (AActor* Actor = InPrimitiveComponent->GetOwner())
			{
				if (InPrimitiveComponent->IsHLODRelevant() && Actor->IsHLODRelevant())
				{
					return FLinearColor::Green;
				}
			}
			return FLinearColor::Red;
		},
		[]() {},
		LOCTEXT("HLODRelevantColor_ToolTip", "Colorize actor if relevant to the HLOD system. Green means relevant, otherwise the color is Red."));
	}
#endif

	if (IsTemplate())
	{
		HLOD_ADD_CLASS_SETTING_FILTER_NAME(BasicSettings, UHLODLayer, UHLODLayer::GetHLODBuilderSettingsPropertyName());
	}
}

UWorldPartitionHLODEditorSubsystem::~UWorldPartitionHLODEditorSubsystem()
{
#if ENABLE_ACTOR_PRIMITIVE_COLOR_HANDLER
	if (HasAnyFlags(RF_ClassDefaultObject) && ExactCast<UWorldPartitionHLODEditorSubsystem>(this))
	{
		FActorPrimitiveColorHandler::Get().UnregisterPrimitiveColorHandler(NAME_HLODRelevantColorHandler);
	}
#endif
}

bool UWorldPartitionHLODEditorSubsystem::IsHLODInEditorEnabled()
{
	const IWorldPartitionEditorModule* WorldPartitionEditorModule = FModuleManager::GetModulePtr<IWorldPartitionEditorModule>("WorldPartitionEditor");
	const bool bShowHLODsInEditorForWorld = WorldPartitionEditorModule && WorldPartitionEditorModule->IsHLODInEditorAllowed(GetWorld());
	const bool bShowHLODsInEditorUserSetting = WorldPartitionEditorModule && WorldPartitionEditorModule->GetShowHLODsInEditor();
	const bool bWorldPartitionLoadingInEditorEnabled = WorldPartitionEditorModule && WorldPartitionEditorModule->GetEnableLoadingInEditor();
	return CVarHLODInEditorEnabled.GetValueOnGameThread() && bShowHLODsInEditorUserSetting && bShowHLODsInEditorForWorld && bWorldPartitionLoadingInEditorEnabled;
}

bool UWorldPartitionHLODEditorSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Editor && !IsRunningCommandlet();
}

void UWorldPartitionHLODEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Ensure the WorldPartitionSubsystem gets created before the HLODEditorSubsystem
	Collection.InitializeDependency<UWorldPartitionSubsystem>();

	Super::Initialize(Collection);

	bForceHLODStateUpdate = true;
	CachedCameraLocation = FVector::Zero();
	CachedHLODMinDrawDistance = 0;
	CachedHLODMaxDrawDistance = 0;
	bCachedShowHLODsOverLoadedRegions = false;
	
	GetWorld()->OnWorldPartitionInitialized().AddUObject(this, &UWorldPartitionHLODEditorSubsystem::OnWorldPartitionInitialized);
	GetWorld()->OnWorldPartitionUninitialized().AddUObject(this, &UWorldPartitionHLODEditorSubsystem::OnWorldPartitionUninitialized);

	GEngine->OnLevelActorListChanged().AddUObject(this, &UWorldPartitionHLODEditorSubsystem::ForceHLODStateUpdate);

	UWorldPartitionEditorSettings::OnSettingsChanged().AddUObject(this, &UWorldPartitionHLODEditorSubsystem::OnWorldPartitionEditorSettingsChanged);

	ApplyHLODSettingsFiltering();
}

void UWorldPartitionHLODEditorSubsystem::Deinitialize()
{
	Super::Deinitialize();

	UWorldPartitionEditorSettings::OnSettingsChanged().RemoveAll(this);

	GEngine->OnLevelActorListChanged().RemoveAll(this);

	GetWorld()->OnWorldPartitionInitialized().RemoveAll(this);
	GetWorld()->OnWorldPartitionUninitialized().RemoveAll(this);
}

void UWorldPartitionHLODEditorSubsystem::OnWorldPartitionEditorSettingsChanged(const FName& InPropertyName, const UWorldPartitionEditorSettings& InWorldPartitionEditorSettings)
{
	if (InPropertyName == UWorldPartitionEditorSettings::GetEnableAdvancedHLODSettingsPropertyName())
	{
		ApplyHLODSettingsFiltering();
	}
}

void UWorldPartitionHLODEditorSubsystem::ApplyHLODSettingsFiltering()
{
	static const FName PropertyPermissionListOwnerName = "AdvancedHLODSettingsFiltering";

	FPropertyEditorPermissionList::Get().UnregisterOwner(PropertyPermissionListOwnerName);

	UEnum* HLODLayerTypeEnum = StaticEnum<EHLODLayerType>();
	if (!GetDefault<UWorldPartitionEditorSettings>()->GetEnableAdvancedHLODSettings())
	{
		for (const TPair<TSoftObjectPtr<UStruct>, TSet<FName>>& StructProperties : StructsPropertiesVisibility.FindOrAdd(EHLODSettingsVisibility::BasicSettings))
		{
			FNamePermissionList PermissionList;

			for (FName PropertyName : StructProperties.Value)
			{
				PermissionList.AddAllowListItem(PropertyPermissionListOwnerName, PropertyName);
			}

			FPropertyEditorPermissionList::Get().AddPermissionList(StructProperties.Key, PermissionList, EPropertyPermissionListRules::UseExistingPermissionList, { PropertyPermissionListOwnerName });
		}
	}
}

void UWorldPartitionHLODEditorSubsystem::OnWorldPartitionInitialized(UWorldPartition* InWorldPartition)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionHLODEditorSubsystem::OnWorldPartitionInitialized);
	
	if (InWorldPartition->IsMainWorldPartition())
	{
		InWorldPartition->LoaderAdapterStateChanged.AddUObject(this, &UWorldPartitionHLODEditorSubsystem::OnLoaderAdapterStateChanged);
		HLODEditorData = MakePimpl<FWorldPartitionHLODEditorData>(InWorldPartition);
		ForceHLODStateUpdate();
	}
}

void UWorldPartitionHLODEditorSubsystem::OnWorldPartitionUninitialized(UWorldPartition* InWorldPartition)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionHLODEditorSubsystem::OnWorldPartitionUninitialized);

	if (InWorldPartition->IsMainWorldPartition())
	{
		InWorldPartition->LoaderAdapterStateChanged.RemoveAll(this);
		HLODEditorData = nullptr;
	}
}

void UWorldPartitionHLODEditorSubsystem::OnLoaderAdapterStateChanged(const IWorldPartitionActorLoaderInterface::ILoaderAdapter* LoaderAdapter)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionHLODEditorSubsystem::OnLoaderAdapterStateChanged);

	ForceHLODStateUpdate();
}

void UWorldPartitionHLODEditorSubsystem::ForceHLODStateUpdate()
{
	if (IsHLODInEditorEnabled())
	{
		bForceHLODStateUpdate = true;
	}
}

void UWorldPartitionHLODEditorSubsystem::Tick(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionHLODEditorSubsystem::Tick);

	if (HLODEditorData)
	{
		HLODEditorData->SetHLODLoadingState(IsHLODInEditorEnabled());
		
		if (IsHLODInEditorEnabled())
		{
			bool bForceHLODVisibilityUpdate = false;

			IWorldPartitionEditorModule* WorldPartitionEditorModule = FModuleManager::GetModulePtr<IWorldPartitionEditorModule>("WorldPartitionEditor");

			// "Show HLODs over loaded region" option changed ?
			if (WorldPartitionEditorModule->GetShowHLODsOverLoadedRegions() != bCachedShowHLODsOverLoadedRegions)
			{ 
				bCachedShowHLODsOverLoadedRegions = WorldPartitionEditorModule->GetShowHLODsOverLoadedRegions();
				bForceHLODVisibilityUpdate = true;

				// Clear loading state of actors if we are going to always show HLODs
				if (bCachedShowHLODsOverLoadedRegions)
				{
					HLODEditorData->ClearLoadedActorsState();
				}
				else
				{
					bForceHLODStateUpdate = true;
				}
			}

			// Actors or regions were loaded ?
			if (bForceHLODStateUpdate && !bCachedShowHLODsOverLoadedRegions)
			{
				HLODEditorData->UpdateLoadedActorsState();
				bForceHLODVisibilityUpdate = true;
			}
			
			// Min/Max draw distance for HLODs was changed ?
			if (WorldPartitionEditorModule->GetHLODInEditorMinDrawDistance() != CachedHLODMinDrawDistance ||
				WorldPartitionEditorModule->GetHLODInEditorMaxDrawDistance() != CachedHLODMaxDrawDistance)
			{
				CachedHLODMinDrawDistance = WorldPartitionEditorModule->GetHLODInEditorMinDrawDistance();
				CachedHLODMaxDrawDistance = WorldPartitionEditorModule->GetHLODInEditorMaxDrawDistance();
				bForceHLODVisibilityUpdate = true;
			}

			UUnrealEditorSubsystem* UnrealEditorSubsystem = GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>();
			if (UnrealEditorSubsystem)
			{
				FVector CameraLocation;
				FRotator CameraRotation;
				bool bCameraMoved = false;

				if (FWorldPartitionEditorModule::GetActiveLevelViewportCameraInfo(CameraLocation, CameraRotation))
				{
					// Camera was moved ?
					bCameraMoved = CameraLocation != CachedCameraLocation;
					if (bCameraMoved)
					{
						CachedCameraLocation = CameraLocation;
					}
				}

				if (bForceHLODVisibilityUpdate || bCameraMoved)
				{
					HLODEditorData->UpdateVisibility(CameraLocation, CachedHLODMinDrawDistance, CachedHLODMaxDrawDistance, bForceHLODVisibilityUpdate);
				}
			}
			bForceHLODStateUpdate = false;
		}
	}
}

TStatId UWorldPartitionHLODEditorSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(WorldPartitionHLODEditorSubsystem, STATGROUP_Tickables);
}

void UWorldPartitionHLODEditorSubsystem::AddHLODSettingsFilter(EHLODSettingsVisibility InSettingsVisibility, TSoftObjectPtr<UStruct> InStruct, FName InPropertyName)
{
	StructsPropertiesVisibility.FindOrAdd(InSettingsVisibility).FindOrAdd(InStruct).Add(InPropertyName);
}

bool UWorldPartitionHLODEditorSubsystem::WriteHLODStats(const IWorldPartitionEditorModule::FWriteHLODStatsParams& Params) const
{
	check(Params.World == GetWorld());

	bool bResult = false;

	switch(Params.StatsType)
	{
	case IWorldPartitionEditorModule::FWriteHLODStatsParams::EStatsType::Default:
		bResult = WriteHLODStats(Params.Filename);
		break;

	case IWorldPartitionEditorModule::FWriteHLODStatsParams::EStatsType::InputDetails:
		bResult = WriteHLODInputStats(Params.Filename);
		break;
	}

	if (bResult)
	{
		UE_LOG(LogHLODEditorSubsystem, Display, TEXT("Wrote HLOD stats to %s"), *Params.Filename);
	}
	else
	{
		UE_LOG(LogHLODEditorSubsystem, Error, TEXT("Failed to write HLOD stats to %s"), *Params.Filename);
	}

	return bResult;
}

bool UWorldPartitionHLODEditorSubsystem::WriteHLODStats(const FString& InFilename) const
{
	UWorld* World = GetWorld();
	UWorldPartition* WorldPartition = World->GetWorldPartition();
	if (!WorldPartition)
	{
		return false;
	}

	typedef TFunction<FString(FWorldPartitionActorDescInstance* InActorDescInstance, const FHLODActorDesc&)> FGetStatFunc;

	auto GetHLODStat = [](FName InStatName)
	{
		return TPair<FName, FGetStatFunc>(InStatName, [InStatName](FWorldPartitionActorDescInstance* InActorDescInstance, const FHLODActorDesc& InActorDesc)
		{
			return FString::Printf(TEXT("%lld"), InActorDesc.GetStat(InStatName));
		});
	};

	const UDataLayerManager* DataLayerManager = WorldPartition->GetDataLayerManager();

	auto GetDataLayerShortName = [DataLayerManager](FName DataLayerInstanceName)
	{
		const UDataLayerInstance* DataLayerInstance = DataLayerManager ? DataLayerManager->GetDataLayerInstance(DataLayerInstanceName) : nullptr;
		return DataLayerInstance ? DataLayerInstance->GetDataLayerShortName() : DataLayerInstanceName.ToString();
	};

	TArray<TPair<FName, FGetStatFunc>> StatsToWrite =
	{
		{ "WorldPackage",		[World](FWorldPartitionActorDescInstance* InActorDescInstance, const FHLODActorDesc& InActorDesc) { return World->GetPackage()->GetName(); } },
		{ "Name",				[](FWorldPartitionActorDescInstance* InActorDescInstance, const FHLODActorDesc& InActorDesc) { return InActorDescInstance->GetActorLabelString(); } },
		{ "HLODLayer",			[](FWorldPartitionActorDescInstance* InActorDescInstance, const FHLODActorDesc& InActorDesc) { return InActorDesc.GetSourceHLODLayer().GetAssetName().ToString(); }},
		{ "SpatiallyLoaded",	[](FWorldPartitionActorDescInstance* InActorDescInstance, const FHLODActorDesc& InActorDesc) { return InActorDescInstance->GetIsSpatiallyLoaded() ? TEXT("true") : TEXT("false"); } },
		{ "DataLayers",			[&GetDataLayerShortName](FWorldPartitionActorDescInstance* InActorDescInstance, const FHLODActorDesc& InActorDesc) { return FString::JoinBy(InActorDescInstance->GetDataLayerInstanceNames().ToArray(), TEXT(" | "), GetDataLayerShortName); }},

		GetHLODStat(FWorldPartitionHLODStats::InputActorCount),
		GetHLODStat(FWorldPartitionHLODStats::InputTriangleCount),
		GetHLODStat(FWorldPartitionHLODStats::InputVertexCount),

		GetHLODStat(FWorldPartitionHLODStats::MeshInstanceCount),
		GetHLODStat(FWorldPartitionHLODStats::MeshNaniteTriangleCount),
		GetHLODStat(FWorldPartitionHLODStats::MeshNaniteVertexCount),
		GetHLODStat(FWorldPartitionHLODStats::MeshTriangleCount),
		GetHLODStat(FWorldPartitionHLODStats::MeshVertexCount),
		GetHLODStat(FWorldPartitionHLODStats::MeshUVChannelCount),

		GetHLODStat(FWorldPartitionHLODStats::MaterialBaseColorTextureSize),
		GetHLODStat(FWorldPartitionHLODStats::MaterialNormalTextureSize),
		GetHLODStat(FWorldPartitionHLODStats::MaterialEmissiveTextureSize),
		GetHLODStat(FWorldPartitionHLODStats::MaterialMetallicTextureSize),
		GetHLODStat(FWorldPartitionHLODStats::MaterialRoughnessTextureSize),
		GetHLODStat(FWorldPartitionHLODStats::MaterialSpecularTextureSize),

		GetHLODStat(FWorldPartitionHLODStats::MemoryMeshResourceSizeBytes),
		GetHLODStat(FWorldPartitionHLODStats::MemoryTexturesResourceSizeBytes),
		GetHLODStat(FWorldPartitionHLODStats::MemoryDiskSizeBytes),

		GetHLODStat(FWorldPartitionHLODStats::BuildTimeLoadMilliseconds),
		GetHLODStat(FWorldPartitionHLODStats::BuildTimeBuildMilliseconds),
		GetHLODStat(FWorldPartitionHLODStats::BuildTimeTotalMilliseconds)
	};

	FStringOutputDevice Output;

	// Write header if file doesn't exist
	if (!IFileManager::Get().FileExists(*InFilename))
	{
		const FString StatHeader = FString::JoinBy(StatsToWrite, TEXT(","), [](const TPair<FName, FGetStatFunc>& Pair) { return Pair.Key.ToString(); });
		Output.Logf(TEXT("%s" LINE_TERMINATOR_ANSI), *StatHeader);
	}

	// Write one line per HLOD actor desc
	for (FActorDescContainerInstanceCollection::TIterator<AWorldPartitionHLOD> HLODIterator(WorldPartition); HLODIterator; ++HLODIterator)
	{
		const FString StatLine = FString::JoinBy(StatsToWrite, TEXT(","), [&HLODIterator](const TPair<FName, FGetStatFunc>& Pair)
			{
				const FHLODActorDesc& HLODActorDesc = *(FHLODActorDesc*)HLODIterator->GetActorDesc();
				return Pair.Value(*HLODIterator, HLODActorDesc);
			});
		Output.Logf(TEXT("%s" LINE_TERMINATOR_ANSI), *StatLine);
	}

	// Write to file
	return FFileHelper::SaveStringToFile(Output, *InFilename, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), EFileWrite::FILEWRITE_Append);
}

bool UWorldPartitionHLODEditorSubsystem::WriteHLODInputStats(const FString& InFilename) const
{
	UWorld* World = GetWorld();
	UWorldPartition* WorldPartition = World->GetWorldPartition();
	if (!WorldPartition)
	{
		return false;
	}

	FWorldPartitionHelpers::FForEachActorWithLoadingParams ForEachActorWithLoadingParams;
	ForEachActorWithLoadingParams.ActorClasses = { AWorldPartitionHLOD::StaticClass() };

	TMap<TPair<int32, FName>, FHLODBuildInputReferencedAssets> BuildersReferencedAssets;

	// Aggregate referenced assets from all HLOD actors
	FWorldPartitionHelpers::ForEachActorWithLoading(WorldPartition, [this, World, WorldPartition, &BuildersReferencedAssets](const FWorldPartitionActorDescInstance* ActorDescInstance)
	{
		AWorldPartitionHLOD* HLODActor = Cast<AWorldPartitionHLOD>(ActorDescInstance->GetActor());
		if (!HLODActor)
		{
			UE_LOG(LogHLODEditorSubsystem, Error, TEXT("HLOD actor failed to load: %s (%s)"), *ActorDescInstance->GetActorNameString(), *ActorDescInstance->GetActorPackage().ToString());
			return false;
		}

		const FHLODBuildInputStats& InputStats = HLODActor->GetInputStats();

		for (const TPair<FName, FHLODBuildInputReferencedAssets>& Entry : InputStats.BuildersReferencedAssets)
		{
			FHLODBuildInputReferencedAssets& BuilderReferencedAssets = BuildersReferencedAssets.FindOrAdd(TPair<int32, FName>(HLODActor->GetLODLevel(), Entry.Key));
			for (const TPair<FTopLevelAssetPath, uint32>& ReferencedMesh : Entry.Value.StaticMeshes)
			{
				BuilderReferencedAssets.StaticMeshes.FindOrAdd(ReferencedMesh.Key) += ReferencedMesh.Value;
			}
		}

		return true;
	}, ForEachActorWithLoadingParams);

	FStringOutputDevice Output;

	Output.Logf(TEXT("HLODLevel,BuilderName,AssetName,RefCount,LastLODTriCount,LastLODVtxCount" LINE_TERMINATOR_ANSI));

	BuildersReferencedAssets.KeySort([](const TPair<int32, FName>& PairA, const TPair<int32, FName> PairB)
	{
		if (PairA.Key != PairB.Key)
		{
			return PairA.Key < PairB.Key;
		}

		return PairA.Value.LexicalLess(PairB.Value);
	});

	for (TPair<TPair<int32,FName>, FHLODBuildInputReferencedAssets>& Entry : BuildersReferencedAssets)
	{
		Entry.Value.StaticMeshes.KeySort(FTopLevelAssetPathFastLess());

		for (const TPair<FTopLevelAssetPath, uint32>& ReferencedMesh : Entry.Value.StaticMeshes)
		{
			const FTopLevelAssetPath& StaticMeshAssetPath = ReferencedMesh.Key;
			
			UObject* LoadedObject = StaticLoadAsset(UObject::StaticClass(), StaticMeshAssetPath, LOAD_NoWarn);
			if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(LoadedObject))
			{
				const FStaticMeshRenderData* RenderData = StaticMesh->GetRenderData();
				const bool bHasRenderData = RenderData && !RenderData->LODResources.IsEmpty();
				if (bHasRenderData)
				{
					const int32 LODIndex = StaticMesh->GetNumLODs() - 1;
					const int64 LastLODTriCount = RenderData->LODResources[LODIndex].GetNumTriangles();
					const int64 LastLODVtxCount = RenderData->LODResources[LODIndex].GetNumVertices();
					Output.Logf(TEXT("HLOD%d,%s,%s,%d,%d,%d" LINE_TERMINATOR_ANSI), Entry.Key.Key, *Entry.Key.Value.ToString(), *StaticMeshAssetPath.GetPackageName().ToString(), ReferencedMesh.Value, LastLODTriCount, LastLODVtxCount);
				}				
			}
		}
	}

	// Write to file
	return FFileHelper::SaveStringToFile(Output, *InFilename);
}

FAutoConsoleCommand HLODDumpStats(
	TEXT("wp.Editor.HLOD.DumpStats"),
	TEXT("Export various HLOD stats to a CSV formatted file."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const FString HLODStatsOutputFilename = FPaths::ProjectLogDir() / TEXT("WorldPartition") / FString::Printf(TEXT("HLODStats-%08x-%s.csv"), FPlatformProcess::GetCurrentProcessId(), *FDateTime::Now().ToString());

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (UWorld* World = Context.World())
			{
				if (UWorldPartitionHLODEditorSubsystem* HLODEditorSubsystem = World->GetSubsystem<UWorldPartitionHLODEditorSubsystem>())
				{
					IWorldPartitionEditorModule::FWriteHLODStatsParams Params;
					Params.World = World;
					Params.StatsType = IWorldPartitionEditorModule::FWriteHLODStatsParams::EStatsType::Default;
					Params.Filename = HLODStatsOutputFilename;
					HLODEditorSubsystem->WriteHLODStats(Params);
				}
			}
		}
	})
);

FAutoConsoleCommand HLODDumpInputStats(
	TEXT("wp.Editor.HLOD.DumpInputStats"),
	TEXT("Export stats regarding the input to HLOD generation to a CSV formatted file."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const FString HLODStatsOutputFilename = FPaths::ProjectLogDir() / TEXT("WorldPartition") / FString::Printf(TEXT("HLODInputStats-%08x-%s.csv"), FPlatformProcess::GetCurrentProcessId(), *FDateTime::Now().ToString());

		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (UWorld* World = Context.World())
			{
				if (UWorldPartitionHLODEditorSubsystem* HLODEditorSubsystem = World->GetSubsystem<UWorldPartitionHLODEditorSubsystem>())
				{
					IWorldPartitionEditorModule::FWriteHLODStatsParams Params;
					Params.World = World;
					Params.StatsType = IWorldPartitionEditorModule::FWriteHLODStatsParams::EStatsType::InputDetails;
					Params.Filename = HLODStatsOutputFilename;
					HLODEditorSubsystem->WriteHLODStats(Params);
				}
			}
		}
	})
);

#undef LOCTEXT_NAMESPACE
