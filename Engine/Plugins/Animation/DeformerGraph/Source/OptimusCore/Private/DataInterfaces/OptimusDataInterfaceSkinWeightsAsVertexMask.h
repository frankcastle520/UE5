// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IOptimusDeformerInstanceAccessor.h"
#include "OptimusComputeDataInterface.h"
#include "OptimusDataType.h"
#include "ComputeFramework/ComputeDataProvider.h"

#include "OptimusDataInterfaceSkinWeightsAsVertexMask.generated.h"

class FSkinWeightsAsVertexMaskDataInterfaceParameters;
class FSkeletalMeshObject;
class USkeletalMeshComponent;
class FSkeletalMeshLODRenderData;
struct FReferenceSkeleton;


/** Compute Framework Data Interface for skeletal data. */
UCLASS(Category = ComputeFramework)
class OPTIMUSCORE_API UOptimusSkinWeightsAsVertexMaskDataInterface : public UOptimusComputeDataInterface
{
	GENERATED_BODY()
	
public:
	
	//~ Begin UOptimusComputeDataInterface Interface
	FString GetDisplayName() const override;
	TArray<FOptimusCDIPinDefinition> GetPinDefinitions() const override;
	TArray<FOptimusCDIPropertyPinDefinition> GetPropertyPinDefinitions() const override;
	TSubclassOf<UActorComponent> GetRequiredComponentClass() const override;

	void RegisterPropertyChangeDelegatesForOwningNode(UOptimusNode_DataInterface* InNode) override;
	//~ End UOptimusComputeDataInterface Interface
	

	//~ Begin UComputeDataInterface Interface
	TCHAR const* GetClassName() const override { return TEXT("SkinWeightMask"); }
	void GetSupportedInputs(TArray<FShaderFunctionDefinition>& OutFunctions) const override;
	void GetShaderParameters(TCHAR const* UID, FShaderParametersMetadataBuilder& InOutBuilder, FShaderParametersMetadataAllocations& InOutAllocations) const override;
	void GetPermutations(FComputeKernelPermutationVector& OutPermutationVector) const override;
	void GetShaderHash(FString& InOutKey) const override;
	TCHAR const* GetShaderVirtualPath() const override;
	void GetHLSL(FString& OutHLSL, FString const& InDataInterfaceName) const override;
	UComputeDataProvider* CreateDataProvider(TObjectPtr<UObject> InBinding, uint64 InInputMask, uint64 InOutputMask) const override;
	//~ End UComputeDataInterface Interface
	
	UPROPERTY(EditAnywhere, Category = "Data Interface")
	FName SkinWeightProfile = NAME_None;

	// Weights of selected bones are combined to form a single a vertex weight map 
	UPROPERTY(EditAnywhere, Category = "Data Interface")
	TArray<FName> BoneNames = {TEXT("Root")};
	
	// Include all bones within the radius by expanding selection towards the root
	UPROPERTY(EditAnywhere, Category = "Data Interface")
	int32 ExpandTowardsRoot = 0;

	// Include children up to the specified depth
	UPROPERTY(EditAnywhere, Category = "Data Interface")
	int32 ExpandTowardsLeaf = 999;

	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bDebugDrawIncludedBones = true;

	UPROPERTY(EditAnywhere, Category = "Debug")
	FColor DebugDrawColor = FColor::Green;
	
private:
	friend class UOptimusSkinWeightsAsVertexMaskDataProvider;
	static FName GetSkinWeightProfilePropertyName();
	static FName GetBoneNamesPropertyName();
	static FName GetExpandTowardsRootPropertyName();
	static FName GetExpandTowardsLeafPropertyName();
	static FName GetDebugDrawIncludedBonesPropertyName();
	
	static TCHAR const* TemplateFilePath;
	FOnPinDefinitionChanged OnPinDefinitionChangedDelegate;
	FOnPinDefinitionRenamed OnPinDefinitionRenamedDelegate;
	FSimpleDelegate OnDisplayNameChangedDelegate;
};


/** Compute Framework Data Provider for reading skeletal mesh. */
UCLASS(BlueprintType, editinlinenew, Category = ComputeFramework)
class UOptimusSkinWeightsAsVertexMaskDataProvider :
	public UComputeDataProvider,
	public IOptimusDeformerInstanceAccessor

{
	GENERATED_BODY()

public:
	void Init(const UOptimusSkinWeightsAsVertexMaskDataInterface* InDataInterface, USkeletalMeshComponent* InSkeletalMesh);
	
	void SetDeformerInstance(UOptimusDeformerInstance* InInstance) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Binding)
	TObjectPtr<USkeletalMeshComponent> SkeletalMesh = nullptr;

	FName SkinWeightProfile = NAME_None;
	
	TArray<FName> BoneNames;
	int32 ExpandTowardsRoot = 0;
	int32 ExpandTowardsLeaf = 0;
	bool bDebugDrawIncludedBones = false;
	FColor DebugDrawColor = FColor::Green;

	bool bIsInitialized = false;
	TSet<int32> CachedSelectedBones;
	TArray<TArray<TArray<uint32>>> CachedBoneIsSelectedPerSectionPerLod;
	
	//~ Begin UComputeDataProvider Interface
	FComputeDataProviderRenderProxy* GetRenderProxy() override;
	//~ End UComputeDataProvider Interface


private:
	UPROPERTY()
	TObjectPtr<UOptimusDeformerInstance> DeformerInstance;

	TWeakObjectPtr<const UOptimusSkinWeightsAsVertexMaskDataInterface> WeakDataInterface;
};

class FOptimusSkinWeightsAsVertexMaskDataProviderProxy : public FComputeDataProviderRenderProxy
{
public:
	FOptimusSkinWeightsAsVertexMaskDataProviderProxy() = default;

	//~ Begin FComputeDataProviderRenderProxy Interface
	bool IsValid(FValidationData const& InValidationData) const override;
	void AllocateResources(FRDGBuilder& GraphBuilder, FAllocationData const& InAllocationData) override;
	void GatherPermutations(FPermutationData& InOutPermutationData) const override;
	void GatherDispatchData(FDispatchData const& InDispatchData) override;
	//~ End FComputeDataProviderRenderProxy Interface

	using FParameters = FSkinWeightsAsVertexMaskDataInterfaceParameters;

	FSkeletalMeshObject* SkeletalMeshObject = nullptr;
	
	FName SkinWeightProfile = NAME_None;

	TArray<TArray<TArray<uint32>>> BoneIsSelectedPerSectionPerLod;
	TArray<FRDGBufferRef> BoneIsSelectedBuffersPerSection;
	TArray<FRDGBufferSRVRef> BoneIsSelectedBufferSRVsPerSection;
};
