// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Units/RigUnit.h"
#include "RigVMCore/RigVMTrait.h"
#include "OptimusDeformer.h"
#include "OptimusDeformerDynamicInstanceManager.h"
#include "RigUnit_Optimus.generated.h"

USTRUCT()
struct OPTIMUSCORE_API FRigVMTrait_OptimusDeformer: public FRigVMTrait
{
	GENERATED_BODY()

	FRigVMTrait_OptimusDeformer() = default;
	FString GetDisplayName() const override;
#if WITH_EDITOR
	bool ShouldCreatePinForProperty(const FProperty* InProperty) const override;
#endif

	UPROPERTY(EditAnywhere, Category = "Deformer Graph")
	TSoftObjectPtr<UOptimusDeformer> DeformerGraph;
};


USTRUCT()
struct OPTIMUSCORE_API FRigVMTrait_OptimusDeformerSettings: public FRigVMTrait
{
	GENERATED_BODY()

	FRigVMTrait_OptimusDeformerSettings() = default;

	UPROPERTY(EditAnywhere, Category = "Trait", meta=(Input))
	EOptimusDeformerExecutionPhase ExecutionPhase = EOptimusDeformerExecutionPhase::AfterDefaultDeformer;
	
	// Deformers are first sorted by execution group index, then by the order in which they are added
	UPROPERTY(EditAnywhere, Category = "Trait", meta=(Input))
	int32 ExecutionGroup = 1;

	// Whether to apply the deformer to all child components as well
	UPROPERTY(EditAnywhere, Category = "Trait", meta=(Input))
	bool DeformChildComponents = true;

	// Deformer won't be applied to child components that have the specified component tag
	UPROPERTY(EditAnywhere, Category = "Trait", meta=(Input))
	FName ExcludeChildComponentsWithTag = NAME_None;
};

/** Adds a deformer to the Skeletal Mesh Component*/
USTRUCT(meta = (DisplayName = "Add Deformer", Category = "Deformer Graph", NodeColor="1 1 1"))
struct OPTIMUSCORE_API FRigUnit_AddOptimusDeformer: public FRigUnitMutable
{
	GENERATED_BODY()
	
	static const TCHAR* DeformerTraitName;
	static const TCHAR* DeformerSettingsTraitName;

	static bool IsVariableTraitName(const FString& InTraitName);

	FRigUnit_AddOptimusDeformer() = default;
	
	void OnUnitNodeCreated(FRigVMUnitNodeCreatedContext& InContext) const override;
	TArray<FRigVMUserWorkflow> GetSupportedWorkflows(const UObject* InSubject) const override;
	
	/** Execute logic for this rig unit */
	RIGVM_METHOD()
	virtual void Execute() override;

	UPROPERTY(meta=(Hidden))
	FGuid DeformerInstanceGuid;
};



USTRUCT()
struct OPTIMUSCORE_API FRigVMTrait_OptimusVariableBase: public FRigVMTrait
{
	GENERATED_BODY()

	virtual void SetValue(UOptimusDeformerInstance* InInstance) const {};
};

USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerIntVariable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	int32 Value = 0;	
};

USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerIntArrayVariable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	TArray<int32> Value;	
};

USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerInt2Variable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	FIntPoint Value = FIntPoint::ZeroValue;	
};

USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerInt2ArrayVariable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	TArray<FIntPoint> Value;	
};

USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerInt3Variable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	FIntVector Value = FIntVector::ZeroValue;	
};

USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerInt3ArrayVariable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	TArray<FIntVector> Value;	
};

USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerInt4Variable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	FIntVector4 Value = FIntVector4::ZeroValue;	
};

USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerInt4ArrayVariable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	TArray<FIntVector4> Value;	
};




USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerFloatVariable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()
	
	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	double Value = 0.0;	
};

USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerFloatArrayVariable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()
	
	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	TArray<double> Value;	
};

USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerVector2Variable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	FVector2D Value = FVector2D::ZeroVector;	
};


USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerVector2ArrayVariable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	TArray<FVector2D> Value;	
};

USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerVectorVariable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	FVector Value = FVector::ZeroVector; 
};

USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerVectorArrayVariable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	TArray<FVector> Value;	
};


USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerVector4Variable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	FVector4 Value = FVector4::Zero();	
};

USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerVector4ArrayVariable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	TArray<FVector4> Value;	
};

USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerLinearColorVariable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	FLinearColor Value = FLinearColor::Black;	
};

USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerLinearColorArrayVariable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	TArray<FLinearColor> Value;	
};


USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerQuatVariable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	FQuat Value = FQuat::Identity;	
};


USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerQuatArrayVariable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	TArray<FQuat> Value;	
};

USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerRotatorVariable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	FRotator Value = FRotator::ZeroRotator;	
};

USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerRotatorArrayVariable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	TArray<FRotator> Value;	
};

USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerTransformVariable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	FTransform Value = FTransform::Identity;	
};

USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerTransformArrayVariable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	TArray<FTransform> Value;	
};

USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerNameVariable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	FName Value = NAME_None;	
};

USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerNameArrayVariable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	TArray<FName> Value;	
};

USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerBoolVariable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	bool Value = false;	
};

USTRUCT(BlueprintType)
struct OPTIMUSCORE_API FRigVMTrait_SetDeformerBoolArrayVariable: public FRigVMTrait_OptimusVariableBase
{
	GENERATED_BODY()

	void SetValue(UOptimusDeformerInstance* InInstance) const override;
	
	UPROPERTY(EditAnywhere, Category = "Trait")
	TArray<bool> Value;	
};
