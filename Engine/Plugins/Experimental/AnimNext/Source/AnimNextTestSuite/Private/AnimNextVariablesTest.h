// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Param/ParamType.h"
#include "RigVMCore/RigVMStruct.h"
#include "Templates/SubclassOf.h"
#include "AnimNextVariablesTest.generated.h"

USTRUCT()
struct FAnimNextParamTypeTestStruct
{
	GENERATED_BODY()

	UPROPERTY()
	bool bBool = false;

	UPROPERTY()
	uint8 Uint8 = 0;

	UPROPERTY()
	int32 Int32 = 0;

	UPROPERTY()
	int64 Int64 = 0;

	UPROPERTY()
	float Float = 0.0f;

	UPROPERTY()
	double Double = 0.0;

	UPROPERTY()
	FName Name;

	UPROPERTY()
	FString String;

	UPROPERTY()
	FText Text;

	UPROPERTY()
	EPropertyBagContainerType Enum = EPropertyBagContainerType::None;

	UPROPERTY()
	FAnimNextParamType Struct;

	UPROPERTY()
	FVector Vector = FVector::ZeroVector;

	UPROPERTY()
	FTransform Transform;

	UPROPERTY()
	FQuat Quat = FQuat::Identity;

	UPROPERTY()
	TObjectPtr<UObject> Object;

	UPROPERTY()
	TObjectPtr<UClass> Class;

	UPROPERTY()
	TSubclassOf<UObject> SubclassOf;

	UPROPERTY()
	TSoftObjectPtr<UObject> SoftObjectPtr;

	UPROPERTY()
	TSoftClassPtr<UObject> SoftClassPtr;

	UPROPERTY()
	TArray<bool> BoolArray;

	UPROPERTY()
	TArray<uint8> Uint8Array;

	UPROPERTY()
	TArray<int32> Int32Array;

	UPROPERTY()
	TArray<int64> Int64Array;

	UPROPERTY()
	TArray<float> FloatArray;

	UPROPERTY()
	TArray<double> DoubleArray;

	UPROPERTY()
	TArray<FName> NameArray;

	UPROPERTY()
	TArray<FString> StringArray;

	UPROPERTY()
	TArray<FText> TextArray;

	UPROPERTY()
	TArray<EPropertyBagContainerType> EnumArray;

	UPROPERTY()
	TArray<FAnimNextParamType> StructArray;

	UPROPERTY()
	TArray<FVector> VectorArray;
	
	UPROPERTY()
	TArray<FTransform> TransformArray;

	UPROPERTY()
	TArray<FQuat> QuatArray;
	
	UPROPERTY()
	TArray<TObjectPtr<UObject>> ObjectArray;

	UPROPERTY()
	TArray<TObjectPtr<UClass>> ClassArray;

	UPROPERTY()
	TArray<TSubclassOf<UObject>> SubclassOfArray;

	UPROPERTY()
	TArray<TSoftObjectPtr<UObject>> SoftObjectPtrArray;

	UPROPERTY()
	TArray<TSoftClassPtr<UObject>> SoftClassPtrArray;
};

USTRUCT(meta=(Hidden))
struct FAnimNextTests_TestOperation : public FRigVMStructMutable
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute() override;

	UPROPERTY(meta=(Input))
	int32 A = 0;

	UPROPERTY(meta=(Input))
	int32 B = 0;

	UPROPERTY(meta=(Output))
	int32 Result = 0;
};

USTRUCT(meta=(Hidden))
struct FAnimNextTests_PrintResult : public FRigVMStructMutable
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute() override;

	UPROPERTY(meta=(Input))
	int32 Result = 0;
};
