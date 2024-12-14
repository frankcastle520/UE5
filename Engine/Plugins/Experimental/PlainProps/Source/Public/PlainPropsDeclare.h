// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlainPropsTypes.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Templates/UniquePtr.h"

namespace PlainProps
{

struct FEnumerator
{
	FNameId					Name;
	uint64					Constant;
};

enum class EEnumMode { Flat, Flag };

struct FEnumDeclaration
{
	FTypeId					Type;			// Could be removed
	EEnumMode				Mode;
	ELeafWidth				Width;
	uint16					NumEnumerators;
	FEnumerator				Enumerators[0];	// Constants must be unique, no aliases allowed

	TConstArrayView<FEnumerator> GetEnumerators() const
	{
		return MakeArrayView(Enumerators, NumEnumerators);
	}
};

enum class EMemberPresence { RequireAll, AllowSparse };

struct FStructDeclaration
{
	uint32					RefCount;
	FStructSchemaId			Id;				// Could be removed, might allow declaration dedup among templated types
	FTypeId					Type;			// Could be removed, might allow declaration dedup among templated types
	FOptionalStructSchemaId	Super;
	EMemberPresence			Occupancy;
	uint16					NumMembers;
	FMemberId				MemberOrder[0];

	TConstArrayView<FMemberId> GetMemberOrder() const
	{
		return MakeArrayView(MemberOrder, NumMembers);
	}
};

class FDeclarations
{
public:
	UE_NONCOPYABLE(FDeclarations);
	explicit FDeclarations(const FDebugIds& In) : Debug(In) {}

	PLAINPROPS_API void								DeclareEnum(FEnumSchemaId DeclId, FTypeId Type, EEnumMode Mode, ELeafWidth Width, TConstArrayView<FEnumerator> Enumerators);
	// Declare struct with ref count 1 or increment it and check that previous declaration matches
	PLAINPROPS_API void								DeclareStruct(FStructSchemaId DeclId, FTypeId Type, TConstArrayView<FMemberId> MemberOrder, EMemberPresence Occupancy, FOptionalStructSchemaId Super = {});
	
	void											DropEnum(FEnumSchemaId DeclId)			{ Check(DeclId); DeclaredEnums[DeclId.Idx].Reset(); }
	void											DropStructRef(FStructSchemaId DeclId);

	const FEnumDeclaration&							Get(FEnumSchemaId DeclId) const			{ Check(DeclId); return *DeclaredEnums[DeclId.Idx]; }
	const FStructDeclaration&						Get(FStructSchemaId DeclId) const		{ Check(DeclId); return *DeclaredStructs[DeclId.Idx]; }
	
	TConstArrayView<TUniquePtr<FEnumDeclaration>>	GetEnums() const				{ return DeclaredEnums; }
	TConstArrayView<TUniquePtr<FStructDeclaration>>	GetStructs() const				{ return DeclaredStructs; }
	const FDebugIds&								GetDebug() const				{ return Debug; }

protected:
	TArray<TUniquePtr<FEnumDeclaration>>			DeclaredEnums;
	TArray<TUniquePtr<FStructDeclaration>>			DeclaredStructs;
	const FDebugIds&								Debug;

#if DO_CHECK
	PLAINPROPS_API void								Check(FEnumSchemaId Id) const;
	PLAINPROPS_API void								Check(FStructSchemaId Id) const;
#else
	void											Check(...) const {}
#endif
};

struct IStructBindIds
{
	virtual ~IStructBindIds() = default;
	virtual FStructSchemaId GetDeclId(FStructSchemaId BindId) const = 0;
};

} // namespace PlainProps