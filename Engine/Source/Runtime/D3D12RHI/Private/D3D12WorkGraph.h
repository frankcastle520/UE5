// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "D3D12RHICommon.h"
#include "RHIResources.h"

class FD3D12Buffer;
class FD3D12Device;
class FD3D12WorkGraphShader;
class FWorkGraphPipelineStateInitializer;

struct FD3D12WorkGraphPipelineState : public FRHIWorkGraphPipelineState
{
public:
	UE_NONCOPYABLE(FD3D12WorkGraphPipelineState)

	FD3D12WorkGraphPipelineState(FD3D12Device* Device, const FWorkGraphPipelineStateInitializer& Initializer);

	TRefCountPtr<FD3D12WorkGraphShader>	Shader;
	TArray<TRefCountPtr<FD3D12WorkGraphShader>> LocalNodeShaders;

#if D3D12_RHI_WORKGRAPHS
	TRefCountPtr<ID3D12StateObject>	StateObject;
	D3D12_PROGRAM_IDENTIFIER ProgramIdentifier = {};
	
	D3D12_GPU_VIRTUAL_ADDRESS_RANGE BackingMemoryAddressRange = {};

	uint32 RootArgStrideInBytes = 0;
	uint32 MaxRootArgOffset = 0;
	TArray<uint32> RootArgOffsets;

	bool bInitialized = false;
#endif
};
