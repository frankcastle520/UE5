// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "StructUtils/InstancedStruct.h"
#include "Tests/ReplicationSystem/ReplicatedTestObject.h"
#include "TestInstancedStructNetSerializer.generated.h"

USTRUCT()
struct FTestInstancedStruct
{
	GENERATED_BODY()

	UPROPERTY()
	FInstancedStruct InstancedStruct;
};

USTRUCT()
struct FTestInstancedStructArray
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FInstancedStruct> InstancedStructArray;
};

USTRUCT()
struct FStructForInstancedStructTestA
{
	GENERATED_BODY()

	UPROPERTY()
	uint16 SomeUint16 = 0;
};

USTRUCT()
struct FStructForInstancedStructTestB
{
	GENERATED_BODY()

	UPROPERTY()
	float SomeFloat = 0.0f;
};

USTRUCT()
struct FStructForInstancedStructTestC
{
	GENERATED_BODY()

	UPROPERTY()
	bool SomeBool = false;
};

USTRUCT()
struct FStructForInstancedStructTestD
{
	GENERATED_BODY()

	// Intentionally has no properties
};

USTRUCT()
struct FStructForInstancedStructTestWithArray
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FStructForInstancedStructTestB> ArrayOfTestB;
};

USTRUCT()
struct FStructForInstancedStructTestWithObjectReference
{
	GENERATED_BODY()

	UPROPERTY();
	TObjectPtr<UObject> SomeObject = nullptr;
};

UCLASS()
class UInstancedStructNetSerializerTestObject : public UReplicatedTestObject
{
	GENERATED_BODY()

protected:
	virtual void RegisterReplicationFragments(UE::Net::FFragmentRegistrationContext& Fragments, UE::Net::EFragmentRegistrationFlags RegistrationFlags) override;

public:
	UPROPERTY(Replicated, Transient)
	FInstancedStruct InstancedStruct;

	UPROPERTY(Replicated, Transient)
	TArray<FInstancedStruct> InstancedStructArray;
};

