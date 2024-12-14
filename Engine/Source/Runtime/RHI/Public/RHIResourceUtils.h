// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHICommandList.h"
#include "RHIResources.h"
#include "Containers/ResourceArray.h"

namespace UE::RHIResourceUtils
{
	static FBufferRHIRef CreateBufferFromArray(FRHICommandListBase& RHICmdList, const TCHAR* Name, EBufferUsageFlags UsageFlags, uint32 InStride, const void* InData, uint32 InSizeInBytes)
	{
		const ERHIAccess InitialState = RHIGetDefaultResourceState(UsageFlags, false);
		FResourceArrayUploadArrayView UploadView(InData, InSizeInBytes);

		FRHIResourceCreateInfo CreateInfo(Name, &UploadView);
		return RHICmdList.CreateBuffer(InSizeInBytes, UsageFlags, InStride, InitialState, CreateInfo);
	}

	template<typename TElementType>
	static FBufferRHIRef CreateBufferFromArray(FRHICommandListBase& RHICmdList, const TCHAR* Name, EBufferUsageFlags UsageFlags, uint32 InStride, ERHIAccess InitialState, TConstArrayView<TElementType> Array)
	{
		FResourceArrayUploadArrayView UploadView(Array);

		FRHIResourceCreateInfo CreateInfo(Name, &UploadView);
		return RHICmdList.CreateBuffer(UploadView.GetResourceDataSize(), UsageFlags, InStride, InitialState, CreateInfo);
	}

	template<typename TElementType>
	static FBufferRHIRef CreateBufferFromArray(FRHICommandListBase& RHICmdList, const TCHAR* Name, EBufferUsageFlags UsageFlags, ERHIAccess InitialState, TConstArrayView<TElementType> Array)
	{
		return CreateBufferFromArray(RHICmdList, Name, UsageFlags, Array.GetTypeSize(), InitialState, Array);
	}

	template<typename TElementType>
	static FBufferRHIRef CreateVertexBufferFromArray(FRHICommandListBase& RHICmdList, const TCHAR* Name, EBufferUsageFlags ExtraFlags, TConstArrayView<TElementType> Array)
	{
		const EBufferUsageFlags Usage = EBufferUsageFlags::VertexBuffer | ExtraFlags;
		const ERHIAccess InitialState = RHIGetDefaultResourceState(Usage, false);
		return CreateBufferFromArray<TElementType>(RHICmdList, Name, Usage, 0, InitialState, Array);
	}

	template<typename TElementType>
	static FBufferRHIRef CreateVertexBufferFromArray(FRHICommandListBase& RHICmdList, const TCHAR* Name, TConstArrayView<TElementType> Array)
	{
		return CreateVertexBufferFromArray<TElementType>(RHICmdList, Name, EBufferUsageFlags::None, Array);
	}

	template<typename TElementType>
	static FBufferRHIRef CreateIndexBufferFromArray(FRHICommandListBase& RHICmdList, const TCHAR* Name, EBufferUsageFlags ExtraFlags, TConstArrayView<TElementType> Array)
	{
		const EBufferUsageFlags Usage = EBufferUsageFlags::IndexBuffer | ExtraFlags;
		const ERHIAccess InitialState = RHIGetDefaultResourceState(Usage, false);
		return CreateBufferFromArray<TElementType>(RHICmdList, Name, Usage, InitialState, Array);
	}

	template<typename TElementType>
	static FBufferRHIRef CreateIndexBufferFromArray(FRHICommandListBase& RHICmdList, const TCHAR* Name, TConstArrayView<TElementType> Array)
	{
		return CreateIndexBufferFromArray<TElementType>(RHICmdList, Name, EBufferUsageFlags::None, Array);
	}
}
