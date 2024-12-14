// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataInterface/AnimNextDataInterface.h"

struct FAnimNextGraphInstance;

namespace UE::AnimNext
{

// Adapter allowing external systems to implement a data interface
// Used at instantiation time (and in editor, re-binding after compilation time) to bind directly to a host's memory for an interfaces variables
class ANIMNEXT_API IDataInterfaceHost
{
public:
	virtual ~IDataInterfaceHost() = default;

protected:
	// Safely get the name of the data interface that this host provides
	FName GetDataInterfaceName() const
	{
		const UAnimNextDataInterface* DataInterface = GetDataInterface();
		return DataInterface != nullptr ? DataInterface->GetFName() : NAME_None;
	}

private:
	// Get the data interface asset that this host represents
	virtual const UAnimNextDataInterface* GetDataInterface() const = 0;

	// Get the memory for the supplied variable, at the specified index
	// @param    InVariableIndex    The index into the data interface of the variable
	// @param    InVariableName     The name of the variable
	// @param    InVariableProperty The property of the variable
	virtual uint8* GetMemoryForVariable(int32 InVariableIndex, FName InVariableName, const FProperty* InVariableProperty) const = 0;

	friend struct ::FAnimNextGraphInstance;
};

}