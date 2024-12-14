// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlainPropsTypes.h"
#include "Containers/Array.h"
#include "Templates/UniquePtr.h"

namespace PlainProps
{

struct FBuiltSchemas;
struct FBuiltStruct;
class FDebugIds;
class FIdIndexerBase;
class FNestedScopeIndexer;
class FParametricTypeIndexer;
struct FWriteIds;
struct IStructBindIds;

enum class ESchemaFormat { StableNames, InMemoryNames };

class FWriter
{
public:
	PLAINPROPS_API FWriter(const FIdIndexerBase& DeclaredIds, const IStructBindIds& BindIds, const FBuiltSchemas& InSchemas, ESchemaFormat Format);
	PLAINPROPS_API ~FWriter();
	
	PLAINPROPS_API bool								Uses(FNameId BuiltId) const;
	PLAINPROPS_API FOptionalStructSchemaId			GetWriteId(FStructSchemaId BuiltId) const;

	PLAINPROPS_API void								WriteSchemas(TArray64<uint8>& Out) const;
	PLAINPROPS_API FStructSchemaId					WriteMembers(TArray64<uint8>& Out, FStructSchemaId BuiltId, const FBuiltStruct& Struct) const;

private:
	const FBuiltSchemas&							Schemas;
	const FDebugIds&								Debug;
	TUniquePtr<FWriteIds>							NewIds;
};

} // namespace PlainProps