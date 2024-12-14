// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityManager.h"
#include "MassEntityManagerConstants.h"
#include "MassArchetypeData.h"
#include "MassCommandBuffer.h"
#include "MassEntityManagerStorage.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"
#include "VisualLogger/VisualLogger.h"
#include "MassExecutionContext.h"
#include "MassDebugger.h"
#include "Misc/Fork.h"
#include "Misc/CoreDelegates.h"
#include "Algo/Find.h"
#include "MassEntityUtils.h"

const FMassEntityHandle FMassEntityManager::InvalidEntity;

namespace UE::Mass::Private
{
	// note: this function doesn't set EntityHandle.SerialNumber
	void ConvertArchetypelessSubchunksIntoEntityHandles(FMassArchetypeEntityCollection::FConstEntityRangeArrayView Subchunks, TArray<FMassEntityHandle>& OutEntityHandles)
	{
		int32 TotalCount = 0;
		for (const FMassArchetypeEntityCollection::FArchetypeEntityRange& Subchunk : Subchunks)
		{
			TotalCount += Subchunk.Length;
		}

		int32 Index = OutEntityHandles.Num();
		OutEntityHandles.AddDefaulted(TotalCount);

		for (const FMassArchetypeEntityCollection::FArchetypeEntityRange& Subchunk : Subchunks)
		{
			for (int i = Subchunk.SubchunkStart; i < Subchunk.SubchunkStart + Subchunk.Length; ++i)
			{
				OutEntityHandles[Index++].Index = i;
			}
		}
	}
}

//-----------------------------------------------------------------------------
// FMassEntityManager::FEntityCreationContext
//-----------------------------------------------------------------------------
FMassEntityManager::FEntityCreationContext::FEntityCreationContext()
	: OwnerThreadId(FPlatformTLS::GetCurrentThreadId())
{	
}

FMassEntityManager::FEntityCreationContext::FEntityCreationContext(FMassEntityManager& InManager, const TConstArrayView<FMassEntityHandle> InCreatedEntities)
	: FEntityCreationContext()
{
	CreatedEntities = InCreatedEntities;
	Manager = InManager.AsShared();
}

FMassEntityManager::FEntityCreationContext::FEntityCreationContext(FMassEntityManager& InManager, const TConstArrayView<FMassEntityHandle> InCreatedEntities
	, FMassArchetypeEntityCollection&& InEntityCollection)
	: FEntityCreationContext(InManager, InCreatedEntities)
{
	checkf(InCreatedEntities.IsEmpty() || InEntityCollection.IsEmpty() == false, TEXT("Trying to create FEntityCreationContext instance with no entities but non-empty entity collection. This is not supported."))
	if (InCreatedEntities.IsEmpty() == false)
	{
		EntityCollections.Add(MoveTemp(InEntityCollection));
	}
}

FMassEntityManager::FEntityCreationContext::~FEntityCreationContext()
{
	if (((EntityCollections.IsEmpty() == false) || (CreatedEntities.IsEmpty() == false)) && ensure(Manager))
	{
		Manager->GetObserverManager().OnPostEntitiesCreated(GetEntityCollections());
	}
}

TConstArrayView<FMassArchetypeEntityCollection> FMassEntityManager::FEntityCreationContext::GetEntityCollections() const
{
	// the EntityCollection has been dirtied, we need to rebuild it
	if (IsDirty() && ensure(Manager))
	{
		UE::Mass::Utils::CreateEntityCollections(*Manager.Get(), CreatedEntities, CollectionCreationDuplicatesHandling, EntityCollections);
	}

	return EntityCollections;
}

void FMassEntityManager::FEntityCreationContext::MarkDirty()
{
	checkf(OwnerThreadId == FPlatformTLS::GetCurrentThreadId(), TEXT("%hs: all FEntityCreationContext operations ere expected to be run in a single thread"), __FUNCTION__);

	EntityCollections.Reset();
}

void FMassEntityManager::FEntityCreationContext::AppendEntities(const TConstArrayView<FMassEntityHandle> EntitiesToAppend)
{
	checkf(OwnerThreadId == FPlatformTLS::GetCurrentThreadId(), TEXT("%hs: all FEntityCreationContext operations ere expected to be run in a single thread"), __FUNCTION__);

	if (EntitiesToAppend.Num())
	{
		if (CreatedEntities.Num())
		{
			// since we already have entities in CreatedEntities (initially ensured to have no duplicates) we cannot 
			// guarantee anymore that we'll have no duplicates after adding EntitiesToAppend
			CollectionCreationDuplicatesHandling = FMassArchetypeEntityCollection::FoldDuplicates;
			MarkDirty();
		}
		// else, if there are no entities the resulting state will be "dirty" by design
		ensureMsgf(EntityCollections.IsEmpty(), TEXT("Having a non-empty array of entity collections is unexpected at this point!"));

		CreatedEntities.Append(EntitiesToAppend);
		ensure(IsDirty());
	}
}

void FMassEntityManager::FEntityCreationContext::AppendEntities(const TConstArrayView<FMassEntityHandle> EntitiesToAppend, FMassArchetypeEntityCollection&& InEntityCollection)
{
	checkf(OwnerThreadId == FPlatformTLS::GetCurrentThreadId(), TEXT("%hs: all FEntityCreationContext operations ere expected to be run in a single thread"), __FUNCTION__);

	if (EntitiesToAppend.Num() == 0)
	{
		return;
	}
	
	AppendEntities(EntitiesToAppend);

	// this condition boils down to checking if this FEntityCreationContext instance only connects the just added EntitiesToAppend
	if (CreatedEntities.Num() == EntitiesToAppend.Num())
	{
		checkf(EntityCollections.Num() == 0, TEXT("We never expect EntityCollections to be non-empty while there are no entities in CreatedEntities."));
		EntityCollections.Add(MoveTemp(InEntityCollection));
	}
}

void FMassEntityManager::FEntityCreationContext::ForceUpdateCurrentThreadID()
{
	OwnerThreadId = FPlatformTLS::GetCurrentThreadId();
}

//-----------------------------------------------------------------------------
// FMassEntityManager
//-----------------------------------------------------------------------------
#if MASS_CONCURRENT_RESERVE
UE::Mass::IEntityStorageInterface& FMassEntityManager::GetEntityStorageInterface()
{
	using namespace UE::Mass;
	struct StorageSelector
	{
		UE::Mass::IEntityStorageInterface* operator()(FEmptyVariantState&) const
		{
			checkf(false, TEXT("Attempt to use EntityStorageInterface without initialization"));
			return nullptr;
		}
		UE::Mass::IEntityStorageInterface* operator()(FSingleThreadedEntityStorage& Storage) const
		{
			return &Storage;
		}
		UE::Mass::IEntityStorageInterface* operator()(FConcurrentEntityStorage& Storage) const
		{
			return &Storage;
		}
	};

	UE::Mass::IEntityStorageInterface* Interface = Visit(StorageSelector{}, EntityStorage);

	return *Interface;
}

const UE::Mass::IEntityStorageInterface& FMassEntityManager::GetEntityStorageInterface() const
{
	using namespace UE::Mass;
	struct StorageSelector
	{
		const UE::Mass::IEntityStorageInterface* operator()(const FEmptyVariantState&) const
		{
			checkf(false, TEXT("Attempt to use EntityStorageInterface without initialization"));
			return nullptr;
		}
		const UE::Mass::IEntityStorageInterface* operator()(const FSingleThreadedEntityStorage& Storage) const
		{
			return &Storage;
		}
		const UE::Mass::IEntityStorageInterface* operator()(const FConcurrentEntityStorage& Storage) const
		{
			return &Storage;
		}
	};

	const UE::Mass::IEntityStorageInterface* Interface = Visit(StorageSelector{}, EntityStorage);

	return *Interface;
}
#else
UE::Mass::FSingleThreadedEntityStorage& FMassEntityManager::GetEntityStorageInterface()
{
	// Get will assert if not initialized
	return EntityStorage.Get<UE::Mass::FSingleThreadedEntityStorage>();
}

const UE::Mass::FSingleThreadedEntityStorage& FMassEntityManager::GetEntityStorageInterface() const
{
	// Get will assert if not initialized
	return EntityStorage.Get<UE::Mass::FSingleThreadedEntityStorage>();
}
#endif

#if WITH_MASSENTITY_DEBUG
UE::Mass::IEntityStorageInterface& FMassEntityManager::DebugGetEntityStorageInterface()
{
	return GetEntityStorageInterface();
}

const UE::Mass::IEntityStorageInterface& FMassEntityManager::DebugGetEntityStorageInterface() const
{
	return GetEntityStorageInterface();
}
#endif

//-----------------------------------------------------------------------------
// FMassEntityManager
//-----------------------------------------------------------------------------
FMassEntityManager::FMassEntityManager(UObject* InOwner)
	: ObserverManager(*this)
	, Owner(InOwner)
{
#if WITH_MASSENTITY_DEBUG
	DebugName = InOwner ? (InOwner->GetName() + TEXT("_EntityManager")) : TEXT("Unset");
#endif
}

FMassEntityManager::~FMassEntityManager()
{
	if (bInitialized)
	{
		Deinitialize();
	}
}

void FMassEntityManager::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	SIZE_T MyExtraSize = GetEntityStorageInterface().GetAllocatedSize()
		+ FragmentHashToArchetypeMap.GetAllocatedSize()
		+ FragmentTypeToArchetypeMap.GetAllocatedSize();

	for (const TSharedPtr<FMassCommandBuffer>& CommandBuffer : DeferredCommandBuffers)
	{
		MyExtraSize += (CommandBuffer ? CommandBuffer->GetAllocatedSize() : 0);
	}
	
	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(MyExtraSize);

	for (const auto& KVP : FragmentHashToArchetypeMap)
	{
		for (const TSharedPtr<FMassArchetypeData>& ArchetypePtr : KVP.Value)
		{
			CumulativeResourceSize.AddDedicatedSystemMemoryBytes(ArchetypePtr->GetAllocatedSize());
		}
	}
}

void FMassEntityManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (FConstSharedStruct& Struct : ConstSharedFragments)
	{
		Struct.AddStructReferencedObjects(Collector);
	}

	for (FSharedStruct& Struct : SharedFragments)
	{
		Struct.AddStructReferencedObjects(Collector);
	}
 
	const class UScriptStruct* ScriptStruct = FMassObserverManager::StaticStruct();
	TWeakObjectPtr<const UScriptStruct> ScriptStructPtr{ScriptStruct};
	Collector.AddReferencedObjects(ScriptStructPtr, &ObserverManager);
}

void FMassEntityManager::Initialize()
{
	FMassEntityManagerStorageInitParams InitializationParams;
	InitializationParams.Emplace<FMassEntityManager_InitParams_SingleThreaded>();
	Initialize(InitializationParams);
}

namespace UE::Mass::Private
{
	struct FEntityStorageInitializer
	{
		void operator()(const FMassEntityManager_InitParams_SingleThreaded& Params)
		{
			EntityStorage->Emplace<UE::Mass::FSingleThreadedEntityStorage>();
			EntityStorage->Get<FSingleThreadedEntityStorage>().Initialize(Params);
		}
		void operator()(const FMassEntityManager_InitParams_Concurrent& Params)
		{
#if MASS_CONCURRENT_RESERVE
			EntityStorage->Emplace<UE::Mass::FConcurrentEntityStorage>();
			EntityStorage->Get<UE::Mass::FConcurrentEntityStorage>().Initialize(Params);
#else
			checkf(false, TEXT("Mass does not support this storage backend"));
#endif
		}
		
		FMassEntityManager::FEntityStorageContainerType* EntityStorage = nullptr;
	};
}

