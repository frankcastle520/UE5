// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/GCObject.h"
#include "MassEntityTypes.h"
#include "MassProcessingTypes.h"
#include "MassEntityQuery.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5
#include "StructUtils/InstancedStruct.h"
#include "StructUtils/StructUtilsTypes.h"
#endif
#include "MassObserverManager.h"
#include "Containers/MpscQueue.h"
#include "MassRequirementAccessDetector.h"
#include "Templates/FunctionFwd.h"
#include "MassEntityManagerStorage.h"


struct FInstancedStruct;
struct FMassEntityQuery;
struct FMassExecutionContext;
struct FMassArchetypeData;
struct FMassCommandBuffer;
struct FMassArchetypeEntityCollection;
class FOutputDevice;
struct FMassDebugger;
enum class EMassFragmentAccess : uint8;
enum class EForkProcessRole : uint8;
namespace UE::Mass::Private
{
	struct FEntityStorageInitializer;
}

#define MASS_CONCURRENT_RESERVE 1

/** 
 * The type responsible for hosting Entities managing Archetypes.
 * Entities are stored as FEntityData entries in a chunked array. 
 * Each valid entity is assigned to an Archetype that stored fragments associated with a given entity at the moment. 
 * 
 * FMassEntityManager supplies API for entity creation (that can result in archetype creation) and entity manipulation.
 * Even though synchronized manipulation methods are available in most cases the entity operations are performed via a command 
 * buffer. The default command buffer can be obtained with a Defer() call. @see FMassCommandBuffer for more details.
 * 
 * FMassEntityManager are meant to be stored with a TSharedPtr or TSharedRef. Some of Mass API pass around 
 * FMassEntityManager& but programmers can always use AsShared() call to obtain a shared ref for a given manager instance 
 * (as supplied by deriving from TSharedFromThis<FMassEntityManager>).
 * IMPORTANT: if you create your own FMassEntityManager instance remember to call Initialize() before using it.
 */
struct MASSENTITY_API FMassEntityManager : public TSharedFromThis<FMassEntityManager>, public FGCObject
{
	friend FMassEntityQuery;
	friend FMassDebugger;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnNewArchetypeDelegate, const FMassArchetypeHandle&);

private:
	// Index 0 is reserved so we can treat that index as an invalid entity handle
	constexpr static int32 NumReservedEntities = 1;
	
