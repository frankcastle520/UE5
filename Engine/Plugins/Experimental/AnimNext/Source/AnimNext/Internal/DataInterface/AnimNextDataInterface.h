// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNextRigVMAsset.h"
#include "AnimNextDataInterface.generated.h"

class UAnimNextDataInterfaceFactory;

namespace UE::AnimNext::Tests
{
	class FDataInterfaceCompile;
}

namespace UE::AnimNext::UncookedOnly
{
	struct FUtils;
}

// Information about an implemented interface
USTRUCT()
struct FAnimNextImplementedDataInterface
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<const UAnimNextDataInterface> DataInterface;

	// Index of the first variable that implements the interface
	UPROPERTY()
	int32 VariableIndex = INDEX_NONE;

	// Number of variables that implement the interface
	UPROPERTY()
	int32 NumVariables = 0;

	// Whether to automatically bind this interface to any host data interface 
	UPROPERTY()
	bool bAutoBindToHost = false;
};

// Data interfaces provide a set of named data that is shared between AnimNext assets and used for communication between assets and functional units
UCLASS(BlueprintType)
class ANIMNEXT_API UAnimNextDataInterface : public UAnimNextRigVMAsset
{
	GENERATED_BODY()

public:
	UAnimNextDataInterface(const FObjectInitializer& ObjectInitializer);

	// Get all the implemented interfaces
	TConstArrayView<FAnimNextImplementedDataInterface> GetImplementedInterfaces() const;

	// Find an implemented interface
	const FAnimNextImplementedDataInterface* FindImplementedInterface(const UAnimNextDataInterface* InDataInterface) const;

private:
	friend class UAnimNextDataInterfaceFactory;
	friend class UE::AnimNext::Tests::FDataInterfaceCompile;
	friend struct UE::AnimNext::UncookedOnly::FUtils;

	// Information about implemented interfaces. Note this includes the 'self' interface (first), if any public variables are specified.
	UPROPERTY()
	TArray<FAnimNextImplementedDataInterface> ImplementedInterfaces;
};
