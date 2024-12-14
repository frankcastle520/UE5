// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "NavigationOctree.h"
#include "AI/Navigation/NavigationDirtyElement.h"


struct FNavigationDirtyElementKeyFunctions : BaseKeyFuncs<FNavigationDirtyElement, FNavigationElementHandle, /*bInAllowDuplicateKeys*/false>
{
	static FNavigationElementHandle GetSetKey(ElementInitType Element)
	{
		return Element.NavigationElement->GetHandle();
	}

	static bool Matches(KeyInitType A, KeyInitType B)
	{
		return A == B;
	}

	static uint32 GetKeyHash(KeyInitType Key)
	{
		return GetTypeHash(Key);
	}
};

struct FNavigationOctreeController
{
	enum EOctreeUpdateMode
	{
		OctreeUpdate_Default = 0,						// regular update, mark dirty areas depending on exported content
		OctreeUpdate_Geometry = 1,						// full update, mark dirty areas for geometry rebuild
		OctreeUpdate_Modifiers = 2,						// quick update, mark dirty areas for modifier rebuild
		OctreeUpdate_Refresh = 4,						// update is used for refresh, don't invalidate pending queue
		OctreeUpdate_ParentChain = 8,					// update child nodes, don't remove anything
	};

	UE_DEPRECATED(5.5, "Use PendingUpdates instead.")
	TSet<FNavigationDirtyElement> PendingOctreeUpdates;
	TSet<FNavigationDirtyElement, FNavigationDirtyElementKeyFunctions> PendingUpdates;
	TSharedPtr<FNavigationOctree, ESPMode::ThreadSafe> NavOctree;

	UE_DEPRECATED(5.5, "This container is no longer used. Use AddChild/RemoveChild/GetChildren methods instead.")
	TMultiMap<UObject*, FWeakObjectPtr> OctreeChildNodesMap;

	/** if set, navoctree updates are ignored, use with caution! */
	uint8 bNavOctreeLock : 1 = false;

	inline void SetNavigationOctreeLock(bool bLock);

	NAVIGATIONSYSTEM_API bool HasPendingUpdateForElement(FNavigationElementHandle Element) const;
	UE_DEPRECATED(5.5, "Use HasPendingUpdateForElement instead.")
	NAVIGATIONSYSTEM_API bool HasPendingObjectNavOctreeId(UObject& Object) const;

	inline void RemoveNode(FOctreeElementId2 ElementId, FNavigationElementHandle GetHandle);

	UE_DEPRECATED(5.5, "Use RemoveNode instead.")
	NAVIGATIONSYSTEM_API void RemoveObjectsNavOctreeId(const UObject& Object);

	NAVIGATIONSYSTEM_API void SetNavigableGeometryStoringMode(FNavigationOctree::ENavGeometryStoringMode NavGeometryMode);

	NAVIGATIONSYSTEM_API void Reset();

	inline const FNavigationOctree* GetOctree() const;
	inline FNavigationOctree* GetMutableOctree();

	inline const FOctreeElementId2* GetNavOctreeIdForElement(FNavigationElementHandle Element) const;
	UE_DEPRECATED(5.5, "Use GetNavOctreeIdForElement instead.")
	NAVIGATIONSYSTEM_API const FOctreeElementId2* GetObjectsNavOctreeId(const UObject& Object) const;

	NAVIGATIONSYSTEM_API bool GetNavOctreeElementData(FNavigationElementHandle Element, ENavigationDirtyFlag& DirtyFlags, FBox& DirtyBounds);
	UE_DEPRECATED(5.5, "Use the version taking ENavigationDirtyFlag& and FNavigationElementHandle as parameter instead.")
	NAVIGATIONSYSTEM_API bool GetNavOctreeElementData(const UObject& NodeOwner, int32& DirtyFlags, FBox& DirtyBounds);

	NAVIGATIONSYSTEM_API const FNavigationRelevantData* GetDataForElement(FNavigationElementHandle Element) const;
	UE_DEPRECATED(5.5, "Use GetDataForElement instead.")
	NAVIGATIONSYSTEM_API const FNavigationRelevantData* GetDataForObject(const UObject& Object) const;

	NAVIGATIONSYSTEM_API FNavigationRelevantData* GetMutableDataForElement(FNavigationElementHandle Element);
	UE_DEPRECATED(5.5, "Use GetMutableDataForElement instead.")
	NAVIGATIONSYSTEM_API FNavigationRelevantData* GetMutableDataForObject(const UObject& Object);