public:
	struct FScopedProcessing
	{
		explicit FScopedProcessing(std::atomic<int32>& InProcessingScopeCount) : ScopedProcessingCount(InProcessingScopeCount)
		{
			++ScopedProcessingCount;
		}
		~FScopedProcessing()
		{
			--ScopedProcessingCount;
		}
	private:
		std::atomic<int32>& ScopedProcessingCount;
	};
	using FStructInitializationCallback = TFunctionRef<void(void* Fragment, const UScriptStruct& FragmentType)>;

	const static FMassEntityHandle InvalidEntity;

	explicit FMassEntityManager(UObject* InOwner = nullptr);
	FMassEntityManager(const FMassEntityManager& Other) = delete;
	virtual ~FMassEntityManager();

	// FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("FMassEntityManager");
	}
	// End of FGCObject interface
	void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize);

	// Default to use single threaded implementation
	void Initialize();
	void Initialize(const FMassEntityManagerStorageInitParams& InitializationParams);
	void PostInitialize();
	void Deinitialize();

	/** 
	 * A special, relaxed but slower version of CreateArchetype functions that allows FragmentAngTagsList to contain 
	 * both fragments and tags. 
	 */
	FMassArchetypeHandle CreateArchetype(TConstArrayView<const UScriptStruct*> FragmentsAndTagsList, const FMassArchetypeCreationParams& CreationParams = FMassArchetypeCreationParams());

	/**
	 * A special, relaxed but slower version of CreateArchetype functions that allows FragmentAngTagsList to contain
	 * both fragments and tags. This version takes an original archetype and copies it layout, then appends any fragments and tags from the
	 * provided list if they're not already in the original archetype.
	 * 
	 * @param SourceArchetype The archetype where the composition will be copied from.
	 * @param FragmentsAndTagsList The list of fragments and tags to add to the copied composition.
	 */
	FMassArchetypeHandle CreateArchetype(FMassArchetypeHandle SourceArchetype, TConstArrayView<const UScriptStruct*> FragmentsAndTagsList);
	
	/**
	 * A special, relaxed but slower version of CreateArchetype functions that allows FragmentAngTagsList to contain
	 * both fragments and tags. This version takes an original archetype and copies it layout, then appends any fragments and tags from the
	 * provided list if they're not already in the original archetype.
	 * 
	 * @param SourceArchetype The archetype where the composition will be copied from.
	 * @param FragmentsAndTagsList The list of fragments and tags to add to the copied composition.
	 * @param CreationParams Additional arguments used to create the new archetype.
	 */
	FMassArchetypeHandle CreateArchetype(FMassArchetypeHandle SourceArchetype, TConstArrayView<const UScriptStruct*> FragmentsAndTagsList, 
		const FMassArchetypeCreationParams& CreationParams);

	/**
	 * CreateArchetype from a composition descriptor and initial values
	 *
	 * @param Composition of fragment, tag and chunk fragment types
	 * @param CreationParams Parameters used during archetype construction
	 * @return a handle of a new archetype 
	 */
	FMassArchetypeHandle CreateArchetype(const FMassArchetypeCompositionDescriptor& Composition, const FMassArchetypeCreationParams& CreationParams = FMassArchetypeCreationParams());

	/**
	 *  Creates an archetype like SourceArchetype + InFragments.
	 *  @param SourceArchetype the archetype used to initially populate the list of fragments of the archetype being created.
	 *  @param InFragments list of unique fragments to add to fragments fetched from SourceArchetype. Note that
	 *   adding an empty list is not supported and doing so will result in failing a `check`
	 *  @return a handle of a new archetype
	 *  @note it's caller's responsibility to ensure that NewFragmentList is not empty and contains only fragment
	 *   types that SourceArchetype doesn't already have. If the caller cannot guarantee it use of AddFragment functions
	 *   family is recommended.
	 */
	FMassArchetypeHandle CreateArchetype(const TSharedPtr<FMassArchetypeData>& SourceArchetype, const FMassFragmentBitSet& InFragments);
	
	/** 
	 *  Creates an archetype like SourceArchetype + InFragments. 
	 *  @param SourceArchetype the archetype used to initially populate the list of fragments of the archetype being created. 
	 *  @param InFragments list of unique fragments to add to fragments fetched from SourceArchetype. Note that 
	 *   adding an empty list is not supported and doing so will result in failing a `check`
	 *  @param CreationParams Parameters used during archetype construction
	 *  @return a handle of a new archetype
	 *  @note it's caller's responsibility to ensure that NewFragmentList is not empty and contains only fragment
	 *   types that SourceArchetype doesn't already have. If the caller cannot guarantee it use of AddFragment functions
	 *   family is recommended.
	 */
	FMassArchetypeHandle CreateArchetype(const TSharedPtr<FMassArchetypeData>& SourceArchetype, const FMassFragmentBitSet& InFragments, 
		const FMassArchetypeCreationParams& CreationParams);

	/** 
	 * A helper function to be used when creating entities with shared fragments provided, or when adding shared fragments
	 * to existing entities
	 * @param ArchetypeHandle that's the assumed target archetype. But we'll be making sure its composition matches SharedFragmentsBitSet
	 * @param SharedFragmentBitSet indicates which shared fragments we want the target archetype to have. If ArchetypeHandle 
	 *	doesn't have these a new archetype will be created.
	 */
	FMassArchetypeHandle GetOrCreateSuitableArchetype(const FMassArchetypeHandle& ArchetypeHandle
		, const FMassSharedFragmentBitSet& SharedFragmentBitSet
		, const FMassConstSharedFragmentBitSet& ConstSharedFragmentBitSet
		, const FMassArchetypeCreationParams& CreationParams = FMassArchetypeCreationParams());

	/** Fetches the archetype for a given Entity. If Entity is not valid it will still return a handle, just with an invalid archetype */
	FMassArchetypeHandle GetArchetypeForEntity(FMassEntityHandle Entity) const;
	/**
	 * Fetches the archetype for a given Entity. Note that it's callers responsibility the given Entity handle is valid.
	 * If you can't ensure that call GetArchetypeForEntity.
	 */
	FMassArchetypeHandle GetArchetypeForEntityUnsafe(FMassEntityHandle Entity) const;

	/** Method to iterate on all the fragment types of an archetype */
	static void ForEachArchetypeFragmentType(const FMassArchetypeHandle& ArchetypeHandle, TFunction< void(const UScriptStruct* /*FragmentType*/)> Function);

	/**
	 * Go through all archetypes and compact entities
	 * @param TimeAllowed to do entity compaction, once it reach that time it will stop and return
	 */
	void DoEntityCompaction(const double TimeAllowed);

	/**
	 * Creates fully built entity ready to be used by the subsystem
	 * @param ArchetypeHandle you want this entity to be
	 * @param SharedFragmentValues to be associated with the entity
	 * @return FMassEntityHandle id of the newly created entity */
	FMassEntityHandle CreateEntity(const FMassArchetypeHandle& ArchetypeHandle, const FMassArchetypeSharedFragmentValues& SharedFragmentValues = {});

	/**
	 * Creates fully built entity ready to be used by the subsystem
	 * @param FragmentInstanceList is the fragments to create the entity from and initialize values
	 * @param SharedFragmentValues to be associated with the entity
	 * @return FMassEntityHandle id of the newly created entity */
	FMassEntityHandle CreateEntity(TConstArrayView<FInstancedStruct> FragmentInstanceList, const FMassArchetypeSharedFragmentValues& SharedFragmentValues = {}, const FMassArchetypeCreationParams& CreationParams = FMassArchetypeCreationParams());

	/**
	 * A dedicated structure for ensuring the "on entities creation" observers get notified only once all other 
	 * initialization operations are done and this creation context instance gets released. 
	 */
	struct MASSENTITY_API FEntityCreationContext
	{
	private:
		FEntityCreationContext();
		explicit FEntityCreationContext(FMassEntityManager& InManager, const TConstArrayView<FMassEntityHandle> InCreatedEntities = {});
		FEntityCreationContext(FMassEntityManager& InManager, const TConstArrayView<FMassEntityHandle> InCreatedEntities, FMassArchetypeEntityCollection&& EntityCollection);
		
	public:
		~FEntityCreationContext();

		/** Returns EntityCollections, reconstructing them if needed (empty or dirtied). */
		TConstArrayView<FMassArchetypeEntityCollection> GetEntityCollections() const;
		int32 GetSpawnedNum() const { return CreatedEntities.Num(); }
		void MarkDirty();
		FORCEINLINE bool IsDirty() const { return EntityCollections.IsEmpty() && (CreatedEntities.IsEmpty() == false); }
		void AppendEntities(const TConstArrayView<FMassEntityHandle> EntitiesToAppend);
		void AppendEntities(const TConstArrayView<FMassEntityHandle> EntitiesToAppend, FMassArchetypeEntityCollection&& EntityCollection);

		/** Function for debugging/testing purposes. We don't expect users to ever call it, always get collections via GetEntityCollections */
		bool DebugAreEntityCollectionsUpToDate() const { return EntityCollections.IsEmpty() == CreatedEntities.IsEmpty(); }

		UE_DEPRECATED(5.5, "This constructor is now deprecated and defunct. Use one of the others instead.")
		explicit FEntityCreationContext(const int32 InNumSpawned) : FEntityCreationContext() {}
		UE_DEPRECATED(5.5, "This function is now deprecated since FEntityCreationContext can contain more than a single collection now. Use GetEntityCollections instead.")
		const FMassArchetypeEntityCollection& GetEntityCollection() const;

	private:
		friend FMassEntityManager;
		/** To be called in case of processor forking. */
		void ForceUpdateCurrentThreadID();

		/**
		 * Identifies the thread where given FEntityCreationContext instance was created. All subsequent operations are 
		 * expected to be run in the same thread.
		 */
		uint32 OwnerThreadId;
		mutable TArray<FMassArchetypeEntityCollection> EntityCollections;
		TArray<FMassEntityHandle> CreatedEntities;
		FMassArchetypeEntityCollection::EDuplicatesHandling CollectionCreationDuplicatesHandling = FMassArchetypeEntityCollection::EDuplicatesHandling::NoDuplicates;
		TSharedPtr<FMassEntityManager> Manager;
	};
	/**
	 * The main use-case for this function is to create a blank FEntityCreationContext and hold on to it while creating 
	 * a bunch of entities (with multiple calls to BatchCreate* and/or BatchBuild*) and modifying them (with mutating batched API)
	 * while not causing multiple Observers to trigger. All the observers will be triggered at one go, once the FEntityCreationContext 
	 * instance gets destroyed. 
	 * @return ActiveCreationContext. If it's valid FEntityCreationContext instance will be created and assigned to ActiveCreationContext first.
	 */
	TSharedRef<FEntityCreationContext> GetOrMakeCreationContext();

	/**
	 * A version of CreateEntity that's creating a number of entities (Count) in one go
	 * @param ArchetypeHandle you want this entity to be
	 * @param SharedFragmentValues to be associated with the entities
	 * @param ReservedEntities a list of reserved entities that have not yet been assigned to an archetype.
	 * @return a creation context that will notify all the interested observers about newly created fragments once the context is released */
	TSharedRef<FEntityCreationContext> BatchCreateReservedEntities(const FMassArchetypeHandle& ArchetypeHandle,
		const FMassArchetypeSharedFragmentValues& SharedFragmentValues, TConstArrayView<FMassEntityHandle> ReservedEntities);
	FORCEINLINE TSharedRef<FEntityCreationContext> BatchCreateReservedEntities(const FMassArchetypeHandle& ArchetypeHandle,
		TConstArrayView<FMassEntityHandle> OutEntities)
	{
		return BatchCreateReservedEntities(ArchetypeHandle, FMassArchetypeSharedFragmentValues(), OutEntities);
	}
	/**
	 * A version of CreateEntity that's creating a number of entities (Count) in one go
	 * @param ArchetypeHandle you want this entity to be
	 * @param SharedFragmentValues to be associated with the entities
	 * @param Count number of entities to create
	 * @param InOutEntities the newly created entities are appended to given array, i.e. the pre-existing content of OutEntities won't be affected by the call
	 * @return a creation context that will notify all the interested observers about newly created fragments once the context is released */
	TSharedRef<FEntityCreationContext> BatchCreateEntities(const FMassArchetypeHandle& ArchetypeHandle, const FMassArchetypeSharedFragmentValues& SharedFragmentValues, const int32 Count, TArray<FMassEntityHandle>& InOutEntities);
	FORCEINLINE TSharedRef<FEntityCreationContext> BatchCreateEntities(const FMassArchetypeHandle& ArchetypeHandle, const int32 Count, TArray<FMassEntityHandle>& InOutEntities)
	{
		return BatchCreateEntities(ArchetypeHandle, FMassArchetypeSharedFragmentValues(), Count, InOutEntities);
	}

	/**
	 * Destroys a fully built entity, use ReleaseReservedEntity if entity was not yet built.
	 * @param Entity to destroy */
	void DestroyEntity(FMassEntityHandle Entity);

	/**
	 * Reserves an entity in the subsystem, the entity is still not ready to be used by the subsystem, need to call BuildEntity()
	 * @return FMassEntityHandle id of the reserved entity */
	FMassEntityHandle ReserveEntity();

	/**
	 * Builds an entity for it to be ready to be used by the subsystem
	 * @param Entity to build which was retrieved with ReserveEntity() method
	 * @param ArchetypeHandle you want this entity to be
	 * @param SharedFragmentValues to be associated with the entity
	 */
	void BuildEntity(FMassEntityHandle Entity, const FMassArchetypeHandle& ArchetypeHandle, const FMassArchetypeSharedFragmentValues& SharedFragmentValues = {});

	/**
	 * Builds an entity for it to be ready to be used by the subsystem
	 * @param Entity to build which was retrieved with ReserveEntity() method
	 * @param FragmentInstanceList is the fragments to create the entity from and initialize values
	 * @param SharedFragmentValues to be associated with the entity
	 */
	void BuildEntity(FMassEntityHandle Entity, TConstArrayView<FInstancedStruct> FragmentInstanceList, const FMassArchetypeSharedFragmentValues& SharedFragmentValues = {});

	/*
	 * Releases a previously reserved entity that was not yet built, otherwise call DestroyEntity
	 * @param Entity to release */
	void ReleaseReservedEntity(FMassEntityHandle Entity);

	/**
	 * Destroys all the entities in the provided array of entities. The function will also gracefully handle entities
	 * that have been reserved but not created yet.
	 * @note the function doesn't handle duplicates in InEntities.
	 * @param InEntities to destroy
	 */
	void BatchDestroyEntities(TConstArrayView<FMassEntityHandle> InEntities);

	/**
	 * Destroys all the entities provided via the Collection. The function will also gracefully handle entities
	 * that have been reserved but not created yet.
	 * @param Collection to destroy
	 */
	void BatchDestroyEntityChunks(const FMassArchetypeEntityCollection& Collection);
	void BatchDestroyEntityChunks(TConstArrayView<FMassArchetypeEntityCollection> Collections);

	void AddFragmentToEntity(FMassEntityHandle Entity, const UScriptStruct* FragmentType);
	void AddFragmentToEntity(FMassEntityHandle Entity, const UScriptStruct* FragmentType, const FStructInitializationCallback& Initializer);

	/** 
	 *  Ensures that only unique fragments are added. 
	 *  @note It's caller's responsibility to ensure Entity's and FragmentList's validity. 
	 */
	void AddFragmentListToEntity(FMassEntityHandle Entity, TConstArrayView<const UScriptStruct*> FragmentList);

	void AddFragmentInstanceListToEntity(FMassEntityHandle Entity, TConstArrayView<FInstancedStruct> FragmentInstanceList);
	void RemoveFragmentFromEntity(FMassEntityHandle Entity, const UScriptStruct* FragmentType);
	void RemoveFragmentListFromEntity(FMassEntityHandle Entity, TConstArrayView<const UScriptStruct*> FragmentList);

	void AddTagToEntity(FMassEntityHandle Entity, const UScriptStruct* TagType);
	void RemoveTagFromEntity(FMassEntityHandle Entity, const UScriptStruct* TagType);
	void SwapTagsForEntity(FMassEntityHandle Entity, const UScriptStruct* FromFragmentType, const UScriptStruct* ToFragmentType);

	/** 
	 * Adds a new const shared fragment to the given entity. Note that it only works if the given entity doesn't have
	 * a shared fragment of the given type. The function will give a soft "pass" if the entity has the shared fragment
	 * of the same value. Setting shared fragment value (i.e. changing) is not supported and the function will log
	 * a warning if that's attempted.
	 * @return whether the Entity has the Fragment value assigned to it, regardless of its original state (i.e. the function will
	 *	return true also if the Entity already had the same values associated with it)
	 */
	bool AddConstSharedFragmentToEntity(const FMassEntityHandle Entity, const FConstSharedStruct& InConstSharedFragment);

	/**
	 * Removes a const shared fragment of the given type from the entity.
	 * Will do nothing if entity did not have the shared fragment.
	 * @return True if fragment removed from entity, false otherwise.
	 */
	bool RemoveConstSharedFragmentFromEntity(const FMassEntityHandle Entity, const UScriptStruct& ConstSharedFragmentType);

	/** 
	 * Reserves Count number of entities and appends them to InOutEntities
	 * @return a view into InOutEntities containing only the freshly reserved entities
	 */
	TConstArrayView<FMassEntityHandle> BatchReserveEntities(const int32 Count, TArray<FMassEntityHandle>& InOutEntities);
	
	/**
	 * Reserves number of entities corresponding to number of entries in the provided array view InOutEntities.
	 * As a result InOutEntities gets filled with handles of reserved entities
	 * @return the number of entities reserved
	 */
	int32 BatchReserveEntities(TArrayView<FMassEntityHandle> InOutEntities);

	TSharedRef<FEntityCreationContext> BatchBuildEntities(const FMassArchetypeEntityCollectionWithPayload& EncodedEntitiesWithPayload, const FMassFragmentBitSet& FragmentsAffected
		, const FMassArchetypeSharedFragmentValues& SharedFragmentValues = {}, const FMassArchetypeCreationParams& CreationParams = FMassArchetypeCreationParams());
	TSharedRef<FEntityCreationContext> BatchBuildEntities(const FMassArchetypeEntityCollectionWithPayload& EncodedEntitiesWithPayload, FMassArchetypeCompositionDescriptor&& Composition
		, const FMassArchetypeSharedFragmentValues& SharedFragmentValues = {}, const FMassArchetypeCreationParams& CreationParams = FMassArchetypeCreationParams());
	void BatchChangeTagsForEntities(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections, const FMassTagBitSet& TagsToAdd, const FMassTagBitSet& TagsToRemove);
	void BatchChangeFragmentCompositionForEntities(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections, const FMassFragmentBitSet& FragmentsToAdd, const FMassFragmentBitSet& FragmentsToRemove);
	void BatchAddFragmentInstancesForEntities(TConstArrayView<FMassArchetypeEntityCollectionWithPayload> EntityCollections, const FMassFragmentBitSet& FragmentsAffected);
	/** 
	 * Adds a new const and non-const shared fragments to all entities provided via EntityCollections 
	 */
	void BatchAddSharedFragmentsForEntities(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections, const FMassArchetypeSharedFragmentValues& AddedFragmentValues);

	/**
	 * Adds fragments and tags indicated by InOutDescriptor to the Entity. The function also figures out which elements
	 * in InOutDescriptor are missing from the current composition of the given entity and then returns the resulting 
	 * delta via InOutDescriptor.
	 */
	void AddCompositionToEntity_GetDelta(FMassEntityHandle Entity, FMassArchetypeCompositionDescriptor& InOutDescriptor);
	void RemoveCompositionFromEntity(FMassEntityHandle Entity, const FMassArchetypeCompositionDescriptor& InDescriptor);

	const FMassArchetypeCompositionDescriptor& GetArchetypeComposition(const FMassArchetypeHandle& ArchetypeHandle) const;

	/** 
	 * Moves an entity over to a new archetype by copying over fragments common to both archetypes
	 * @param Entity is the entity to move 
	 * @param NewArchetypeHandle the handle to the new archetype
	 */
	void MoveEntityToAnotherArchetype(FMassEntityHandle Entity, FMassArchetypeHandle NewArchetypeHandle);

	/** Copies values from FragmentInstanceList over to Entity's fragment. Caller is responsible for ensuring that 
	 *  the given entity does have given fragments. Failing this assumption will cause a check-fail.*/
	void SetEntityFragmentsValues(FMassEntityHandle Entity, TArrayView<const FInstancedStruct> FragmentInstanceList);

	/** Copies values from FragmentInstanceList over to fragments of given entities collection. The caller is responsible 
	 *  for ensuring that the given entity archetype (FMassArchetypeEntityCollection .Archetype) does have given fragments. 
	 *  Failing this assumption will cause a check-fail. */
	static void BatchSetEntityFragmentsValues(const FMassArchetypeEntityCollection& SparseEntities, TArrayView<const FInstancedStruct> FragmentInstanceList);

	static void BatchSetEntityFragmentsValues(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections, TArrayView<const FInstancedStruct> FragmentInstanceList);

	// Return true if it is an valid built entity
	bool IsEntityActive(FMassEntityHandle Entity) const 
	{
		return IsEntityValid(Entity) && IsEntityBuilt(Entity);
	}

	// Returns true if Entity is valid
	bool IsEntityValid(FMassEntityHandle Entity) const;

	// Returns true if Entity is has been fully built (expecting a valid Entity)
	bool IsEntityBuilt(FMassEntityHandle Entity) const;

	// Asserts that IsEntityValid
	void CheckIfEntityIsValid(FMassEntityHandle Entity) const;

	// Asserts that IsEntityBuilt
	void CheckIfEntityIsActive(FMassEntityHandle Entity) const;

	template<typename FragmentType>
	FragmentType& GetFragmentDataChecked(FMassEntityHandle Entity) const
	{
		static_assert(TIsDerivedFrom<FragmentType, FMassFragment>::IsDerived
			, "Given struct doesn't represent a valid fragment type. Make sure to inherit from FMassFragment or one of its child-types.");
		return *((FragmentType*)InternalGetFragmentDataChecked(Entity, FragmentType::StaticStruct()));
	}

	template<typename FragmentType>
	FragmentType* GetFragmentDataPtr(FMassEntityHandle Entity) const
	{
		static_assert(TIsDerivedFrom<FragmentType, FMassFragment>::IsDerived
			, "Given struct doesn't represent a valid fragment type. Make sure to inherit from FMassFragment or one of its child-types.");
		return (FragmentType*)InternalGetFragmentDataPtr(Entity, FragmentType::StaticStruct());
	}

	FStructView GetFragmentDataStruct(FMassEntityHandle Entity, const UScriptStruct* FragmentType) const
	{
		checkf((FragmentType != nullptr) && FragmentType->IsChildOf(FMassFragment::StaticStruct())
			, TEXT("GetFragmentDataStruct called with an invalid fragment type '%s'"), *GetPathNameSafe(FragmentType));
		return FStructView(FragmentType, static_cast<uint8*>(InternalGetFragmentDataPtr(Entity, FragmentType)));
	}

	template<typename ConstSharedFragmentType>
	ConstSharedFragmentType* GetConstSharedFragmentDataPtr(FMassEntityHandle Entity) const
	{
		static_assert(TIsDerivedFrom<ConstSharedFragmentType, FMassConstSharedFragment>::IsDerived, "Given struct doesn't represent a valid const shared fragment type. Make sure to inherit from FMassConstSharedFragment or one of its child-types.");
		const FConstSharedStruct* ConstSharedStruct = InternalGetConstSharedFragmentPtr(Entity, ConstSharedFragmentType::StaticStruct());
		return (ConstSharedFragmentType*)(ConstSharedStruct ? ConstSharedStruct->GetMemory() : nullptr);
	}

	template<typename ConstSharedFragmentType>
	ConstSharedFragmentType& GetConstSharedFragmentDataChecked(FMassEntityHandle Entity) const
	{
		ConstSharedFragmentType* TypePtr = GetConstSharedFragmentDataPtr<ConstSharedFragmentType>(Entity);
		check(TypePtr);
		return *TypePtr;
	}

	FConstStructView GetConstSharedFragmentDataStruct(FMassEntityHandle Entity, const UScriptStruct* ConstSharedFragmentType) const
	{
		checkf((ConstSharedFragmentType != nullptr) && ConstSharedFragmentType->IsChildOf(FMassConstSharedFragment::StaticStruct())
			, TEXT("GetConstSharedFragmentDataStruct called with an invalid fragment type '%s'"), *GetPathNameSafe(ConstSharedFragmentType));
		const FConstSharedStruct* ConstSharedStruct = InternalGetConstSharedFragmentPtr(Entity, ConstSharedFragmentType);
		return ConstSharedStruct
			? FConstStructView(*ConstSharedStruct)
			: FConstStructView();
	}

	template<typename SharedFragmentType>
	TConstArrayView<FSharedStruct> GetSharedFragmentsOfType()
	{
		static_assert(TIsDerivedFrom<SharedFragmentType, FMassSharedFragment>::IsDerived
			, "Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types.");

		TArray<FSharedStruct>* InstancesOfType = SharedFragmentsTypeMap.Find(SharedFragmentType::StaticStruct());
		return InstancesOfType ? *InstancesOfType : TConstArrayView<FSharedStruct>();
	}

	template<typename SharedFragmentType>
	SharedFragmentType* GetSharedFragmentDataPtr(FMassEntityHandle Entity) const
	{
		static_assert(TIsDerivedFrom<SharedFragmentType, FMassSharedFragment>::IsDerived
			, "Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types.");
		const FSharedStruct* FragmentPtr = InternalGetSharedFragmentPtr(Entity, SharedFragmentType::StaticStruct());
		return (SharedFragmentType*)(FragmentPtr ? FragmentPtr->GetMemory() : nullptr);
	}

	template<typename SharedFragmentType>
	SharedFragmentType& GetSharedFragmentDataChecked(FMassEntityHandle Entity) const
	{
		SharedFragmentType* TypePtr = GetSharedFragmentDataPtr<SharedFragmentType>(Entity);
		check(TypePtr);
		return *TypePtr;
	}

	FConstStructView GetSharedFragmentDataStruct(FMassEntityHandle Entity, const UScriptStruct* SharedFragmentType) const
	{
		checkf((SharedFragmentType != nullptr) && SharedFragmentType->IsChildOf(FMassSharedFragment::StaticStruct())
			, TEXT("GetSharedFragmentDataStruct called with an invalid fragment type '%s'"), *GetPathNameSafe(SharedFragmentType));
		const FSharedStruct* FragmentPtr = InternalGetSharedFragmentPtr(Entity, SharedFragmentType);
		return FragmentPtr
			? FConstStructView(*FragmentPtr)
			: FConstStructView();
	}

	uint32 GetArchetypeDataVersion() const { return ArchetypeDataVersion; }

	/**
	 * Creates and initializes a FMassExecutionContext instance.
	 */
	FMassExecutionContext CreateExecutionContext(const float DeltaSeconds);

	FScopedProcessing NewProcessingScope() { return FScopedProcessing(ProcessingScopeCount); }

	/** 
	 * Indicates whether there are processors out there performing operations on this instance of MassEntityManager. 
	 * Used to ensure that mutating operations (like entity destruction) are not performed while processors are running, 
	 * which rely on the assumption that the data layout doesn't change during calculations. 
	 */
	bool IsProcessing() const { return ProcessingScopeCount > 0; }

	FMassCommandBuffer& Defer() const { return *DeferredCommandBuffers[OpenedCommandBufferIndex].Get(); }
	/** 
	 * @param InCommandBuffer if not set then the default command buffer will be flushed. If set and there's already 
	 *		a command buffer being flushed (be it the main one or a previously requested one) then this command buffer 
	 *		will be queue itself.
	 */
	void FlushCommands(TSharedPtr<FMassCommandBuffer>& InCommandBuffer);

	void FlushCommands();

	/** 
	 * Depending on the current state of Manager's command buffer the function will either move all the commands out of 
	 * InOutCommandBuffer into the main command buffer or append it to the list of command buffers waiting to be flushed.
	 * @note as a consequence of the call InOutCommandBuffer can get its contents emptied due some of the underlying code using Move semantics
	 */
	void AppendCommands(TSharedPtr<FMassCommandBuffer>& InOutCommandBuffer);

	template<typename T>
	UE_DEPRECATED(5.5, "This method will no longer be exposed. Use GetOrCreateConstSharedFragment instead.")
	const FConstSharedStruct& GetOrCreateConstSharedFragmentByHash(const uint32 Hash, const T& Fragment)
	{
		static_assert(TIsDerivedFrom<T, FMassConstSharedFragment>::IsDerived, "Given struct doesn't represent a valid const shared fragment type. Make sure to inherit from FMassConstSharedFragment or one of its child-types.");
		int32& Index = ConstSharedFragmentsMap.FindOrAddByHash(Hash, Hash, INDEX_NONE);
		if (Index == INDEX_NONE)
		{
			Index = ConstSharedFragments.Add(FConstSharedStruct::Make(Fragment));
		}
		return ConstSharedFragments[Index];
	}

