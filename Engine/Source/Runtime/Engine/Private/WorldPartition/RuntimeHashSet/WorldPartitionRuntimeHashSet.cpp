// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/RuntimeHashSet/WorldPartitionRuntimeHashSet.h"
#include "WorldPartition/RuntimeHashSet/WorldPartitionRuntimeCellDataHashSet.h"
#include "WorldPartition/RuntimeHashSet/RuntimePartition.h"
#include "WorldPartition/RuntimeHashSet/RuntimePartitionLHGrid.h"
#include "WorldPartition/RuntimeHashSet/RuntimePartitionPersistent.h"
#include "WorldPartition/WorldPartitionRuntimeLevelStreamingCell.h"
#include "WorldPartition/WorldPartitionLevelStreamingDynamic.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/ContentBundle/ContentBundleDescriptor.h"
#include "WorldPartition/DataLayer/DataLayersID.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "Algo/RemoveIf.h"
#include "Algo/Transform.h"
#include "Misc/HashBuilder.h"
#include "Misc/ArchiveMD5.h"

#include "WorldPartition/RuntimeHashSet/StaticSpatialIndex.h"

void FRuntimePartitionStreamingData::CreatePartitionsSpatialIndex() const
{
	if (!SpatialIndex)
	{
		uint32 StaticIndexAllocatedSize = 0;

		TArray<UWorldPartitionRuntimeCell*> SpatiallyLoadedCells3D;
		TArray<UWorldPartitionRuntimeCell*> SpatiallyLoadedCells2D;
		
		SpatiallyLoadedCells3D.Reserve(SpatiallyLoadedCells.Num());		
		SpatiallyLoadedCells2D.Reserve(SpatiallyLoadedCells.Num());

		for (UWorldPartitionRuntimeCell* Cell : SpatiallyLoadedCells)
		{
			if (CastChecked<UWorldPartitionRuntimeCellDataHashSet>(Cell->RuntimeCellData)->bIs2D)
			{
				SpatiallyLoadedCells2D.Add(Cell);
			}
			else
			{
				SpatiallyLoadedCells3D.Add(Cell);
			}
		}

		if (SpatiallyLoadedCells3D.Num())
		{
			SpatialIndex = MakeUnique<FStaticSpatialIndexType>();
			{
				TArray<TPair<FBox, TObjectPtr<UWorldPartitionRuntimeCell>>> PartitionsElements;
				Algo::Transform(SpatiallyLoadedCells3D, PartitionsElements, [](UWorldPartitionRuntimeCell* Cell)
				{
					return TPair<FBox, TObjectPtr<UWorldPartitionRuntimeCell>>(Cell->GetStreamingBounds(), Cell);
				});
				SpatialIndex->Init(MoveTemp(PartitionsElements));
				StaticIndexAllocatedSize += SpatialIndex->GetAllocatedSize();
			}
		
			SpatialIndexForce2D = MakeUnique<FStaticSpatialIndexType2D>();
			{
				TArray<TPair<FBox2D, TObjectPtr<UWorldPartitionRuntimeCell>>> PartitionsElements;
				Algo::Transform(SpatiallyLoadedCells3D, PartitionsElements, [](UWorldPartitionRuntimeCell* Cell)
				{
					const FBox CellBounds = Cell->GetStreamingBounds();
					const FBox2D CellBounds2D = FBox2D(FVector2D(CellBounds.Min), FVector2D(CellBounds.Max));
					return TPair<FBox2D, TObjectPtr<UWorldPartitionRuntimeCell>>(CellBounds2D, Cell);
				});
				SpatialIndexForce2D->Init(MoveTemp(PartitionsElements));
				StaticIndexAllocatedSize += SpatialIndexForce2D->GetAllocatedSize();
			}
		}

		if (SpatiallyLoadedCells2D.Num())
		{
			SpatialIndex2D = MakeUnique<FStaticSpatialIndexType2D>();
			{
				TArray<TPair<FBox2D, TObjectPtr<UWorldPartitionRuntimeCell>>> PartitionsElements;
				Algo::Transform(SpatiallyLoadedCells2D, PartitionsElements, [](UWorldPartitionRuntimeCell* Cell)
				{
					const FBox CellBounds = Cell->GetStreamingBounds();
					const FBox2D CellBounds2D = FBox2D(FVector2D(CellBounds.Min), FVector2D(CellBounds.Max));
					return TPair<FBox2D, TObjectPtr<UWorldPartitionRuntimeCell>>(CellBounds2D, Cell);
				});
				SpatialIndex2D->Init(MoveTemp(PartitionsElements));
				StaticIndexAllocatedSize += SpatialIndex2D->GetAllocatedSize();
			}
		}

#if WITH_EDITOR
		UE_LOG(LogWorldPartition, Verbose, TEXT("CreatePartitionsSpatialIndex: %s used %s"), *DebugName, *FGenericPlatformMemory::PrettyMemory(StaticIndexAllocatedSize));
#endif
	}
}

void FRuntimePartitionStreamingData::DestroyPartitionsSpatialIndex() const
{
	SpatialIndex.Reset();
	SpatialIndexForce2D.Reset();
	SpatialIndex2D.Reset();
}

int32 FRuntimePartitionStreamingData::GetLoadingRange() const
{
#if !UE_BUILD_SHIPPING
	int32 OverriddenLoadingRange;
	if (UWorldPartitionSubsystem::GetOverrideLoadingRange(Name, OverriddenLoadingRange))
	{
		return OverriddenLoadingRange;
	}
#endif
	return LoadingRange;
}

void URuntimeHashSetExternalStreamingObject::CreatePartitionsSpatialIndex() const
{
	for (const FRuntimePartitionStreamingData& StreamingData : RuntimeStreamingData)
	{
		StreamingData.CreatePartitionsSpatialIndex();
	}
}

void URuntimeHashSetExternalStreamingObject::DestroyPartitionsSpatialIndex() const
{
	for (const FRuntimePartitionStreamingData& StreamingData : RuntimeStreamingData)
	{
		StreamingData.DestroyPartitionsSpatialIndex();
	}
}

void URuntimeHashSetExternalStreamingObject::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
#if WITH_EDITOR
	URuntimeHashSetExternalStreamingObject* This = CastChecked<URuntimeHashSetExternalStreamingObject>(InThis);
	for (const FRuntimePartitionStreamingData& StreamingData : This->RuntimeStreamingData)
	{
		if (StreamingData.SpatialIndex.IsValid())
		{
			StreamingData.SpatialIndex->AddReferencedObjects(Collector);
		}
		
		if (StreamingData.SpatialIndexForce2D.IsValid())
		{
			StreamingData.SpatialIndexForce2D->AddReferencedObjects(Collector);
		}

		if (StreamingData.SpatialIndex2D.IsValid())
		{
			StreamingData.SpatialIndex2D->AddReferencedObjects(Collector);
		}
	}
#endif

	Super::AddReferencedObjects(InThis, Collector);
}