void FMassEntityManager::Initialize(const FMassEntityManagerStorageInitParams& InitializationParams)
{
	if (bInitialized)
	{
		UE_LOG(LogMass, Log, TEXT("Calling %hs on already initialized entity manager owned by %s")
			, __FUNCTION__, *GetNameSafe(Owner.Get()));
		return;
	}

	Visit(UE::Mass::Private::FEntityStorageInitializer{&EntityStorage}, InitializationParams);

	for (TSharedPtr<FMassCommandBuffer>& CommandBuffer : DeferredCommandBuffers)
	{
		CommandBuffer = MakeShareable(new FMassCommandBuffer());
	}

	// if we get forked we need to update the command buffer's CurrentThreadID
	if (FForkProcessHelper::IsForkRequested())
	{
		OnPostForkHandle = FCoreDelegates::OnPostFork.AddSP(AsShared(), &FMassEntityManager::OnPostFork);
	}

	// creating these bitset instances to populate respective bitset types' StructTrackers
	FMassFragmentBitSet Fragments;
	FMassTagBitSet Tags;
	FMassChunkFragmentBitSet ChunkFragments;
	FMassSharedFragmentBitSet LocalSharedFragments;

	for (TObjectIterator<UScriptStruct> StructIt; StructIt; ++StructIt)
	{
		if (StructIt->IsChildOf(FMassFragment::StaticStruct()))
		{
			if (*StructIt != FMassFragment::StaticStruct())
			{
				Fragments.Add(**StructIt);
			}
		}
		else if (StructIt->IsChildOf(FMassTag::StaticStruct()))
		{
			if (*StructIt != FMassTag::StaticStruct())
			{
				Tags.Add(**StructIt);
			}
		}
		else if (StructIt->IsChildOf(FMassChunkFragment::StaticStruct()))
		{
			if (*StructIt != FMassChunkFragment::StaticStruct())
			{
				ChunkFragments.Add(**StructIt);
			}
		}
		else if (StructIt->IsChildOf(FMassSharedFragment::StaticStruct()))
		{
			if (*StructIt != FMassSharedFragment::StaticStruct())
			{
				LocalSharedFragments.Add(**StructIt);
			}
		}
	}
#if WITH_MASSENTITY_DEBUG
	RequirementAccessDetector.Initialize();
	FMassDebugger::RegisterEntityManager(*this);
#endif // WITH_MASSENTITY_DEBUG

	bInitialized = true;
	bFirstCommandFlush = true;
}

void FMassEntityManager::PostInitialize()
{
	ensure(bInitialized);
	// this needs to be done after all the subsystems have been initialized since some processors might want to access
	// them during processors' initialization
	ObserverManager.Initialize();
}

void FMassEntityManager::Deinitialize()
{
	if (bInitialized)
	{
		FCoreDelegates::OnPostFork.Remove(OnPostForkHandle);

		// closing down so no point in actually flushing commands, but need to clean them up to avoid warnings on destruction
		for (TSharedPtr<FMassCommandBuffer>& CommandBuffer : DeferredCommandBuffers)
		{
			if (CommandBuffer)
			{
				CommandBuffer->CleanUp();
			}
		}

#if WITH_MASSENTITY_DEBUG
		FMassDebugger::UnregisterEntityManager(*this);
#endif // WITH_MASSENTITY_DEBUG

		EntityStorage.Emplace<FEmptyVariantState>();

		ObserverManager.DeInitialize();
		
		bInitialized = false;
	}
	else
	{
		UE_LOG(LogMass, Log, TEXT("Calling %hs on already deinitialized entity manager owned by %s")
			, __FUNCTION__, *GetNameSafe(Owner.Get()));
	}
}

void FMassEntityManager::OnPostFork(EForkProcessRole Role)
{
	if (Role == EForkProcessRole::Child)
	{
		for (TSharedPtr<FMassCommandBuffer>& CommandBuffer : DeferredCommandBuffers)
		{
			if (CommandBuffer)
			{
				CommandBuffer->ForceUpdateCurrentThreadID();
			}
			else
			{
				CommandBuffer = MakeShareable(new FMassCommandBuffer());
			}
		}

		if (TSharedPtr<FEntityCreationContext> ActiveContext = ActiveCreationContext.Pin())
		{
			ActiveContext->ForceUpdateCurrentThreadID();
		}
	}
}

FMassArchetypeHandle FMassEntityManager::CreateArchetype(TConstArrayView<const UScriptStruct*> FragmentsAndTagsList, const FMassArchetypeCreationParams& CreationParams)
{
	FMassArchetypeCompositionDescriptor Composition;
	InternalAppendFragmentsAndTagsToArchetypeCompositionDescriptor(Composition, FragmentsAndTagsList);
	return CreateArchetype(Composition, CreationParams);
}

FMassArchetypeHandle FMassEntityManager::CreateArchetype(FMassArchetypeHandle SourceArchetype, TConstArrayView<const UScriptStruct*> FragmentsAndTagsList)
{
	const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(SourceArchetype); 
	return CreateArchetype(SourceArchetype, FragmentsAndTagsList, FMassArchetypeCreationParams(ArchetypeData));
}

FMassArchetypeHandle FMassEntityManager::CreateArchetype(FMassArchetypeHandle SourceArchetype,
	TConstArrayView<const UScriptStruct*> FragmentsAndTagsList, const FMassArchetypeCreationParams& CreationParams)
{
	const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(SourceArchetype);
	FMassArchetypeCompositionDescriptor Composition = ArchetypeData.GetCompositionDescriptor();
	InternalAppendFragmentsAndTagsToArchetypeCompositionDescriptor(Composition, FragmentsAndTagsList);
	return CreateArchetype(Composition, CreationParams);
}

FMassArchetypeHandle FMassEntityManager::CreateArchetype(const TSharedPtr<FMassArchetypeData>& SourceArchetype, const FMassFragmentBitSet& AddedFragments)
{
	return CreateArchetype(SourceArchetype, AddedFragments, FMassArchetypeCreationParams(*SourceArchetype));
}

FMassArchetypeHandle FMassEntityManager::CreateArchetype(const TSharedPtr<FMassArchetypeData>& SourceArchetype, const FMassFragmentBitSet& AddedFragments, const FMassArchetypeCreationParams& CreationParams)
{
	check(SourceArchetype.IsValid());
	checkf(AddedFragments.IsEmpty() == false, TEXT("%hs Adding an empty fragment list to an archetype is not supported."), __FUNCTION__);

	const FMassArchetypeCompositionDescriptor Composition(AddedFragments + SourceArchetype->GetFragmentBitSet()
		, SourceArchetype->GetTagBitSet()
		, SourceArchetype->GetChunkFragmentBitSet()
		, SourceArchetype->GetSharedFragmentBitSet()
		, SourceArchetype->GetConstSharedFragmentBitSet());
	return CreateArchetype(Composition, CreationParams);
}

FMassArchetypeHandle FMassEntityManager::GetOrCreateSuitableArchetype(const FMassArchetypeHandle& ArchetypeHandle
	, const FMassSharedFragmentBitSet& SharedFragmentBitSet
	, const FMassConstSharedFragmentBitSet& ConstSharedFragmentBitSet
	, const FMassArchetypeCreationParams& CreationParams)
{
	const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
	if (SharedFragmentBitSet != ArchetypeData.GetSharedFragmentBitSet()
		|| ConstSharedFragmentBitSet != ArchetypeData.GetConstSharedFragmentBitSet())
	{
		FMassArchetypeCompositionDescriptor NewDescriptor = ArchetypeData.GetCompositionDescriptor();
		NewDescriptor.SharedFragments = SharedFragmentBitSet;
		NewDescriptor.ConstSharedFragments = ConstSharedFragmentBitSet;
		return CreateArchetype(NewDescriptor);
	}
	return ArchetypeHandle;
}

FMassArchetypeHandle FMassEntityManager::CreateArchetype(const FMassArchetypeCompositionDescriptor& Composition, const FMassArchetypeCreationParams& CreationParams)
{
	const uint32 TypeHash = Composition.CalculateHash();

	TArray<TSharedPtr<FMassArchetypeData>>& HashRow = FragmentHashToArchetypeMap.FindOrAdd(TypeHash);

	TSharedPtr<FMassArchetypeData> ArchetypeDataPtr;
	for (const TSharedPtr<FMassArchetypeData>& Ptr : HashRow)
	{
		if (Ptr->IsEquivalent(Composition))
		{
#if WITH_MASSENTITY_DEBUG
			// Keep track of all names for this archetype.
			if (!CreationParams.DebugName.IsNone())
			{
				Ptr->AddUniqueDebugName(CreationParams.DebugName);
			}
#endif // WITH_MASSENTITY_DEBUG
			if (CreationParams.ChunkMemorySize > 0 && CreationParams.ChunkMemorySize != Ptr->GetChunkAllocSize())
			{
				UE_LOG(LogMass, Warning, TEXT("Reusing existing Archetype, but the requested ChunkMemorySize is different. Requested %d, existing: %llu")
					, CreationParams.ChunkMemorySize, Ptr->GetChunkAllocSize());
			}
			ArchetypeDataPtr = Ptr;
			break;
		}
	}

	if (!ArchetypeDataPtr.IsValid())
	{
		// Important to pre-increment the version as the queries will use this value to do incremental updates
		++ArchetypeDataVersion;

		// Create a new archetype
		FMassArchetypeData* NewArchetype = new FMassArchetypeData(CreationParams);
		NewArchetype->Initialize(Composition, ArchetypeDataVersion);
		ArchetypeDataPtr = HashRow.Add_GetRef(MakeShareable(NewArchetype));
		AllArchetypes.Add(ArchetypeDataPtr);
		ensure(AllArchetypes.Num() == ArchetypeDataVersion);

		for (const FMassArchetypeFragmentConfig& FragmentConfig : NewArchetype->GetFragmentConfigs())
		{
			checkSlow(FragmentConfig.FragmentType)
			FragmentTypeToArchetypeMap.FindOrAdd(FragmentConfig.FragmentType).Add(ArchetypeDataPtr);
		}

		OnNewArchetypeEvent.Broadcast(FMassArchetypeHandle(ArchetypeDataPtr));
	}

	return FMassArchetypeHelper::ArchetypeHandleFromData(ArchetypeDataPtr);
}

FMassArchetypeHandle FMassEntityManager::InternalCreateSimilarArchetype(const TSharedPtr<FMassArchetypeData>& SourceArchetype, const FMassTagBitSet& OverrideTags)
{
	checkSlow(SourceArchetype.IsValid());
	const FMassArchetypeData& SourceArchetypeRef = *SourceArchetype.Get();
	FMassArchetypeCompositionDescriptor NewComposition(SourceArchetypeRef.GetFragmentBitSet()
		, OverrideTags
		, SourceArchetypeRef.GetChunkFragmentBitSet()
		, SourceArchetypeRef.GetSharedFragmentBitSet()
		, SourceArchetypeRef.GetConstSharedFragmentBitSet());
	return InternalCreateSimilarArchetype(SourceArchetypeRef, MoveTemp(NewComposition));
}

FMassArchetypeHandle FMassEntityManager::InternalCreateSimilarArchetype(const TSharedPtr<FMassArchetypeData>& SourceArchetype, const FMassFragmentBitSet& OverrideFragments)
{
	checkSlow(SourceArchetype.IsValid());
	const FMassArchetypeData& SourceArchetypeRef = *SourceArchetype.Get();
	FMassArchetypeCompositionDescriptor NewComposition(OverrideFragments
		, SourceArchetypeRef.GetTagBitSet()
		, SourceArchetypeRef.GetChunkFragmentBitSet()
		, SourceArchetypeRef.GetSharedFragmentBitSet()
		, SourceArchetypeRef.GetConstSharedFragmentBitSet());
	return InternalCreateSimilarArchetype(SourceArchetypeRef, MoveTemp(NewComposition));
}

FMassArchetypeHandle FMassEntityManager::InternalCreateSimilarArchetype(const FMassArchetypeData& SourceArchetypeRef, FMassArchetypeCompositionDescriptor&& NewComposition)
{
	const uint32 TypeHash = NewComposition.CalculateHash();

	TArray<TSharedPtr<FMassArchetypeData>>& HashRow = FragmentHashToArchetypeMap.FindOrAdd(TypeHash);

	TSharedPtr<FMassArchetypeData> ArchetypeDataPtr;
	for (const TSharedPtr<FMassArchetypeData>& Ptr : HashRow)
	{
		if (Ptr->IsEquivalent(NewComposition))
		{
			ArchetypeDataPtr = Ptr;
			break;
		}
	}

	if (!ArchetypeDataPtr.IsValid())
	{
		// Important to pre-increment the version as the queries will use this value to do incremental updates
		++ArchetypeDataVersion;

		// Create a new archetype
		FMassArchetypeData* NewArchetype = new FMassArchetypeData(FMassArchetypeCreationParams(SourceArchetypeRef));
		NewArchetype->InitializeWithSimilar(SourceArchetypeRef, MoveTemp(NewComposition), ArchetypeDataVersion);
		NewArchetype->CopyDebugNamesFrom(SourceArchetypeRef);

		ArchetypeDataPtr = HashRow.Add_GetRef(MakeShareable(NewArchetype));
		AllArchetypes.Add(ArchetypeDataPtr);
		ensure(AllArchetypes.Num() == ArchetypeDataVersion);

		for (const FMassArchetypeFragmentConfig& FragmentConfig : NewArchetype->GetFragmentConfigs())
		{
			checkSlow(FragmentConfig.FragmentType)
			FragmentTypeToArchetypeMap.FindOrAdd(FragmentConfig.FragmentType).Add(ArchetypeDataPtr);
		}

		OnNewArchetypeEvent.Broadcast(FMassArchetypeHandle(ArchetypeDataPtr));
	}

	return FMassArchetypeHelper::ArchetypeHandleFromData(ArchetypeDataPtr);
}