private:
	template<typename T>
	const FSharedStruct& GetOrCreateSharedFragmentByHash(const uint32 Hash, const T& Fragment)
	{
		static_assert(TIsDerivedFrom<T, FMassSharedFragment>::IsDerived, "Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types.");
		int32& Index = SharedFragmentsMap.FindOrAddByHash(Hash, Hash, INDEX_NONE);
		if (Index == INDEX_NONE)
		{
			Index = SharedFragments.Add(FSharedStruct::Make(Fragment));
			// note that even though we're copying the freshly created FSharedStruct instance it's perfectly fine since 
			// FSharedStruct do guarantee there's not going to be data duplication (via a member shared pointer to hosted data)
			TArray<FSharedStruct>& InstancesOfType = SharedFragmentsTypeMap.FindOrAdd(T::StaticStruct(), {});
			InstancesOfType.Add(SharedFragments[Index]);
		}

		return SharedFragments[Index];
	}

	const FConstSharedStruct& GetOrCreateConstSharedFragmentByHash(const uint32 Hash, const UScriptStruct* InScriptStruct, const uint8* InStructMemory)
	{
		int32& Index = ConstSharedFragmentsMap.FindOrAddByHash(Hash, Hash, INDEX_NONE);
		if (Index == INDEX_NONE)
		{
			Index = ConstSharedFragments.Add(FConstSharedStruct::Make(InScriptStruct, InStructMemory));
		}
		return ConstSharedFragments[Index];
	}

	const FSharedStruct& GetOrCreateSharedFragmentByHash(const uint32 Hash, const UScriptStruct* InScriptStruct, const uint8* InStructMemory)
	{
		int32& Index = SharedFragmentsMap.FindOrAddByHash(Hash, Hash, INDEX_NONE);
		if (Index == INDEX_NONE)
		{
			Index = SharedFragments.Add(FSharedStruct::Make(InScriptStruct, InStructMemory));
			// note that even though we're copying the freshly created FSharedStruct instance it's perfectly fine since 
			// FSharedStruct do guarantee there's not going to be data duplication (via a member shared pointer to hosted data)
			TArray<FSharedStruct>& InstancesOfType = SharedFragmentsTypeMap.FindOrAdd(InScriptStruct, {});
			InstancesOfType.Add(SharedFragments[Index]);
		}
		return SharedFragments[Index];
	}

	template<typename T, typename... TArgs>
	const FConstSharedStruct& GetOrCreateConstSharedFragmentByHash(const uint32 Hash, TArgs&&... InArgs)
	{
		static_assert(TIsDerivedFrom<T, FMassConstSharedFragment>::IsDerived, "Given struct doesn't represent a valid const shared fragment type. Make sure to inherit from FMassConstSharedFragment or one of its child-types.");
		int32& Index = ConstSharedFragmentsMap.FindOrAddByHash(Hash, Hash, INDEX_NONE);
		if (Index == INDEX_NONE)
		{
			Index = ConstSharedFragments.Add(FConstSharedStruct::Make<T>(Forward<TArgs>(InArgs)...));
		}

		return ConstSharedFragments[Index];
	}

