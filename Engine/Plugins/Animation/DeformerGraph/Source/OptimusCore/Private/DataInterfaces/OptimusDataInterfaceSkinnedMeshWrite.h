// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IOptimusOutputBufferWriter.h"
#include "OptimusComputeDataInterface.h"
#include "ComputeFramework/ComputeDataProvider.h"

#include "OptimusDataInterfaceSkinnedMeshWrite.generated.h"

class FRDGBuffer;
class FRDGBufferUAV;
class FSkeletalMeshObject;
class FSkinedMeshWriteDataInterfaceParameters;
class USkinnedMeshComponent;

/** Compute Framework Data Interface for writing skinned mesh. */
UCLASS(Category = ComputeFramework)
class OPTIMUSCORE_API UOptimusSkinnedMeshWriteDataInterface :
	public UOptimusComputeDataInterface,
	public IOptimusOutputBufferWriter
{
	GENERATED_BODY()

public:
	//~ Begin UOptimusComputeDataInterface Interface
	FString GetDisplayName() const override;
	FName GetCategory() const override;
	TArray<FOptimusCDIPinDefinition> GetPinDefinitions() const override;
	TSubclassOf<UActorComponent> GetRequiredComponentClass() const override;
	//~ End UOptimusComputeDataInterface Interface
	
	//~ Begin UComputeDataInterface Interface
	TCHAR const* GetClassName() const override { return TEXT("SkinnedMeshWrite"); }
	bool CanSupportUnifiedDispatch() const override { return true; }
	void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetSupportedOutputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const override;
	TCHAR const* GetShaderVirtualPath() const override;
	void GetShaderHash(FString& InOutKey) const override;
	void GetHLSL(FString& OutHLSL, FString const& InDataInterfaceName) const override;
	UComputeDataProvider* CreateDataProvider(TObjectPtr<UObject> InBinding, uint64 InInputMask, uint64 InOutputMask) const override;
	//~ End UComputeDataInterface Interface

	//~ Begin IOptimusOutputBufferWriter Interface
	EMeshDeformerOutputBuffer GetOutputBuffer(int32 InBoundOutputFunctionIndex) const override;
	//~ End IOptimusOutputBufferWriter Interface 
private:
	static TCHAR const* TemplateFilePath;
};

/** Compute Framework Data Provider for writing skinned mesh. */
UCLASS(BlueprintType, editinlinenew, Category = ComputeFramework)
class UOptimusSkinnedMeshWriteDataProvider : public UComputeDataProvider
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Binding)
	TObjectPtr<USkinnedMeshComponent> SkinnedMesh = nullptr;

	uint64 OutputMask = 0;

	// Served as persistent storage for the provider proxy, should not be used by the data provider itself
	int32 LastLodIndexCachedByRenderProxy = 0;

	//~ Begin UComputeDataProvider Interface
	FComputeDataProviderRenderProxy* GetRenderProxy() override;
	//~ End UComputeDataProvider Interface
};

class FOptimusSkinnedMeshWriteDataProviderProxy : public FComputeDataProviderRenderProxy
{
public:
	FOptimusSkinnedMeshWriteDataProviderProxy(USkinnedMeshComponent* InSkinnedMeshComponent, uint64 InOutputMask, int32* InLastLodIndexPtr);

	//~ Begin FComputeDataProviderRenderProxy Interface
	bool IsValid(FValidationData const& InValidationData) const override;
	void AllocateResources(FRDGBuilder& GraphBuilder) override;
	void GatherDispatchData(FDispatchData const& InDispatchData) override;
	//~ End FComputeDataProviderRenderProxy Interface

private:
	using FParameters = FSkinedMeshWriteDataInterfaceParameters;

	FSkeletalMeshObject* SkeletalMeshObject = nullptr;
	uint64 OutputMask = 0;
	int32* LastLodIndexPtr = nullptr; 

	FRDGBuffer* PositionBuffer = nullptr;
	FRDGBufferUAV* PositionBufferUAV = nullptr;
	FRDGBuffer* TangentBuffer = nullptr;
	FRDGBufferUAV* TangentBufferUAV = nullptr;
	FRDGBuffer* ColorBuffer = nullptr;
	FRDGBufferUAV* ColorBufferUAV = nullptr;
};
