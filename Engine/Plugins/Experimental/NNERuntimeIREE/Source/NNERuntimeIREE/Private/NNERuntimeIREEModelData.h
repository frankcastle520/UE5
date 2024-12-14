// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ContainersFwd.h"
#include "NNERuntimeIREECompiler.h"
#include "NNETypes.h"
#include "UObject/Class.h"

#include "NNERuntimeIREEModelData.generated.h"

/**
 * IREE model data class.
 */
UCLASS()
class UNNERuntimeIREEModelData : public UObject
{
	GENERATED_BODY()

public:
	// UObject interface
	virtual void Serialize(FArchive& Ar) override;
	// End of UObject interface

	/**
	 * Check data for matching Guid and Version without deserializing everything.
	 */
	static bool IsSameGuidAndVersion(TConstArrayView64<uint8> Data, FGuid Guid, int32 Version);

	/**
	 * A Guid that uniquely identifies this IREE model data.
	 */
	FGuid GUID;

	/**
	 * Current version of this IREE model data.
	 */
	int32 Version;

	/**
	 * A Guid that uniquely identifies the model.
	 */
	FGuid FileId;

	/**
	 * Serialized module meta data.
	 */
	TArray64<uint8> ModuleMetaData;

	/**
	 * Serialized compiler output.
	 */
	TArray64<uint8> CompilerResult;
};
