// Copyright Epic Games, Inc. All Rights Reserved.
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetalRHIPrivate.h"

#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
#include "MetalResources.h"
#include "MetalShaderResources.h"
#include "Containers/DynamicRHIResourceArray.h"

class FMetalCommandEncoder;
class FMetalDevice;

struct FMetalDescriptorHeap
{
                                    FMetalDescriptorHeap(FMetalDevice& MetalDevice, const ERHIDescriptorHeapType DescriptorType);

    void                            Init(const int32 HeapSize);

    FRHIDescriptorHandle            ReserveDescriptor();
    void                            FreeDescriptor(FRHIDescriptorHandle DescriptorHandle);
    uint32                          GetFreeResourceIndex();

    void                            UpdateDescriptor(FRHIDescriptorHandle DescriptorHandle, struct IRDescriptorTableEntry DescriptorData);
    void                            BindHeap(FMetalCommandEncoder* Encoder, MTL::FunctionType FunctionType, const uint32 BindIndex);
    
    //
	FMetalDevice& 					Device;
	
    FCriticalSection                FreeListCS;
    TQueue<uint32>                  FreeList;

    std::atomic<uint32>             PeakDescriptorCount;
    struct IRDescriptorTableEntry*  Descriptors;
    uint32_t                        ResourceHeapLength;
    FBufferRHIRef			        ResourceHeap;
	
	TResourceArray<IRDescriptorTableEntry>    ComputeDescriptorEntries;
	TResourceArray<uint32_t>                  ComputeDescriptorIndices;
	bool									  DescriptorsDirty;
	uint32_t								  MinDirtyIndex;
	uint32_t								  MaxDirtyIndex;
    
    const ERHIDescriptorHeapType    Type;
};

enum EDescriptorUpdateType
{
    EDescriptorUpdateType_Immediate,
    EDescriptorUpdateType_GPU
};

class FMetalBindlessDescriptorManager
{
public:
                            FMetalBindlessDescriptorManager(FMetalDevice& MetalDevice);
                            ~FMetalBindlessDescriptorManager();

    void                    Init();

    FRHIDescriptorHandle    ReserveDescriptor(ERHIDescriptorHeapType InType);
    void                    FreeDescriptor(FRHIDescriptorHandle DescriptorHandle);

    void                    BindSampler(FRHIDescriptorHandle DescriptorHandle, MTL::SamplerState* Sampler);
    void                    BindResource(FRHIDescriptorHandle DescriptorHandle, FMetalResourceViewBase* Resource);
    void                    BindTexture(FRHICommandListBase& RHICmdList, FRHIDescriptorHandle DescriptorHandle, MTL::Texture* Texture, EDescriptorUpdateType UpdateType);

    void                    BindDescriptorHeapsToEncoder(FMetalCommandEncoder* Encoder, MTL::FunctionType FunctionType, EMetalShaderStages Frequency);

	bool					IsSupported() {return bIsSupported;}
    
	void 					UpdateDescriptorsWithGPU(FMetalRHICommandContext* Context);
    
private:
	void                    UpdateDescriptorsWithCompute();
	void 					UpdateDescriptorsWithCopy(FMetalRHICommandContext* Context);
	
	bool 					bIsSupported = false;
	FMetalDevice& 			Device;
    FMetalDescriptorHeap    StandardResources;
    FMetalDescriptorHeap    SamplerResources;

    FCriticalSection        ComputeDescriptorCS;
};

#endif