void FMassEntityManager::InternalAppendFragmentsAndTagsToArchetypeCompositionDescriptor(
	FMassArchetypeCompositionDescriptor& InOutComposition, TConstArrayView<const UScriptStruct*> FragmentsAndTagsList) const
{
	for (const UScriptStruct* Type : FragmentsAndTagsList)
	{
		if (Type->IsChildOf(FMassFragment::StaticStruct()))
		{
			InOutComposition.Fragments.Add(*Type);
		}
		else if (Type->IsChildOf(FMassTag::StaticStruct()))
		{
			InOutComposition.Tags.Add(*Type);
		}
		else if (Type->IsChildOf(FMassChunkFragment::StaticStruct()))
		{
			InOutComposition.ChunkFragments.Add(*Type);
		}
		else
		{
			UE_LOG(LogMass, Warning, TEXT("%hs: %s is not a valid fragment nor tag type. Ignoring.")
				, __FUNCTION__, *GetNameSafe(Type));
		}
	}
}

FMassArchetypeHandle FMassEntityManager::GetArchetypeForEntity(FMassEntityHandle Entity) const
{
	if (IsEntityValid(Entity))
	{
		return FMassArchetypeHelper::ArchetypeHandleFromData(GetEntityStorageInterface().GetArchetypeAsShared(Entity.Index));
	}
	return FMassArchetypeHandle();
}

FMassArchetypeHandle FMassEntityManager::GetArchetypeForEntityUnsafe(FMassEntityHandle Entity) const
{
	check(GetEntityStorageInterface().IsValidIndex(Entity.Index));
	return FMassArchetypeHelper::ArchetypeHandleFromData(GetEntityStorageInterface().GetArchetypeAsShared(Entity.Index));
}

void FMassEntityManager::ForEachArchetypeFragmentType(const FMassArchetypeHandle& ArchetypeHandle, TFunction< void(const UScriptStruct* /*FragmentType*/)> Function)
{
	const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
	ArchetypeData.ForEachFragmentType(Function);
}

void FMassEntityManager::DoEntityCompaction(const double TimeAllowed)
{
	int32 TotalEntitiesMoved = 0;
	const double TimeAllowedEnd = FPlatformTime::Seconds() + TimeAllowed;

	bool bReachedTimeLimit = false;
	for (const auto& KVP : FragmentHashToArchetypeMap)
	{
		for (const TSharedPtr<FMassArchetypeData>& ArchetypePtr : KVP.Value)
		{
			const double TimeAllowedLeft = TimeAllowedEnd - FPlatformTime::Seconds();
			bReachedTimeLimit = TimeAllowedLeft <= 0.0;
			if (bReachedTimeLimit)
			{
 				break;
			}
			TotalEntitiesMoved += ArchetypePtr->CompactEntities(TimeAllowedLeft);
		}
		if (bReachedTimeLimit)
		{
			break;
		}
	}

	UE_CVLOG(TotalEntitiesMoved, GetOwner(), LogMass, Verbose, TEXT("Entity Compaction: moved %d entities"), TotalEntitiesMoved);
}

FMassEntityHandle FMassEntityManager::CreateEntity(const FMassArchetypeHandle& ArchetypeHandle, const FMassArchetypeSharedFragmentValues& SharedFragmentValues)
{
	checkf(IsProcessing() == false, TEXT("Synchronous API function %hs called during mass processing. Use asynchronous API instead."), __FUNCTION__);
	check(ArchetypeHandle.IsValid());

	const FMassEntityHandle Entity = ReserveEntity();
	InternalBuildEntity(Entity
		, GetOrCreateSuitableArchetype(ArchetypeHandle, SharedFragmentValues.GetSharedFragmentBitSet(), SharedFragmentValues.GetConstSharedFragmentBitSet())
		, SharedFragmentValues);

	return Entity;
}

FMassEntityHandle FMassEntityManager::CreateEntity(TConstArrayView<FInstancedStruct> FragmentInstanceList, const FMassArchetypeSharedFragmentValues& SharedFragmentValues, const FMassArchetypeCreationParams& CreationParams)
{
	checkf(IsProcessing() == false, TEXT("Synchronous API function %hs called during mass processing. Use asynchronous API instead."), __FUNCTION__);
	check(FragmentInstanceList.Num() > 0);

	const FMassArchetypeHandle& ArchetypeHandle = CreateArchetype(FMassArchetypeCompositionDescriptor(FragmentInstanceList,
		FMassTagBitSet(), FMassChunkFragmentBitSet(), FMassSharedFragmentBitSet(), FMassConstSharedFragmentBitSet()), CreationParams);
	check(ArchetypeHandle.IsValid());

	const FMassEntityHandle Entity = ReserveEntity();

	// Using a creation context to prevent InternalBuildEntity from notifying observers before we set fragments data
	const TSharedRef<FEntityCreationContext> CreationContext = GetOrMakeCreationContext();
	CreationContext->AppendEntities({Entity});

	InternalBuildEntity(Entity, ArchetypeHandle, SharedFragmentValues);

	FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(CurrentArchetype);
	CurrentArchetype->SetFragmentsData(Entity, FragmentInstanceList);

	return Entity;
}

FMassEntityHandle FMassEntityManager::ReserveEntity()
{
	FMassEntityHandle Result = GetEntityStorageInterface().AcquireOne();

	return Result;
}

void FMassEntityManager::ReleaseReservedEntity(FMassEntityHandle Entity)
{
	checkf(!IsEntityBuilt(Entity), TEXT("Entity is already built, use DestroyEntity() instead"));

	InternalReleaseEntity(Entity);
}

void FMassEntityManager::BuildEntity(FMassEntityHandle Entity, const FMassArchetypeHandle& ArchetypeHandle, const FMassArchetypeSharedFragmentValues& SharedFragmentValues)
{
	checkf(IsProcessing() == false, TEXT("Synchronous API function %hs called during mass processing. Use asynchronous API instead."), __FUNCTION__);
	checkf(!IsEntityBuilt(Entity), TEXT("Expecting an entity that is not already built"));
	check(ArchetypeHandle.IsValid());

	InternalBuildEntity(Entity, ArchetypeHandle, SharedFragmentValues);
}

void FMassEntityManager::BuildEntity(FMassEntityHandle Entity, TConstArrayView<FInstancedStruct> FragmentInstanceList, const FMassArchetypeSharedFragmentValues& SharedFragmentValues)
{
	checkf(IsProcessing() == false, TEXT("Synchronous API function %hs called during mass processing. Use asynchronous API instead."), __FUNCTION__);
	check(FragmentInstanceList.Num() > 0);
	checkf(!IsEntityBuilt(Entity), TEXT("Expecting an entity that is not already built"));

	checkf(SharedFragmentValues.IsSorted(), TEXT("Expecting shared fragment values to be previously sorted"));
	FMassArchetypeCompositionDescriptor Composition(FragmentInstanceList, FMassTagBitSet(), FMassChunkFragmentBitSet(), FMassSharedFragmentBitSet(), FMassConstSharedFragmentBitSet());
	for (const FConstSharedStruct& SharedFragment : SharedFragmentValues.GetConstSharedFragments())
	{
		Composition.ConstSharedFragments.Add(*SharedFragment.GetScriptStruct());
	}
	for (const FSharedStruct& SharedFragment : SharedFragmentValues.GetSharedFragments())
	{
		Composition.SharedFragments.Add(*SharedFragment.GetScriptStruct());
	}

	const FMassArchetypeHandle& ArchetypeHandle = CreateArchetype(Composition);
	check(ArchetypeHandle.IsValid());

	// Using a creation context to prevent InternalBuildEntity from notifying observers before we set fragments data
	const TSharedRef<FEntityCreationContext> CreationContext = GetOrMakeCreationContext();
	CreationContext->AppendEntities({Entity});

	InternalBuildEntity(Entity, ArchetypeHandle, SharedFragmentValues);

	FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(CurrentArchetype);
	CurrentArchetype->SetFragmentsData(Entity, FragmentInstanceList);
}

TConstArrayView<FMassEntityHandle> FMassEntityManager::BatchReserveEntities(const int32 Count, TArray<FMassEntityHandle>& InOutEntities)
{
	const int32 Index = InOutEntities.Num();
	const int32 NumAdded = GetEntityStorageInterface().Acquire(Count, InOutEntities);
	ensureMsgf(NumAdded == Count, TEXT("Failed to reserve %d entities, was able to only reserve %d"), Count, NumAdded);

	return MakeArrayView(InOutEntities.GetData() + Index, NumAdded);
}

int32 FMassEntityManager::BatchReserveEntities(TArrayView<FMassEntityHandle> InOutEntities)
{
	return GetEntityStorageInterface().Acquire(InOutEntities);
}

TSharedRef<FMassEntityManager::FEntityCreationContext> FMassEntityManager::BatchBuildEntities(const FMassArchetypeEntityCollectionWithPayload& EncodedEntitiesWithPayload
	, const FMassFragmentBitSet& FragmentsAffected, const FMassArchetypeSharedFragmentValues& SharedFragmentValues, const FMassArchetypeCreationParams& CreationParams)
{
	checkf(IsProcessing() == false, TEXT("Synchronous API function %hs called during mass processing. Use asynchronous API instead."), __FUNCTION__);
	check(SharedFragmentValues.IsSorted());

	FMassArchetypeCompositionDescriptor Composition(FragmentsAffected, FMassTagBitSet(), FMassChunkFragmentBitSet(), FMassSharedFragmentBitSet(), FMassConstSharedFragmentBitSet());
	for (const FConstSharedStruct& SharedFragment : SharedFragmentValues.GetConstSharedFragments())
	{
		Composition.SharedFragments.Add(*SharedFragment.GetScriptStruct());
	}
	for (const FSharedStruct& SharedFragment : SharedFragmentValues.GetSharedFragments())
	{
		Composition.SharedFragments.Add(*SharedFragment.GetScriptStruct());
	}

	return BatchBuildEntities(EncodedEntitiesWithPayload, MoveTemp(Composition), SharedFragmentValues, CreationParams);
}

