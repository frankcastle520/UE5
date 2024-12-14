// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavigationOctreeController.h"
#include "NavigationSystem.h"


//----------------------------------------------------------------------//
// FNavigationOctreeController
//----------------------------------------------------------------------//
void FNavigationOctreeController::Reset()
{
	if (NavOctree.IsValid())
	{
		NavOctree->Destroy();
		NavOctree = nullptr;
	}
	PendingUpdates.Empty(32);
}

bool FNavigationOctreeController::HasPendingUpdateForElement(const FNavigationElementHandle Element) const
{ 
	return PendingUpdates.Contains(Element);
}

void FNavigationOctreeController::SetNavigableGeometryStoringMode(FNavigationOctree::ENavGeometryStoringMode NavGeometryMode)
{
	check(NavOctree.IsValid());
	NavOctree->SetNavigableGeometryStoringMode(NavGeometryMode);
}

bool FNavigationOctreeController::GetNavOctreeElementData(const FNavigationElementHandle Element, ENavigationDirtyFlag& OutDirtyFlags, FBox& OutDirtyBounds)
{
	const FOctreeElementId2* ElementId = GetNavOctreeIdForElement(Element);
	if (ElementId != nullptr && IsValidElement(*ElementId))
	{
		// mark area occupied by given actor as dirty
		const FNavigationOctreeElement& ElementData = NavOctree->GetElementById(*ElementId);
		OutDirtyFlags = ElementData.Data->GetDirtyFlag();
		OutDirtyBounds = ElementData.Bounds.GetBox();
		return true;
	}

	return false;
}

// Deprecated
bool FNavigationOctreeController::GetNavOctreeElementData(const UObject& NodeOwner, int32& DirtyFlags, FBox& DirtyBounds)
{
	ENavigationDirtyFlag TmpDirtyFlags = ENavigationDirtyFlag::None;
	const bool bSuccess = GetNavOctreeElementData(FNavigationElementHandle(&NodeOwner), TmpDirtyFlags, DirtyBounds);
	DirtyFlags = static_cast<int32>(TmpDirtyFlags);
	return bSuccess;
}

// Deprecated
const FNavigationRelevantData* FNavigationOctreeController::GetDataForObject(const UObject& Object) const
{
	return GetDataForElement(FNavigationElementHandle(&Object));
}

const FNavigationRelevantData* FNavigationOctreeController::GetDataForElement(const FNavigationElementHandle Element) const
{
	if (const FOctreeElementId2* ElementId = GetNavOctreeIdForElement(Element); IsValidElement(ElementId))
	{
		return NavOctree->GetDataForID(*ElementId);
	}

	return nullptr;
}

// Deprecated
FNavigationRelevantData* FNavigationOctreeController::GetMutableDataForObject(const UObject& Object)
{
	return GetMutableDataForElement(FNavigationElementHandle(&Object));
}

FNavigationRelevantData* FNavigationOctreeController::GetMutableDataForElement(const FNavigationElementHandle Element)
{
	if (const FOctreeElementId2* ElementId = GetNavOctreeIdForElement(Element); IsValidElement(ElementId))
	{
		return NavOctree->GetMutableDataForID(*ElementId);
	}

	return nullptr;
}

//----------------------------------------------------------------------//
// Deprecated methods
//----------------------------------------------------------------------//

PRAGMA_DISABLE_DEPRECATION_WARNINGS

// Deprecated
bool FNavigationOctreeController::HasPendingObjectNavOctreeId(UObject& Object) const
{
	return HasPendingUpdateForElement(FNavigationElementHandle(&Object));
}

// Deprecated
bool FNavigationOctreeController::HasObjectsNavOctreeId(const UObject& Object) const
{
	return HasElementNavOctreeId(FNavigationElementHandle(&Object));
}

// Deprecated
const FOctreeElementId2* FNavigationOctreeController::GetObjectsNavOctreeId(const UObject& Object) const
{
	return GetNavOctreeIdForElement(FNavigationElementHandle(&Object));
}

// Deprecated
void FNavigationOctreeController::RemoveObjectsNavOctreeId(const UObject& Object)
{
	const FNavigationElementHandle ElementHandle(&Object);
	if (const FOctreeElementId2* ElementId = GetNavOctreeIdForElement(ElementHandle); IsValidElement(ElementId))
	{
		RemoveNode(*ElementId, ElementHandle);
	}	
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS