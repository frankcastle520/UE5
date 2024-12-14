// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataInterface/AnimNextDataInterfaceHost.h"
#include "RigVMCore/RigVMTrait.h"
#include "RigVMTrait_AnimNextPublicVariables.generated.h"

class UAnimNextRigVMAsset;
class FRigVMTraitScope;

// Represents public variables of an asset via a trait 
USTRUCT(BlueprintType)
struct ANIMNEXT_API FRigVMTrait_AnimNextPublicVariables : public FRigVMTrait
{
	GENERATED_BODY()

	// The data interface that any programmatic pins will be derived from
	UPROPERTY(meta = (Hidden))
	TObjectPtr<UAnimNextDataInterface> Asset = nullptr;

	// Variable names that are exposed
	UPROPERTY(meta = (Hidden))
	TArray<FName> VariableNames;

	// FRigVMTrait interface
#if WITH_EDITOR
	virtual FString GetDisplayName() const override;
	virtual void GetProgrammaticPins(URigVMController* InController, int32 InParentPinIndex, const FString& InDefaultValue, FRigVMPinInfoArray& OutPinArray) const override;
	virtual bool ShouldCreatePinForProperty(const FProperty* InProperty) const override;
#endif
};

namespace UE::AnimNext
{

struct FPublicVariablesTraitToDataInterfaceHostAdapter : public IDataInterfaceHost
{
	FPublicVariablesTraitToDataInterfaceHostAdapter(const FRigVMTrait_AnimNextPublicVariables& InTrait, const FRigVMTraitScope& InTraitScope)
		: Trait(InTrait)
		, TraitScope(InTraitScope)
	{}

	// IDataInterfaceHost interface
	virtual const UAnimNextDataInterface* GetDataInterface() const override;
	virtual uint8* GetMemoryForVariable(int32 InVariableIndex, FName InVariableName, const FProperty* InVariableProperty) const override;

	const FRigVMTrait_AnimNextPublicVariables& Trait;
	const FRigVMTraitScope& TraitScope;
};

}