public:

	template<typename T, typename... TArgs>
	UE_DEPRECATED(5.5, "This method will no longer be exposed. Use GetOrCreateSharedFragment instead.")
	const FSharedStruct& GetOrCreateSharedFragmentByHash(const uint32 Hash, TArgs&&... InArgs)
	{
		static_assert(TIsDerivedFrom<T, FMassSharedFragment>::IsDerived, "Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types.");
		int32& Index = SharedFragmentsMap.FindOrAddByHash(Hash, Hash, INDEX_NONE);
		if (Index == INDEX_NONE)
		{
			Index = SharedFragments.Add(FSharedStruct::Make<T>(Forward<TArgs>(InArgs)...));
			// note that even though we're copying the freshly created FSharedStruct instance it's perfectly fine since 
			// FSharedStruct do guarantee there's not going to be data duplication (via a member shared pointer to hosted data)
			TArray<FSharedStruct>& InstancesOfType = SharedFragmentsTypeMap.FindOrAdd(T::StaticStruct(), {});
			InstancesOfType.Add(SharedFragments[Index]);
		}

		return SharedFragments[Index];
	}

	/**
	 * Returns or creates a shared struct associated to a given shared fragment set of values
	 * identified internally by a CRC.
	 * Use this overload when an instance of the desired const shared fragment type is available and
	 * that can be used directly to compute a CRC (i.e., UE::StructUtils::GetStructCrc32)
	 *	e.g.,
	 *	USTRUCT()
	 *	struct FIntConstSharedFragment : public FMassConstSharedFragment
	 *	{
	 *		GENERATED_BODY()
	 *
	 *		UPROPERTY()
	 *		int32 Value = 0;
	 *	};
	 *
	 *	FIntConstSharedFragment Fragment;
	 *	Fragment.Value = 123;
	 *	const FConstSharedStruct SharedStruct = EntityManager.GetOrCreateConstSharedFragment(Fragment);
	 *
	 * @params Fragment Instance of the desired fragment type
	 * @return FConstSharedStruct to the matching, or newly created shared fragment
	 */
	template<typename T>
	const FConstSharedStruct& GetOrCreateConstSharedFragment(const T& Fragment)
	{
		static_assert(TIsDerivedFrom<T, FMassConstSharedFragment>::IsDerived,
			"Given struct doesn't represent a valid const shared fragment type. Make sure to inherit from FMassConstSharedFragment or one of its child-types.");
		const uint32 Hash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(Fragment));
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return GetOrCreateConstSharedFragmentByHash(Hash, Fragment);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	/**
	 * Returns or creates a shared struct associated to a given shared fragment set of values
	 * identified internally by a CRC.
	 * Use this overload when an instance of the desired shared fragment type is available and
	 * that can be used directly to compute a CRC (i.e., UE::StructUtils::GetStructCrc32)
	 *	e.g.,
	 *	USTRUCT()
	 *	struct FIntSharedFragment : public FMassSharedFragment
	 *	{
	 *		GENERATED_BODY()
	 *
	 *		UPROPERTY()
	 *		int32 Value = 0;
	 *	};
	 *
	 *	FIntSharedFragment Fragment;
	 *	Fragment.Value = 123;
	 *	const FSharedStruct SharedStruct = EntityManager.GetOrCreateSharedFragment(Fragment);
	 *
	 * @params Fragment Instance of the desired fragment type
	 * @return FSharedStruct to the matching, or newly created shared fragment
	 */
	template<typename T>
	const FSharedStruct& GetOrCreateSharedFragment(const T& Fragment)
	{
		static_assert(TIsDerivedFrom<T, FMassSharedFragment>::IsDerived,
			"Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types.");
		const uint32 Hash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(Fragment));
		return GetOrCreateSharedFragmentByHash(Hash, Fragment);
	}

	/**
	 * Returns or creates a shared struct associated to a given shared fragment set of values
	 * identified internally by a CRC.
	 * Use this overload when values can be provided as constructor arguments for the desired const shared fragment type and
	 * that can be used directly to compute a CRC (i.e., UE::StructUtils::GetStructCrc32)
 	 *	e.g.,
	 *	USTRUCT()
	 *	struct FIntConstSharedFragment : public FMassConstSharedFragment
	 *	{
	 *		GENERATED_BODY()
	 *
	 *		FIntConstSharedFragment(const int32 InValue) : Value(InValue) {}
	 *
	 *		UPROPERTY()
	 *		int32 Value = 0;
	 *	};
	 *
	 *	const FConstSharedStruct SharedStruct = EntityManager.GetOrCreateConstSharedFragment<FIntConstSharedFragment>(123);
	 *
	 * @params InArgs List of arguments provided to the constructor of the desired fragment type
	 * @return FConstSharedStruct to the matching, or newly created shared fragment
	 */
	template<typename T, typename... TArgs>
	const FConstSharedStruct& GetOrCreateConstSharedFragment(TArgs&&... InArgs)
	{
		static_assert(TIsDerivedFrom<T, FMassConstSharedFragment>::IsDerived,
			"Given struct doesn't represent a valid const shared fragment type. Make sure to inherit from FMassConstSharedFragment or one of its child-types.");
		T Struct(Forward<TArgs>(InArgs)...);
		const uint32 Hash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(Struct));
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return GetOrCreateConstSharedFragmentByHash(Hash, MoveTemp(Struct));
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	/**
	 * Returns or creates a shared struct associated to a given shared fragment set of values
	 * identified internally by a CRC.
	 * Use this overload when values can be provided as constructor arguments for the desired shared fragment type and
	 * that can be used directly to compute a CRC (i.e., UE::StructUtils::GetStructCrc32)
 	 *	e.g.,
	 *	USTRUCT()
	 *	struct FIntSharedFragment : public FMassSharedFragment
	 *	{
	 *		GENERATED_BODY()
	 *
	 *		FIntSharedFragment(const int32 InValue) : Value(InValue) {}
	 *
	 *		UPROPERTY()
	 *		int32 Value = 0;
	 *	};
	 *
	 *	const FSharedStruct SharedStruct = EntityManager.GetOrCreateSharedFragment<FIntSharedFragment>(123);
	 *
	 * @params InArgs List of arguments provided to the constructor of the desired fragment type
	 * @return FSharedStruct to the matching, or newly created shared fragment
	 */
	template<typename T, typename... TArgs>
	const FSharedStruct& GetOrCreateSharedFragment(TArgs&&... InArgs)
	{
		static_assert(TIsDerivedFrom<T, FMassSharedFragment>::IsDerived,
			"Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types.");
		T Struct(Forward<TArgs>(InArgs)...);
		const uint32 Hash = UE::StructUtils::GetStructCrc32(FConstStructView::Make(Struct));
		return GetOrCreateSharedFragmentByHash(Hash, MoveTemp(Struct));
	}

	/**
	 * Returns or creates a shared struct associated to a given shared fragment set of values
	 * identified internally by a CRC.
	 * Use this overload when the reflection data and the memory of an instance of the desired const shared fragment type
	 * is available and that can be used directly to compute a CRC (i.e., UE::StructUtils::GetStructCrc32)
	 * e.g.,
	 * FSharedStruct SharedStruct = EntityManager.GetOrCreateConstSharedFragment(*StructView.GetScriptStruct(), StructView.GetMemory());
	 *
	 * @params InScriptStruct Reflection data structure associated to the desired fragment type
	 * @params InStructMemory Actual data of the desired fragment type 
	 * @return FConstSharedStruct to the matching, or newly created shared fragment
	 */
	const FConstSharedStruct& GetOrCreateConstSharedFragment(const UScriptStruct& InScriptStruct, const uint8* InStructMemory)
	{
		checkf(InScriptStruct.IsChildOf(TBaseStructure<FMassConstSharedFragment>::Get()),
			TEXT("Given struct doesn't represent a valid const shared fragment type. Make sure to inherit from FMassConstSharedFragment or one of its child-types."));
		const uint32 Hash = UE::StructUtils::GetStructCrc32(InScriptStruct, InStructMemory);
		return GetOrCreateConstSharedFragmentByHash(Hash, &InScriptStruct, InStructMemory);
	}

	/**
	 * Returns or creates a shared struct associated to a given shared fragment set of values
	 * identified internally by a CRC.
	 * Use this overload when the reflection data and the memory of an instance of the desired shared fragment type
	 * is available and that can be used directly to compute a CRC (i.e., UE::StructUtils::GetStructCrc32)
	 * e.g.,
	 * FSharedStruct SharedStruct = EntityManager.GetOrCreateSharedFragment(*StructView.GetScriptStruct(), StructView.GetMemory());
	 *
	 * @params InScriptStruct Reflection data structure associated to the desired fragment type
	 * @params InStructMemory Actual data of the desired fragment type 
	 * @return FSharedStruct to the matching, or newly created shared fragment
	 */
	const FSharedStruct& GetOrCreateSharedFragment(const UScriptStruct& InScriptStruct, const uint8* InStructMemory)
	{
		checkf(InScriptStruct.IsChildOf(TBaseStructure<FMassSharedFragment>::Get()),
			TEXT("Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types."));
		const uint32 Hash = UE::StructUtils::GetStructCrc32(InScriptStruct, InStructMemory);
		return GetOrCreateSharedFragmentByHash(Hash, &InScriptStruct, InStructMemory);
	}

	/**
	 * Returns or creates a shared struct associated to a given shared fragment set of values
	 * identified internally by a CRC.
	 * Use this overload when a different struct should be used to compute a CRC (i.e., UE::StructUtils::GetStructCrc32)
	 * and values can be provided as constructor arguments for the desired const shared fragment type
	 *	e.g.,
	 *
	 *	USTRUCT()
	 *	struct FIntConstSharedFragmentParams
	 *	{
	 *		GENERATED_BODY()
	 *
	 *		FIntConstSharedFragmentParams(const int32 InValue) : Value(InValue) {}
	 *
	 *		UPROPERTY()
	 *		int32 Value = 0;
	 *	};
	 *
	 *	USTRUCT()
	 *	struct FIntConstSharedFragment : public FMassConstSharedFragment
	 *	{
	 *		GENERATED_BODY()
	 *
	 *		FIntConstSharedFragment(const FIntConstSharedFragmentParams& InParams) : Value(InParams.Value) {}
	 *
	 *		int32 Value = 0;
	 *	};
	 *
	 *	FIntConstSharedFragmentParams Params(123);
	 *	const FConstSharedStruct SharedStruct = EntityManager.GetOrCreateConstSharedFragment<FIntConstSharedFragment>(FConstStructView::Make(Params), Params);
	 *
	 * @params HashingHelperStruct Struct view passed to UE::StructUtils::GetStructCrc32 to compute the CRC
	 * @params InArgs List of arguments provided to the constructor of the desired fragment type
	 * @return FConstSharedStruct to the matching, or newly created shared fragment
	 */
	template<typename T, typename... TArgs>
	const FConstSharedStruct& GetOrCreateConstSharedFragment(const FConstStructView HashingHelperStruct, TArgs&&... InArgs)
	{
		static_assert(TIsDerivedFrom<T, FMassConstSharedFragment>::IsDerived,
			"Given struct doesn't represent a valid const shared fragment type. Make sure to inherit from FMassConstSharedFragment or one of its child-types.");
		T Fragment(Forward<TArgs>(InArgs)...);
		const uint32 Hash = UE::StructUtils::GetStructCrc32(HashingHelperStruct);
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return GetOrCreateConstSharedFragmentByHash(Hash, MoveTemp(Fragment));
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	/**
	 * Returns or creates a shared struct associated to a given shared fragment set of values
	 * identified internally by a CRC.
	 * Use this overload when a different struct should be used to compute a CRC (i.e., UE::StructUtils::GetStructCrc32)
	 * and values can be provided as constructor arguments for the desired shared fragment type
	 *	e.g.,
	 *
	 *	USTRUCT()
	 *	struct FIntSharedFragmentParams
	 *	{
	 *		GENERATED_BODY()
	 *
	 *		FInSharedFragmentParams(const int32 InValue) : Value(InValue) {}
	 *
	 *		UPROPERTY()
	 *		int32 Value = 0;
	 *	};
	 *
	 *	USTRUCT()
	 *	struct FIntSharedFragment : public FMassSharedFragment
	 *	{
	 *		GENERATED_BODY()
	 *
	 *		FIntSharedFragment(const FIntConstSharedFragmentParams& InParams) : Value(InParams.Value) {}
	 *
	 *		int32 Value = 0;
	 *	};
	 *
	 *	FIntSharedFragmentParams Params(123);
	 *	const FSharedStruct SharedStruct = EntityManager.GetOrCreateSharedFragment<FIntSharedFragment>(FConstStructView::Make(Params), Params);
	 *
	 * @params HashingHelperStruct Struct view passed to UE::StructUtils::GetStructCrc32 to compute the CRC
	 * @params InArgs List of arguments provided to the constructor of the desired fragment type
	 * @return FSharedStruct to the matching, or newly created shared fragment
	 */
	template<typename T, typename... TArgs>
	const FSharedStruct& GetOrCreateSharedFragment(const FConstStructView HashingHelperStruct, TArgs&&... InArgs)
	{
		static_assert(TIsDerivedFrom<T, FMassSharedFragment>::IsDerived,
			"Given struct doesn't represent a valid shared fragment type. Make sure to inherit from FMassSharedFragment or one of its child-types.");
		T Fragment(Forward<TArgs>(InArgs)...);
		const uint32 Hash = UE::StructUtils::GetStructCrc32(HashingHelperStruct);
		return GetOrCreateSharedFragmentByHash(Hash, MoveTemp(Fragment));
	}

	template<typename T>
	void ForEachSharedFragment(TFunctionRef< void(T& /*SharedFragment*/) > ExecuteFunction)
	{
		if (TArray<FSharedStruct>* InstancesOfType = SharedFragmentsTypeMap.Find(T::StaticStruct()))
		{
			for (const FSharedStruct& SharedStruct : *InstancesOfType)
			{
				ExecuteFunction(SharedStruct.Get<T>());
			}
		}
	}

	template<typename T>
	void ForEachSharedFragmentConditional(TFunctionRef< bool(T& /*SharedFragment*/) > ConditionFunction, TFunctionRef< void(T& /*SharedFragment*/) > ExecuteFunction)
	{
		if (TArray<FSharedStruct>* InstancesOfType = SharedFragmentsTypeMap.Find(T::StaticStruct()))
		{
			for (const FSharedStruct& SharedStruct : *InstancesOfType)
			{
				T& StructInstanceRef = SharedStruct.Get<T>();
				if (ConditionFunction(StructInstanceRef))
				{
					ExecuteFunction(StructInstanceRef);
				}
			}
		}
	}

	FMassObserverManager& GetObserverManager() { return ObserverManager; }

	FOnNewArchetypeDelegate& GetOnNewArchetypeEvent() { return OnNewArchetypeEvent; }
	/** 
	 * Fetches the world associated with the Owner. 
	 * @note that it's ok for a given EntityManager to not have an owner or the owner not being part of a UWorld, depending on the use case
	 */
	UWorld* GetWorld() const { return Owner.IsValid() ? Owner->GetWorld() : nullptr; }
	UObject* GetOwner() const { return Owner.Get(); }

	void SetDebugName(const FString& NewDebugGame);
#if WITH_MASSENTITY_DEBUG
	void DebugPrintArchetypes(FOutputDevice& Ar, const bool bIncludeEmpty = true) const;
	void DebugGetArchetypesStringDetails(FOutputDevice& Ar, const bool bIncludeEmpty = true) const;
	void DebugGetArchetypeFragmentTypes(const FMassArchetypeHandle& Archetype, TArray<const UScriptStruct*>& InOutFragmentList) const;
	int32 DebugGetArchetypeEntitiesCount(const FMassArchetypeHandle& Archetype) const;
	int32 DebugGetArchetypeEntitiesCountPerChunk(const FMassArchetypeHandle& Archetype) const;
	int32 DebugGetEntityCount() const;
	int32 DebugGetArchetypesCount() const;
	void DebugRemoveAllEntities();
	void DebugForceArchetypeDataVersionBump();
	void DebugGetArchetypeStrings(const FMassArchetypeHandle& Archetype, TArray<FName>& OutFragmentNames, TArray<FName>& OutTagNames);
	FMassEntityHandle DebugGetEntityIndexHandle(const int32 EntityIndex) const;
	const FString& DebugGetName() const;

	FMassRequirementAccessDetector& GetRequirementAccessDetector();

	// For use by the friend MassDebugger
	UE::Mass::IEntityStorageInterface& DebugGetEntityStorageInterface();
	// For use by the friend MassDebugger
	const UE::Mass::IEntityStorageInterface& DebugGetEntityStorageInterface() const;
#endif // WITH_MASSENTITY_DEBUG

protected:
	/** Called on the child process upon process's forking */
	void OnPostFork(EForkProcessRole Role);

	void GetMatchingArchetypes(const FMassFragmentRequirements& Requirements, TArray<FMassArchetypeHandle>& OutValidArchetypes, const uint32 FromArchetypeDataVersion) const;
	
	/** 
	 * A "similar" archetype is an archetype exactly the same as SourceArchetype except for one composition aspect 
	 * like Fragments or "Tags" 
	 */
	FMassArchetypeHandle InternalCreateSimilarArchetype(const TSharedPtr<FMassArchetypeData>& SourceArchetype, const FMassTagBitSet& OverrideTags);
	FMassArchetypeHandle InternalCreateSimilarArchetype(const TSharedPtr<FMassArchetypeData>& SourceArchetype, const FMassFragmentBitSet& OverrideFragments);

	FMassArchetypeHandle InternalCreateSimilarArchetype(const FMassArchetypeData& SourceArchetypeRef, FMassArchetypeCompositionDescriptor&& NewComposition);

	void InternalAppendFragmentsAndTagsToArchetypeCompositionDescriptor(FMassArchetypeCompositionDescriptor& InOutComposition,
		TConstArrayView<const UScriptStruct*> FragmentsAndTagsList) const;

private:
	void InternalBuildEntity(FMassEntityHandle Entity, const FMassArchetypeHandle& ArchetypeHandle, const FMassArchetypeSharedFragmentValues& SharedFragmentValues);
	void InternalReleaseEntity(FMassEntityHandle Entity);

	/** 
	 *  Adds fragments in FragmentList to Entity. Only the unique fragments will be added.
	 *  @return Bitset for the added fragments (might be empty or a subset of `InFragments` depending on the current archetype fragments)
	 */
	FMassFragmentBitSet InternalAddFragmentListToEntityChecked(FMassEntityHandle Entity, const FMassFragmentBitSet& InFragments);

	/** 
	 *  Similar to InternalAddFragmentListToEntity but expects NewFragmentList not overlapping with current entity's
	 *  fragment list. It's callers responsibility to ensure that's true. Failing this will cause a `check` fail.
	 */
	void InternalAddFragmentListToEntity(FMassEntityHandle Entity, const FMassFragmentBitSet& InFragments);
	/** Note that it's the caller's responsibility to ensure `FragmentType` is a kind of FMassFragment */
	void* InternalGetFragmentDataChecked(FMassEntityHandle Entity, const UScriptStruct* FragmentType) const;
	/** Note that it's the caller's responsibility to ensure `FragmentType` is a kind of FMassFragment */
	void* InternalGetFragmentDataPtr(FMassEntityHandle Entity, const UScriptStruct* FragmentType) const;
	/** Note that it's the caller's responsibility to ensure `ConstSharedFragmentType` is a kind of FMassSharedFragment */
	const FConstSharedStruct* InternalGetConstSharedFragmentPtr(FMassEntityHandle Entity, const UScriptStruct* ConstSharedFragmentType) const;
	/** Note that it's the caller's responsibility to ensure `SharedFragmentType` is a kind of FMassSharedFragment */
	const FSharedStruct* InternalGetSharedFragmentPtr(FMassEntityHandle Entity, const UScriptStruct* SharedFragmentType) const;

	TSharedRef<FEntityCreationContext> InternalBatchCreateReservedEntities(const FMassArchetypeHandle& ArchetypeHandle,
		const FMassArchetypeSharedFragmentValues& SharedFragmentValues, TConstArrayView<FMassEntityHandle> ReservedEntities);
	
#if MASS_CONCURRENT_RESERVE
	UE::Mass::IEntityStorageInterface& GetEntityStorageInterface();
	const UE::Mass::IEntityStorageInterface& GetEntityStorageInterface() const;
#else
	UE::Mass::FSingleThreadedEntityStorage& GetEntityStorageInterface();
	const UE::Mass::FSingleThreadedEntityStorage& GetEntityStorageInterface() const;
#endif

	/**
	 * If ActiveCreationContext is not valid the function creates a new shared FEntityCreationContext instance and returns that.
	 * Otherwise ActiveCreationContext will get extended with ReservedEntities and EntityCollection, and returned by the function.
	 */
	TSharedRef<FEntityCreationContext> GetOrMakeCreationContext(TConstArrayView<FMassEntityHandle> ReservedEntities, FMassArchetypeEntityCollection&& EntityCollection);
	bool IsDuringEntityCreation() const { return ActiveCreationContext.IsValid(); }
	
	/** 
	 * This type is used in entity mutating batched API to ensure the active FEntityCreationContext gets dirtied 
	 * upon function's end (since the mutating operations render FEntityCreationContext.EntityCollections invalid).
	 * It also serves as a cached IsDuringEntityCreation value.
	 */
	struct FScopedCreationContextOperations
	{
		FScopedCreationContextOperations(FMassEntityManager& InManager)
			: bIsDuringEntityCreation(InManager.IsDuringEntityCreation())
			, Manager(InManager)
		{}
		~FScopedCreationContextOperations()
		{
			if (bIsDuringEntityCreation)
			{
				Manager.DirtyCreationContext();
			}
			// else, there's nothing to do, there's no creation context to call functions for
		}

		bool IsAllowedToTriggerObservers() const { return (bIsDuringEntityCreation == false); }

	private:
		const bool bIsDuringEntityCreation = false;
		FMassEntityManager& Manager;
	};
	friend FScopedCreationContextOperations;

	/** @return whether actual context dirtying took place which is equivalent to IsDuringEntityCreation */
	bool DirtyCreationContext();

	/** 
	 * @return whether it's allowed for observers to get triggered. If not then the active creation context will be dirtied
	 *	to cause observers triggering upon context's destruction
	 */
	bool IsAllowedToTriggerObservers() { return DirtyCreationContext() == false; }
	bool DebugDoCollectionsOverlapCreationContext(TConstArrayView<FMassArchetypeEntityCollection> EntityCollections) const;
		
private:

	friend struct UE::Mass::Private::FEntityStorageInitializer;
	using FEntityStorageContainerType = TVariant<
		FEmptyVariantState,
		UE::Mass::FSingleThreadedEntityStorage,
		UE::Mass::FConcurrentEntityStorage>;
	FEntityStorageContainerType EntityStorage;

	/** Never access directly, use GetOrMakeCreationContext instead. */
	TWeakPtr<FEntityCreationContext> ActiveCreationContext;

	std::atomic<bool> bCommandBufferFlushingInProgress = false;

	/**
	 * This index will be enough to control which buffer is available for pushing commands since flashing is taking place 
	 * in the game thread and pushing commands to the buffer fetched by Defer() is only supported also on the game thread
	 * (due to checking the cached thread ID).
	 * The whole CL aims to support non-mass code trying to push commands while the flushing is going on (as triggered
	 * by MassObservers reacting to the commands being flushed currently).
	 */
	uint8 OpenedCommandBufferIndex = 0;
	TStaticArray<TSharedPtr<FMassCommandBuffer>, 2> DeferredCommandBuffers;
	
	std::atomic<int32> ProcessingScopeCount = 0;

	// the "version" number increased every time an archetype gets added
	uint32 ArchetypeDataVersion = 0;

	// Map of hash of sorted fragment list to archetypes with that hash
	TMap<uint32, TArray<TSharedPtr<FMassArchetypeData>>> FragmentHashToArchetypeMap;

	// Map to list of archetypes that contain the specified fragment type
	TMap<const UScriptStruct*, TArray<TSharedPtr<FMassArchetypeData>>> FragmentTypeToArchetypeMap;

	// Contains all archetypes ever created. The array always growing and a given archetypes remains at a given index 
	// throughout its lifetime, and the index is never reused for another archetype. 
	TArray<TSharedPtr<FMassArchetypeData>> AllArchetypes;

	// Shared fragments
	TArray<FConstSharedStruct> ConstSharedFragments;
	// Hash/Index in array pair
	TMap<uint32, int32> ConstSharedFragmentsMap;

	TArray<FSharedStruct> SharedFragments;
	// Hash/Index in array pair, indices point at SharedFragments
	TMap<uint32, int32> SharedFragmentsMap;
	// Maps specific struct type to a collection of FSharedStruct instances of that type
	TMap<const UScriptStruct*, TArray<FSharedStruct>> SharedFragmentsTypeMap;

	FMassObserverManager ObserverManager;

#if WITH_MASSENTITY_DEBUG
	FMassRequirementAccessDetector RequirementAccessDetector;
	FString DebugName;
#endif // WITH_MASSENTITY_DEBUG

	TWeakObjectPtr<UObject> Owner;

	FOnNewArchetypeDelegate OnNewArchetypeEvent;

	bool bInitialized = false;
	bool bFirstCommandFlush = true;

	FDelegateHandle OnPostForkHandle;
};
