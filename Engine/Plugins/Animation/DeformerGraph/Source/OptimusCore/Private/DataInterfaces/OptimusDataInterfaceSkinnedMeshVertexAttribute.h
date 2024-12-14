// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IOptimusDeformerInstanceAccessor.h"
#include "OptimusComputeDataInterface.h"

#include "ComputeFramework/ComputeDataProvider.h"

#include "OptimusDataInterfaceSkinnedMeshVertexAttribute.generated.h"


class FSkeletalMeshObject;
class FSkinnedMeshVertexAttributeDataInterfaceParameters;


UCLASS(Category = ComputeFramework)
class UOptimusSkinnedMeshVertexAttributeDataInterface :
	public UOptimusComputeDataInterface
{
	GENERATED_BODY()
	
	//~ Begin UOptimusComputeDataInterface Interface
	FString GetDisplayName() const override;
	TArray<FOptimusCDIPinDefinition> GetPinDefinitions() const override;
	TArray<FOptimusCDIPropertyPinDefinition> GetPropertyPinDefinitions() const override;
	TSubclassOf<UActorComponent> GetRequiredComponentClass() const override;
	//~ End UOptimusComputeDataInterface Interface

	//~ Begin UComputeDataInterface Interface
	TCHAR const* GetClassName() const override { return TEXT("VertexAttribute"); }
	virtual bool CanSupportUnifiedDispatch() const override { return true; };
	virtual void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const override;
	void GetHLSL(FString& OutHLSL, FString const& InDataInterfaceName) const override;
	void GetShaderHash(FString& InOutKey) const override;
	UComputeDataProvider* CreateDataProvider(TObjectPtr<UObject> InBinding, uint64 InInputMask, uint64 InOutputMask) const override;
	//~ End UComputeDataInterface Interface

	UPROPERTY(EditAnywhere, Category="Vertex Attribute")
	FName AttributeName;
private:
	friend class UOptimusSkinnedMeshVertexAttributeDataProvider;
	static FName GetAttributeNamePropertyName();
};


/** Compute Framework Data Provider for reading skeletal mesh. */
UCLASS(BlueprintType, editinlinenew, Category = ComputeFramework)
class UOptimusSkinnedMeshVertexAttributeDataProvider :
	public UComputeDataProvider,
	public IOptimusDeformerInstanceAccessor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Binding)
	TObjectPtr<USkinnedMeshComponent> SkinnedMeshComponent = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = VertexAttribute)
	FName AttributeName;

    TWeakObjectPtr<const UOptimusSkinnedMeshVertexAttributeDataInterface> WeakDataInterface;
	
	//~ Begin UComputeDataProvider Interface
	bool IsValid() const override;
	FComputeDataProviderRenderProxy* GetRenderProxy() override;
	//~ End UComputeDataProvider Interface

	void SetDeformerInstance(UOptimusDeformerInstance* InInstance) override;

	
private:
	UPROPERTY()
	TObjectPtr<UOptimusDeformerInstance> DeformerInstance;
};

class FOptimusSkinnedMeshVertexAttributeDataProviderProxy : public FComputeDataProviderRenderProxy
{
public:
	FOptimusSkinnedMeshVertexAttributeDataProviderProxy(
		USkinnedMeshComponent* InSkinnedMeshComponent,
		FName InAttributeName);

	//~ Begin FComputeDataProviderRenderProxy Interface
	bool IsValid(FValidationData const& InValidationData) const override;
	void GatherDispatchData(FDispatchData const& InDispatchData) override;
	//~ End FComputeDataProviderRenderProxy Interface

private:
	using FParameters = FSkinnedMeshVertexAttributeDataInterfaceParameters;
	
	FSkeletalMeshObject* SkeletalMeshObject;
	FName AttributeName;
};