UWorldPartitionRuntimeHashSet::UWorldPartitionRuntimeHashSet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITOR
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		if (UClass* RuntimeSpatialHashClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.WorldPartitionRuntimeSpatialHash")))
		{
			RegisterWorldPartitionRuntimeHashConverter(RuntimeSpatialHashClass, GetClass(), [](const UWorldPartitionRuntimeHash* SrcHash) -> UWorldPartitionRuntimeHash*
			{
				return CreateFrom(SrcHash);
			});
		}
	}
#endif
}

void UWorldPartitionRuntimeHashSet::OnBeginPlay()
{
	Super::OnBeginPlay();

	if (GetTypedOuter<UWorld>()->IsGameWorld())
	{
		ForEachStreamingData([this](const FRuntimePartitionStreamingData& StreamingData)
		{
#if !WITH_EDITOR
			FRuntimePartitionStreamingData& NonConstStreamingData = (const_cast<FRuntimePartitionStreamingData&>(StreamingData));		
			NonConstStreamingData.SpatiallyLoadedCells.SetNum(Algo::RemoveIf(NonConstStreamingData.SpatiallyLoadedCells, [this](UWorldPartitionRuntimeCell* Cell) { return !IsCellRelevantFor(Cell->GetClientOnlyVisible()); }));
			NonConstStreamingData.NonSpatiallyLoadedCells.SetNum(Algo::RemoveIf(NonConstStreamingData.NonSpatiallyLoadedCells, [this](UWorldPartitionRuntimeCell* Cell) { return !IsCellRelevantFor(Cell->GetClientOnlyVisible()); }));
#endif
			StreamingData.CreatePartitionsSpatialIndex();
			return true;
		});
	}

	UpdateRuntimeDataGridMap();
}

#if WITH_EDITOR
void UWorldPartitionRuntimeHashSet::SetDefaultValues()
{
	check(RuntimePartitions.IsEmpty());

	FRuntimePartitionDesc& RuntimePartitionDesc = RuntimePartitions.AddDefaulted_GetRef();
	RuntimePartitionDesc.Class = URuntimePartitionLHGrid::StaticClass();
	RuntimePartitionDesc.Name = TEXT("MainPartition");

	RuntimePartitionDesc.MainLayer = NewObject<URuntimePartitionLHGrid>(this, NAME_None);
	RuntimePartitionDesc.MainLayer->Name = RuntimePartitionDesc.Name;
	RuntimePartitionDesc.MainLayer->SetDefaultValues();

	UWorldPartition* WorldPartition = GetTypedOuter<UWorldPartition>();
	check(WorldPartition);

	if (const UHLODLayer* HLODLayer = WorldPartition->GetDefaultHLODLayer())
	{
		uint32 HLODIndex = 0;
		while (HLODLayer)
		{
			FRuntimePartitionHLODSetup& HLODSetup = RuntimePartitionDesc.HLODSetups.AddDefaulted_GetRef();

			HLODSetup.Name = HLODLayer->GetFName();
			HLODSetup.bIsSpatiallyLoaded = HLODLayer->IsSpatiallyLoaded();
			HLODSetup.HLODLayers = { HLODLayer };

			if (HLODSetup.bIsSpatiallyLoaded)
			{
				URuntimePartitionLHGrid* HLODLHGrid = NewObject<URuntimePartitionLHGrid>(this, NAME_None);
				HLODLHGrid->CellSize = CastChecked<URuntimePartitionLHGrid>(RuntimePartitionDesc.MainLayer)->CellSize * (2 << HLODIndex);
				HLODLHGrid->LoadingRange = RuntimePartitionDesc.MainLayer->LoadingRange * (2 << HLODIndex);
				HLODSetup.PartitionLayer = HLODLHGrid;
			}
			else
			{
				HLODSetup.PartitionLayer = NewObject<URuntimePartitionPersistent>(this, NAME_None);
				HLODSetup.PartitionLayer->LoadingRange = 0;
			}

			HLODSetup.PartitionLayer->Name = HLODSetup.Name;
			HLODSetup.PartitionLayer->bBlockOnSlowStreaming = false;
			HLODSetup.PartitionLayer->bClientOnlyVisible = true;
			HLODSetup.PartitionLayer->Priority = 0;
			HLODSetup.PartitionLayer->HLODIndex = HLODIndex;

			HLODLayer = HLODLayer->GetParentLayer();
			HLODIndex++;
		}
	}
}

void UWorldPartitionRuntimeHashSet::FlushStreamingContent()
{
	Super::FlushStreamingContent();
	RuntimeStreamingData.Empty();
	UpdateRuntimeDataGridMap();
}

bool UWorldPartitionRuntimeHashSet::IsValidGrid(FName GridName, const UClass* ActorClass) const
{
	return GridName.IsNone() || !!ResolveRuntimePartition(GridName);
}
#endif

const URuntimePartition* UWorldPartitionRuntimeHashSet::ResolveRuntimePartition(FName GridName, bool bMainPartitionLayer) const
{
	TArray<FName> MainPartitionTokens;
	TArray<FName> HLODPartitionTokens;

	// Parse the potentially dot separated grid name to identiy the associated runtime partition
	if (ParseGridName(GridName, MainPartitionTokens, HLODPartitionTokens))
	{
		// The None grid name will always map to the first runtime partition in the list
		if (MainPartitionTokens[0].IsNone())
		{
			if (RuntimePartitions.IsValidIndex(0))
			{
				return RuntimePartitions[0].MainLayer;
			}
			else
			{
				return nullptr;
			}
		}

		// Make sure the runtime partition is valid
		const FRuntimePartitionDesc* FoundRuntimePartitionDesc = RuntimePartitions.FindByPredicate([&MainPartitionTokens](const FRuntimePartitionDesc& RuntimePartitionDesc)
		{
			return RuntimePartitionDesc.Name == MainPartitionTokens[0];
		});
		
		if (!FoundRuntimePartitionDesc || !FoundRuntimePartitionDesc->MainLayer || !FoundRuntimePartitionDesc->MainLayer->IsValidPartitionTokens(MainPartitionTokens))
		{
			return nullptr;
		}
	
		// If an HLOD partition token was specified ("MainPartition:HLODPartition"), make sure it's valid too
		if (!bMainPartitionLayer && HLODPartitionTokens.IsValidIndex(0))
		{
			const FRuntimePartitionHLODSetup* FoundRuntimePartitionHLODSetup = FoundRuntimePartitionDesc->HLODSetups.FindByPredicate([&HLODPartitionTokens](const FRuntimePartitionHLODSetup& RuntimePartitionHLODSetup)
			{
				return RuntimePartitionHLODSetup.Name == HLODPartitionTokens[0];
			});

			if (!FoundRuntimePartitionHLODSetup || !FoundRuntimePartitionHLODSetup->PartitionLayer || !FoundRuntimePartitionHLODSetup->PartitionLayer->IsValidPartitionTokens(HLODPartitionTokens))
			{
				return nullptr;
			}

			return FoundRuntimePartitionHLODSetup->PartitionLayer;
		}

		return FoundRuntimePartitionDesc->MainLayer;
	}			

	return nullptr;
}

