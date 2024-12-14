// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/MeshPrivate.h"
#include "MuR/ConvertData.h"
#include "MuR/MutableTrace.h"
#include "MuR/Platform.h"


namespace mu
{

	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------
	inline void MeshOptimizeSkinning(Mesh* Result, const Mesh* InMesh, bool& bOutSuccess)
	{
		MUTABLE_CPUPROFILER_SCOPE(MeshOptimizeSkinning);

		bOutSuccess = true;

		if (!InMesh)
		{
			bOutSuccess = false;
			return;
		}

		uint32 MaxBoneMapIndex = 0;
		for (const FMeshSurface& Surface : InMesh->Surfaces)
		{
			MaxBoneMapIndex = FMath::Max(MaxBoneMapIndex, Surface.BoneMapCount);
		}

		// We can't optimize the skinning if the mesh requires 16 bit bone indices 
		if (MaxBoneMapIndex > MAX_uint8)
		{
			bOutSuccess = false;
			return;
		}

		bool bRequiresFormatChange = false;

		// Desired format if MaxBoneMapIndex <= MAX_uint8
		const EMeshBufferFormat DesiredBoneIndexFormat = mu::MBF_UINT8;
		
		// Iterate all vertex buffers and check if the BoneIndices buffers have the desired format
		const FMeshBufferSet& InMeshVertexBuffers = InMesh->GetVertexBuffers();
		for (int32 VertexBufferIndex = 0; !bRequiresFormatChange && VertexBufferIndex < InMeshVertexBuffers.Buffers.Num(); ++VertexBufferIndex)
		{
			const FMeshBuffer& Buffer = InMeshVertexBuffers.Buffers[VertexBufferIndex];

			const int32 ChannelsCount = InMeshVertexBuffers.GetBufferChannelCount(VertexBufferIndex);
			for (int32 ChannelIndex = 0; ChannelIndex < ChannelsCount; ++ChannelIndex)
			{
				if (Buffer.Channels[ChannelIndex].Semantic == MBS_BONEINDICES)
				{
					bRequiresFormatChange = Buffer.Channels[ChannelIndex].Format != DesiredBoneIndexFormat;
					break;
				}
			}
		}

		// Not sure if bRequiresFormatChange will ever be true.
		if (!bRequiresFormatChange)
		{
			bOutSuccess = false;
			return;
		}

		MUTABLE_CPUPROFILER_SCOPE(MeshOptimizeSkinning_Format);

		// Format Bone Indices. For some reason, the bone index format is MBF_UINT16 and it should be MBF_UINT8.

		// TODO: Replace by MeshFormatInPlace once implemented
		const int32 VertexBuffersCount = InMeshVertexBuffers.GetBufferCount();
		const int32 ElementCount = InMeshVertexBuffers.GetElementCount();

		// Clone mesh without VertexBuffers, they will be copied manually.
		constexpr EMeshCopyFlags CopyFlags = ~EMeshCopyFlags::WithVertexBuffers;
		Result->CopyFrom(*InMesh, CopyFlags);

		mu::FMeshBufferSet& VertexBuffers = Result->GetVertexBuffers();

		VertexBuffers.Buffers.Reserve(VertexBuffersCount);
		VertexBuffers.SetElementCount(ElementCount);

		for (int32 BufferIndex = 0; BufferIndex < VertexBuffersCount; ++BufferIndex)
		{
			const FMeshBuffer& SourceBuffer = InMeshVertexBuffers.Buffers[BufferIndex];
			const int32 ChannelsCount = InMeshVertexBuffers.GetBufferChannelCount(BufferIndex);

			int32 BoneIndexChannelIndex = INDEX_NONE;

			int32 ElementSize = 0;
			TArray<EMeshBufferSemantic> Semantics;
			TArray<int32> SemanticIndices;
			TArray<EMeshBufferFormat> Formats;
			TArray<int32> Components;
			TArray<int32> Offsets;

			EMeshBufferFormat SourceBoneIndexFormat = mu::MBF_NONE;

			// Offset accumulator
			int32 AuxOffset = 0;

			// Copy and fix channel details
			for (int32 ChannelIndex = 0; ChannelIndex < ChannelsCount; ++ChannelIndex)
			{
				const FMeshBufferChannel& Channel = SourceBuffer.Channels[ChannelIndex];
				EMeshBufferFormat Format = Channel.Format;

				if (Channel.Semantic == MBS_BONEINDICES)
				{
					SourceBoneIndexFormat = Channel.Format;
					Format = mu::MBF_UINT8;
					BoneIndexChannelIndex = ChannelIndex;
				}

				Semantics.Add(Channel.Semantic);
				SemanticIndices.Add(Channel.SemanticIndex);
				Formats.Add(Format);
				Components.Add(Channel.ComponentCount);
				Offsets.Add(AuxOffset);

				const int32 FormatSize = GetMeshFormatData(Format).SizeInBytes;

				ElementSize += FormatSize;
				AuxOffset += FormatSize * Channel.ComponentCount;
			}

			// Copy buffers
			if (BoneIndexChannelIndex != INDEX_NONE && SourceBoneIndexFormat == mu::MBF_UINT16)
			{
				// This buffer has a BoneIndex channel that needs to be manually fixed. 
				VertexBuffers.SetBufferCount(BufferIndex + 1);
				VertexBuffers.SetBuffer(BufferIndex, ElementSize, ChannelsCount, Semantics.GetData(), SemanticIndices.GetData(), Formats.GetData(), Components.GetData(), Offsets.GetData());
				uint8* Data = VertexBuffers.GetBufferData(BufferIndex);

				const uint8* SourceData = (const uint8*)SourceBuffer.Data.GetData();
				const int32 SourceBoneIndexSize = GetMeshFormatData(SourceBoneIndexFormat).SizeInBytes;

				const int32 BoneIndexSize = GetMeshFormatData(mu::MBF_UINT8).SizeInBytes;
				const int32 BoneIndexComponentCount = Components[BoneIndexChannelIndex];
				const int32 BoneIndexChannelOffset = Offsets[BoneIndexChannelIndex];

				const int32 TailSize = AuxOffset - (BoneIndexChannelOffset + BoneIndexComponentCount * BoneIndexSize);


				for (const FMeshSurface& Surface : InMesh->Surfaces)
				{
					const uint32 NumBonesInBoneMap = Surface.BoneMapCount;

					for (const FSurfaceSubMesh SubMesh : Surface.SubMeshes)
					{
						for (int32 VertexIndex = SubMesh.VertexBegin; VertexIndex < SubMesh.VertexEnd; ++VertexIndex)
						{
							FMemory::Memcpy(Data, SourceData, BoneIndexChannelOffset);
							Data += BoneIndexChannelOffset;
							SourceData += BoneIndexChannelOffset;

							for (int32 ComponentIndex = 0; ComponentIndex < BoneIndexComponentCount; ++ComponentIndex)
							{
								const uint16 SourceIndex = *((const uint16*)SourceData);
								if (SourceIndex < NumBonesInBoneMap)
								{
									*Data = (uint8)SourceIndex;
								}
								else
								{
									*Data = 0;
								}
								
								Data += BoneIndexSize;
								SourceData += SourceBoneIndexSize;
							}


							FMemory::Memcpy(Data, SourceData, TailSize);
							Data += TailSize;
							SourceData += TailSize;
						}
					}
				}
			}
			else
			{
				// SourceBoneIndexFormat must be mu::MBF_UINT8 or none if the buffer doesn't have BoneIndices
				check(SourceBoneIndexFormat == mu::MBF_NONE || SourceBoneIndexFormat == mu::MBF_UINT8);

				// Add buffers that don't require a fix up
				VertexBuffers.AddBuffer(InMeshVertexBuffers, BufferIndex);
			}

		}
	}
}