TSharedRef<FMassEntityManager::FEntityCreationContext> FMassEntityManager::BatchBuildEntities(const FMassArchetypeEntityCollectionWithPayload& EncodedEntitiesWithPayload
	, FMassArchetypeCompositionDescriptor&& Composition
	, const FMassArchetypeSharedFragmentValues& SharedFragmentValues, const FMassArchetypeCreationParams& CreationParams)
{
	checkf(IsProcessing() == false, TEXT("Synchronous API function %hs called during mass processing. Use asynchronous API instead."), __FUNCTION__);

	TRACE_CPUPROFILER_EVENT_SCOPE(Mass_BatchBuildEntities);

	FMassArchetypeEntityCollection::FEntityRangeArray TargetArchetypeEntityRanges;

	// "built" entities case, this is verified during FMassArchetypeEntityCollectionWithPayload construction
	FMassArchetypeHandle TargetArchetypeHandle = CreateArchetype(Composition, CreationParams);
	check(TargetArchetypeHandle.IsValid());

	// there are some extra steps in creating EncodedEntities from the original given entity handles and then back
	// to handles here, but this way we're consistent in how stuff is handled, and there are some slight benefits 
	// to having entities ordered by their index (like accessing the Entities data below).
	TArray<FMassEntityHandle> EntityHandles;
	UE::Mass::Private::ConvertArchetypelessSubchunksIntoEntityHandles(EncodedEntitiesWithPayload.GetEntityCollection().GetRanges(), EntityHandles);

	// since the handles encoded via FMassArchetypeEntityCollectionWithPayload miss the SerialNumber we need to update it
	// before passing over the new archetype. Thankfully we need to iterate over all the entity handles anyway
	// to update the manager's information on these entities (stored in FMassEntityManager::Entities)
	for (FMassEntityHandle& Entity : EntityHandles)
	{
		check(GetEntityStorageInterface().IsValidIndex(Entity.Index));

		const UE::Mass::IEntityStorageInterface::EEntityState EntityState = GetEntityStorageInterface().GetEntityState(Entity.Index);
		checkf(EntityState == UE::Mass::IEntityStorageInterface::EEntityState::Reserved, TEXT("Trying to build entities that are not reserved. Check all handles are reserved or consider using BatchCreateEntities"));

		const int32 SerialNumber = GetEntityStorageInterface().GetSerialNumber(Entity.Index);
		Entity.SerialNumber = SerialNumber;
		
		GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, TargetArchetypeHandle.DataPtr);
	}

	TargetArchetypeHandle.DataPtr->BatchAddEntities(EntityHandles, SharedFragmentValues, TargetArchetypeEntityRanges);

	if (EncodedEntitiesWithPayload.GetPayload().IsEmpty() == false)
	{
		// at this point all the entities are in the target archetype, we can set the values
		// note that even though the "subchunk" information could have changed the order of entities is the same and 
		// corresponds to the order in FMassArchetypeEntityCollectionWithPayload's payload
		TargetArchetypeHandle.DataPtr->BatchSetFragmentValues(TargetArchetypeEntityRanges, EncodedEntitiesWithPayload.GetPayload());
	}

	// With this call we're either creating a fresh context populated with EntityHandles, or it will append 
	// EntityHandles to active context.
	// Not creating the context sooner since we want to reuse TargetArchetypeEntityRanges by moving it over to the context.
	// Note that we can afford to create this context so late since all previous operations were on the archetype level
	// and as such won't cause observers triggering (which usually is prevented by context's existence), and that we 
	// strongly assume the all entity creation/building (not to be mistaken with "reserving") takes place in a single thread
	// @todo add checks/ensures enforcing the assumption mentioned above.
	return GetOrMakeCreationContext(EntityHandles
		, FMassArchetypeEntityCollection(TargetArchetypeHandle, MoveTemp(TargetArchetypeEntityRanges)));
}

TSharedRef<FMassEntityManager::FEntityCreationContext> FMassEntityManager::BatchCreateReservedEntities(const FMassArchetypeHandle& ArchetypeHandle
	, const FMassArchetypeSharedFragmentValues& SharedFragmentValues, TConstArrayView<FMassEntityHandle> ReservedEntities)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Mass_BatchCreateReservedEntities);

	checkf(IsProcessing() == false, TEXT("Synchronous API function %hs called during mass processing. Use asynchronous API instead."), __FUNCTION__);
	checkf(!ReservedEntities.IsEmpty(), TEXT("No reserved entities given to batch create."));

	return InternalBatchCreateReservedEntities(
		GetOrCreateSuitableArchetype(ArchetypeHandle, SharedFragmentValues.GetSharedFragmentBitSet(), SharedFragmentValues.GetConstSharedFragmentBitSet())
		, SharedFragmentValues, ReservedEntities);
}

TSharedRef<FMassEntityManager::FEntityCreationContext> FMassEntityManager::BatchCreateEntities(const FMassArchetypeHandle& ArchetypeHandle
	, const FMassArchetypeSharedFragmentValues& SharedFragmentValues, const int32 Count, TArray<FMassEntityHandle>& InOutEntities)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Mass_BatchCreateEntities);

	checkf(IsProcessing() == false, TEXT("Synchronous API function %hs called during mass processing. Use asynchronous API instead."), __FUNCTION__);
	testableCheckfReturn(ArchetypeHandle.IsValid(), return GetOrMakeCreationContext()
		, TEXT("%hs expecting a valid ArchetypeHandle"), __FUNCTION__);

	TConstArrayView<FMassEntityHandle> ReservedEntities = BatchReserveEntities(Count, InOutEntities);
	
	return InternalBatchCreateReservedEntities(
		GetOrCreateSuitableArchetype(ArchetypeHandle, SharedFragmentValues.GetSharedFragmentBitSet(), SharedFragmentValues.GetConstSharedFragmentBitSet())
		, SharedFragmentValues, ReservedEntities);
}

TSharedRef<FMassEntityManager::FEntityCreationContext> FMassEntityManager::InternalBatchCreateReservedEntities(const FMassArchetypeHandle& ArchetypeHandle
	, const FMassArchetypeSharedFragmentValues& SharedFragmentValues, TConstArrayView<FMassEntityHandle> ReservedEntities)
{
	// Functions calling into this one are required to verify that the archetype handle is valid
	FMassArchetypeData* ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandle(ArchetypeHandle);
	checkf(ArchetypeData, TEXT("Functions calling into this one are required to verify that the archetype handle is valid"));

	for (FMassEntityHandle Entity : ReservedEntities)
	{
		check(IsEntityValid(Entity));
		const UE::Mass::IEntityStorageInterface::EEntityState EntityState = GetEntityStorageInterface().GetEntityState(Entity.Index);
		checkf(EntityState == UE::Mass::IEntityStorageInterface::EEntityState::Reserved, TEXT("Trying to build entities that are not reserved. Check all handles are reserved or consider using BatchCreateEntities"));
		
		GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, ArchetypeHandle.DataPtr);
	}

	FMassArchetypeEntityCollection::FEntityRangeArray TargetArchetypeEntityRanges;
	ArchetypeData->BatchAddEntities(ReservedEntities, SharedFragmentValues, TargetArchetypeEntityRanges);

	return GetOrMakeCreationContext(ReservedEntities, FMassArchetypeEntityCollection(ArchetypeHandle, MoveTemp(TargetArchetypeEntityRanges)));
}

void FMassEntityManager::DestroyEntity(FMassEntityHandle Entity)
{
	checkf(IsProcessing() == false, TEXT("Synchronous API function %hs called during mass processing. Use asynchronous API instead."), __FUNCTION__);
	
	CheckIfEntityIsActive(Entity);

	FMassArchetypeData* Archetype = GetEntityStorageInterface().GetArchetype(Entity.Index);

	if (Archetype)
	{
		ObserverManager.OnPreEntityDestroyed(Archetype->GetCompositionDescriptor(), Entity);
		Archetype->RemoveEntity(Entity);
	}

	InternalReleaseEntity(Entity);
}

void FMassEntityManager::BatchDestroyEntities(TConstArrayView<FMassEntityHandle> InEntities)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Mass_BatchDestroyEntities);

	checkf(IsProcessing() == false, TEXT("Synchronous API function %hs called during mass processing. Use asynchronous API instead."), __FUNCTION__);
	checkf(IsDuringEntityCreation() == false, TEXT("%hs: Trying to destroy entities while entity creation is under way. This operation is not supported."), __FUNCTION__);

	for (const FMassEntityHandle Entity : InEntities)
	{
		if (GetEntityStorageInterface().IsValidIndex(Entity.Index) == false)
		{
			continue;
		}

		const int32 SerialNumber = GetEntityStorageInterface().GetSerialNumber(Entity.Index);
		if (SerialNumber != Entity.SerialNumber)
		{
			continue;
		}

		if (FMassArchetypeData* Archetype = GetEntityStorageInterface().GetArchetype(Entity.Index))
		{
			ObserverManager.OnPreEntityDestroyed(Archetype->GetCompositionDescriptor(), Entity);
			Archetype->RemoveEntity(Entity);
		}
		// else it's a "reserved" entity so it has not been assigned to an archetype yet, no archetype nor observers to notify
	}

	GetEntityStorageInterface().Release(InEntities);
}

void FMassEntityManager::BatchDestroyEntityChunks(const FMassArchetypeEntityCollection& EntityCollection)
{
	BatchDestroyEntityChunks(MakeArrayView(&EntityCollection, 1));
}

void FMassEntityManager::BatchDestroyEntityChunks(TConstArrayView<FMassArchetypeEntityCollection> Collections)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Mass_BatchDestroyEntityChunks);

	checkf(IsProcessing() == false, TEXT("Synchronous API function %hs called during mass processing. Use asynchronous API instead."), __FUNCTION__);
	checkf(IsDuringEntityCreation() == false, TEXT("%hs: Trying to destroy entities while entity creation is under way. This operation is not supported."), __FUNCTION__);

	TArray<FMassEntityHandle> EntitiesRemoved;
	// note that it's important to place the context instance in the same scope as the loop below that updates 
	// FMassEntityManager.EntityData, otherwise, if there are commands flushed as part of FMassProcessingContext's 
	// destruction the commands will work on outdated information (which might result in crashes).
	FMassProcessingContext ProcessingContext(*this, /*TimeDelta=*/0.0f);
	ProcessingContext.bFlushCommandBuffer = false;
	ProcessingContext.CommandBuffer = MakeShareable(new FMassCommandBuffer());

	for (const FMassArchetypeEntityCollection& EntityCollection : Collections)
	{
		EntitiesRemoved.Reset();
		if (EntityCollection.GetArchetype().IsValid())
		{
			ObserverManager.OnPreEntitiesDestroyed(ProcessingContext, EntityCollection);

			FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(EntityCollection.GetArchetype());
			ArchetypeData.BatchDestroyEntityChunks(EntityCollection.GetRanges(), EntitiesRemoved);
		
			GetEntityStorageInterface().Release(EntitiesRemoved);
		}
		else
		{
			UE::Mass::Private::ConvertArchetypelessSubchunksIntoEntityHandles(EntityCollection.GetRanges(), EntitiesRemoved);
			GetEntityStorageInterface().ForceRelease(EntitiesRemoved);
		}
	}
}

void FMassEntityManager::AddFragmentToEntity(FMassEntityHandle Entity, const UScriptStruct* FragmentType)
{
	checkf(FragmentType, TEXT("Null fragment type passed in to %hs"), __FUNCTION__);
	checkf(IsProcessing() == false, TEXT("Synchronous API function %hs called during mass processing. Use asynchronous API instead."), __FUNCTION__);

	CheckIfEntityIsActive(Entity);

	const FMassArchetypeCompositionDescriptor Descriptor(InternalAddFragmentListToEntityChecked(Entity, FMassFragmentBitSet(*FragmentType)));

	if (IsAllowedToTriggerObservers())
	{
		ObserverManager.OnPostCompositionAdded(Entity, Descriptor);
	}
}

void FMassEntityManager::AddFragmentToEntity(FMassEntityHandle Entity, const UScriptStruct* FragmentType, const FStructInitializationCallback& Initializer)
{
	checkf(FragmentType, TEXT("Null fragment type passed in to %hs"), __FUNCTION__);
	checkf(IsProcessing() == false, TEXT("Synchronous API function %hs called during mass processing. Use asynchronous API instead."), __FUNCTION__);

	CheckIfEntityIsActive(Entity);

	FMassFragmentBitSet Fragments = InternalAddFragmentListToEntityChecked(Entity, FMassFragmentBitSet(*FragmentType));
	FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(CurrentArchetype);
	void* FragmentData = CurrentArchetype->GetFragmentDataForEntity(FragmentType, Entity.Index);
	Initializer(FragmentData, *FragmentType);

	const FMassArchetypeCompositionDescriptor Descriptor(MoveTemp(Fragments));
	
	if (IsAllowedToTriggerObservers())
	{
		ObserverManager.OnPostCompositionAdded(Entity, Descriptor);
	}
}

void FMassEntityManager::AddFragmentListToEntity(FMassEntityHandle Entity, TConstArrayView<const UScriptStruct*> FragmentList)
{
	CheckIfEntityIsActive(Entity);

	const FMassArchetypeCompositionDescriptor Descriptor(InternalAddFragmentListToEntityChecked(Entity, FMassFragmentBitSet(FragmentList)));
	
	if (IsAllowedToTriggerObservers())
	{
		ObserverManager.OnPostCompositionAdded(Entity, Descriptor);
	}
}

