// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Collection.h"
#include "CollectionManagerTypes.h"
#include "ICollectionManager.h"
#include "Misc/Guid.h"
#include "Templates/PimplPtr.h"

class ITextFilterExpressionContext;
namespace DirectoryWatcher{ class FFileCache; }
class FCollectionManagerCache;

// Objects wrapping locks to read, write, or begin-reading-then-write (for cache updates) internal state. 
// Used as internal function parameters to show what lock type must be held to perform the operation and prevent 
// recursive lock acquisition
// Functions taking Lock/Lock_Read need to be able to read data but not update caches
class FCollectionLock;
class FCollectionLock_Read;
// Functions taking Lock_RW may need to promote the lock to a write state to update caches
class FCollectionLock_RW;
// Functions taking Lock_Write have exclusive access and can update collections as well as update caches
class FCollectionLock_Write;

/** Collection info for a given object - gives the collection name, as well as the reason this object is considered to be part of this collection */
struct FObjectCollectionInfo
{
	explicit FObjectCollectionInfo(const FCollectionNameType& InCollectionKey)
		: CollectionKey(InCollectionKey)
		, Reason(0)
	{
	}

	FObjectCollectionInfo(const FCollectionNameType& InCollectionKey, const ECollectionRecursionFlags::Flags InReason)
		: CollectionKey(InCollectionKey)
		, Reason(InReason)
	{
	}

	/** The key identifying the collection that contains this object */
	FCollectionNameType CollectionKey;
	/** The reason(s) why this collection contains this object - this can be tested against the recursion mode when getting the collections for an object */
	ECollectionRecursionFlags::Flags Reason;
};

enum class ECollectionCacheFlags
{
	None = 0,
	Names = 1<<0,
	Objects = 1<<1,
	Hierarchy = 1<<2,
	Colors = 1 <<3,

	// Necessary cache updates for calling collection recursion worker
	RecursionWorker = Names | Hierarchy,
	All = Names | Objects | Hierarchy | Colors,
};
ENUM_CLASS_FLAGS(ECollectionCacheFlags);


UE_DEPRECATED(5.5, "These typedefs have been deprecated. Replace them with their concrete types.")
typedef TMap<FCollectionNameType, TSharedRef<FCollection>> FAvailableCollectionsMap;
UE_DEPRECATED(5.5, "These typedefs have been deprecated. Replace them with their concrete types.")
typedef TMap<FGuid, FCollectionNameType> FGuidToCollectionNamesMap;
UE_DEPRECATED(5.5, "These typedefs have been deprecated. Replace them with their concrete types.")
typedef TMap<FSoftObjectPath, TArray<FObjectCollectionInfo>> FCollectionObjectsMap;
UE_DEPRECATED(5.5, "These typedefs have been deprecated. Replace them with their concrete types.")
typedef TMap<FGuid, TArray<FGuid>> FCollectionHierarchyMap;
UE_DEPRECATED(5.5, "These typedefs have been deprecated. Replace them with their concrete types.")
typedef TArray<FLinearColor> FCollectionColorArray;

class FCollectionManager : public ICollectionManager
{
public:
	FCollectionManager();
	virtual ~FCollectionManager();

