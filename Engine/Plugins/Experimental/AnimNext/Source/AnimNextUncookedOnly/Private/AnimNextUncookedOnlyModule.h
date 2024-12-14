// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAnimNextUncookedOnlyModule.h"

namespace UE::AnimNext::UncookedOnly
{
	struct FUtils;
}

namespace UE::AnimNext::UncookedOnly
{

class FModule : public IAnimNextUncookedOnlyModule
{
private:
	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// IAnimNextUncookedOnlyModule interface
	virtual void RegisterVariableBindingType(FName InStructName, TSharedPtr<IVariableBindingType> InType) override;
	virtual void UnregisterVariableBindingType(FName InStructName) override;
	virtual TSharedPtr<IVariableBindingType> FindVariableBindingType(const UScriptStruct* InInstanceIdStruct) const override;

	TMap<FName, TSharedPtr<IVariableBindingType>> VariableBindingTypes;

	friend struct FUtils;
};

}