void FMassEntityManager::AddCompositionToEntity_GetDelta(FMassEntityHandle Entity, FMassArchetypeCompositionDescriptor& InDescriptor)
{
	CheckIfEntityIsActive(Entity);

	FMassArchetypeData* OldArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(OldArchetype);

	InDescriptor.Fragments -= OldArchetype->GetCompositionDescriptor().Fragments;
	InDescriptor.Tags -= OldArchetype->GetCompositionDescriptor().Tags;

	ensureMsgf(InDescriptor.ChunkFragments.IsEmpty(), TEXT("Adding new chunk fragments is not supported"));

	if (InDescriptor.IsEmpty() == false)
	{
		FMassArchetypeCompositionDescriptor NewDescriptor = OldArchetype->GetCompositionDescriptor();
		NewDescriptor.Fragments += InDescriptor.Fragments;
		NewDescriptor.Tags += InDescriptor.Tags;

		const FMassArchetypeHandle NewArchetypeHandle = CreateArchetype(NewDescriptor, FMassArchetypeCreationParams(*OldArchetype));

		if (ensure(NewArchetypeHandle.DataPtr.Get() != OldArchetype))
		{
			// Move the entity over
			FMassArchetypeData& NewArchetype = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(NewArchetypeHandle);
			NewArchetype.CopyDebugNamesFrom(*OldArchetype);
			OldArchetype->MoveEntityToAnotherArchetype(Entity, NewArchetype);

			GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);

			if (IsAllowedToTriggerObservers())
			{
				ObserverManager.OnPostCompositionAdded(Entity, InDescriptor);
			}
		}
	}
}

void FMassEntityManager::RemoveCompositionFromEntity(FMassEntityHandle Entity, const FMassArchetypeCompositionDescriptor& InDescriptor)
{
	CheckIfEntityIsActive(Entity);

	if(InDescriptor.IsEmpty() == false)
	{
		FMassArchetypeData* OldArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
		check(OldArchetype);

		FMassArchetypeCompositionDescriptor NewDescriptor = OldArchetype->GetCompositionDescriptor();
		NewDescriptor.Fragments -= InDescriptor.Fragments;
		NewDescriptor.Tags -= InDescriptor.Tags;

		ensureMsgf(InDescriptor.ChunkFragments.IsEmpty(), TEXT("Removing chunk fragments is not supported"));
		ensureMsgf(InDescriptor.SharedFragments.IsEmpty(), TEXT("Removing shared fragments is not supported"));

		if (NewDescriptor.IsEquivalent(OldArchetype->GetCompositionDescriptor()) == false)
		{
			ensureMsgf(OldArchetype->GetCompositionDescriptor().HasAll(InDescriptor), TEXT("Some of the elements being removed are already missing from entity\'s composition."));
			
			if (IsAllowedToTriggerObservers())
			{
				ObserverManager.OnPreCompositionRemoved(Entity, InDescriptor);
			}

			const FMassArchetypeHandle NewArchetypeHandle = CreateArchetype(NewDescriptor, FMassArchetypeCreationParams(*OldArchetype));

			if (ensure(NewArchetypeHandle.DataPtr.Get() != OldArchetype))
			{
				// Move the entity over
				FMassArchetypeData& NewArchetype = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(NewArchetypeHandle);
				NewArchetype.CopyDebugNamesFrom(*OldArchetype);
				OldArchetype->MoveEntityToAnotherArchetype(Entity, NewArchetype);
				GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
			}
		}
	}
}

const FMassArchetypeCompositionDescriptor& FMassEntityManager::GetArchetypeComposition(const FMassArchetypeHandle& ArchetypeHandle) const
{
	const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
	return ArchetypeData.GetCompositionDescriptor();
}

void FMassEntityManager::InternalBuildEntity(FMassEntityHandle Entity, const FMassArchetypeHandle& ArchetypeHandle, const FMassArchetypeSharedFragmentValues& SharedFragmentValues)
{
	const TSharedPtr<FMassArchetypeData>& NewArchetype = ArchetypeHandle.DataPtr;
	GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, ArchetypeHandle.DataPtr);
	NewArchetype->AddEntity(Entity, SharedFragmentValues);

	if (IsAllowedToTriggerObservers())
	{
		ObserverManager.OnPostCompositionAdded(Entity, NewArchetype->GetCompositionDescriptor());
	}
}

void FMassEntityManager::InternalReleaseEntity(FMassEntityHandle Entity)
{
	// Using force release by bypass serial number check since we have verified the validity of the handle earlier.
	GetEntityStorageInterface().ForceReleaseOne(Entity);
}

FMassFragmentBitSet FMassEntityManager::InternalAddFragmentListToEntityChecked(FMassEntityHandle Entity, const FMassFragmentBitSet& InFragments)
{
	TSharedPtr<FMassArchetypeData>& OldArchetype = GetEntityStorageInterface().GetArchetypeAsShared(Entity.Index);
	check(OldArchetype);

	UE_CLOG(OldArchetype->GetFragmentBitSet().HasAny(InFragments), LogMass, Log
		, TEXT("Trying to add a new fragment type to an entity, but it already has some of them. (%s)")
		, *InFragments.GetOverlap(OldArchetype->GetFragmentBitSet()).DebugGetStringDesc());

	FMassFragmentBitSet NewFragments = InFragments - OldArchetype->GetFragmentBitSet();
	if (NewFragments.IsEmpty() == false)
	{
		InternalAddFragmentListToEntity(Entity, NewFragments);
	}
	return MoveTemp(NewFragments);
}

void FMassEntityManager::InternalAddFragmentListToEntity(FMassEntityHandle Entity, const FMassFragmentBitSet& InFragments)
{
	checkf(InFragments.IsEmpty() == false, TEXT("%hs is intended for internal calls with non empty NewFragments parameter"), __FUNCTION__);
	check(GetEntityStorageInterface().IsValidIndex(Entity.Index));
	TSharedPtr<FMassArchetypeData>& OldArchetype = GetEntityStorageInterface().GetArchetypeAsShared(Entity.Index);
	check(OldArchetype.IsValid());

	// fetch or create the new archetype
	const FMassArchetypeHandle NewArchetypeHandle = CreateArchetype(OldArchetype, InFragments);
	checkf(NewArchetypeHandle.DataPtr != OldArchetype, TEXT("%hs is intended for internal calls with non overlapping fragment list."), __FUNCTION__);

	// Move the entity over
	FMassArchetypeData& NewArchetype = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(NewArchetypeHandle);
	NewArchetype.CopyDebugNamesFrom(*OldArchetype);
	OldArchetype->MoveEntityToAnotherArchetype(Entity, NewArchetype);

	GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
}

void FMassEntityManager::AddFragmentInstanceListToEntity(FMassEntityHandle Entity, TConstArrayView<FInstancedStruct> FragmentInstanceList)
{
	checkf(IsProcessing() == false, TEXT("Synchronous API function %hs called during mass processing. Use asynchronous API instead."), __FUNCTION__);

	CheckIfEntityIsActive(Entity);
	checkf(FragmentInstanceList.Num() > 0, TEXT("Need to specify at least one fragment instances for this operation"));

	const FMassArchetypeCompositionDescriptor Descriptor(InternalAddFragmentListToEntityChecked(Entity, FMassFragmentBitSet(FragmentInstanceList)));
	
	FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(CurrentArchetype);
	CurrentArchetype->SetFragmentsData(Entity, FragmentInstanceList);

	if (IsAllowedToTriggerObservers())
	{
		ObserverManager.OnPostCompositionAdded(Entity, Descriptor);
	}
}

void FMassEntityManager::RemoveFragmentFromEntity(FMassEntityHandle Entity, const UScriptStruct* FragmentType)
{
	RemoveFragmentListFromEntity(Entity, MakeArrayView(&FragmentType, 1));
}

void FMassEntityManager::RemoveFragmentListFromEntity(FMassEntityHandle Entity, TConstArrayView<const UScriptStruct*> FragmentList)
{
	checkf(IsProcessing() == false, TEXT("Synchronous API function %hs called during mass processing. Use asynchronous API instead."), __FUNCTION__);

	CheckIfEntityIsActive(Entity);
	
	FMassArchetypeData* OldArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(OldArchetype);

	const FMassFragmentBitSet FragmentsToRemove(FragmentList);

	if (OldArchetype->GetFragmentBitSet().HasAny(FragmentsToRemove))
	{
		// If all the fragments got removed this will result in fetching of the empty archetype
		const FMassArchetypeCompositionDescriptor NewComposition(OldArchetype->GetFragmentBitSet() - FragmentsToRemove
			, OldArchetype->GetTagBitSet()
			, OldArchetype->GetChunkFragmentBitSet()
			, OldArchetype->GetSharedFragmentBitSet()
			, OldArchetype->GetConstSharedFragmentBitSet());
		const FMassArchetypeHandle NewArchetypeHandle = CreateArchetype(NewComposition, FMassArchetypeCreationParams(*OldArchetype));

		FMassArchetypeCompositionDescriptor CompositionDelta;
		// Find overlap.  It isn't guaranteed that the old archetype has all of the fragments being removed.
		CompositionDelta.Fragments = OldArchetype->GetFragmentBitSet().GetOverlap(FragmentsToRemove);

		if (IsAllowedToTriggerObservers())
		{
			ObserverManager.OnPreCompositionRemoved(Entity, CompositionDelta);
		}

		// Move the entity over
		FMassArchetypeData& NewArchetype = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(NewArchetypeHandle);
		NewArchetype.CopyDebugNamesFrom(*OldArchetype);
		OldArchetype->MoveEntityToAnotherArchetype(Entity, NewArchetype);
		
		GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
	}
}

void FMassEntityManager::SwapTagsForEntity(FMassEntityHandle Entity, const UScriptStruct* OldTagType, const UScriptStruct* NewTagType)
{
	checkf(IsProcessing() == false, TEXT("Synchronous API function %hs called during mass processing. Use asynchronous API instead."), __FUNCTION__);

	CheckIfEntityIsActive(Entity);

	checkf((OldTagType != nullptr) && OldTagType->IsChildOf(FMassTag::StaticStruct()), TEXT("%hs works only with tags while '%s' is not one."), __FUNCTION__, *GetPathNameSafe(OldTagType));
	checkf((NewTagType != nullptr) && NewTagType->IsChildOf(FMassTag::StaticStruct()), TEXT("%hs works only with tags while '%s' is not one."), __FUNCTION__, *GetPathNameSafe(NewTagType));
	
	TSharedPtr<FMassArchetypeData>& CurrentArchetype = GetEntityStorageInterface().GetArchetypeAsShared(Entity.Index);
	check(CurrentArchetype);

	FMassTagBitSet NewTagBitSet = CurrentArchetype->GetTagBitSet();
	NewTagBitSet.Remove(*OldTagType);
	NewTagBitSet.Add(*NewTagType);
	
	if (NewTagBitSet != CurrentArchetype->GetTagBitSet())
	{
		const FMassArchetypeHandle NewArchetypeHandle = InternalCreateSimilarArchetype(CurrentArchetype, NewTagBitSet);
		checkSlow(NewArchetypeHandle.IsValid());

		// Move the entity over
		CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetypeHandle.DataPtr.Get());
		
		GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
	}
}

void FMassEntityManager::AddTagToEntity(FMassEntityHandle Entity, const UScriptStruct* TagType)
{
	checkf((TagType != nullptr) && TagType->IsChildOf(FMassTag::StaticStruct()), TEXT("%hs works only with tags while '%s' is not one."), __FUNCTION__, *GetPathNameSafe(TagType));

	CheckIfEntityIsActive(Entity);
	
	TSharedPtr<FMassArchetypeData>& CurrentArchetype = GetEntityStorageInterface().GetArchetypeAsShared(Entity.Index);
	check(CurrentArchetype);

	if (CurrentArchetype->HasTagType(TagType) == false)
	{
		//FMassTagBitSet NewTags = CurrentArchetype->GetTagBitSet() - *TagType;
		FMassTagBitSet NewTags = CurrentArchetype->GetTagBitSet();
		NewTags.Add(*TagType);
		const FMassArchetypeHandle NewArchetypeHandle = InternalCreateSimilarArchetype(CurrentArchetype, NewTags);
		checkSlow(NewArchetypeHandle.IsValid());

		// Move the entity over
		CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetypeHandle.DataPtr.Get());
		GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);

		FMassArchetypeCompositionDescriptor CompositionDelta;
		FMassTagBitSet TagDelta;
		TagDelta.Add(*TagType);
		CompositionDelta.Tags = TagDelta;
		
		if (IsAllowedToTriggerObservers())
		{
			ObserverManager.OnPostCompositionAdded(Entity, CompositionDelta);
		}
	}
}
	