#if WITH_EDITOR
bool UWorldPartitionRuntimeHashSet::IsValidHLODLayer(FName GridName, const FSoftObjectPath& HLODLayerPath) const
{
	return ResolveRuntimePartitionForHLODLayer(GridName, HLODLayerPath) != nullptr;
}

const URuntimePartition* UWorldPartitionRuntimeHashSet::ResolveRuntimePartitionForHLODLayer(FName GridName, const FSoftObjectPath& HLODLayerPath) const
{
	const URuntimePartition* RuntimePartition = nullptr;

	if (!RuntimePartitions.IsEmpty())
	{
	    if (const UHLODLayer* HLODLayer = Cast<UHLODLayer>(HLODLayerPath.ResolveObject()))
	    {
		    // The None grid name will always map to the first runtime partition in the list
		    int32 RuntimePartitionIndex = GridName.IsNone() ? 0 : INDEX_NONE;
    
		    if (RuntimePartitionIndex == INDEX_NONE)
		    {
			    TArray<FName> PartitionTokens;
			    TArray<FName> HLODPartitionTokens;
    
			    // Parse the potentially dot separated grid name to identiy the associated runtime partition
			    if (ParseGridName(GridName, PartitionTokens, HLODPartitionTokens))
			    {
				    int32 RuntimePartitionIndexLookup = 0;
				    for (const FRuntimePartitionDesc& RuntimePartitionDesc : RuntimePartitions)
				    {
					    if (RuntimePartitionDesc.Name == PartitionTokens[0])
					    {
						    RuntimePartitionIndex = RuntimePartitionIndexLookup;
						    break;
					    }
    
					    RuntimePartitionIndexLookup++;
				    }
			    }
		    }
    
		    if (RuntimePartitionIndex != INDEX_NONE)
		    {
		        const FRuntimePartitionHLODSetup* FoundRuntimePartitionHLODSetup = RuntimePartitions[RuntimePartitionIndex].HLODSetups.FindByPredicate([&HLODLayer](const FRuntimePartitionHLODSetup& RuntimePartitionHLODSetup)
		        {
			        return RuntimePartitionHLODSetup.HLODLayers.Contains(HLODLayer);
		        });

				RuntimePartition = FoundRuntimePartitionHLODSetup ? FoundRuntimePartitionHLODSetup->PartitionLayer : nullptr;
    		}
	    }
	}

	return RuntimePartition;
}
#endif

bool UWorldPartitionRuntimeHashSet::ParseGridName(FName GridName, TArray<FName>& MainPartitionTokens, TArray<FName>& HLODPartitionTokens)
{
	// If the grid name is none, it directly maps to the main partition
	if (GridName.IsNone())
	{
		MainPartitionTokens.Add(NAME_None);
		return true;
	}

	// Split grid name into its partition and HLOD parts
	TArray<FString> GridNameTokens;
	if (!GridName.ToString().ParseIntoArray(GridNameTokens, TEXT(":")))
	{
		GridNameTokens.Add(GridName.ToString());
	}

	// Parsed grid names token should be either "RuntimeHash" or "RuntimeHash:HLODLayer"
	if (GridNameTokens.Num() > 2)
	{
		return false;
	}

	// Parse the target main partition
	TArray<FString> MainPartitionTokensStr;
	if (GridNameTokens[0].ParseIntoArray(MainPartitionTokensStr, TEXT(".")))
	{
		Algo::Transform(MainPartitionTokensStr, MainPartitionTokens, [](const FString& GridName) { return *GridName; });
	}

	// Parse the target HLOD partition
	if (GridNameTokens.IsValidIndex(1))
	{
		HLODPartitionTokens.Add(*GridNameTokens[1]);
	}

	return true;
}

#if WITH_EDITOR
bool UWorldPartitionRuntimeHashSet::HasStreamingContent() const
{
	return !RuntimeStreamingData.IsEmpty();
}

void UWorldPartitionRuntimeHashSet::StoreStreamingContentToExternalStreamingObject(URuntimeHashExternalStreamingObjectBase* OutExternalStreamingObject)
{
	check(!RuntimeStreamingData.IsEmpty());

	Super::StoreStreamingContentToExternalStreamingObject(OutExternalStreamingObject);

	URuntimeHashSetExternalStreamingObject* StreamingObject = CastChecked<URuntimeHashSetExternalStreamingObject>(OutExternalStreamingObject);
	StreamingObject->RuntimeStreamingData = MoveTemp(RuntimeStreamingData);

	for (FRuntimePartitionStreamingData& StreamingData : StreamingObject->RuntimeStreamingData)
	{
		for (UWorldPartitionRuntimeCell* Cell : StreamingData.SpatiallyLoadedCells)
		{
			Cell->Rename(nullptr, StreamingObject,  REN_DoNotDirty);
		}

		for (UWorldPartitionRuntimeCell* Cell : StreamingData.NonSpatiallyLoadedCells)
		{
			Cell->Rename(nullptr, StreamingObject,  REN_DoNotDirty);
		}
	}
}
#endif

bool UWorldPartitionRuntimeHashSet::InjectExternalStreamingObject(URuntimeHashExternalStreamingObjectBase* ExternalStreamingObject)
{
	if (Super::InjectExternalStreamingObject(ExternalStreamingObject))
	{
		URuntimeHashSetExternalStreamingObject* HashSetExternalStreamingObject = CastChecked<URuntimeHashSetExternalStreamingObject>(ExternalStreamingObject);
		HashSetExternalStreamingObject->CreatePartitionsSpatialIndex();
		UpdateRuntimeDataGridMap();
		return true;
	}

	return false;
}

bool UWorldPartitionRuntimeHashSet::RemoveExternalStreamingObject(URuntimeHashExternalStreamingObjectBase* ExternalStreamingObject)
{
	if (Super::RemoveExternalStreamingObject(ExternalStreamingObject))
	{
		URuntimeHashSetExternalStreamingObject* HashSetExternalStreamingObject = CastChecked<URuntimeHashSetExternalStreamingObject>(ExternalStreamingObject);
		HashSetExternalStreamingObject->DestroyPartitionsSpatialIndex();
		UpdateRuntimeDataGridMap();
		return true;
	}

	return false;
}

// Streaming interface
void UWorldPartitionRuntimeHashSet::ForEachStreamingCells(TFunctionRef<bool(const UWorldPartitionRuntimeCell*)> Func) const
{
	auto ForEachCells = [this, &Func](const TArray<TObjectPtr<UWorldPartitionRuntimeCell>>& InCells)
	{
		for (UWorldPartitionRuntimeCell* Cell : InCells)
		{
			if (!Func(Cell))
			{
				return false;
			}
		}
		return true;
	};

	ForEachStreamingData([&ForEachCells](const FRuntimePartitionStreamingData& StreamingData)
	{
		return ForEachCells(StreamingData.SpatiallyLoadedCells) && ForEachCells(StreamingData.NonSpatiallyLoadedCells);
	});
}

