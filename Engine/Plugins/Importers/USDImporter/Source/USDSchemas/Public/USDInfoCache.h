// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Set.h"
#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

#include "UsdWrappers/SdfPath.h"

struct FUsdSchemaTranslationContext;
namespace UE
{
	class FSdfPath;
	class FUsdPrim;
}

enum class ECollapsingType
{
	Assets,
	Components
};

namespace UsdUtils
{
	struct FUsdPrimMaterialSlot;
}

struct FUsdInfoCacheImpl;

/**
 * Caches information about a specific USD Stage
 */
class USDSCHEMAS_API FUsdInfoCache
{
public:
	FUsdInfoCache();
	virtual ~FUsdInfoCache();

	void CopyImpl(const FUsdInfoCache& Other);

	bool Serialize(FArchive& Ar);

	// Returns whether we contain any info about prim at 'Path' at all
	bool ContainsInfoAboutPrim(const UE::FSdfPath& Path) const;

	// Retrieves the children of a prim from the cached information
	TArray<UE::FSdfPath> GetChildren(const UE::FSdfPath& ParentPath) const;

	// Returns a list of all prims we have generic info about
	UE_DEPRECATED(5.5, "No longer used")
	TSet<UE::FSdfPath> GetKnownPrims() const;

	void RebuildCacheForSubtree(const UE::FUsdPrim& Prim, FUsdSchemaTranslationContext& Context);
	void RebuildCacheForSubtrees(const TArray<UE::FSdfPath>& SubtreeRoots, FUsdSchemaTranslationContext& Context);

	void Clear();
	bool IsEmpty();

public:
	bool IsPathCollapsed(const UE::FSdfPath& Path, ECollapsingType CollapsingType) const;
	bool DoesPathCollapseChildren(const UE::FSdfPath& Path, ECollapsingType CollapsingType) const;

	// Returns Path in case it represents an uncollapsed prim, or returns the path to the prim that collapsed it
	UE::FSdfPath UnwindToNonCollapsedPath(const UE::FSdfPath& Path, ECollapsingType CollapsingType) const;

public:
	// Returns the paths to prims that, when translated into assets or components, also require reading the prim at
	// 'Path'. e.g. providing the path to a Shader prim will return the paths to all Material prims for which the
	// translation involves reading that particular Shader.
	TSet<UE::FSdfPath> GetMainPrims(const UE::FSdfPath& AuxPrimPath) const;

	// The inverse of the function above: Provide it with the path to a Material prim and it will return the set of
	// paths to all Shader prims that need to be read to translate that Material prim into material assets
	TSet<UE::FSdfPath> GetAuxiliaryPrims(const UE::FSdfPath& MainPrimPath) const;

public:
	TSet<UE::FSdfPath> GetMaterialUsers(const UE::FSdfPath& Path) const;
	bool IsMaterialUsed(const UE::FSdfPath& Path) const;

public:
	// Provides the total vertex or material slots counts for each prim *and* its subtree.
	// This is built inside RebuildCacheForSubtree, so it will factor in the used Context's bMergeIdenticalMaterialSlots.
	// Note that these aren't affected by actual collapsing: A prim that doesn't collapse its children will still
	// provide the total sum of vertex counts of its entire subtree when queried
	TOptional<uint64> GetSubtreeVertexCount(const UE::FSdfPath& Path);
	TOptional<uint64> GetSubtreeMaterialSlotCount(const UE::FSdfPath& Path);
	TOptional<TArray<UsdUtils::FUsdPrimMaterialSlot>> GetSubtreeMaterialSlots(const UE::FSdfPath& Path);

	// Returns true if Path could potentially be collapsed as a Geometry Cache asset
	UE_DEPRECATED(5.5, "No longer used")
	bool IsPotentialGeometryCacheRoot(const UE::FSdfPath& Path) const;

public:
	// Marks/checks if the provided path to a prototype prim is already being translated.
	// This is used during scene translation with instanceables, so that the schema translators can early out
	// in case they have been created to translate multiple instances of the same prototype
	void ResetTranslatedPrototypes();
	bool IsPrototypeTranslated(const UE::FSdfPath& PrototypePath);
	void MarkPrototypeAsTranslated(const UE::FSdfPath& PrototypePath);

public:
	UE_DEPRECATED(5.5, "Use the UUsdPrimLinkCache object and its analogous function instead")
	void LinkAssetToPrim(const UE::FSdfPath& Path, UObject* Asset);

	UE_DEPRECATED(5.5, "Use the UUsdPrimLinkCache object and its analogous function instead")
	void UnlinkAssetFromPrim(const UE::FSdfPath& Path, UObject* Asset);

	UE_DEPRECATED(5.5, "Use the UUsdPrimLinkCache object and its analogous function instead")
	TArray<TWeakObjectPtr<UObject>> RemoveAllAssetPrimLinks(const UE::FSdfPath& Path);

	UE_DEPRECATED(5.5, "Use the UUsdPrimLinkCache object and its analogous function instead")
	TArray<UE::FSdfPath> RemoveAllAssetPrimLinks(const UObject* Asset);

	UE_DEPRECATED(5.5, "Use the UUsdPrimLinkCache object and its analogous function instead")
	void RemoveAllAssetPrimLinks();

	UE_DEPRECATED(5.5, "Use the UUsdPrimLinkCache object and its analogous function instead")
	TArray<TWeakObjectPtr<UObject>> GetAllAssetsForPrim(const UE::FSdfPath& Path) const;

	template<typename T = UObject>
	UE_DEPRECATED(5.5, "Use the UUsdPrimLinkCache object and its analogous function instead")
	T* GetSingleAssetForPrim(const UE::FSdfPath& Path) const
	{
		return nullptr;
	}

	template<typename T>
	UE_DEPRECATED(5.5, "Use the UUsdPrimLinkCache object and its analogous function instead")
	TArray<T*> GetAssetsForPrim(const UE::FSdfPath& Path) const
	{
		return {};
	}

	UE_DEPRECATED(5.5, "Use the UUsdPrimLinkCache object and its analogous function instead")
	TArray<UE::FSdfPath> GetPrimsForAsset(const UObject* Asset) const;

	UE_DEPRECATED(5.5, "Use the UUsdPrimLinkCache object and its analogous function instead")
	TMap<UE::FSdfPath, TArray<TWeakObjectPtr<UObject>>> GetAllAssetPrimLinks() const;

private:
	friend class FUsdGeomXformableTranslator;
	friend class FUsdGeometryCacheTranslator;

	// Returns true if every prim on the subtree below RootPath (including the RootPath prim itself) returns true for
	// CanBeCollapsed(), according to their own schema translators.
	//
	// WARNING: This is intended for internal use, and exclusively during the actual info cache build process as it will
	// need to query the prim/stage directly. Calling it after the info cache build may yield back an empty optional,
	// meaning it is unknown at this point whether the prim CanBeCollapsed or not.
	//
	// In general, you shouldn't call this, but just use "IsPathCollapsed" or "DoesPathCollapseChildren" instead.
	TOptional<bool> CanXformableSubtreeBeCollapsed(const UE::FSdfPath& RootPath, FUsdSchemaTranslationContext& Context) const;

	// Analogous to the function above, this overload of IsPotentialGeometryCacheRoot is meant for internal use, and exists because
	// during the info cache build (in some contexts) we can fill in this geometry cache information on-demand, for better performance.
	bool IsPotentialGeometryCacheRoot(const UE::FUsdPrim& Prim) const;

private:
	TUniquePtr<FUsdInfoCacheImpl> Impl;
};