void FMassEntityManager::RemoveTagFromEntity(FMassEntityHandle Entity, const UScriptStruct* TagType)
{
	checkf((TagType != nullptr) && TagType->IsChildOf(FMassTag::StaticStruct()), TEXT("%hs works only with tags while '%s' is not one."), __FUNCTION__, *GetPathNameSafe(TagType));

	CheckIfEntityIsActive(Entity);

	TSharedPtr<FMassArchetypeData>& CurrentArchetype = GetEntityStorageInterface().GetArchetypeAsShared(Entity.Index);
	check(CurrentArchetype);

	if (CurrentArchetype->HasTagType(TagType))
	{
		FMassArchetypeCompositionDescriptor CompositionDelta;
		FMassTagBitSet TagDelta;
		TagDelta.Add(*TagType);
		CompositionDelta.Tags = TagDelta;
		
		if (IsAllowedToTriggerObservers())
		{
			ObserverManager.OnPreCompositionRemoved(Entity, CompositionDelta);
		}
		
		// CurrentArchetype->GetTagBitSet() -  *TagType
		const FMassTagBitSet NewTagComposition = CurrentArchetype->GetTagBitSet() - TagDelta;
		const FMassArchetypeHandle NewArchetypeHandle = InternalCreateSimilarArchetype(CurrentArchetype, NewTagComposition);
		checkSlow(NewArchetypeHandle.IsValid());

		// Move the entity over
		CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetypeHandle.DataPtr.Get());
		GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
	}
}

bool FMassEntityManager::AddConstSharedFragmentToEntity(const FMassEntityHandle Entity, const FConstSharedStruct& InConstSharedFragment)
{
	if (!ensureMsgf(InConstSharedFragment.IsValid(), TEXT("%hs parameter Fragment is expected to be valid"), __FUNCTION__))
	{
		return false;
	}

	CheckIfEntityIsActive(Entity);
	
	FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetypeAsShared(Entity.Index).Get();
	check(CurrentArchetype);

	const UScriptStruct* StructType = InConstSharedFragment.GetScriptStruct();
	CA_ASSUME(StructType);
	if (CurrentArchetype->GetCompositionDescriptor().ConstSharedFragments.Contains(*StructType))
	{
		const FMassArchetypeSharedFragmentValues& SharedFragmentValues = CurrentArchetype->GetSharedFragmentValues(Entity);
		FConstSharedStruct ExistingConstSharedStruct = SharedFragmentValues.GetConstSharedFragmentStruct(StructType);
		if (ExistingConstSharedStruct == InConstSharedFragment || ExistingConstSharedStruct.CompareStructValues(InConstSharedFragment))
		{
			// nothing to do
			return true;
		}
		UE_LOG(LogMass, Warning, TEXT("Changing shared fragment value of entities is not supported"));
		return false;
	}
	
	FMassArchetypeCompositionDescriptor NewComposition(CurrentArchetype->GetCompositionDescriptor());
	NewComposition.ConstSharedFragments.Add(*StructType);
	const FMassArchetypeHandle NewArchetypeHandle = CreateArchetype(NewComposition, FMassArchetypeCreationParams(*CurrentArchetype));
	check(NewArchetypeHandle.IsValid());
	FMassArchetypeData* NewArchetype = NewArchetypeHandle.DataPtr.Get();
	check(NewArchetype);

	const FMassArchetypeSharedFragmentValues& OldSharedFragmentValues = CurrentArchetype->GetSharedFragmentValues(Entity.Index);
	check(!OldSharedFragmentValues.ContainsType(StructType));
	FMassArchetypeSharedFragmentValues NewSharedFragmentValues(OldSharedFragmentValues);
	NewSharedFragmentValues.AddConstSharedFragment(InConstSharedFragment);
	NewSharedFragmentValues.Sort();

	CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetype, &NewSharedFragmentValues);

	// Change the entity archetype
	GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);

	return true;
}

bool FMassEntityManager::RemoveConstSharedFragmentFromEntity(const FMassEntityHandle Entity, const UScriptStruct& ConstSharedFragmentType)
{
	if (!ensureMsgf(ConstSharedFragmentType.IsChildOf(FMassConstSharedFragment::StaticStruct()), TEXT("%hs parameter ConstSharedFragmentType is expected to be a FMassConstSharedFragment"), __FUNCTION__))
	{
		return false;
	}
	
	CheckIfEntityIsActive(Entity);
	
	FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetypeAsShared(Entity.Index).Get();
	check(CurrentArchetype);
	
	if (!CurrentArchetype->GetCompositionDescriptor().ConstSharedFragments.Contains(ConstSharedFragmentType))
	{
		// Nothing to do. Returning false to indicate nothing has been removed, as per function's documentation 
		return false;
	}

	FMassArchetypeCompositionDescriptor NewComposition(CurrentArchetype->GetCompositionDescriptor());
	NewComposition.ConstSharedFragments.Remove(ConstSharedFragmentType);
	const FMassArchetypeHandle NewArchetypeHandle = CreateArchetype(NewComposition);
	check(NewArchetypeHandle.IsValid());
	FMassArchetypeData* NewArchetype = NewArchetypeHandle.DataPtr.Get();
	check(NewArchetype);
	
	const FMassArchetypeSharedFragmentValues& OldSharedFragmentValues = CurrentArchetype->GetSharedFragmentValues(Entity.Index);
	check(OldSharedFragmentValues.ContainsType(&ConstSharedFragmentType));
	FMassArchetypeSharedFragmentValues NewSharedFragmentValues(OldSharedFragmentValues);
	
	const FMassConstSharedFragmentBitSet ToRemove(ConstSharedFragmentType);
	NewSharedFragmentValues.Remove(ToRemove);
	NewSharedFragmentValues.Sort();
	
	CurrentArchetype->MoveEntityToAnotherArchetype(Entity, *NewArchetype, &NewSharedFragmentValues);

	// Change the entity archetype
	GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
	
	return true;
}

void FMassEntityManager::BatchChangeTagsForEntities(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections, const FMassTagBitSet& TagsToAdd, const FMassTagBitSet& TagsToRemove)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Mass_BatchChangeTagsForEntities);

	const FScopedCreationContextOperations CreationContextOperations(*this);
	
	for (const FMassArchetypeEntityCollection& Collection : EntityCollections)
	{
		FMassArchetypeData* CurrentArchetype = Collection.GetArchetype().DataPtr.Get();
		const FMassTagBitSet NewTagComposition = CurrentArchetype
			? (CurrentArchetype->GetTagBitSet() + TagsToAdd - TagsToRemove)
			: (TagsToAdd - TagsToRemove);

		if (ensure(CurrentArchetype) && CurrentArchetype->GetTagBitSet() != NewTagComposition)
		{
			FMassTagBitSet TagsAdded = TagsToAdd - CurrentArchetype->GetTagBitSet();
			FMassTagBitSet TagsRemoved = TagsToRemove.GetOverlap(CurrentArchetype->GetTagBitSet());

			if (CreationContextOperations.IsAllowedToTriggerObservers()
				&& ObserverManager.HasObserversForBitSet(TagsRemoved, EMassObservedOperation::Remove))
			{
				// @todo should use OnPreCompositionRemoved here instead, but we're missing a FMassArchetypeEntityCollection version
				ObserverManager.OnCompositionChanged(Collection, FMassArchetypeCompositionDescriptor(MoveTemp(TagsRemoved)), EMassObservedOperation::Remove);
			}
			const bool bTagsAddedAreObserved = ObserverManager.HasObserversForBitSet(TagsAdded, EMassObservedOperation::Add);

			FMassArchetypeHandle NewArchetypeHandle = InternalCreateSimilarArchetype(Collection.GetArchetype().DataPtr, NewTagComposition);
			checkSlow(NewArchetypeHandle.IsValid());

			// Move the entity over
			FMassArchetypeEntityCollection::FEntityRangeArray NewArchetypeEntityRanges;
			TArray<FMassEntityHandle> EntitiesBeingMoved;
			CurrentArchetype->BatchMoveEntitiesToAnotherArchetype(Collection, *NewArchetypeHandle.DataPtr.Get(), EntitiesBeingMoved
				, bTagsAddedAreObserved ? &NewArchetypeEntityRanges : nullptr);

			for (const FMassEntityHandle& Entity : EntitiesBeingMoved)
			{
				check(GetEntityStorageInterface().IsValidIndex(Entity.Index));
				
				GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
			}

			if (bTagsAddedAreObserved && CreationContextOperations.IsAllowedToTriggerObservers())
			{
				// @todo should use OnPostCompositionAdded here instead, but we're missing a FMassArchetypeEntityCollection version
				ObserverManager.OnCompositionChanged(
					FMassArchetypeEntityCollection(NewArchetypeHandle, MoveTemp(NewArchetypeEntityRanges))
					, FMassArchetypeCompositionDescriptor(MoveTemp(TagsAdded))
					, EMassObservedOperation::Add);
			}
		}
	}
}

void FMassEntityManager::BatchChangeFragmentCompositionForEntities(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections, const FMassFragmentBitSet& FragmentsToAdd, const FMassFragmentBitSet& FragmentsToRemove)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Mass_BatchChangeFragmentCompositionForEntities);

	FScopedCreationContextOperations CreationContextOperations(*this);

	for (const FMassArchetypeEntityCollection& Collection : EntityCollections)
	{
		FMassArchetypeData* CurrentArchetype = Collection.GetArchetype().DataPtr.Get();
		const FMassFragmentBitSet NewFragmentComposition = CurrentArchetype
			? (CurrentArchetype->GetFragmentBitSet() + FragmentsToAdd - FragmentsToRemove)
			: (FragmentsToAdd - FragmentsToRemove);

		if (CurrentArchetype)
		{
			if (CurrentArchetype->GetFragmentBitSet() != NewFragmentComposition)
			{
				FMassFragmentBitSet FragmentsAdded = FragmentsToAdd - CurrentArchetype->GetFragmentBitSet();
				const bool bFragmentsAddedAreObserved = ObserverManager.HasObserversForBitSet(FragmentsAdded, EMassObservedOperation::Add);
				FMassFragmentBitSet FragmentsRemoved = FragmentsToRemove.GetOverlap(CurrentArchetype->GetFragmentBitSet());
				
				if (CreationContextOperations.IsAllowedToTriggerObservers()
					&& ObserverManager.HasObserversForBitSet(FragmentsRemoved, EMassObservedOperation::Remove))
				{
					ObserverManager.OnCompositionChanged(Collection, FMassArchetypeCompositionDescriptor(MoveTemp(FragmentsRemoved)), EMassObservedOperation::Remove);
				}

				FMassArchetypeHandle NewArchetypeHandle = InternalCreateSimilarArchetype(Collection.GetArchetype().DataPtr, NewFragmentComposition);
				checkSlow(NewArchetypeHandle.IsValid());

				// Move the entity over
				FMassArchetypeEntityCollection::FEntityRangeArray NewArchetypeEntityRanges;
				TArray<FMassEntityHandle> EntitiesBeingMoved;
				CurrentArchetype->BatchMoveEntitiesToAnotherArchetype(Collection, *NewArchetypeHandle.DataPtr.Get(), EntitiesBeingMoved
					, bFragmentsAddedAreObserved ? &NewArchetypeEntityRanges : nullptr);

				for (const FMassEntityHandle& Entity : EntitiesBeingMoved)
				{
					check(GetEntityStorageInterface().IsValidIndex(Entity.Index));

					GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
				}

				if (bFragmentsAddedAreObserved && CreationContextOperations.IsAllowedToTriggerObservers())
				{
					ObserverManager.OnCompositionChanged(
						FMassArchetypeEntityCollection(NewArchetypeHandle, MoveTemp(NewArchetypeEntityRanges))
						, FMassArchetypeCompositionDescriptor(MoveTemp(FragmentsAdded))
						, EMassObservedOperation::Add);
				}
			}
		}
		else
		{
			BatchBuildEntities(FMassArchetypeEntityCollectionWithPayload(Collection), NewFragmentComposition, FMassArchetypeSharedFragmentValues());
		}
	}
}

