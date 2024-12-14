// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlainPropsBuild.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/PagedArray.h"

namespace PlainProps
{

struct FMemberSchema;
struct FEnumSchemaBuilder;
struct FStructSchemaBuilder;

struct FBuiltStructSchema
{
	FTypeId							Type;
	FStructSchemaId					Id;
	FOptionalStructSchemaId			Super;
	bool							bDense = false;
	TArray<FMemberId>				MemberNames;
	TArray<const FMemberSchema*>	MemberSchemas;
};

struct FBuiltEnumSchema
{
	FTypeId							Type;
	FEnumSchemaId					Id;
	EEnumMode						Mode = EEnumMode::Flat;
	ELeafWidth						Width = ELeafWidth::B8;
	TArray<FNameId>					Names;
	TArray<uint64>					Constants;
};

struct FBuiltSchemas
{
	TArray<FBuiltStructSchema>		Structs; // Same size as number of declared structs
	TArray<FBuiltEnumSchema>		Enums; // Same size as number of declared enums
};

class FSchemasBuilder
{
public:
	using FStructDeclarations = TConstArrayView<TUniquePtr<FStructDeclaration>>;
	using FEnumDeclarations = TConstArrayView<TUniquePtr<FEnumDeclaration>>;

	PLAINPROPS_API FSchemasBuilder(const FDeclarations& Declarations, const IStructBindIds& BindIds, FScratchAllocator& Scratch);
	PLAINPROPS_API FSchemasBuilder(FStructDeclarations InStructs, FEnumDeclarations InEnums, const IStructBindIds& BindIds, const FDebugIds& InDebug, FScratchAllocator& Scratch);
	PLAINPROPS_API ~FSchemasBuilder();

	PLAINPROPS_API FEnumSchemaBuilder&			NoteEnum(FEnumSchemaId Id);
	PLAINPROPS_API FStructSchemaBuilder&		NoteStruct(FStructSchemaId BindId);
	PLAINPROPS_API void							NoteStructAndMembers(FStructSchemaId BindId, const FBuiltStruct& Struct);
	PLAINPROPS_API FBuiltSchemas				Build();
	
	FScratchAllocator&							GetScratch() const { return Scratch; }
	const FDebugIds&							GetDebug() const { return Debug; }

private:
	FStructDeclarations							DeclaredStructs;
	FEnumDeclarations							DeclaredEnums;
	TArray<int32>								StructIndices;
	TArray<int32>								EnumIndices;
	const IStructBindIds&						BindIds;
	TPagedArray<FStructSchemaBuilder, 4096>		Structs;		// TPagedArray for stable references
	TPagedArray<FEnumSchemaBuilder, 4096>		Enums;			// TPagedArray for stable references
	FScratchAllocator&							Scratch;
	const FDebugIds&							Debug;
	bool										bBuilt = false;

	void										NoteInheritanceChains();
};

} // namespace PlainProps