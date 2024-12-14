// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataInterface/AnimNextDataInterface.h"
#include "Param/ParamType.h"
#include "RigVMCore/RigVMExecuteContext.h"
#include "StructUtils/PropertyBag.h"
#include "AnimNextDataInterfaceInstance.generated.h"

class UAnimNextDataInterface;

// Base struct for data interface-derived instances
USTRUCT()
struct ANIMNEXT_API FAnimNextDataInterfaceInstance
{
	GENERATED_BODY()

	// Get the data interface asset that this instance represents
	const UAnimNextDataInterface* GetDataInterface() const
	{
		return DataInterface;
	}

	// Safely get the name of the data interface that this host provides
	FName GetDataInterfaceName() const
	{
		return DataInterface ? DataInterface->GetFName() : NAME_None;
	}

	// Get the property bag that holds external variables for this instance
	const FInstancedPropertyBag& GetVariables() const
	{
		return Variables;
	}

	// Get the RigVM extended execute context
	FRigVMExtendedExecuteContext& GetExtendedExecuteContext()
	{
		return ExtendedExecuteContext;
	}

	// Helper function used for bindings
	// Get the memory for the supplied variable, at the specified index
	// @param    InVariableIndex    The index into the data interface of the variable
	// @param    InVariableName     The name of the variable
	// @param    InVariableProperty The property of the variable
	uint8* GetMemoryForVariable(int32 InVariableIndex, FName InVariableName, const FProperty* InVariableProperty) const;

	// Get a variable's value given its name.
	// @param	InVariableName		The name of the variable to get he value of
	// @param	OutResult			Result that will be filled if no errors occur
	// @return nullptr if the variable is not present
	template<typename ValueType>
	EPropertyBagResult GetVariable(FName InVariableName, ValueType& OutResult) const
	{
		return GetVariableInternal(InVariableName, FAnimNextParamType::GetType<ValueType>(), TArrayView<uint8>(reinterpret_cast<uint8*>(&OutResult), sizeof(ValueType)));
	}

private:
	// Helper function for GetVariable
	EPropertyBagResult GetVariableInternal(FName InVariableName, const FAnimNextParamType& InType, TArrayView<uint8> OutResult) const;

protected:
	// Hard reference to the asset used to create this instance to ensure we can release it safely
	UPROPERTY(Transient)
	TObjectPtr<const UAnimNextDataInterface> DataInterface;

	// User variables used to operate the graph
	UPROPERTY(Transient)
	FInstancedPropertyBag Variables;

	// Extended execute context instance for this graph instance, we own it
	UPROPERTY(Transient)
	FRigVMExtendedExecuteContext ExtendedExecuteContext;
};