void FMassEntityManager::BatchAddFragmentInstancesForEntities(TConstArrayView<FMassArchetypeEntityCollectionWithPayload> EntityCollections, const FMassFragmentBitSet& FragmentsAffected)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Mass_BatchAddFragmentInstancesForEntities);

	// here's the scenario:
	// * we get entities from potentially different archetypes
	// * adding a fragment instance consists of two operations: A) add fragment type & B) set fragment value
	//		* some archetypes might already have the "added" fragments so no need for step A
	//		* there might be an "empty" archetype in the mix - then step A results in archetype creation and assigning
	//		* if step A is required then the initial FMassArchetypeEntityCollection instance is no longer valid
	// * setting value can be done uniformly for all entities, remembering some might be in different chunks already
	// * @todo note that after adding fragment type some entities originally in different archetypes end up in the same 
	//		archetype. This could be utilized as a basis for optimization. To be investigated.
	// 

	FScopedCreationContextOperations CreationContextOperations(*this);

	for (const FMassArchetypeEntityCollectionWithPayload& EntityRangesWithPayload : EntityCollections)
	{
		FMassArchetypeHandle TargetArchetypeHandle = EntityRangesWithPayload.GetEntityCollection().GetArchetype();
		FMassArchetypeData* CurrentArchetype = TargetArchetypeHandle.DataPtr.Get();

		if (CurrentArchetype)
		{
			FMassArchetypeEntityCollection::FEntityRangeArray TargetArchetypeEntityRanges;
			bool bFragmentsAddedAreObserved = false;
			FMassFragmentBitSet NewFragmentComposition = CurrentArchetype
				? (CurrentArchetype->GetFragmentBitSet() + FragmentsAffected)
				: FragmentsAffected;
			FMassFragmentBitSet FragmentsAdded;

			if (CurrentArchetype->GetFragmentBitSet() != NewFragmentComposition)
			{
				FragmentsAdded = FragmentsAffected - CurrentArchetype->GetFragmentBitSet();
				bFragmentsAddedAreObserved = ObserverManager.HasObserversForBitSet(FragmentsAdded, EMassObservedOperation::Add);

				FMassArchetypeHandle NewArchetypeHandle = InternalCreateSimilarArchetype(TargetArchetypeHandle.DataPtr, NewFragmentComposition);
				checkSlow(NewArchetypeHandle.IsValid());

				// Move the entity over
				TArray<FMassEntityHandle> EntitiesBeingMoved;
				CurrentArchetype->BatchMoveEntitiesToAnotherArchetype(EntityRangesWithPayload.GetEntityCollection(), *NewArchetypeHandle.DataPtr.Get()
					, EntitiesBeingMoved, &TargetArchetypeEntityRanges);

				for (const FMassEntityHandle& Entity : EntitiesBeingMoved)
				{
					check(GetEntityStorageInterface().IsValidIndex(Entity.Index));

					GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
				}

				TargetArchetypeHandle = NewArchetypeHandle;
			}
			else
			{
				TargetArchetypeEntityRanges = EntityRangesWithPayload.GetEntityCollection().GetRanges();
			}

			// at this point all the entities are in the target archetype, we can set the values
			// note that even though the "subchunk" information could have changed the order of entities is the same and 
			// corresponds to the order in FMassArchetypeEntityCollectionWithPayload's payload
			TargetArchetypeHandle.DataPtr->BatchSetFragmentValues(TargetArchetypeEntityRanges, EntityRangesWithPayload.GetPayload());
			
			if (bFragmentsAddedAreObserved && CreationContextOperations.IsAllowedToTriggerObservers())
			{
				ObserverManager.OnCompositionChanged(
					FMassArchetypeEntityCollection(TargetArchetypeHandle, MoveTemp(TargetArchetypeEntityRanges))
					, FMassArchetypeCompositionDescriptor(MoveTemp(FragmentsAdded))
					, EMassObservedOperation::Add);
			}
		}
		else 
		{
			BatchBuildEntities(EntityRangesWithPayload, FragmentsAffected, FMassArchetypeSharedFragmentValues());
		}
	}
}

void FMassEntityManager::BatchAddSharedFragmentsForEntities(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections
	, const FMassArchetypeSharedFragmentValues& AddedFragmentValues)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Mass_BatchAddConstSharedFragmentForEntities);

	for (const FMassArchetypeEntityCollection& Collection : EntityCollections)
	{
		FMassArchetypeData* CurrentArchetype = Collection.GetArchetype().DataPtr.Get();
		testableCheckfReturn(CurrentArchetype, continue, TEXT("Adding shared fragments to archetype-less entities is not supported"));

		FMassArchetypeCompositionDescriptor NewComposition(CurrentArchetype->GetCompositionDescriptor());
		NewComposition.SharedFragments += AddedFragmentValues.GetSharedFragmentBitSet();
		NewComposition.ConstSharedFragments += AddedFragmentValues.GetConstSharedFragmentBitSet();

		const FMassArchetypeHandle NewArchetypeHandle = CreateArchetype(NewComposition, FMassArchetypeCreationParams(*CurrentArchetype));
		check(NewArchetypeHandle.IsValid());
		FMassArchetypeData* NewArchetype = NewArchetypeHandle.DataPtr.Get();
		check(NewArchetype);
		if (!testableEnsureMsgf(CurrentArchetype != NewArchetype, TEXT("Setting shared fragment values without archetype change is not supported")))
		{
			UE_LOG(LogMass, Warning, TEXT("Trying to set shared fragment values, without adding new shared fragments, is not supported."));
			continue;
		}

		TArray<FMassEntityHandle> EntitiesBeingMoved;
		CurrentArchetype->BatchMoveEntitiesToAnotherArchetype(Collection, *NewArchetype, EntitiesBeingMoved, /*OutNewChunks=*/nullptr, &AddedFragmentValues);

		for (const FMassEntityHandle& Entity : EntitiesBeingMoved)
		{
			check(GetEntityStorageInterface().IsValidIndex(Entity.Index));
			
			GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
		}
	}
}

void FMassEntityManager::MoveEntityToAnotherArchetype(FMassEntityHandle Entity, FMassArchetypeHandle NewArchetypeHandle)
{
	CheckIfEntityIsActive(Entity);

	FMassArchetypeData& NewArchetype = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(NewArchetypeHandle);

	// Move the entity over
	FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(CurrentArchetype);
	CurrentArchetype->MoveEntityToAnotherArchetype(Entity, NewArchetype);
	GetEntityStorageInterface().SetArchetypeFromShared(Entity.Index, NewArchetypeHandle.DataPtr);
}

void FMassEntityManager::SetEntityFragmentsValues(FMassEntityHandle Entity, TArrayView<const FInstancedStruct> FragmentInstanceList)
{
	CheckIfEntityIsActive(Entity);

	FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(CurrentArchetype);
	CurrentArchetype->SetFragmentsData(Entity, FragmentInstanceList);
}

void FMassEntityManager::BatchSetEntityFragmentsValues(const FMassArchetypeEntityCollection& SparseEntities, TArrayView<const FInstancedStruct> FragmentInstanceList)
{
	if (FragmentInstanceList.Num())
	{
		BatchSetEntityFragmentsValues(MakeArrayView(&SparseEntities, 1), FragmentInstanceList);
	}
}

void FMassEntityManager::BatchSetEntityFragmentsValues(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections, TArrayView<const FInstancedStruct> FragmentInstanceList)
{
	if (FragmentInstanceList.IsEmpty())
	{
		return;
	}

	for (const FMassArchetypeEntityCollection& SparseEntities : EntityCollections)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Mass_BatchSetEntityFragmentsValues);

		FMassArchetypeData* Archetype = SparseEntities.GetArchetype().DataPtr.Get();
		check(Archetype);

		for (const FInstancedStruct& FragmentTemplate : FragmentInstanceList)
		{
			Archetype->SetFragmentData(SparseEntities.GetRanges(), FragmentTemplate);
		}
	}
}

void* FMassEntityManager::InternalGetFragmentDataChecked(FMassEntityHandle Entity, const UScriptStruct* FragmentType) const
{
	// note that FragmentType is guaranteed to be of valid type - it's either statically checked by the template versions
	// or `checkf`ed by the non-template one
	CheckIfEntityIsActive(Entity);
	const FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(CurrentArchetype);
	return CurrentArchetype->GetFragmentDataForEntityChecked(FragmentType, Entity.Index);
}

void* FMassEntityManager::InternalGetFragmentDataPtr(FMassEntityHandle Entity, const UScriptStruct* FragmentType) const
{
	// note that FragmentType is guaranteed to be of valid type - it's either statically checked by the template versions
	// or `checkf`ed by the non-template one
	CheckIfEntityIsActive(Entity);
	const FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(CurrentArchetype);
	return CurrentArchetype->GetFragmentDataForEntity(FragmentType, Entity.Index);
}

const FConstSharedStruct* FMassEntityManager::InternalGetConstSharedFragmentPtr(FMassEntityHandle Entity, const UScriptStruct* ConstSharedFragmentType) const
{
	// note that ConstSharedFragmentType is guaranteed to be of valid type - it's either statically checked by the template versions
	// or `checkf`ed by the non-template one
	CheckIfEntityIsActive(Entity);
	const FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(CurrentArchetype);
	const FConstSharedStruct* SharedFragment = CurrentArchetype->GetSharedFragmentValues(Entity).GetConstSharedFragments().FindByPredicate(FStructTypeEqualOperator(ConstSharedFragmentType));
	return SharedFragment;
}

const FSharedStruct* FMassEntityManager::InternalGetSharedFragmentPtr(FMassEntityHandle Entity, const UScriptStruct* SharedFragmentType) const
{
	// note that SharedFragmentType is guaranteed to be of valid type - it's either statically checked by the template versions
	// or `checkf`ed by the non-template one
	CheckIfEntityIsActive(Entity);
	const FMassArchetypeData* CurrentArchetype = GetEntityStorageInterface().GetArchetype(Entity.Index);
	check(CurrentArchetype);
	const FSharedStruct* SharedFragment = CurrentArchetype->GetSharedFragmentValues(Entity).GetSharedFragments().FindByPredicate(FStructTypeEqualOperator(SharedFragmentType));
	return SharedFragment;
}

bool FMassEntityManager::IsEntityValid(FMassEntityHandle Entity) const
{
	return (Entity.Index != UE::Mass::Private::InvalidEntityIndex) 
		&& GetEntityStorageInterface().IsValidIndex(Entity.Index) 
		&& (GetEntityStorageInterface().GetSerialNumber(Entity.Index) == Entity.SerialNumber);
}

bool FMassEntityManager::IsEntityBuilt(FMassEntityHandle Entity) const
{
	CheckIfEntityIsValid(Entity);
	const UE::Mass::IEntityStorageInterface::EEntityState CurrentState = GetEntityStorageInterface().GetEntityState(Entity.Index);
	return CurrentState == UE::Mass::IEntityStorageInterface::EEntityState::Created;
}

void FMassEntityManager::CheckIfEntityIsValid(FMassEntityHandle Entity) const
{
	checkf(IsEntityValid(Entity), TEXT("Invalid entity (ID: %d, SN:%d, %s)"), Entity.Index, Entity.SerialNumber,
		   (Entity.Index == 0) ? TEXT("was never initialized") : TEXT("already destroyed"));
}

void FMassEntityManager::CheckIfEntityIsActive(FMassEntityHandle Entity) const
{
	checkf(IsEntityBuilt(Entity), TEXT("Entity not yet created(ID: %d, SN:%d)"), Entity.Index, Entity.SerialNumber);
}

void FMassEntityManager::GetMatchingArchetypes(const FMassFragmentRequirements& Requirements, TArray<FMassArchetypeHandle>& OutValidArchetypes, const uint32 FromArchetypeDataVersion) const
{
	for (int32 ArchetypeIndex = FromArchetypeDataVersion; ArchetypeIndex < AllArchetypes.Num(); ++ArchetypeIndex)
	{
		checkf(AllArchetypes[ArchetypeIndex].IsValid(), TEXT("We never expect to get any invalid shared ptrs in AllArchetypes"));

		FMassArchetypeData& Archetype = *(AllArchetypes[ArchetypeIndex].Get());

		// Only return archetypes with a newer created version than the specified version, this is for incremental query updates
		ensureMsgf(Archetype.GetCreatedArchetypeDataVersion() > FromArchetypeDataVersion
			, TEXT("There's a stron assumption that archetype's data version corresponds to its index in AllArchetypes"));

		if (Requirements.DoesArchetypeMatchRequirements(Archetype.GetCompositionDescriptor()))
		{
			OutValidArchetypes.Add(AllArchetypes[ArchetypeIndex]);
		}
#if WITH_MASSENTITY_DEBUG
		else
		{
			UE_VLOG_UELOG(GetOwner(), LogMass, VeryVerbose, TEXT("%s")
				, *FMassDebugger::GetArchetypeRequirementCompatibilityDescription(Requirements, Archetype.GetCompositionDescriptor()));
		}
#endif // WITH_MASSENTITY_DEBUG
	}
}