void UWorldPartitionRuntimeHashSet::ForEachStreamingCellsQuery(const FWorldPartitionStreamingQuerySource& QuerySource, TFunctionRef<bool(const UWorldPartitionRuntimeCell*)> Func, FWorldPartitionQueryCache* QueryCache) const
{
	auto ShouldAddCell = [this](const UWorldPartitionRuntimeCell* Cell, const FWorldPartitionStreamingQuerySource& QuerySource)
	{
		if (IsCellRelevantFor(Cell->GetClientOnlyVisible()))
		{
			if (Cell->HasDataLayers())
			{
				if (Cell->GetDataLayers().FindByPredicate([&](const FName& DataLayerName) { return QuerySource.DataLayers.Contains(DataLayerName); }))
				{
					return true;
				}
			}
			else if (!QuerySource.bDataLayersOnly)
			{
				return true;
			}
		}

		return false;
	};

	auto ForEachSpatiallyLoadedCells = [&ShouldAddCell, QueryCache, &QuerySource, &Func]<typename SpatialIndexType>(SpatialIndexType* InSpatialIndex, int32 InLoadingRange, FName InGridName)
	{
		if (InSpatialIndex)
		{
			QuerySource.ForEachShape(InLoadingRange, InGridName, false, [InSpatialIndex, QueryCache, &ShouldAddCell, &QuerySource, &Func](const FSphericalSector& Shape)
			{
				auto ForEachIntersectingElement = [InSpatialIndex, QueryCache, &ShouldAddCell, &Shape, &QuerySource, &Func]<typename ShapeType>(const ShapeType& InShape)
				{
					InSpatialIndex->ForEachIntersectingElement(InShape, [QueryCache, &ShouldAddCell, &Shape, &QuerySource, &Func](UWorldPartitionRuntimeCell* RuntimeCell)
					{
						if (QueryCache)
						{
							QueryCache->AddCellInfo(RuntimeCell, Shape);
						}

						return !ShouldAddCell(RuntimeCell, QuerySource) || Func(RuntimeCell);
					});
				};

				if (Shape.IsSphere())
				{
					const FStaticSpatialIndex::FSphere Sphere(Shape.GetCenter(), Shape.GetRadius());
					ForEachIntersectingElement(Sphere);
				}
				else
				{
					const FStaticSpatialIndex::FCone Cone(Shape.GetCenter(), Shape.GetAxis(), Shape.GetRadius(), Shape.GetAngle());
					ForEachIntersectingElement(Cone);
				}
			});
		}

		return true;
	};

	auto ForEachNonSpatiallyLoadedCells = [&ShouldAddCell, &QuerySource, &Func](TArray<TObjectPtr<UWorldPartitionRuntimeCell>> InNonSpatiallyLoadedCells)
	{
		for (UWorldPartitionRuntimeCell* Cell : InNonSpatiallyLoadedCells)
		{
			if (ShouldAddCell(Cell, QuerySource))
			{
				if (!Func(Cell))
				{
					return false;
				}
			}
		}
		return true;
	};

	ForEachStreamingData([&QuerySource, &ForEachSpatiallyLoadedCells, &ForEachNonSpatiallyLoadedCells](const FRuntimePartitionStreamingData& StreamingData)
	{
		return ForEachSpatiallyLoadedCells(StreamingData.SpatialIndex.Get(), StreamingData.GetLoadingRange(), StreamingData.Name) && 
			   ForEachSpatiallyLoadedCells(StreamingData.SpatialIndex2D.Get(), StreamingData.GetLoadingRange(), StreamingData.Name) &&
			   ForEachNonSpatiallyLoadedCells(StreamingData.NonSpatiallyLoadedCells);
	});
}

void UWorldPartitionRuntimeHashSet::ForEachStreamingCellsSources(const TArray<FWorldPartitionStreamingSource>& Sources, TFunctionRef<bool(const UWorldPartitionRuntimeCell*, EStreamingSourceTargetState)> Func, const FWorldPartitionStreamingContext& InContext) const
{
	// Build a context when none is provided (for backward compatibility)
	const FWorldPartitionStreamingContext StackContext = !InContext.IsValid() ? FWorldPartitionStreamingContext::Create(GetTypedOuter<UWorld>()) : FWorldPartitionStreamingContext();
	const FWorldPartitionStreamingContext& Context = InContext.IsValid() ? InContext : StackContext;
	check(Context.IsValid());

	// Non-spatially loaded cells
	for (const FRuntimePartitionStreamingData* StreamingData : RuntimeNonSpatiallyLoadedDataGridList)
	{
		for (UWorldPartitionRuntimeCell* Cell : StreamingData->NonSpatiallyLoadedCells)
		{
#if WITH_EDITOR
			if (!IsCellRelevantFor(Cell->GetClientOnlyVisible()))
			{
				continue;
			}
#else
			check(IsCellRelevantFor(Cell->GetClientOnlyVisible()));
#endif
			const EDataLayerRuntimeState CellEffectiveWantedState = Cell->GetCellEffectiveWantedState(Context);
			if (CellEffectiveWantedState != EDataLayerRuntimeState::Unloaded)
			{
				Func(Cell, (CellEffectiveWantedState == EDataLayerRuntimeState::Loaded) ? EStreamingSourceTargetState::Loaded : EStreamingSourceTargetState::Activated);
			}
		}
	}

	// Spatially loaded cells
	for (const FWorldPartitionStreamingSource& Source : Sources)
	{
		// Build the source target grids based on target behavior
		TArray<FName, TInlineAllocator<8>> TargetGrids;
		if (Source.TargetBehavior == EStreamingSourceTargetBehavior::Include)
		{
			if (Source.TargetGrids.Num())
			{
				TargetGrids = Source.TargetGrids.Array();
			}
			else
			{
				RuntimeSpatiallyLoadedDataGridMap.GenerateKeyArray(TargetGrids);
			}
		}
		else if (Source.TargetBehavior == EStreamingSourceTargetBehavior::Exclude)
		{
			for (auto& [GridName, StreamingDataList] : RuntimeSpatiallyLoadedDataGridMap)
			{
				if (!Source.TargetGrids.Contains(GridName))
				{
					TargetGrids.Add(GridName);
				}
			}
		}

		for (FName GridName : TargetGrids)
		{
			if (const TArray<const FRuntimePartitionStreamingData*>* StreamingDataList = RuntimeSpatiallyLoadedDataGridMap.Find(GridName))
			{
				check(FStreamingSourceShapeHelper::IsSourceAffectingGrid(Source.TargetGrids, Source.TargetBehavior, GridName));

				for (const FRuntimePartitionStreamingData* StreamingData : *StreamingDataList)
				{
					Source.ForEachShape(StreamingData->GetLoadingRange(), false, [this, &Source, StreamingData, &Context, &Func](const FSphericalSector& Shape)
					{
						auto ForEachIntersectingElementFunc = [this, &Source, &Shape, &Context, &Func](UWorldPartitionRuntimeCell* Cell)
						{
#if WITH_EDITOR
							if (!IsCellRelevantFor(Cell->GetClientOnlyVisible()))
							{
								return;
							}
#else
							check(IsCellRelevantFor(Cell->GetClientOnlyVisible()));
#endif
							const EDataLayerRuntimeState CellEffectiveWantedState = Cell->GetCellEffectiveWantedState(Context);
							if (CellEffectiveWantedState != EDataLayerRuntimeState::Unloaded)
							{
								Cell->AppendStreamingSourceInfo(Source, Shape, Context);
								Func(Cell, ((CellEffectiveWantedState == EDataLayerRuntimeState::Loaded) || (Source.TargetState == EStreamingSourceTargetState::Loaded)) ? EStreamingSourceTargetState::Loaded : EStreamingSourceTargetState::Activated);
							}
						};

						auto ForEachIntersectingElement = [&StreamingData, &Source, &ForEachIntersectingElementFunc]<typename ShapeType>(const ShapeType& Shape)
						{
							if (StreamingData->SpatialIndex.IsValid())
							{
								if (Source.bForce2D)
								{
									StreamingData->SpatialIndexForce2D->ForEachIntersectingElement(Shape, ForEachIntersectingElementFunc);
								}
								else
								{
									StreamingData->SpatialIndex->ForEachIntersectingElement(Shape, ForEachIntersectingElementFunc);
								}
							}

							if (StreamingData->SpatialIndex2D.IsValid())
							{
								StreamingData->SpatialIndex2D->ForEachIntersectingElement(Shape, ForEachIntersectingElementFunc);
							}
						};

						if (Shape.IsSphere())
						{
							const FStaticSpatialIndex::FSphere ShapeSphere(Shape.GetCenter(), Shape.GetRadius());
							ForEachIntersectingElement(ShapeSphere);
						}
						else
						{
							const FStaticSpatialIndex::FCone ShapeCone(Shape.GetCenter(), Shape.GetAxis(), Shape.GetRadius(), Shape.GetAngle());
							ForEachIntersectingElement(ShapeCone);
						}
					});
				}
			}
		}
	}
}

