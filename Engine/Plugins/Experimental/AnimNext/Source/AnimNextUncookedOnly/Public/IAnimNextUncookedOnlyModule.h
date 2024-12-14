// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

template<typename T> struct TInstancedStruct;

namespace UE::AnimNext::UncookedOnly
{
	class IVariableBindingType;
}

namespace UE::AnimNext::UncookedOnly
{

class IAnimNextUncookedOnlyModule : public IModuleInterface
{
public:
	// Register a variable binding type, used to query information and process variable bindings
	// @param   InStructName     The full path name of the struct type for this variable binding's data (must be a child struct of FAnimNextVariableBindingData)
	// @param   InType           The type to register
	virtual void RegisterVariableBindingType(FName InStructName, TSharedPtr<IVariableBindingType> InType) = 0;
	
	// Unregister a variable binding type previously passed to RegisterVariableBindingType
	// @param   InStructName     The full path name of the struct type for this variable binding's data
	virtual void UnregisterVariableBindingType(FName InStructName) = 0;

	// Find a variable binding type previously passed to RegisterVariableBindingType
	// @param   InStruct         The struct type for this variable binding's data (must be a child struct of FAnimNextVariableBindingData)
	virtual TSharedPtr<IVariableBindingType> FindVariableBindingType(const UScriptStruct* InInstanceIdStruct) const = 0;
};

}
