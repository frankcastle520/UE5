// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dataflow/DataflowTypePolicy.h"

#include "DataflowAnyType.generated.h"

/** Any supported type */
USTRUCT()
struct FDataflowAnyType
{
	using FPolicyType = FDataflowAllTypesPolicy;
	using FStorageType = void;

	GENERATED_USTRUCT_BODY()
	DATAFLOWCORE_API static const FName TypeName;
};

/** Any supported type */
USTRUCT()
struct FDataflowAllTypes : public FDataflowAnyType
{
	using FPolicyType = FDataflowAllTypesPolicy;
	using FStorageType = void;
	GENERATED_USTRUCT_BODY()
};

/**
* Numeric types
* (double, float, int64, uint64, int32, uint32, int16, uint16, int8, uint8)"
*/
USTRUCT()
struct FDataflowNumericTypes : public FDataflowAnyType
{
	using FPolicyType = FDataflowNumericTypePolicy;
	using FStorageType = double;

	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category=Value)
	double Value = 0.0;
};

/**
* Vector types
* (2D, 3D and 4D vector, single and double precision)
*/
USTRUCT()
struct FDataflowVectorTypes : public FDataflowAnyType
{
	using FPolicyType = FDataflowVectorTypePolicy;
	using FStorageType = FVector4;

	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = Value)
	FVector4 Value = FVector4(0, 0, 0, 0);
};

/** String types (FString or FName) */
USTRUCT()
struct FDataflowStringTypes : public FDataflowAnyType
{
	using FPolicyType = FDataflowStringTypePolicy;
	using FStorageType = FString;

	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = Value)
	FString Value;
};

/**
* String convertible types
* (String types, Numeric types, Vector types and Booleans)
*/
USTRUCT()
struct FDataflowStringConvertibleTypes : public FDataflowAnyType
{
	using FPolicyType = FDataflowStringConvertibleTypePolicy;
	using FStorageType = FString;

	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = Value)
	FString Value;
};

/** UObject types */
USTRUCT()
struct FDataflowUObjectConvertibleTypes : public FDataflowAnyType
{
	using FPolicyType = FDataflowUObjectConvertibleTypePolicy;
	using FStorageType = TObjectPtr<UObject>;

	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = Value)
	TObjectPtr<UObject> Value;
};