bool UWorldPartitionRuntimeHashSet::SupportsWorldAssetStreaming(const FName& InTargetGrid)
{
	return ResolveRuntimePartition(InTargetGrid) != nullptr;
}

FGuid UWorldPartitionRuntimeHashSet::RegisterWorldAssetStreaming(const UWorldPartition::FRegisterWorldAssetStreamingParams& InParams)
{
	if (!InParams.IsValid())
	{
		UE_LOG(LogWorldPartition, Error, TEXT("RegisterWorldAssetStreaming: Invalid parameters provided."));
		return FGuid();
	}

	if (WorldAssetStreamingObjects.Contains(InParams.Guid))
	{
		UE_LOG(LogWorldPartition, Error, TEXT("RegisterWorldAssetStreaming: World asset guid '%s' was already registered."), *InParams.Guid.ToString());
		return FGuid();
	}

	FName RuntimePartitionName;
	const URuntimePartition* RuntimePartition = ResolveRuntimePartition(InParams.TargetGrid);
	if (!RuntimePartition)
	{
		UE_LOG(LogWorldPartition, Error, TEXT("RegisterWorldAssetStreaming: Unable to resolve TargetGrid '%s'."), *InParams.TargetGrid.ToString());
		return FGuid();
	}

	FName HLODRuntimePartitionName;
	const URuntimePartition* HLODRuntimePartition = nullptr;
	if (!InParams.WorldAssetHLOD.IsNull() && !InParams.TargetGridHLOD.IsNone())
	{
		HLODRuntimePartition = ResolveRuntimePartition(InParams.TargetGridHLOD);
		if (!HLODRuntimePartition)
		{
			UE_LOG(LogWorldPartition, Error, TEXT("RegisterWorldAssetStreaming: Unable to resolve TargetGridHLOD '%s'."), *InParams.TargetGridHLOD.ToString());
			return FGuid();
		}
	}

	URuntimeHashSetExternalStreamingObject* StreamingObject = CastChecked<URuntimeHashSetExternalStreamingObject>(CreateExternalStreamingObject(URuntimeHashSetExternalStreamingObject::StaticClass(), this, GetTypedOuter<UWorld>()));
	if (!StreamingObject)
	{
		UE_LOG(LogWorldPartition, Error, TEXT("RegisterWorldAssetStreaming: Couldn't create ExternalStreamingObject."));
		return FGuid();
	}		

	FGuid SourceCellGuid;
		
	// 2 passes : 1st for target grid, 2nd for HLOD target grid
	for (const bool bIsHLODPass : { false, true })
	{
		const URuntimePartition* TargetPartition = bIsHLODPass ? HLODRuntimePartition : RuntimePartition;
		if (!TargetPartition)
		{
			continue;
		}

		bool bClientOnlyVisible = TargetPartition->bClientOnlyVisible;
		bool bBlockOnSlowStreaming = TargetPartition->bBlockOnSlowStreaming;

		FRuntimePartitionStreamingData StreamingData;
		StreamingData.Name = TargetPartition->Name;
		StreamingData.LoadingRange = TargetPartition->LoadingRange;

		// Create Cell
		FName TargetPartitionName = TargetPartition->Name;
		FGuid InstanceGuid = InParams.Guid;
		FArchiveMD5 ArMD5;
		ArMD5 << TargetPartitionName << InstanceGuid;
		const FGuid CellGuid = ArMD5.GetGuidFromHash();
		check(CellGuid.IsValid());
		if (!bIsHLODPass)
		{
			SourceCellGuid = CellGuid;
		}

		const FString CellName = FString::Printf(TEXT("InjectedCell_%s"), *CellGuid.ToString());
		const TSoftObjectPtr<UWorld>& WorldAsset = bIsHLODPass ? InParams.WorldAssetHLOD : InParams.WorldAsset;

		if (UWorldPartitionRuntimeLevelStreamingCell* RuntimeCell = Cast<UWorldPartitionRuntimeLevelStreamingCell>(
			CreateRuntimeCell(UWorldPartitionRuntimeLevelStreamingCell::StaticClass(), UWorldPartitionRuntimeCellDataHashSet::StaticClass(), CellName, InParams.CellInstanceSuffix, StreamingObject)))
		{
			RuntimeCell->SetClientOnlyVisible(bClientOnlyVisible);
			RuntimeCell->SetBlockOnSlowLoading(bBlockOnSlowStreaming);
			RuntimeCell->SetIsHLOD(bIsHLODPass);
			RuntimeCell->SetGuid(CellGuid);
			RuntimeCell->SetCellDebugColor(FLinearColor::MakeRandomSeededColor(GetTypeHash(CellName)));

			if (bIsHLODPass)
			{
				RuntimeCell->SetSourceCellGuid(SourceCellGuid);
			}

			UWorldPartitionRuntimeCellDataHashSet* RuntimeCellData = CastChecked<UWorldPartitionRuntimeCellDataHashSet>(RuntimeCell->RuntimeCellData);
			RuntimeCellData->DebugName = CellName + InParams.CellInstanceSuffix;
			RuntimeCellData->CellBounds = InParams.Bounds;
			RuntimeCellData->ContentBounds = InParams.Bounds;
			RuntimeCellData->HierarchicalLevel = MAX_int32;
			RuntimeCellData->Priority = InParams.Priority;
			RuntimeCellData->GridName = TargetPartition->Name;
			RuntimeCellData->bIs2D = false;

			if (RuntimeCell->CreateAndSetLevelStreaming(WorldAsset, InParams.Transform))
			{
				StreamingData.SpatiallyLoadedCells.Add(RuntimeCell);
				StreamingObject->RuntimeStreamingData.Emplace(MoveTemp(StreamingData));
			}
			else
			{
				UE_LOG(LogWorldPartition, Error, TEXT("Error creating streaming cell %s for world asset %s at %s"), *RuntimeCell->GetName(), *WorldAsset.ToString(), *InParams.Transform.ToString());
				return FGuid();
			}
		}		
		else
		{
			UE_LOG(LogWorldPartition, Error, TEXT("Error creating streaming cell %s for world asset %s at %s"), *CellName, *WorldAsset.ToString(), *InParams.Transform.ToString());
			return FGuid();
		}
	}

	GetOuterUWorldPartition()->InjectExternalStreamingObject(StreamingObject);
	WorldAssetStreamingObjects.Add(InParams.Guid, StreamingObject);

	return InParams.Guid;
}

