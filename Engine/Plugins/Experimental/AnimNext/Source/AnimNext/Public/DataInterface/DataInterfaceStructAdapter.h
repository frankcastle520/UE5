// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataInterface/AnimNextDataInterfaceHost.h"

namespace UE::AnimNext
{

// Allows a struct to host a data interface's variables
struct ANIMNEXT_API FDataInterfaceStructAdapter : public IDataInterfaceHost
{
public:
	explicit FDataInterfaceStructAdapter(const UAnimNextDataInterface* InDataInterface, FStructView InStructView)
		: DataInterface(InDataInterface)
		, StructView(InStructView)
	{}

private:
	// IDataInterfaceHost interface
	virtual const UAnimNextDataInterface* GetDataInterface() const override;
	virtual uint8* GetMemoryForVariable(int32 InVariableIndex, FName InVariableName, const FProperty* InVariableProperty) const override;

	// The data interface we wrap
	const UAnimNextDataInterface* DataInterface;

	// The struct we host
	FStructView StructView;
};

}