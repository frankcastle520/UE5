// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VVMCell.h"
#include "VVMIntrinsics.h"
#include "VVMPackage.h"
#include "VVMTupleType.h"
#include "VerseVM/VVMNameValueMap.h"

namespace Verse
{
struct VProgram : VCell
{
	DECLARE_DERIVED_VCPPCLASSINFO(COREUOBJECT_API, VCell);
	COREUOBJECT_API static TGlobalTrivialEmergentTypePtr<&StaticCppClassInfo> GlobalTrivialEmergentType;

	uint32 Num() const { return PackageMap.Num(); }
	const VArray& GetName(uint32 Index) const { return PackageMap.GetName(Index); }
	VPackage& GetPackage(uint32 Index) const { return PackageMap.GetCell<VPackage>(Index); }
	void AddPackage(FAllocationContext Context, VArray& Name, VPackage& Package) { PackageMap.AddValue(Context, Name, VValue(Package)); }
	VPackage* LookupPackage(FUtf8StringView VersePackageName) const { return PackageMap.LookupCell<VPackage>(VersePackageName); }
	void ResetPackages(FAllocationContext Context) { PackageMap.Reset(Context); }

	void AddTupleType(FAllocationContext Context, VArray& Name, VTupleType& TupleType) { TupleTypeMap.AddValue(Context, Name, VValue(TupleType)); }
	VTupleType* LookupTupleType(FUtf8StringView MangledName) const { return TupleTypeMap.LookupCell<VTupleType>(MangledName); }

	const VIntrinsics& GetIntrinsics() const { return *Intrinsics.Get(); }

	static VProgram& New(FAllocationContext Context, uint32 Capacity)
	{
		return *new (Context.AllocateFastCell(sizeof(VProgram))) VProgram(Context, Capacity);
	}

private:
	VProgram(FAllocationContext Context, uint32 Capacity)
		: VCell(Context, &GlobalTrivialEmergentType.Get(Context))
		, PackageMap(Context, Capacity)
		, TupleTypeMap(Context, 256)
		, Intrinsics(Context, &VIntrinsics::New(Context))
	{
	}

	VNameValueMap PackageMap;
	VNameValueMap TupleTypeMap;
	TWriteBarrier<VIntrinsics> Intrinsics;
};
} // namespace Verse
#endif // WITH_VERSE_VM