bool UWorldPartitionRuntimeHashSet::UnregisterWorldAssetStreaming(const FGuid& InWorldAssetStreamingGuid)
{
	if (TObjectPtr<URuntimeHashSetExternalStreamingObject>* StreamingObject = WorldAssetStreamingObjects.Find(InWorldAssetStreamingGuid))
	{
		// External streaming objects are created with a provided name which helps to detect invalid runtime states of injected content.
		// Before releasing these objects, trash their name to make sure they won't be recycled if the tile re-injects the objects before a GC was triggered first.
		// Apply the same logic on the LevelStreaming object of each injected cell as it is named using the injected cell name and is outered to the owning world.
		auto TrashExternalStreamingData = [](URuntimeHashSetExternalStreamingObject* InStreamingObject)
		{
			auto TrashObject = [](UObject* InObject)
			{
				FName NewUniqueTrashName = MakeUniqueObjectName(InObject->GetOuter(), InObject->GetClass(), NAME_TrashedPackage);
				InObject->Rename(*NewUniqueTrashName.ToString(), nullptr, REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
			};

			InStreamingObject->ForEachStreamingCells([TrashObject](UWorldPartitionRuntimeCell& Cell)
			{
				UWorldPartitionRuntimeLevelStreamingCell* InjectedCell = Cast<UWorldPartitionRuntimeLevelStreamingCell>(&Cell);
				if (UWorldPartitionLevelStreamingDynamic* LevelStreaming = InjectedCell ? InjectedCell->GetLevelStreaming() : nullptr)
				{
					TrashObject(LevelStreaming);
					// Make sure to flag this streaming level to be unloaded and removed as we don't want any future RequestLevel 
					// of a newly created streaming level of the same WorldAsset to fail. 
					LevelStreaming->SetIsRequestingUnloadAndRemoval(true);
				}
			});

			TrashObject(InStreamingObject);
		};

		if (IsValid(*StreamingObject))
		{
			if (IsValid(GetOuterUWorldPartition()))
			{
				GetOuterUWorldPartition()->RemoveExternalStreamingObject(*StreamingObject);
			}
			TrashExternalStreamingData(*StreamingObject);
		}
		WorldAssetStreamingObjects.Remove(InWorldAssetStreamingGuid);
		return true;
	}

	return false;
}

TArray<UWorldPartitionRuntimeCell*> UWorldPartitionRuntimeHashSet::GetWorldAssetStreamingCells(const FGuid& InWorldAssetStreamingGuid)
{
	TArray<UWorldPartitionRuntimeCell*> Result;
	if (TObjectPtr<URuntimeHashSetExternalStreamingObject>* StreamingObject = WorldAssetStreamingObjects.Find(InWorldAssetStreamingGuid))
	{
		(*StreamingObject)->ForEachStreamingCells([&Result](UWorldPartitionRuntimeCell& Cell) { Result.Add(&Cell); });
	}
	return Result;
}

#if WITH_EDITOR
void UWorldPartitionRuntimeHashSet::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	static FName NAME_RuntimePartitions(TEXT("RuntimePartitions"));
	static FName NAME_HLODSetups(TEXT("HLODSetups"));
	static FName NAME_HLODLayers(TEXT("HLODLayers"));

	FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(FRuntimePartitionDesc, Class))
	{
		int32 RuntimePartitionIndex = PropertyChangedEvent.GetArrayIndex(NAME_RuntimePartitions.ToString());
		check(RuntimePartitions.IsValidIndex(RuntimePartitionIndex));

		FRuntimePartitionDesc& RuntimePartitionDesc = RuntimePartitions[RuntimePartitionIndex];
		
		RuntimePartitionDesc.MainLayer = nullptr;

		if (RuntimePartitionDesc.Class)
		{
			RuntimePartitionDesc.Name = *FString::Printf(TEXT("%s_%d"), *RuntimePartitionDesc.Class->GetName(), RuntimePartitionIndex);
			RuntimePartitionDesc.MainLayer = NewObject<URuntimePartition>(this, RuntimePartitionDesc.Class, NAME_None);
			RuntimePartitionDesc.MainLayer->SetDefaultValues();
			RuntimePartitionDesc.MainLayer->Name = RuntimePartitionDesc.Name;
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FRuntimePartitionDesc, Name))
	{
		int32 RuntimePartitionIndex = PropertyChangedEvent.GetArrayIndex(NAME_RuntimePartitions.ToString());
		check(RuntimePartitions.IsValidIndex(RuntimePartitionIndex));

		FRuntimePartitionDesc& RuntimePartitionDesc = RuntimePartitions[RuntimePartitionIndex];

		int32 HLODSetupsIndex = PropertyChangedEvent.GetArrayIndex(NAME_HLODSetups.ToString());
		if (RuntimePartitionDesc.HLODSetups.IsValidIndex(HLODSetupsIndex))
		{
			FRuntimePartitionHLODSetup& RuntimePartitionHLODSetup = RuntimePartitionDesc.HLODSetups[HLODSetupsIndex];

			for (int32 CurHLODSetupsIndex = 0; CurHLODSetupsIndex < RuntimePartitionDesc.HLODSetups.Num(); CurHLODSetupsIndex++)
			{
				if (CurHLODSetupsIndex != HLODSetupsIndex)
				{
					if (RuntimePartitionHLODSetup.Name == RuntimePartitionDesc.HLODSetups[CurHLODSetupsIndex].Name)
					{
						RuntimePartitionHLODSetup.Name = *FString::Printf(TEXT("HLOD_%d"), RuntimePartitionHLODSetup.PartitionLayer->HLODIndex);
						break;
					}
				}
			}

			RuntimePartitionHLODSetup.PartitionLayer->Name = RuntimePartitionHLODSetup.Name;
		}
		else
		{
			for (int32 CurRuntimePartitionIndex = 0; CurRuntimePartitionIndex < RuntimePartitions.Num(); CurRuntimePartitionIndex++)
			{
				if (CurRuntimePartitionIndex != RuntimePartitionIndex)
				{
					if (RuntimePartitionDesc.Name == RuntimePartitions[CurRuntimePartitionIndex].Name)
					{
						RuntimePartitionDesc.Name = RuntimePartitionDesc.Class->GetFName();
						break;
					}
				}
			}

			RuntimePartitionDesc.MainLayer->Name = RuntimePartitionDesc.Name;
		}
	}
	else if (PropertyName == NAME_HLODSetups)
	{
		int32 RuntimePartitionIndex = PropertyChangedEvent.GetArrayIndex(NAME_RuntimePartitions.ToString());
		if (RuntimePartitions.IsValidIndex(RuntimePartitionIndex))
		{
			FRuntimePartitionDesc& RuntimePartitionDesc = RuntimePartitions[RuntimePartitionIndex];

			int32 HLODSetupsIndex = PropertyChangedEvent.GetArrayIndex(NAME_HLODSetups.ToString());
			if (RuntimePartitionDesc.HLODSetups.IsValidIndex(HLODSetupsIndex))
			{
				FRuntimePartitionHLODSetup& RuntimePartitionHLODSetup = RuntimePartitionDesc.HLODSetups[HLODSetupsIndex];
				URuntimePartition* ParentRuntimePartition = HLODSetupsIndex ? RuntimePartitionDesc.HLODSetups[HLODSetupsIndex - 1].PartitionLayer : RuntimePartitionDesc.MainLayer;
				RuntimePartitionHLODSetup.Name = *FString::Printf(TEXT("HLOD_%d"), HLODSetupsIndex);
				RuntimePartitionHLODSetup.bIsSpatiallyLoaded = true;
				RuntimePartitionHLODSetup.PartitionLayer = ParentRuntimePartition->CreateHLODRuntimePartition(HLODSetupsIndex);
				RuntimePartitionHLODSetup.PartitionLayer->Name = RuntimePartitionHLODSetup.Name;
			}
		}
	}
	else if (PropertyName == NAME_HLODLayers)
	{
		int32 RuntimePartitionIndex = PropertyChangedEvent.GetArrayIndex(NAME_RuntimePartitions.ToString());
		if (RuntimePartitions.IsValidIndex(RuntimePartitionIndex))
		{
			FRuntimePartitionDesc& RuntimePartitionDesc = RuntimePartitions[RuntimePartitionIndex];

			int32 HLODSetupsIndex = PropertyChangedEvent.GetArrayIndex(NAME_HLODSetups.ToString());
			if (RuntimePartitionDesc.HLODSetups.IsValidIndex(HLODSetupsIndex))
			{
				FRuntimePartitionHLODSetup& RuntimePartitionHLODSetup = RuntimePartitionDesc.HLODSetups[HLODSetupsIndex];

				int32 HLODLayersIndex = PropertyChangedEvent.GetArrayIndex(NAME_HLODLayers.ToString());
				if (RuntimePartitionHLODSetup.HLODLayers.IsValidIndex(HLODLayersIndex))
				{
					const UHLODLayer* HLODLayer = RuntimePartitionHLODSetup.HLODLayers[HLODLayersIndex];

					// Remove duplicated entries
					for (int32 CurrentHLODSetupsIndex = 0; CurrentHLODSetupsIndex < RuntimePartitionDesc.HLODSetups.Num(); CurrentHLODSetupsIndex++)
					{
						FRuntimePartitionHLODSetup& CurrentRuntimePartitionHLODSetup = RuntimePartitionDesc.HLODSetups[CurrentHLODSetupsIndex];
						for (int32 CurrentHLODLayerIndex = 0; CurrentHLODLayerIndex < CurrentRuntimePartitionHLODSetup.HLODLayers.Num(); CurrentHLODLayerIndex++)
						{
							const UHLODLayer* CurrentHLODLayer = CurrentRuntimePartitionHLODSetup.HLODLayers[CurrentHLODLayerIndex];
							if (((CurrentHLODSetupsIndex != HLODSetupsIndex) || (CurrentHLODLayerIndex != HLODLayersIndex)) && (CurrentHLODLayer == HLODLayer))
							{
								CurrentRuntimePartitionHLODSetup.HLODLayers.RemoveAt(CurrentHLODLayerIndex--);
								break;
							}
						}
					}
				}
			}
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FRuntimePartitionHLODSetup, bIsSpatiallyLoaded))
	{
		int32 RuntimePartitionIndex = PropertyChangedEvent.GetArrayIndex(NAME_RuntimePartitions.ToString());
		if (RuntimePartitions.IsValidIndex(RuntimePartitionIndex))
		{
			FRuntimePartitionDesc& RuntimePartitionDesc = RuntimePartitions[RuntimePartitionIndex];

			int32 HLODSetupsIndex = PropertyChangedEvent.GetArrayIndex(NAME_HLODSetups.ToString());
			if (RuntimePartitionDesc.HLODSetups.IsValidIndex(HLODSetupsIndex))
			{
				FRuntimePartitionHLODSetup& RuntimePartitionHLODSetup = RuntimePartitionDesc.HLODSetups[HLODSetupsIndex];

				if (RuntimePartitionHLODSetup.bIsSpatiallyLoaded)
				{
					URuntimePartition* ParentRuntimePartition = HLODSetupsIndex ? RuntimePartitionDesc.HLODSetups[HLODSetupsIndex - 1].PartitionLayer : RuntimePartitionDesc.MainLayer;
					RuntimePartitionHLODSetup.PartitionLayer = ParentRuntimePartition->CreateHLODRuntimePartition(HLODSetupsIndex);
					RuntimePartitionHLODSetup.PartitionLayer->Name = RuntimePartitionHLODSetup.Name;
				}
				else
				{
					RuntimePartitionHLODSetup.PartitionLayer = NewObject<URuntimePartitionPersistent>(this, NAME_None);
					RuntimePartitionHLODSetup.PartitionLayer->Name = RuntimePartitionHLODSetup.Name;
				}
			}
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FRuntimePartitionDesc, MainLayer))
	{
		int32 RuntimePartitionIndex = PropertyChangedEvent.GetArrayIndex(NAME_RuntimePartitions.ToString());
		if (RuntimePartitions.IsValidIndex(RuntimePartitionIndex))
		{
			for (const FRuntimePartitionHLODSetup& HLODSetup : RuntimePartitions[RuntimePartitionIndex].HLODSetups)
			{
				if (HLODSetup.PartitionLayer)
				{
					HLODSetup.PartitionLayer->UpdateHLODRuntimePartitionFrom(RuntimePartitions[RuntimePartitionIndex].MainLayer);
				}
			}
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UWorldPartitionRuntimeHashSet, RuntimePartitions))
	{
		if (RuntimePartitions.IsEmpty())
		{
			// Reapply the default values as we need at least a main partition
			SetDefaultValues();
		}
	}
}