	// ICollectionManager implementation
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	virtual bool GetAssetsInCollection(FName CollectionName, ECollectionShareType::Type ShareType, TArray<FName>& AssetPaths, ECollectionRecursionFlags::Flags RecursionMode = ECollectionRecursionFlags::Self) const override
	{
		TArray<FSoftObjectPath> Temp;
		if (GetAssetsInCollection(CollectionName, ShareType, Temp, RecursionMode))
		{
			AssetPaths.Append(UE::SoftObjectPath::Private::ConvertSoftObjectPaths(Temp));
			return true;
		}
		return false;
	}
	virtual bool GetObjectsInCollection(FName CollectionName, ECollectionShareType::Type ShareType, TArray<FName>& ObjectPaths, ECollectionRecursionFlags::Flags RecursionMode = ECollectionRecursionFlags::Self) const override
	{
		TArray<FSoftObjectPath> Temp;
		if (GetObjectsInCollection(CollectionName, ShareType, Temp, RecursionMode))
		{
			ObjectPaths.Append(UE::SoftObjectPath::Private::ConvertSoftObjectPaths(Temp));
			return true;
		}
		return false;
	}
	virtual bool GetClassesInCollection(FName CollectionName, ECollectionShareType::Type ShareType, TArray<FName>& ClassPaths, ECollectionRecursionFlags::Flags RecursionMode = ECollectionRecursionFlags::Self) const override
	{
		TArray<FTopLevelAssetPath> Temp;
		if (GetClassesInCollection(CollectionName, ShareType, Temp, RecursionMode))
		{
			for (FTopLevelAssetPath Path : Temp)
			{
				ClassPaths.Add(Path.ToFName());
			}
		
			return true;
		}
		return false;
	}
	virtual void GetCollectionsContainingObject(FName ObjectPath, ECollectionShareType::Type ShareType, TArray<FName>& OutCollectionNames, ECollectionRecursionFlags::Flags RecursionMode = ECollectionRecursionFlags::Self) const override
	{
		GetCollectionsContainingObject(FSoftObjectPath(ObjectPath), ShareType, OutCollectionNames, RecursionMode);
	}
	virtual void GetCollectionsContainingObject(FName ObjectPath, TArray<FCollectionNameType>& OutCollections, ECollectionRecursionFlags::Flags RecursionMode = ECollectionRecursionFlags::Self) const override
	{
		GetCollectionsContainingObject(FSoftObjectPath(ObjectPath), OutCollections, RecursionMode);
	}
	virtual void GetCollectionsContainingObjects(const TArray<FName>& ObjectPathNames, TMap<FCollectionNameType, TArray<FName>>& OutCollectionsAndMatchedObjects, ECollectionRecursionFlags::Flags RecursionMode = ECollectionRecursionFlags::Self) const override
	{
		TArray<FSoftObjectPath> Paths = UE::SoftObjectPath::Private::ConvertObjectPathNames(ObjectPathNames);
		TMap<FCollectionNameType, TArray<FSoftObjectPath>> TmpMap;
		GetCollectionsContainingObjects(Paths, TmpMap, RecursionMode);
		for (const TPair<FCollectionNameType, TArray<FSoftObjectPath>>& Pair : TmpMap)
		{
			TArray<FName>& Names = OutCollectionsAndMatchedObjects.FindOrAdd(Pair.Key);
			Names.Append(UE::SoftObjectPath::Private::ConvertSoftObjectPaths(Pair.Value));
		}
	}
	virtual FString GetCollectionsStringForObject(FName ObjectPath, ECollectionShareType::Type ShareType, ECollectionRecursionFlags::Flags RecursionMode = ECollectionRecursionFlags::Self, bool bFullPaths = true) const override
	{
		return GetCollectionsStringForObject(FSoftObjectPath(ObjectPath), ShareType, RecursionMode, bFullPaths);
	}
	virtual bool AddToCollection(FName CollectionName, ECollectionShareType::Type ShareType, FName ObjectPath) override
	{
		return AddToCollection(CollectionName, ShareType, FSoftObjectPath(ObjectPath));
	}
	virtual bool AddToCollection(FName CollectionName, ECollectionShareType::Type ShareType, const TArray<FName>& ObjectPaths, int32* OutNumAdded = nullptr) override
	{
		return AddToCollection(CollectionName, ShareType, UE::SoftObjectPath::Private::ConvertObjectPathNames(ObjectPaths), OutNumAdded);
	}
	virtual bool RemoveFromCollection(FName CollectionName, ECollectionShareType::Type ShareType, FName ObjectPath) override
	{
		return RemoveFromCollection(CollectionName, ShareType, FSoftObjectPath(ObjectPath));
	}
	virtual bool RemoveFromCollection(FName CollectionName, ECollectionShareType::Type ShareType, const TArray<FName>& ObjectPaths, int32* OutNumRemoved = nullptr) override
	{
		return RemoveFromCollection(CollectionName, ShareType, UE::SoftObjectPath::Private::ConvertObjectPathNames(ObjectPaths), OutNumRemoved);
	}
	virtual bool IsObjectInCollection(FName ObjectPath, FName CollectionName, ECollectionShareType::Type ShareType, ECollectionRecursionFlags::Flags RecursionMode = ECollectionRecursionFlags::Self) const override
	{
		return IsObjectInCollection(FSoftObjectPath(ObjectPath), CollectionName, ShareType, RecursionMode);
	}
	virtual bool HandleRedirectorDeleted(const FName& ObjectPath) override
	{
		return HandleRedirectorDeleted(FSoftObjectPath(ObjectPath));
	}
	virtual void HandleObjectRenamed(const FName& OldObjectPath, const FName& NewObjectPath) override
	{
		return HandleObjectRenamed(FSoftObjectPath(OldObjectPath), FSoftObjectPath(NewObjectPath));
	}
	virtual void HandleObjectDeleted(const FName& ObjectPath) override
	{
		return HandleObjectDeleted(FSoftObjectPath(ObjectPath));
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	virtual bool HasCollections() const override;
	virtual void GetCollections(TArray<FCollectionNameType>& OutCollections) const override;
	virtual void GetCollections(FName CollectionName, TArray<FCollectionNameType>& OutCollections) const override;
	virtual void GetCollectionNames(ECollectionShareType::Type ShareType, TArray<FName>& CollectionNames) const override;
	virtual void GetRootCollections(TArray<FCollectionNameType>& OutCollections) const override;
	virtual void GetRootCollectionNames(ECollectionShareType::Type ShareType, TArray<FName>& CollectionNames) const override;
	virtual void GetChildCollections(FName CollectionName, ECollectionShareType::Type ShareType, TArray<FCollectionNameType>& OutCollections) const override;
	virtual void GetChildCollectionNames(FName CollectionName, ECollectionShareType::Type ShareType, ECollectionShareType::Type ChildShareType, TArray<FName>& CollectionNames) const override;
	virtual TOptional<FCollectionNameType> GetParentCollection(FName CollectionName, ECollectionShareType::Type ShareType) const override;
	virtual bool CollectionExists(FName CollectionName, ECollectionShareType::Type ShareType) const override;
	virtual bool GetAssetsInCollection(FName CollectionName, ECollectionShareType::Type ShareType, TArray<FSoftObjectPath>& AssetPaths, ECollectionRecursionFlags::Flags RecursionMode = ECollectionRecursionFlags::Self) const override;
	virtual bool GetObjectsInCollection(FName CollectionName, ECollectionShareType::Type ShareType, TArray<FSoftObjectPath>& ObjectPaths, ECollectionRecursionFlags::Flags RecursionMode = ECollectionRecursionFlags::Self) const override;
	virtual bool GetClassesInCollection(FName CollectionName, ECollectionShareType::Type ShareType, TArray<FTopLevelAssetPath>& ClassPaths, ECollectionRecursionFlags::Flags RecursionMode = ECollectionRecursionFlags::Self) const override;
	virtual void GetCollectionsContainingObject(const FSoftObjectPath& ObjectPath, ECollectionShareType::Type ShareType, TArray<FName>& OutCollectionNames, ECollectionRecursionFlags::Flags RecursionMode = ECollectionRecursionFlags::Self) const override;
	virtual void GetCollectionsContainingObject(const FSoftObjectPath& ObjectPath, TArray<FCollectionNameType>& OutCollections, ECollectionRecursionFlags::Flags RecursionMode = ECollectionRecursionFlags::Self) const override;
	virtual void GetCollectionsContainingObjects(const TArray<FSoftObjectPath>& ObjectPaths, TMap<FCollectionNameType, TArray<FSoftObjectPath>>& OutCollectionsAndMatchedObjects, ECollectionRecursionFlags::Flags RecursionMode = ECollectionRecursionFlags::Self) const override;
	virtual FString GetCollectionsStringForObject(const FSoftObjectPath& ObjectPath, ECollectionShareType::Type ShareType, ECollectionRecursionFlags::Flags RecursionMode = ECollectionRecursionFlags::Self, bool bFullPaths = true) const override;
	virtual void CreateUniqueCollectionName(const FName& BaseName, ECollectionShareType::Type ShareType, FName& OutCollectionName) const override;
	virtual bool IsValidCollectionName(const FString& CollectionName, ECollectionShareType::Type ShareType, FText* OutError = nullptr) const override;
	virtual bool CreateCollection(FName CollectionName, ECollectionShareType::Type ShareType, ECollectionStorageMode::Type StorageMode, FText* OutError = nullptr) override;
	virtual bool RenameCollection(FName CurrentCollectionName, ECollectionShareType::Type CurrentShareType, FName NewCollectionName, ECollectionShareType::Type NewShareType, FText* OutError = nullptr) override;
	virtual bool ReparentCollection(FName CollectionName, ECollectionShareType::Type ShareType, FName ParentCollectionName, ECollectionShareType::Type ParentShareType, FText* OutError = nullptr) override;
	virtual bool DestroyCollection(FName CollectionName, ECollectionShareType::Type ShareType, FText* OutError = nullptr) override;
	virtual bool AddToCollection(FName CollectionName, ECollectionShareType::Type ShareType, const FSoftObjectPath& ObjectPath, FText* OutError = nullptr) override;
	virtual bool AddToCollection(FName CollectionName, ECollectionShareType::Type ShareType, TConstArrayView<FSoftObjectPath> ObjectPaths, int32* OutNumAdded = nullptr, FText* OutError = nullptr) override;
	virtual bool RemoveFromCollection(FName CollectionName, ECollectionShareType::Type ShareType, const FSoftObjectPath& ObjectPath, FText* OutError = nullptr) override;
	virtual bool RemoveFromCollection(FName CollectionName, ECollectionShareType::Type ShareType, TConstArrayView<FSoftObjectPath> ObjectPaths, int32* OutNumRemoved = nullptr, FText* OutError = nullptr) override;
	virtual bool SetDynamicQueryText(FName CollectionName, ECollectionShareType::Type ShareType, const FString& InQueryText, FText* OutError = nullptr) override;
	virtual bool GetDynamicQueryText(FName CollectionName, ECollectionShareType::Type ShareType, FString& OutQueryText, FText* OutError = nullptr) const override;
	virtual bool TestDynamicQuery(FName CollectionName, ECollectionShareType::Type ShareType, const ITextFilterExpressionContext& InContext, bool& OutResult, FText* OutError = nullptr) const override;
	virtual bool EmptyCollection(FName CollectionName, ECollectionShareType::Type ShareType, FText* OutError = nullptr) override;
	virtual bool SaveCollection(FName CollectionName, ECollectionShareType::Type ShareType, FText* OutError = nullptr) override;
	virtual bool UpdateCollection(FName CollectionName, ECollectionShareType::Type ShareType, FText* OutError = nullptr) override;
	virtual bool GetCollectionStatusInfo(FName CollectionName, ECollectionShareType::Type ShareType, FCollectionStatusInfo& OutStatusInfo, FText* OutError = nullptr) const override;
	virtual bool HasCollectionColors(TArray<FLinearColor>* OutColors = nullptr) const override;
	virtual bool GetCollectionColor(FName CollectionName, ECollectionShareType::Type ShareType, TOptional<FLinearColor>& OutColor, FText* OutError = nullptr) const override;
	virtual bool SetCollectionColor(FName CollectionName, ECollectionShareType::Type ShareType, const TOptional<FLinearColor>& NewColor, FText* OutError = nullptr) override;
	virtual bool GetCollectionStorageMode(FName CollectionName, ECollectionShareType::Type ShareType, ECollectionStorageMode::Type& OutStorageMode, FText* OutError = nullptr) const override;
	virtual bool IsObjectInCollection(const FSoftObjectPath& ObjectPath, FName CollectionName, ECollectionShareType::Type ShareType, ECollectionRecursionFlags::Flags RecursionMode = ECollectionRecursionFlags::Self, FText* OutError = nullptr) const override;
	virtual bool IsValidParentCollection(FName CollectionName, ECollectionShareType::Type ShareType, FName ParentCollectionName, ECollectionShareType::Type ParentShareType, FText* OutError) const override;
	UE_DEPRECATED(5.5, "Deprecated for thread safety reasons.")
	virtual FText GetLastError() const override { return FText::GetEmpty(); }
	virtual void HandleFixupRedirectors(ICollectionRedirectorFollower& InRedirectorFollower) override;
	virtual bool HandleRedirectorDeleted(const FSoftObjectPath& ObjectPath, FText* OutError = nullptr) override;
	virtual bool HandleRedirectorsDeleted(TConstArrayView<FSoftObjectPath> ObjectPaths, FText* OutError = nullptr) override;
	virtual void HandleObjectRenamed(const FSoftObjectPath& OldObjectPath, const FSoftObjectPath& NewObjectPath) override;
	virtual void HandleObjectDeleted(const FSoftObjectPath& ObjectPath) override;
	virtual void HandleObjectsDeleted(TConstArrayView<FSoftObjectPath> ObjectPaths) override;

	virtual void SuppressObjectDeletionHandling() override;
	virtual void ResumeObjectDeletionHandling() override;

	/** Event for when collections are created */
	DECLARE_DERIVED_EVENT( FCollectionManager, ICollectionManager::FCollectionCreatedEvent, FCollectionCreatedEvent );
	virtual FCollectionCreatedEvent& OnCollectionCreated() override { return CollectionCreatedEvent; }

	/** Event for when collections are destroyed */
	DECLARE_DERIVED_EVENT( FCollectionManager, ICollectionManager::FCollectionDestroyedEvent, FCollectionDestroyedEvent );
	virtual FCollectionDestroyedEvent& OnCollectionDestroyed() override { return CollectionDestroyedEvent; }

	/** Event for when assets are added to a collection */
	virtual FOnAssetsAddedToCollection& OnAssetsAddedToCollection() override { return AssetsAddedToCollectionDelegate; }

	/** Event for when assets are removed from a collection */
	virtual FOnAssetsRemovedFromCollection& OnAssetsRemovedFromCollection() override { return AssetsRemovedFromCollectionDelegate; }

	/** Event for when collections are renamed */
	DECLARE_DERIVED_EVENT( FCollectionManager, ICollectionManager::FCollectionRenamedEvent, FCollectionRenamedEvent );
	virtual FCollectionRenamedEvent& OnCollectionRenamed() override { return CollectionRenamedEvent; }

	/** Event for when collections are re-parented */
	DECLARE_DERIVED_EVENT( FCollectionManager, ICollectionManager::FCollectionReparentedEvent, FCollectionReparentedEvent );
	virtual FCollectionReparentedEvent& OnCollectionReparented() override { return CollectionReparentedEvent; }

	/** Event for when collections is updated, or otherwise changed and we can't tell exactly how (eg, after updating from source control and merging) */
	DECLARE_DERIVED_EVENT( FCollectionManager, ICollectionManager::FCollectionUpdatedEvent, FCollectionUpdatedEvent );
	virtual FCollectionUpdatedEvent& OnCollectionUpdated() override { return CollectionUpdatedEvent; }

	/** Event for when collections is updated, or otherwise changed and we can't tell exactly how (eg, after updating from source control and merging) */
	DECLARE_DERIVED_EVENT( FCollectionManager, ICollectionManager::FAddToCollectionCheckinDescriptionEvent, FAddToCollectionCheckinDescriptionEvent);
	virtual FAddToCollectionCheckinDescriptionEvent& OnAddToCollectionCheckinDescriptionEvent() override { return AddToCollectionCheckinDescriptionEvent; }

private:
	/** Tick this collection manager so it can process any file cache events */
	bool TickFileCache(float InDeltaTime);

	/** Loads all collection files from disk. Must only be called from constructor as it does not lock for the full duration. */
	void LoadCollections();

	/** Returns true if the specified share type requires source control */
	bool ShouldUseSCC(ECollectionShareType::Type ShareType) const;

	/** Given a collection name and share type, work out the full filename for the collection to use on disk */
	FString GetCollectionFilename(const FName& InCollectionName, const ECollectionShareType::Type InCollectionShareType) const;

	/** Adds a collection to the lookup maps */
	bool AddCollection(FCollectionLock_Write& InGuard, const TSharedRef<FCollection>& CollectionRef, ECollectionShareType::Type ShareType);

	/** Removes a collection from the lookup maps */
	bool RemoveCollection(FCollectionLock_Write& InGuard, const TSharedRef<FCollection>& CollectionRef, ECollectionShareType::Type ShareType);

	/** Removes an object from any collections that contain it */
	void RemoveObjectFromCollections(FCollectionLock_Write& InGuard, const FSoftObjectPath& ObjectPath, TArray<FCollectionNameType>& OutUpdatedCollections);

	/** Replaces an object with another in any collections that contain it */
	void ReplaceObjectInCollections(
		FCollectionLock_Write& InGuard, const FSoftObjectPath& OldObjectPath, const FSoftObjectPath& NewObjectPath,
		TArray<FCollectionNameType>& OutUpdatedCollections);

	/** Internal common functionality for saving a collection
	 * bForceCommitToRevisionControl - If the collection's storage mode will save it to source control, then
	 * bForceCommitToRevisionControl will ensure that it is committed after save.  If this is false, then the collection
	 * will be left as a modified file which can be advantageous for slow source control servers.
	 */
	bool InternalSaveCollection(FCollectionLock_Write&, const TSharedRef<FCollection>& CollectionRef, FText* OutError, bool bForceCommitToRevisionControl);

	/* 
	 * Internal version of IsValidParentCollection to avoid taking lock recursively.
	 * Cache must be updated for recursion before calling.
	 */
	bool IsValidParentCollection_Locked(FCollectionLock& InGuard, FName CollectionName, ECollectionShareType::Type ShareType, FName ParentCollectionName, ECollectionShareType::Type ParentShareType, FText* OutError) const;

	/** 
	 * Check if the given collection exists.
	 * Using the public API function risks acquiring the lock recursively.
	 */
	bool CollectionExists_Locked(FCollectionLock& InGuard, FName CollectionName, ECollectionShareType::Type ShareType) const;

private:
	/** Required for updating caches as well as write operations to collections */
	mutable FRWLock Lock;

	/** The folders that contain collections */
	FString CollectionFolders[ECollectionShareType::CST_All];

	/** The extension used for collection files */
	static FStringView CollectionExtension;

	/** Array of file cache instances that are watching for the collection files changing on disk */
	TSharedPtr<DirectoryWatcher::FFileCache> CollectionFileCaches[ECollectionShareType::CST_All];

	/** Delegate handle for the TickFileCache function */
	FTSTicker::FDelegateHandle TickFileCacheDelegateHandle;

	/** A map of collection names to FCollection objects */
	TMap<FCollectionNameType, TSharedRef<FCollection>> AvailableCollections;

	TArray<FSoftObjectPath> DeferredDeletedObjects;

	/** Cache of collection hierarchy, identity, etc */
	TPimplPtr<FCollectionManagerCache> CollectionCache;

	/** Event for when assets are added to a collection */
	FOnAssetsAddedToCollection AssetsAddedToCollectionDelegate;

	/** Event for when assets are removed from a collection */
	FOnAssetsRemovedFromCollection AssetsRemovedFromCollectionDelegate;

	/** Event for when collections are renamed */
	FCollectionRenamedEvent CollectionRenamedEvent;

	/** Event for when collections are re-parented */
	FCollectionReparentedEvent CollectionReparentedEvent;

	/** Event for when collections are updated, or otherwise changed and we can't tell exactly how (eg, after updating from source control and merging) */
	FCollectionUpdatedEvent CollectionUpdatedEvent;

	/** Event for when collections are created */
	FCollectionCreatedEvent CollectionCreatedEvent;

	/** Event for when collections are destroyed */
	FCollectionDestroyedEvent CollectionDestroyedEvent;

	/** When a collection checkin happens, use this event to add additional text to the changelist description */
	FAddToCollectionCheckinDescriptionEvent AddToCollectionCheckinDescriptionEvent;

	/** Ref count for deferring calls to HandleObjectsDeleted. When the ref count reaches 0 we flush all deferred notifications */
	std::atomic<int32> SuppressObjectDeletionRefCount = 0;

	/** When true, redirectors will not be automatically followed in collections during startup */
	bool bNoFixupRedirectors;
};
