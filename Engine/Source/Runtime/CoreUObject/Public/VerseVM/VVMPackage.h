// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "Misc/Optional.h"
#include "VVMCell.h"
#include "VerseVM/Inline/VVMValueInline.h"
#include "VerseVM/VVMNameValueMap.h"
#include "VerseVM/VVMNames.h"
#include "VerseVM/VVMWeakCellMap.h"

class UPackage;

namespace Verse
{
struct VClass;
struct VTupleType;

enum class EPackageStage : uint8
{
	Global,
	Temp,
	Dead
};

enum class EDigestVariant : uint8
{
	PublicAndEpicInternal = 0,
	PublicOnly = 1,
};

struct FVersionedDigest
{
	TWriteBarrier<VArray> Code;
	uint32 EffectiveVerseVersion;
};

struct VPackage : VCell
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VCell);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	TOptional<FVersionedDigest> DigestVariants[2]; // One for each variant

	VArray& GetName() const { return *PackageName; }

	uint32 Num() const { return Map.Num(); }
	const VArray& GetName(uint32 Index) const { return Map.GetName(Index); }
	VValue GetDefinition(uint32 Index) const { return Map.GetValue(Index).Follow(); }
	void AddDefinition(FAllocationContext Context, FUtf8StringView Name, VValue Definition) { Map.AddValue(Context, Name, Definition); }
	void AddDefinition(FAllocationContext Context, VArray& Name, VValue Definition) { Map.AddValue(Context, Name, Definition); }
	VValue LookupDefinition(FUtf8StringView Name) const { return Map.Lookup(Name); }
	template <typename CellType>
	CellType* LookupDefinition(FUtf8StringView Name) const { return Map.LookupCell<CellType>(Name); }

	// The following two variations are primarily for VFunction lookup where the path must get prepended to the name prior to the map lookup
	VValue LookupDefinition(FUtf8StringView Path, FUtf8StringView Name) const { return Map.Lookup(Names::GetDecoratedName(Path, Name).ToView()); }
	template <typename CellType>
	CellType* LookupDefinition(FUtf8StringView Path, FUtf8StringView Name) const { return Map.LookupCell<CellType>(Names::GetDecoratedName(Path, Name).ToView()); }

	COREUOBJECT_API UPackage* GetUPackage(const TCHAR* UEPackageName) const;
	COREUOBJECT_API UPackage* GetOrCreateUPackage(FAllocationContext Context, const TCHAR* UEPackageName);

	COREUOBJECT_API void NotifyUsedTupleType(FAllocationContext Context, VTupleType* TupleType);
	template <typename FunctorType> // FunctorType is (VTupleType*) -> void
	void ForEachUsedTupleType(FunctorType&& Functor);

	EPackageStage GetStage() const { return PackageStage; }
	COREUOBJECT_API void SetStage(EPackageStage InPackageStage);

	static VPackage& New(FAllocationContext Context, VArray& Name, uint32 Capacity, EPackageStage InPackageStage = EPackageStage::Global)
	{
		return *new (Context.Allocate(FHeap::DestructorAndCensusSpace, sizeof(VPackage))) VPackage(Context, Name, Capacity, InPackageStage);
	}

private:
	VPackage(FAllocationContext Context, VArray& Name, uint32 Capacity, EPackageStage InPackageStage)
		: VCell(Context, &GlobalTrivialEmergentType.Get(Context))
		, PackageName(Context, &Name)
		, Map(Context, Capacity)
		, UPackageMap(Context, 0)
	{
	}

	UPackage* GetUPackageInternal(FUtf8StringView UEPackageName) const;

	TWriteBarrier<VArray> PackageName;
	VNameValueMap Map;
	VNameValueMap UPackageMap;
	TWriteBarrier<VWeakCellMap> UsedTupleTypes;
	EPackageStage PackageStage;
};
} // namespace Verse
#endif // WITH_VERSE_VM