	inline bool HasElementNavOctreeId(const FNavigationElementHandle Element) const;
	UE_DEPRECATED(5.5, "Use HasElementNavOctreeId instead.")
	NAVIGATIONSYSTEM_API bool HasObjectsNavOctreeId(const UObject& Object) const;

	inline bool IsNavigationOctreeLocked() const;

	/** basically says if navoctree has been created already */
	bool IsValid() const { return NavOctree.IsValid(); }

	inline bool IsValidElement(const FOctreeElementId2* ElementId) const;
	inline bool IsValidElement(const FOctreeElementId2& ElementId) const;

	bool IsEmpty() const
	{
		return (IsValid() == false) || NavOctree->GetSizeBytes() == 0;
	}

	inline void AddChild(FNavigationElementHandle Parent, const TSharedRef<const FNavigationElement>& Child);
	inline void RemoveChild(FNavigationElementHandle Parent, const TSharedRef<const FNavigationElement>& Child);
	inline void GetChildren(FNavigationElementHandle Parent, TArray<const TSharedRef<const FNavigationElement>>& OutChildren) const;

private:
	UE_DEPRECATED(5.5, "This method will no be longer be used by the navigation system.")
	static NAVIGATIONSYSTEM_API uint32 HashObject(const UObject& Object);

	/** Map of all elements that are tied to indexed navigation parent */
	TMultiMap<FNavigationElementHandle, const TSharedRef<const FNavigationElement>> OctreeParentChildNodesMap;
};

//----------------------------------------------------------------------//
// inlines
//----------------------------------------------------------------------//

// deprecated
FORCEINLINE uint32 FNavigationOctreeController::HashObject(const UObject& Object)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return FNavigationOctree::HashObject(Object);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

FORCEINLINE_DEBUGGABLE const FOctreeElementId2* FNavigationOctreeController::GetNavOctreeIdForElement(const FNavigationElementHandle Element) const
{ 
	return NavOctree.IsValid()
		? NavOctree->ElementToOctreeId.Find(Element)
		: nullptr;
}

FORCEINLINE_DEBUGGABLE bool FNavigationOctreeController::HasElementNavOctreeId(const FNavigationElementHandle Element) const
{
	return NavOctree.IsValid() && (NavOctree->ElementToOctreeId.Find(Element) != nullptr);
}

FORCEINLINE_DEBUGGABLE void FNavigationOctreeController::RemoveNode(const FOctreeElementId2 ElementId, const FNavigationElementHandle ElementHandle)
{ 
	if (NavOctree.IsValid())
	{
		NavOctree->RemoveNode(ElementId);
		NavOctree->ElementToOctreeId.Remove(ElementHandle);
	}
}

FORCEINLINE const FNavigationOctree* FNavigationOctreeController::GetOctree() const
{ 
	return NavOctree.Get(); 
}

FORCEINLINE FNavigationOctree* FNavigationOctreeController::GetMutableOctree()
{ 
	return NavOctree.Get(); 
}

FORCEINLINE bool FNavigationOctreeController::IsNavigationOctreeLocked() const
{ 
	return bNavOctreeLock; 
}

FORCEINLINE void FNavigationOctreeController::SetNavigationOctreeLock(bool bLock) 
{ 
	bNavOctreeLock = bLock; 
}

FORCEINLINE bool FNavigationOctreeController::IsValidElement(const FOctreeElementId2* ElementId) const
{
	return ElementId && IsValidElement(*ElementId);
}

FORCEINLINE bool FNavigationOctreeController::IsValidElement(const FOctreeElementId2& ElementId) const
{
	return IsValid() && NavOctree->IsValidElementId(ElementId);
}

void FNavigationOctreeController::AddChild(const FNavigationElementHandle Parent, const TSharedRef<const FNavigationElement>& Child)
{
	OctreeParentChildNodesMap.AddUnique(Parent, Child);
}

void FNavigationOctreeController::RemoveChild(const FNavigationElementHandle Parent, const TSharedRef<const FNavigationElement>& Child)
{
	OctreeParentChildNodesMap.RemoveSingle(Parent, Child);
}

void FNavigationOctreeController::GetChildren(const FNavigationElementHandle Parent, TArray<const TSharedRef<const FNavigationElement>>& OutChildren) const
{
	OctreeParentChildNodesMap.MultiFind(Parent, OutChildren);
}