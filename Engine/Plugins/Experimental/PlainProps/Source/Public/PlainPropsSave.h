// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlainPropsTypes.h"
#include "Templates/UniquePtr.h"

namespace PlainProps 
{

struct FBuiltRange;
struct FBuiltStruct;
class FCustomBindings;
class FDeclarations;
struct FRangeMemberBinding;
class FSchemaBindings;
class FScratchAllocator;

// Temporary data structure, will be replaced by something more sophisticated
// perhaps deduplicating all zero-memory defaults
struct FDefaultStruct { FStructSchemaId Id; const void* Struct; };
using FDefaultStructs = TConstArrayView<FDefaultStruct>;

struct FSaveContext
{
	const FDeclarations&		Declarations;
	const FSchemaBindings&		Schemas;
	FCustomBindings&			Customs;
	FScratchAllocator&			Scratch;
	FDefaultStructs				Defaults;
};

template<typename Runtime>
FSaveContext MakeSaveContext(FDefaultStructs Defaults, FScratchAllocator& Scratch)
{
	return { Runtime::GetTypes(), Runtime::GetSchemas(), Runtime::GetCustoms(), Scratch, Defaults };
}

[[nodiscard]] PLAINPROPS_API FBuiltStruct*	SaveStruct(const void* Struct, FStructSchemaId BindId, const FSaveContext& Context);
[[nodiscard]] PLAINPROPS_API FBuiltStruct*	SaveStructDelta(const void* Struct, const void* Default, FStructSchemaId BindId, const FSaveContext& Context);
[[nodiscard]] PLAINPROPS_API FBuiltRange*	SaveRange(const void* Range, FRangeMemberBinding Member, const FSaveContext& Ctx);
} // namespace PlainProps