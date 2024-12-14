// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "PlainPropsRead.h"
#include "Memory/MemoryFwd.h"
#include "Templates/UniquePtr.h"

namespace PlainProps 
{
	
class FCustomBindings;
class FDeclarations;
struct FLoadBatch;
class FRangeBinding;
class FSchemaBindings;
struct FTypedRange;

struct FLoadBatchDeleter 
{
	PLAINPROPS_API void operator()(FLoadBatch* Ptr) const;
};
using FLoadBatchPtr = TUniquePtr<FLoadBatch, FLoadBatchDeleter>;

template<class Runtime>
[[nodiscard]] FLoadBatchPtr CreateLoadPlans(FReadBatchId ReadId, TConstArrayView<FStructSchemaId> RuntimeIds)
{
	return CreateLoadPlans(ReadId, Runtime::GetTypes(), Runtime::GetCustoms(), Runtime::GetSchemas(), RuntimeIds);
}

PLAINPROPS_API FLoadBatchPtr				CreateLoadPlans(FReadBatchId ReadId, const FDeclarations& Declarations, const FCustomBindings& Customs, const FSchemaBindings& Schemas, TConstArrayView<FStructSchemaId> RuntimeIds);
PLAINPROPS_API void							LoadStruct(void* Dst, FByteReader Src, FStructSchemaId Id, const FLoadBatch& Batch);
PLAINPROPS_API void							LoadStruct(void* Dst, FStructView Src, const FLoadBatch& Batch);
PLAINPROPS_API void							ConstructAndLoadStruct(void* Dst, FByteReader Src, FStructSchemaId Id, const FLoadBatch& Batch);
PLAINPROPS_API void							ConstructAndLoadStruct(void* Dst, FStructView Src, const FLoadBatch& Batch);
PLAINPROPS_API void							LoadRange(void* Dst, FRangeView Src, ERangeSizeType MaxType, TConstArrayView<FRangeBinding> Bindings, const FLoadBatch& Batch);

} // namespace PlainProps