FMassExecutionContext FMassEntityManager::CreateExecutionContext(const float DeltaSeconds)
{
	FMassExecutionContext ExecutionContext(*this, DeltaSeconds);
	ExecutionContext.SetDeferredCommandBuffer(DeferredCommandBuffers[OpenedCommandBufferIndex]);
	return MoveTemp(ExecutionContext);
}

void FMassEntityManager::FlushCommands(TSharedPtr<FMassCommandBuffer>& InCommandBuffer)
{
	if (!ensureMsgf(IsInGameThread(), TEXT("Calling %hs is supported only on the Game Tread"), __FUNCTION__))
	{
		return;
	}
	if (!ensureMsgf(IsProcessing() == false, TEXT("Calling %hs is not supported while Mass Processing is active. Call FMassEntityManager::AppendCommands instead."), __FUNCTION__))
	{
		return;
	}

	if (InCommandBuffer && InCommandBuffer->HasPendingCommands()
		&& (Algo::Find(DeferredCommandBuffers, InCommandBuffer) == nullptr))
	{
		AppendCommands(InCommandBuffer);
	}
	FlushCommands();
}

void FMassEntityManager::FlushCommands()
{
	constexpr int32 MaxIterations = 5;

	if (!ensureMsgf(IsInGameThread(), TEXT("Calling %hs is supported only on the Game Tread"), __FUNCTION__))
	{
		return;
	}
	if (!ensureMsgf(IsProcessing() == false, TEXT("Calling %hs is not supported while Mass Processing is active. Call FMassEntityManager::AppendCommands instead."), __FUNCTION__))
	{
		return;
	}

	if (bCommandBufferFlushingInProgress == false && IsProcessing() == false)
	{
		ON_SCOPE_EXIT
		{
			bCommandBufferFlushingInProgress = false;
		};
		bCommandBufferFlushingInProgress = true;

		int32 IterationCount = 0;
		do 
		{
			const int32 CommandBufferIndexToFlush = OpenedCommandBufferIndex;

			// buffer swap. Code instigated by observers can still use Defer() to push commands.
			OpenedCommandBufferIndex = (OpenedCommandBufferIndex + 1) % DeferredCommandBuffers.Num();
			ensureMsgf(DeferredCommandBuffers[OpenedCommandBufferIndex]->HasPendingCommands() == false
				, TEXT("The freshly opened command buffer is expected to be empty upon switching"));

			DeferredCommandBuffers[CommandBufferIndexToFlush]->Flush(*this);

			// repeat if there were commands submitted while commands were being flushed (by observers for example)
		} while (DeferredCommandBuffers[OpenedCommandBufferIndex]->HasPendingCommands() && ++IterationCount < MaxIterations);

		UE_CVLOG_UELOG(IterationCount >= MaxIterations, GetOwner(), LogMass, Error, TEXT("Reached loop count limit while flushing commands. Limiting the number of commands pushed during commands flushing could help."));
	}
}

void FMassEntityManager::AppendCommands(TSharedPtr<FMassCommandBuffer>& InOutCommandBuffer)
{
	if (!ensureMsgf(Algo::Find(DeferredCommandBuffers, InOutCommandBuffer) == nullptr
		, TEXT("We don't expect AppendCommands to be called with EntityManager's command buffer as the input parameter")))
	{
		return;
	}
	Defer().MoveAppend(*InOutCommandBuffer.Get());
}

TSharedRef<FMassEntityManager::FEntityCreationContext> FMassEntityManager::GetOrMakeCreationContext()
{
	if (ActiveCreationContext.IsValid())
	{
		return ActiveCreationContext.Pin().ToSharedRef();
	}
	else
	{
		FEntityCreationContext* CreationContext = new FEntityCreationContext(*this);
		TSharedRef<FEntityCreationContext> SharedContext = MakeShareable(CreationContext);
		ActiveCreationContext = SharedContext;
		return SharedContext;
	}
}

TSharedRef<FMassEntityManager::FEntityCreationContext> FMassEntityManager::GetOrMakeCreationContext(TConstArrayView<FMassEntityHandle> ReservedEntities
	, FMassArchetypeEntityCollection&& EntityCollection)
{
	if (ActiveCreationContext.IsValid())
	{
		TSharedPtr<FEntityCreationContext> SharedContext = ActiveCreationContext.Pin();
		CA_ASSUME(SharedContext);
		SharedContext->AppendEntities(ReservedEntities, MoveTemp(EntityCollection));
		return SharedContext.ToSharedRef();
	}
	else
	{
		FEntityCreationContext* CreationContext = new FEntityCreationContext(*this, ReservedEntities, MoveTemp(EntityCollection));
		TSharedRef<FEntityCreationContext> SharedContext = MakeShareable(CreationContext);
		ActiveCreationContext = SharedContext;
		return SharedContext;
	}
}

bool FMassEntityManager::DirtyCreationContext()
{
	if (TSharedPtr<FEntityCreationContext> AsSharedPtr = ActiveCreationContext.Pin())
	{
		AsSharedPtr->MarkDirty();
		return true;
	}
	return false;
}

bool FMassEntityManager::DebugDoCollectionsOverlapCreationContext(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections) const
{
	if (TSharedPtr<FEntityCreationContext> AsSharedPtr = ActiveCreationContext.Pin())
	{
		TConstArrayView<FMassArchetypeEntityCollection> CreationCollections = AsSharedPtr->EntityCollections;
		return CreationCollections.GetData() <= EntityCollections.GetData()
			&& EntityCollections.GetData() <= CreationCollections.GetData() + CreationCollections.Num();
	}

	return false;
}

void FMassEntityManager::SetDebugName(const FString& NewDebugGame) 
{ 
#if WITH_MASSENTITY_DEBUG
	DebugName = NewDebugGame; 
#endif // WITH_MASSENTITY_DEBUG
}

#if WITH_MASSENTITY_DEBUG
void FMassEntityManager::DebugPrintArchetypes(FOutputDevice& Ar, const bool bIncludeEmpty) const
{
	Ar.Logf(ELogVerbosity::Log, TEXT("Listing archetypes contained in EntityManager owned by %s"), *GetPathNameSafe(GetOwner()));

	int32 NumBuckets = 0;
	int32 NumArchetypes = 0;
	int32 LongestArchetypeBucket = 0;
	for (const auto& KVP : FragmentHashToArchetypeMap)
	{
		for (const TSharedPtr<FMassArchetypeData>& ArchetypePtr : KVP.Value)
		{
			if (ArchetypePtr.IsValid() && (bIncludeEmpty == true || ArchetypePtr->GetChunkCount() > 0))
			{
				ArchetypePtr->DebugPrintArchetype(Ar);
			}
		}

		const int32 NumArchetypesInBucket = KVP.Value.Num();
		LongestArchetypeBucket = FMath::Max(LongestArchetypeBucket, NumArchetypesInBucket);
		NumArchetypes += NumArchetypesInBucket;
		++NumBuckets;
	}

	Ar.Logf(ELogVerbosity::Log, TEXT("FragmentHashToArchetypeMap: %d archetypes across %d buckets, longest bucket is %d"),
		NumArchetypes, NumBuckets, LongestArchetypeBucket);
}

void FMassEntityManager::DebugGetArchetypesStringDetails(FOutputDevice& Ar, const bool bIncludeEmpty) const
{
	Ar.SetAutoEmitLineTerminator(true);
	for (auto Pair : FragmentHashToArchetypeMap)
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("\n-----------------------------------\nHash: %u"), Pair.Key);
		for (TSharedPtr<FMassArchetypeData> Archetype : Pair.Value)
		{
			if (Archetype.IsValid() && (bIncludeEmpty == true || Archetype->GetChunkCount() > 0))
			{
				Archetype->DebugPrintArchetype(Ar);
				Ar.Logf(ELogVerbosity::Log, TEXT("+++++++++++++++++++++++++\n"));
			}
		}
	}
}

void FMassEntityManager::DebugGetArchetypeFragmentTypes(const FMassArchetypeHandle& Archetype, TArray<const UScriptStruct*>& InOutFragmentList) const
{
	if (Archetype.IsValid())
	{
		const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(Archetype);
		ArchetypeData.GetCompositionDescriptor().Fragments.ExportTypes(InOutFragmentList);
	}
}

int32 FMassEntityManager::DebugGetArchetypeEntitiesCount(const FMassArchetypeHandle& Archetype) const
{
	return Archetype.IsValid() ? FMassArchetypeHelper::ArchetypeDataFromHandleChecked(Archetype).GetNumEntities() : 0;
}

int32 FMassEntityManager::DebugGetArchetypeEntitiesCountPerChunk(const FMassArchetypeHandle& Archetype) const
{
	return Archetype.IsValid() ? FMassArchetypeHelper::ArchetypeDataFromHandleChecked(Archetype).GetNumEntitiesPerChunk() : 0;
}

int32 FMassEntityManager::DebugGetEntityCount() const
{
	return GetEntityStorageInterface().Num() - NumReservedEntities - GetEntityStorageInterface().ComputeFreeSize();
}

int32 FMassEntityManager::DebugGetArchetypesCount() const
{
	return AllArchetypes.Num();
}

void FMassEntityManager::DebugRemoveAllEntities()
{
	for (int EntityIndex = NumReservedEntities, EndIndex = GetEntityStorageInterface().Num(); EntityIndex < EndIndex; ++EntityIndex)
	{
		if (GetEntityStorageInterface().IsValid(EntityIndex) == false)
		{
			// already dead
			continue;
		}
		FMassArchetypeData* Archetype = GetEntityStorageInterface().GetArchetype(EntityIndex);
		check(Archetype);
		FMassEntityHandle Entity;
		Entity.Index = EntityIndex;
		Entity.SerialNumber = GetEntityStorageInterface().GetSerialNumber(EntityIndex);
		Archetype->RemoveEntity(Entity);

		GetEntityStorageInterface().ForceReleaseOne(Entity);
	}
}

void FMassEntityManager::DebugForceArchetypeDataVersionBump()
{
	++ArchetypeDataVersion;
}

void FMassEntityManager::DebugGetArchetypeStrings(const FMassArchetypeHandle& Archetype, TArray<FName>& OutFragmentNames, TArray<FName>& OutTagNames)
{
	if (Archetype.IsValid() == false)
	{
		return;
	}

	const FMassArchetypeData& ArchetypeRef = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(Archetype);
	
	OutFragmentNames.Reserve(ArchetypeRef.GetFragmentConfigs().Num());
	for (const FMassArchetypeFragmentConfig& FragmentConfig : ArchetypeRef.GetFragmentConfigs())
	{
		checkSlow(FragmentConfig.FragmentType);
		OutFragmentNames.Add(FragmentConfig.FragmentType->GetFName());
	}

	ArchetypeRef.GetTagBitSet().DebugGetIndividualNames(OutTagNames);
}

FMassEntityHandle FMassEntityManager::DebugGetEntityIndexHandle(const int32 EntityIndex) const
{
	return GetEntityStorageInterface().IsValidIndex(EntityIndex) ? FMassEntityHandle(EntityIndex, GetEntityStorageInterface().GetSerialNumber(EntityIndex)) : FMassEntityHandle();
}

const FString& FMassEntityManager::DebugGetName() const
{
	return DebugName;
}

FMassRequirementAccessDetector& FMassEntityManager::GetRequirementAccessDetector()
{
	return RequirementAccessDetector;
}

#endif // WITH_MASSENTITY_DEBUG


//-----------------------------------------------------------------------------
// DEPRECATED
//-----------------------------------------------------------------------------

const FMassArchetypeEntityCollection& FMassEntityManager::FEntityCreationContext::GetEntityCollection() const
{
	static FMassArchetypeEntityCollection EmptyCollection;
	return EntityCollections.Num() ? EntityCollections[0] : EmptyCollection;
}