void UWorldPartitionRuntimeHashSet::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
	
	// In PIE, create streaming datas spatial indexes and runtime grid data maps right after world duplication to allow making spatial queries from calls 
	// to BlockTillLevelStreamingCompleted before the world has issued OnBeginPlay (see UGameInstance::StartPlayInEditorGameInstance).
	if (DuplicateMode == EDuplicateMode::PIE)
	{
		ForEachStreamingData([this](const FRuntimePartitionStreamingData& StreamingData) { StreamingData.CreatePartitionsSpatialIndex(); return true; });
		UpdateRuntimeDataGridMap();
	}
}

void UWorldPartitionRuntimeHashSet::PostLoad()
{
	Super::PostLoad();

	// Make sure that HLOD partitions settings are updated to match the main layer settings
	for (const FRuntimePartitionDesc& RuntimePartition : RuntimePartitions)
	{
		for (const FRuntimePartitionHLODSetup& HLODSetup : RuntimePartition.HLODSetups)
		{
			if (HLODSetup.PartitionLayer)
			{
				HLODSetup.PartitionLayer->UpdateHLODRuntimePartitionFrom(RuntimePartition.MainLayer);
			}
		}
	}
}

UWorldPartitionRuntimeHashSet::FCellUniqueId UWorldPartitionRuntimeHashSet::GetCellUniqueId(const URuntimePartition::FCellDescInstance& InCellDescInstance) const
{
	FCellUniqueId CellUniqueId;
	FName CellNameID = InCellDescInstance.Name;
	FDataLayersID DataLayersID(InCellDescInstance.DataLayerInstances);
	FGuid ContentBundleID(InCellDescInstance.ContentBundleID);

	// Build cell unique name
	{
		UWorld* OuterWorld = GetTypedOuter<UWorld>();
		check(OuterWorld);

		FString WorldName = FPackageName::GetShortName(OuterWorld->GetPackage());

		if (!IsRunningCookCommandlet() && OuterWorld->IsGameWorld())
		{
			FString SourceWorldPath;
			FString InstancedWorldPath;
			if (OuterWorld->GetSoftObjectPathMapping(SourceWorldPath, InstancedWorldPath))
			{
				const FTopLevelAssetPath SourceAssetPath(SourceWorldPath);
				WorldName = FPackageName::GetShortName(SourceAssetPath.GetPackageName());
						
				InstancedWorldPath = UWorld::RemovePIEPrefix(InstancedWorldPath);

				const FString SourcePackageName = SourceAssetPath.GetPackageName().ToString();
				const FTopLevelAssetPath InstanceAssetPath(InstancedWorldPath);
				const FString InstancePackageName = InstanceAssetPath.GetPackageName().ToString();

				if (int32 Index = InstancePackageName.Find(SourcePackageName); Index != INDEX_NONE)
				{
					CellUniqueId.InstanceSuffix = InstancePackageName.Mid(Index + SourcePackageName.Len());
				}
			}
		}

		TStringBuilder<128> CellNameBuilder;
		CellNameBuilder.Appendf(TEXT("%s_%s"), *WorldName, *CellNameID.ToString());

		if (DataLayersID.GetHash())
		{
			CellNameBuilder.Appendf(TEXT("_d%X"), DataLayersID.GetHash());
		}

		if (ContentBundleID.IsValid())
		{
			CellNameBuilder.Appendf(TEXT("_c%s"), *UContentBundleDescriptor::GetContentBundleCompactString(ContentBundleID));
		}

		CellUniqueId.Name = CellNameBuilder.ToString();
	}

	// Build cell guid
	{
		FArchiveMD5 ArMD5;
		ArMD5 << CellNameID << DataLayersID << ContentBundleID;
		InCellDescInstance.SourcePartition->AppendCellGuid(ArMD5);
		CellUniqueId.Guid = ArMD5.GetGuidFromHash();
		check(CellUniqueId.Guid.IsValid());
	}

	return CellUniqueId;
}
#endif

