// Copyright Epic Games, Inc. All Rights Reserved.

#include "D3D12ResourceCollection.h"

#if PLATFORM_SUPPORTS_BINDLESS_RENDERING

#include "D3D12RHIPrivate.h"
#include "D3D12CommandContext.h"
#include "D3D12TextureReference.h"

FD3D12ResourceCollection::FD3D12ResourceCollection(FD3D12Device* InParent, FRHICommandListBase& RHICmdList, FD3D12Buffer* InBuffer, TConstArrayView<FRHIResourceCollectionMember> InMembers, FD3D12ResourceCollection* FirstLinkedObject)
	: FRHIResourceCollection(InMembers)
	, FD3D12DeviceChild(InParent)
	, Buffer(InBuffer->GetLinkedObject(InParent->GetGPUIndex()))
{
	const uint32 GpuIndex = InParent->GetGPUIndex();

	for (const FRHIResourceCollectionMember& Member : InMembers)
	{
		switch (Member.Type)
		{
		case FRHIResourceCollectionMember::EType::Texture:
		{
			if (FRHITextureReference* TextureReferenceRHI = static_cast<FRHITexture*>(Member.Resource)->GetTextureReference())
			{
				FD3D12RHITextureReference* TextureReference = FD3D12CommandContext::RetrieveObject<FD3D12RHITextureReference>(TextureReferenceRHI, GpuIndex);
				AllTextureReferences.Emplace(TextureReference);
			}
			else
			{
				FD3D12Texture* Texture = FD3D12CommandContext::RetrieveTexture(static_cast<FRHITexture*>(Member.Resource), GpuIndex);
				AllSrvs.Emplace(Texture->GetShaderResourceView());
			}
		}
		break;
		case FRHIResourceCollectionMember::EType::TextureReference:
		{
			FD3D12RHITextureReference* TextureReference = FD3D12CommandContext::RetrieveObject<FD3D12RHITextureReference>(Member.Resource, GpuIndex);
			AllTextureReferences.Emplace(TextureReference);
		}
		break;
		case FRHIResourceCollectionMember::EType::ShaderResourceView:
		{
			FD3D12ShaderResourceView_RHI* ShaderResourceView = FD3D12CommandContext::RetrieveObject<FD3D12ShaderResourceView_RHI>(Member.Resource, GpuIndex);
			AllSrvs.Emplace(ShaderResourceView);
		}
		break;
		}
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc{};
	SRVDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.Buffer.FirstElement = InBuffer->ResourceLocation.GetOffsetFromBaseOfResource() / 4;
	SRVDesc.Buffer.NumElements = UE::RHICore::CalculateResourceCollectionMemorySize(InMembers) / 4;
	SRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;

	BufferSRV = MakeShared<FD3D12ShaderResourceView>(InParent, FirstLinkedObject ? FirstLinkedObject->BufferSRV.Get() : nullptr);
	BufferSRV->CreateView(InBuffer, SRVDesc, FD3D12ShaderResourceView::EFlags::None);
}

FD3D12ResourceCollection::~FD3D12ResourceCollection() = default;

FRHIDescriptorHandle FD3D12ResourceCollection::GetBindlessHandle() const
{
	return BufferSRV->GetBindlessHandle();
}

FRHIResourceCollectionRef FD3D12DynamicRHI::RHICreateResourceCollection(FRHICommandListBase& RHICmdList, TConstArrayView<FRHIResourceCollectionMember> InMembers)
{
	FRHIBuffer* RHIBuffer = UE::RHICore::CreateResourceCollectionBuffer(RHICmdList, InMembers);
	FD3D12Buffer* Buffer = ResourceCast(RHIBuffer);

	FRHIViewDesc::FBufferSRV::FInitializer ViewDesc = FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Raw);
	FShaderResourceViewRHIRef ShaderResourceView = RHICmdList.CreateShaderResourceView(Buffer, ViewDesc);

	return GetAdapter().CreateLinkedObject<FD3D12ResourceCollection>(FRHIGPUMask::All(), [&RHICmdList, Buffer, InMembers](FD3D12Device* Device, FD3D12ResourceCollection* FirstLinkedObject)
	{
		return new FD3D12ResourceCollection(Device, RHICmdList, Buffer, InMembers, FirstLinkedObject);
	});
}

#endif // PLATFORM_SUPPORTS_BINDLESS_RENDERING
