// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IOptimusDeformerInstanceAccessor.h"
#include "OptimusComputeDataInterface.h"
#include "ComputeFramework/ComputeDataProvider.h"

#include "OptimusDataInterfaceSkinnedMeshRead.generated.h"

class FRDGBuffer;
class FRDGBufferSRV;
class FSkeletalMeshObject;
class FSkinnedMeshReadDataInterfaceParameters;
class USkinnedMeshComponent;
enum class EMeshDeformerOutputBuffer : uint8;

/** Compute Framework Data Interface for writing skinned mesh. */
UCLASS(Category = ComputeFramework)
class OPTIMUSCORE_API UOptimusSkinnedMeshReadDataInterface :
	public UOptimusComputeDataInterface
{
	GENERATED_BODY()

public:
	static const TCHAR* ReadableOutputBufferPermutationName;
	//~ Begin UOptimusComputeDataInterface Interface
	FString GetDisplayName() const override;
	FName GetCategory() const override;
	TArray<FOptimusCDIPinDefinition> GetPinDefinitions() const override;
	TSubclassOf<UActorComponent> GetRequiredComponentClass() const override;
	//~ End UOptimusComputeDataInterface Interface
	
	//~ Begin UComputeDataInterface Interface
	TCHAR const* GetClassName() const override { return TEXT("SkinnedMeshRead"); }
	bool CanSupportUnifiedDispatch() const override { return true; }
	void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const override;
	TCHAR const* GetShaderVirtualPath() const override;
	void GetShaderHash(FString& InOutKey) const override;
	void GetHLSL(FString& OutHLSL, FString const& InDataInterfaceName) const override;
	void GetDefines(FComputeKernelDefinitionSet& OutDefinitionSet) const override;
	void GetPermutations(FComputeKernelPermutationVector& OutPermutationVector) const override;
	UComputeDataProvider* CreateDataProvider(TObjectPtr<UObject> InBinding, uint64 InInputMask, uint64 InOutputMask) const override;
	//~ End UComputeDataInterface Interface

private:
	static TCHAR const* TemplateFilePath;
};

/** Compute Framework Data Provider for writing skinned mesh. */
UCLASS(BlueprintType, editinlinenew, Category = ComputeFramework)
class UOptimusSkinnedMeshReadDataProvider :
	public UComputeDataProvider,
	public IOptimusDeformerInstanceAccessor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Binding)
	TObjectPtr<USkinnedMeshComponent> SkinnedMesh = nullptr;

	uint64 InputMask = 0;

	// Served as persistent storage for the provider proxy, should not be used by the data provider itself
	int32 LastLodIndexCachedByRenderProxy = 0;

	//~ Begin UComputeDataProvider Interface
	FComputeDataProviderRenderProxy* GetRenderProxy() override;
	//~ End UComputeDataProvider Interface

	//~ Begin IOptimusDeformerInstanceAccessor Interface
	void SetDeformerInstance(UOptimusDeformerInstance* InInstance) override;
	//~ End IOptimusDeformerInstanceAccessor Interface

private:
	UPROPERTY()
	TObjectPtr<UOptimusDeformerInstance> DeformerInstance = nullptr;
};

class FOptimusSkinnedMeshReadDataProviderProxy : public FComputeDataProviderRenderProxy
{
public:
	FOptimusSkinnedMeshReadDataProviderProxy(USkinnedMeshComponent* InSkinnedMeshComponent, uint64 InInputMask, EMeshDeformerOutputBuffer InOutputBuffersWithValidData, int32* InLastLodIndexPtr);

	//~ Begin FComputeDataProviderRenderProxy Interface
	bool IsValid(FValidationData const& InValidationData) const override;
	void AllocateResources(FRDGBuilder& GraphBuilder) override;
	void GatherPermutations(FPermutationData& InOutPermutationData) const override;
	void GatherDispatchData(FDispatchData const& InDispatchData) override;
	//~ End FComputeDataProviderRenderProxy Interface

private:
	using FParameters = FSkinnedMeshReadDataInterfaceParameters;

	FSkeletalMeshObject* SkeletalMeshObject = nullptr;
	uint64 InputMask = 0;
	int32* LastLodIndexPtr = nullptr;
	EMeshDeformerOutputBuffer OutputBuffersFromPreviousInstances;

	// Using UAV here because we might be reading and writing to these buffers in the same kernel
	// if we have a setup like Read -> Kernel -> Write 
	FRDGBufferUAV* PositionBufferUAV = nullptr;
	FRDGBufferUAV* TangentBufferUAV = nullptr;
	FRDGBufferUAV* ColorBufferUAV = nullptr;
};
