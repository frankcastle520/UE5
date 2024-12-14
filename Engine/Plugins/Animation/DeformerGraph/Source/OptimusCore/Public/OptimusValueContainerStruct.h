// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5
#include "CoreMinimal.h"
#endif

#include "OptimusDataType.h"
#include "StructUtils/PropertyBag.h"
#include "OptimusValueContainerStruct.generated.h"

USTRUCT()
struct OPTIMUSCORE_API FOptimusValueContainerStruct
{
	GENERATED_BODY()

	static const TCHAR* ValuePropertyName;
	
	UPROPERTY(EditAnywhere, Category="Value", meta=(FixedLayout, ShowOnlyInnerProperties))
	FInstancedPropertyBag Value;

	bool IsInitialized() const;
	void SetType(FOptimusDataTypeRef InDataType);
	void SetValue(FOptimusDataTypeRef InDataType, TArrayView<const uint8> InValue);
	FShaderValueContainer GetShaderValue(FOptimusDataTypeRef InDataType) const;
	FString GetValueAsString() const;
	
private:
	const FProperty* GetValueProperty() const;
	const uint8* GetValueMemory() const;

};