void UWorldPartitionRuntimeHashSet::ForEachStreamingData(TFunctionRef<bool(const FRuntimePartitionStreamingData&)> Func) const
{
	for (const FRuntimePartitionStreamingData& StreamingData : RuntimeStreamingData)
	{
		if (!Func(StreamingData))
		{
			return;
		}
	}

	for (const TWeakObjectPtr<URuntimeHashExternalStreamingObjectBase>& InjectedExternalStreamingObject : InjectedExternalStreamingObjects)
	{
		if (InjectedExternalStreamingObject.IsValid())
		{
			URuntimeHashSetExternalStreamingObject* ExternalStreamingObject = CastChecked<URuntimeHashSetExternalStreamingObject>(InjectedExternalStreamingObject.Get());
			
			for (const FRuntimePartitionStreamingData& StreamingData : ExternalStreamingObject->RuntimeStreamingData)
			{
				if (!Func(StreamingData))
				{
					return;
				}
			}
		}
	}
}

void UWorldPartitionRuntimeHashSet::UpdateRuntimeDataGridMap()
{
	RuntimeSpatiallyLoadedDataGridMap.Reset();
	RuntimeNonSpatiallyLoadedDataGridList.Reset();

	ForEachStreamingData([this](const FRuntimePartitionStreamingData& StreamingData)
	{
		if (StreamingData.SpatiallyLoadedCells.Num())
		{
			TArray<const FRuntimePartitionStreamingData*>& StreamingDataList = RuntimeSpatiallyLoadedDataGridMap.FindOrAdd(StreamingData.Name);
			StreamingDataList.Add(&StreamingData);
		}

		if (StreamingData.NonSpatiallyLoadedCells.Num())
		{
			RuntimeNonSpatiallyLoadedDataGridList.Add(&StreamingData);
		}
		return true;
	});
	
	if (RuntimeSpatiallyLoadedDataGridMap.IsEmpty())
	{
		RuntimeSpatiallyLoadedDataGridMap.Empty();
	}
	if (RuntimeNonSpatiallyLoadedDataGridList.IsEmpty())
	{
		RuntimeNonSpatiallyLoadedDataGridList.Empty();
	}
}
