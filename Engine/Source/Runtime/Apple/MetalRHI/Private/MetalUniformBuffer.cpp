// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalConstantBuffer.cpp: Metal Constant buffer implementation.
=============================================================================*/

#include "MetalUniformBuffer.h"
#include "MetalRHIPrivate.h"
#include "MetalTempAllocator.h"
#include "MetalDevice.h"
#include "ShaderParameterStruct.h"
#include "RHIUniformBufferDataShared.h"

#pragma mark Suballocated Uniform Buffer Implementation

FMetalSuballocatedUniformBuffer::FMetalSuballocatedUniformBuffer(FMetalDevice& InDevice, const void *Contents, const FRHIUniformBufferLayout* Layout,
																EUniformBufferUsage Usage, EUniformBufferValidation InValidation)
    : FRHIUniformBuffer(Layout)
    , LastFrameUpdated(0)
    , Shadow(FMemory::Malloc(GetSize()))
	, Device(InDevice)
#if METAL_UNIFORM_BUFFER_VALIDATION
    , Validation(InValidation)
#endif // METAL_UNIFORM_BUFFER_VALIDATION
{
	if (Contents)
	{
        UE::RHICore::UpdateUniformBufferConstants(Shadow, Contents, GetLayout());
		CopyResourceTable(Contents, ResourceTable);
	}
}

FMetalSuballocatedUniformBuffer::~FMetalSuballocatedUniformBuffer()
{
	if(BackingBuffer)
	{
		BackingBuffer = nullptr;
	}
	
	FMemory::Free(Shadow);
}

void FMetalSuballocatedUniformBuffer::Update(const void* Contents)
{
    UE::RHICore::UpdateUniformBufferConstants(Shadow, Contents, GetLayout());
	CopyResourceTable(Contents, ResourceTable);
	PushToGPUBacking(Shadow);
}

// Acquires a region in the current frame's uniform buffer and
// pushes the data in Contents into that GPU backing store
// The amount of data read from Contents is given by the Layout
void FMetalSuballocatedUniformBuffer::PushToGPUBacking(const void* Contents)
{    
	FMetalTempAllocator* Allocator = Device.GetUniformAllocator();
	FMetalBufferPtr Buffer = Allocator->Allocate(GetSize());
	// copy contents into backing
	BackingBuffer = Buffer;
	
	uint8* ConstantSpace = reinterpret_cast<uint8*>(Buffer->Contents());
	FMemory::Memcpy(ConstantSpace, Contents, GetSize());
	LastFrameUpdated = Device.GetFrameNumberRHIThread();
}

// Because we can create a uniform buffer on frame N and may not bind it until frame N+10
// we need to keep a copy of the most recent data. Then when it's time to bind this
// uniform buffer we can push the data into the GPU backing.
void FMetalSuballocatedUniformBuffer::PrepareToBind()
{
    if(!LastFrameUpdated || LastFrameUpdated < Device.GetFrameNumberRHIThread())
    {
        PushToGPUBacking(Shadow);
    }
}

void FMetalSuballocatedUniformBuffer::CopyResourceTable(const void* Contents, TArray<TRefCountPtr<FRHIResource> >& OutResourceTable) const
{
#if METAL_UNIFORM_BUFFER_VALIDATION
	if (Validation == EUniformBufferValidation::ValidateResources)
	{
		ValidateShaderParameterResourcesRHI(Contents, GetLayout());
	}
#endif // METAL_UNIFORM_BUFFER_VALIDATION

	const FRHIUniformBufferLayout& Layout = GetLayout();
    const uint32 NumResources = Layout.Resources.Num();
    if (NumResources > 0)
    {
		OutResourceTable.Empty(NumResources);
		OutResourceTable.AddZeroed(NumResources);
        
        for (uint32 Index = 0; Index < NumResources; ++Index)
		{
			OutResourceTable[Index] = GetShaderParameterResourceRHI(Contents, Layout.Resources[Index].MemberOffset, Layout.Resources[Index].MemberType);
		}
    }
}
