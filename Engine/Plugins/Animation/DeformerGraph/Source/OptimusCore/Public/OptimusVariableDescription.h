// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusDataType.h"

#include "CoreMinimal.h"
#include "OptimusValueContainerStruct.h"
#include "UObject/NameTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "OptimusVariableDescription.generated.h"


class UOptimusDeformer;
class UOptimusValueContainer;


USTRUCT()
struct FOptimusVariableMetaDataEntry
{
	GENERATED_BODY()

	FOptimusVariableMetaDataEntry() {}
	FOptimusVariableMetaDataEntry(FName InKey, FString&& InValue)
	    : Key(InKey), Value(MoveTemp(InValue))
	{}

	/** Name of metadata key */
	UPROPERTY(EditAnywhere, Category = VariableMetaDataEntry)
	FName Key;

	/** Name of metadata value */
	UPROPERTY(EditAnywhere, Category = VariableMetaDataEntry)
	FString Value;
};


UCLASS(BlueprintType)
class OPTIMUSCORE_API UOptimusVariableDescription : 
	public UObject
{
	GENERATED_BODY()
public:
	/** 
	 * Set the data type, and recreate the backing data storage as well
	 */
	void SetDataType(FOptimusDataTypeRef InDataType);

	/** Returns the owning deformer to operate on this variable */
	// FIXME: Move to interface-based system.
	UOptimusDeformer* GetOwningDeformer() const;

	/** Returns the index of the variable within the container */ 
	int32 GetIndex() const;

	/** An identifier that uniquely identifies this variable */
	UPROPERTY()
	FGuid Guid;

	/** Name of the variable */
	UPROPERTY(EditAnywhere, Category = VariableDefinition)
	FName VariableName;

	/** The data type of the variable */
	UPROPERTY(EditAnywhere, Category = VariableDefinition, meta=(UseInVariable, UseInProperty))
	FOptimusDataTypeRef DataType;

	/** The default value for the variable. */
	UPROPERTY(EditAnywhere, Category = VariableDefinition)
	FOptimusValueContainerStruct DefaultValueStruct;
	
	/**	Runtime container for variable values in a deformer instance*/
	UPROPERTY(Transient)
	FShaderValueContainer CachedShaderValue;

	void PostLoad();
#if WITH_EDITOR
	void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	void PreEditUndo() override;
	void PostEditUndo() override;
#endif

private:

	// Deprecated 
    UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Use CachedShaderValue instead"))
    TArray<uint8> ValueData_DEPRECATED;

	// Deprecated 
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "use DefaultValueStruct instead"))
	TObjectPtr<UOptimusValueContainer> DefaultValue_DEPRECATED = nullptr;
	
#if WITH_EDITORONLY_DATA
	FName VariableNameForUndo;
#endif
};
