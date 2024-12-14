// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "StructUtils/PropertyBag.h"
#include "RigVMTraitDefaultValueStruct.generated.h"

USTRUCT()
struct FRigVMTraitDefaultValueStruct
{
	GENERATED_BODY()

	static const TCHAR* DefaultValuePropertyName;

	UPROPERTY()
	FInstancedPropertyBag PropertyBag;
	
	void Init(UScriptStruct* InTraitScriptStruct);
	void SetValue(const FString& InDefaultValue);
	FString GetValue() const;
};