// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuR/OpMeshFormat.h"

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "HAL/PlatformCrt.h"
#include "HAL/PlatformMath.h"
#include "HAL/UnrealMemory.h"
#include "Misc/AssertionMacros.h"
#include "MuR/ConvertData.h"
#include "MuR/Layout.h"
#include "MuR/MeshBufferSet.h"
#include "MuR/MeshPrivate.h"
#include "MuR/MutableMath.h"
#include "MuR/MutableTrace.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuR/MutableRuntimeModule.h"

#include "GPUSkinPublicDefs.h"

namespace mu
{

	//-------------------------------------------------------------------------------------------------
	void MeshFormatBuffer( const FMeshBufferSet& Source, FMeshBuffer& ResultBuffer, int32 ResultOffsetElements, bool bHasSpecialSemantics, uint32 IDPrefix )
	{
		int32 SourceElementCount = Source.GetElementCount();
		if (!SourceElementCount)
		{
			return;
		}

		// This happens in the debugger
		if (ResultBuffer.Channels.IsEmpty() || ResultBuffer.ElementSize==0)
		{
			return;
		}

		check(ResultBuffer.ElementSize);
		int32 ResultElementCount = ResultBuffer.Data.Num() / ResultBuffer.ElementSize;
		check(SourceElementCount + ResultOffsetElements <= ResultElementCount);

		// For every channel in this buffer
		for (int32 ResultChannelIndex = 0; ResultChannelIndex < ResultBuffer.Channels.Num(); ++ResultChannelIndex)
		{
			FMeshBufferChannel& ResultChannel = ResultBuffer.Channels[ResultChannelIndex];

			// Find this channel in the source mesh
			int32 SourceBufferIndex;
			int32 SourceChannelIndex;
			Source.FindChannel(ResultChannel.Semantic, ResultChannel.SemanticIndex, &SourceBufferIndex, &SourceChannelIndex);

			int32 resultElemSize = ResultBuffer.ElementSize;
			int32 resultComponents = ResultChannel.ComponentCount;
			int32 resultChannelSize = GetMeshFormatData(ResultChannel.Format).SizeInBytes * resultComponents;
			uint8* pResultBuf = ResultBuffer.Data.GetData() + ResultOffsetElements*ResultBuffer.ElementSize;
			pResultBuf += ResultChannel.Offset;

			// Case 1: Special semantics that may be implicit or relative
			//---------------------------------------------------------------------------------
			if (bHasSpecialSemantics && ResultChannel.Semantic == MBS_VERTEXINDEX)
			{
				bool bHasVertexIndices = (SourceBufferIndex >=0 );
				if (bHasVertexIndices)
				{
					check(SourceChannelIndex == 0 && Source.Buffers[SourceBufferIndex].Channels.Num() == 1);

					const FMeshBuffer& SourceBuffer = Source.Buffers[SourceBufferIndex];
					const FMeshBufferChannel& SourceChannel = SourceBuffer.Channels[SourceChannelIndex];

					bool bHasSameFormat = SourceChannel.Format == ResultChannel.Format;
					if (bHasSameFormat)
					{
						check(SourceChannel == ResultChannel);
						FMemory::Memcpy(pResultBuf,SourceBuffer.Data.GetData(),SourceBuffer.Data.Num());
					}
					else
					{
						// Relative vertex IDs
						check(IDPrefix);
						check(SourceChannel.Format == MBF_UINT32);
						const uint32* SourceData = reinterpret_cast<const uint32*>(SourceBuffer.Data.GetData());

						check(ResultChannel.Format == MBF_UINT64);
						uint64* ResultData = reinterpret_cast<uint64*>(pResultBuf);

						for (int32 i = 0; i < SourceElementCount; ++i)
						{
							uint32 SourceId = *SourceData++;
							uint64 Id = (uint64(IDPrefix) << 32) | uint64(SourceId);
							*ResultData = Id;
							++ResultData;
						}
					}
				}
				else
				{
					// Implicit IDs
					check(IDPrefix);

					check(ResultChannel.Format == MBF_UINT64);
					uint64* ResultData = reinterpret_cast<uint64*>(pResultBuf);

					for (int32 VertexIndex = 0; VertexIndex < SourceElementCount; ++VertexIndex)
					{
						uint64 Id = (uint64(IDPrefix) << 32) | uint64(VertexIndex);
						*ResultData = Id;
						++ResultData;
					}
				}

				continue;
			}

			else if (bHasSpecialSemantics && ResultChannel.Semantic == MBS_LAYOUTBLOCK)
			{
				bool bHasBlockIds = (SourceBufferIndex >= 0);

				if (bHasBlockIds)
				{
					const FMeshBuffer& SourceBuffer = Source.Buffers[SourceBufferIndex];
					const FMeshBufferChannel& SourceChannel = SourceBuffer.Channels[SourceChannelIndex];

					bool bHasSameFormat = SourceChannel.Format == ResultChannel.Format;
					if (bHasSameFormat)
					{
						check(SourceChannel==ResultChannel);
						FMemory::Memcpy(pResultBuf, SourceBuffer.Data.GetData(), SourceBuffer.Data.Num());
					}
					else
					{
						// Relative vertex IDs
						check(SourceChannel.Format == MBF_UINT16);
						const uint16* SourceData = reinterpret_cast<const uint16*>(SourceBuffer.Data.GetData());

						check(ResultChannel.Format == MBF_UINT64);
						uint64* ResultData = reinterpret_cast<uint64*>(pResultBuf);

						for (int32 i = 0; i < SourceElementCount; ++i)
						{
							uint16 SourceId = *SourceData++;
							uint64 Id = (uint64(IDPrefix) << 32) | uint64(SourceId);
							*ResultData = Id;
							++ResultData;
						}
					}
					continue;
				}
				else
				{
					// This seems to happen with objects that mix meshes with layouts with meshes without layouts.
					// If we don't do anything here, it will be filled with zeros in the following code.
					//ensure(false);
				}

			}

			// Case 2: Not found in source: generate with default values, depending on semantic
			//---------------------------------------------------------------------------------
			if (SourceBufferIndex < 0)
			{
				// Not found: fill with zeros.

				// Special case for derived channel data
				bool generated = false;

				// If we have to add colour channels, we will add them as white, to be neutral.
				// \todo: normal channels also should have special values.
				if (ResultChannel.Semantic == MBS_COLOUR)
				{
					generated = true;

					switch (ResultChannel.Format)
					{
					case MBF_FLOAT32:
					{
						for (int32 v = 0; v < SourceElementCount; ++v)
						{
							float* pTypedResultBuf = (float*)pResultBuf;
							for (int32 i = 0; i < resultComponents; ++i)
							{
								pTypedResultBuf[i] = 1.0f;
							}
							pResultBuf += resultElemSize;
						}
						break;
					}

					case MBF_NUINT8:
					{
						for (int32 v = 0; v < SourceElementCount; ++v)
						{
							uint8* pTypedResultBuf = (uint8*)pResultBuf;
							for (int32 i = 0; i < resultComponents; ++i)
							{
								pTypedResultBuf[i] = 255;
							}
							pResultBuf += resultElemSize;
						}
						break;
					}

					case MBF_NUINT16:
					{
						for (int32 v = 0; v < SourceElementCount; ++v)
						{
							uint16* pTypedResultBuf = (uint16*)pResultBuf;
							for (int32 i = 0; i < resultComponents; ++i)
							{
								pTypedResultBuf[i] = 65535;
							}
							pResultBuf += resultElemSize;
						}
						break;
					}

					default:
						// Format not implemented
						check(false);
						break;
					}
				}

				if (!generated)
				{
					// TODO: and maybe raise a warning?
					for (int32 v = 0; v < SourceElementCount; ++v)
					{
						FMemory::Memzero(pResultBuf, resultChannelSize);
						pResultBuf += resultElemSize;
					}
				}

				continue;
			}


			// Case 3: Convert element by element
			//---------------------------------------------------------------------------------
			{
				// Get the data about the source format
				EMeshBufferSemantic sourceSemantic;
				int32 sourceSemanticIndex;
				EMeshBufferFormat sourceFormat;
				int32 sourceComponents;
				int32 sourceOffset;
				Source.GetChannel
				(
					SourceBufferIndex, SourceChannelIndex,
					&sourceSemantic, &sourceSemanticIndex,
					&sourceFormat, &sourceComponents,
					&sourceOffset
				);
				check(sourceSemantic == ResultChannel.Semantic
					&&
					sourceSemanticIndex == ResultChannel.SemanticIndex);

				int32 sourceElemSize = Source.GetElementSize(SourceBufferIndex);
				const uint8* pSourceBuf = Source.GetBufferData(SourceBufferIndex);
				pSourceBuf += sourceOffset;

				// Copy element by element
				for (int32 v = 0; v < SourceElementCount; ++v)
				{
					if (ResultChannel.Format == sourceFormat && resultComponents == sourceComponents)
					{
						FMemory::Memcpy(pResultBuf, pSourceBuf, resultChannelSize);
					}
					else if (ResultChannel.Format == MBF_PACKEDDIR8_W_TANGENTSIGN
						||
						ResultChannel.Format == MBF_PACKEDDIRS8_W_TANGENTSIGN)
					{
						check(sourceComponents >= 3);
						check(resultComponents == 4);

						// convert the 3 first components
						for (int32 i = 0; i < 3; ++i)
						{
							if (i < sourceComponents)
							{
								ConvertData
								(
									i,
									pResultBuf, ResultChannel.Format,
									pSourceBuf, sourceFormat
								);
							}
						}


						// Add the tangent sign
						uint8* pData = reinterpret_cast<uint8*>(pResultBuf);

						// Look for the full tangent space
						int32 tanXBuf, tanXChan, tanYBuf, tanYChan, tanZBuf, tanZChan;
						Source.FindChannel(MBS_TANGENT, ResultChannel.SemanticIndex, &tanXBuf, &tanXChan);
						Source.FindChannel(MBS_BINORMAL, ResultChannel.SemanticIndex, &tanYBuf, &tanYChan);
						Source.FindChannel(MBS_NORMAL, ResultChannel.SemanticIndex, &tanZBuf, &tanZChan);

						if (tanXBuf >= 0 && tanYBuf >= 0 && tanZBuf >= 0)
						{
							UntypedMeshBufferIteratorConst xIt(Source, MBS_TANGENT, ResultChannel.SemanticIndex);
							UntypedMeshBufferIteratorConst yIt(Source, MBS_BINORMAL, ResultChannel.SemanticIndex);
							UntypedMeshBufferIteratorConst zIt(Source, MBS_NORMAL, ResultChannel.SemanticIndex);

							xIt += v;
							yIt += v;
							zIt += v;

							FMatrix44f Mat(xIt.GetAsVec3f(), yIt.GetAsVec3f(), zIt.GetAsVec3f(), FVector3f(0, 0, 0));

							uint8 sign = 0;
							if (ResultChannel.Format == MBF_PACKEDDIR8_W_TANGENTSIGN)
							{
								sign = Mat.RotDeterminant() < 0 ? 0 : 255;
							}
							else if (ResultChannel.Format == MBF_PACKEDDIRS8_W_TANGENTSIGN)
							{
								sign = Mat.RotDeterminant() < 0 ? -128 : 127;
							}
							pData[3] = sign;
						}
						else
						{
							// At least initialize it to avoid garbage.
							pData[3] = 0;
						}
					}
					else
					{
						// Convert formats
						for (int32 i = 0; i < resultComponents; ++i)
						{
							if (i < sourceComponents)
							{
								ConvertData
								(
									i,
									pResultBuf, ResultChannel.Format,
									pSourceBuf, sourceFormat
								);
							}
							else
							{
								// Add zeros. TODO: Warning?
								FMemory::Memzero
								(
									pResultBuf + GetMeshFormatData(ResultChannel.Format).SizeInBytes * i,
									GetMeshFormatData(ResultChannel.Format).SizeInBytes
								);
							}
						}


						// Extra step to normalise some semantics in some formats
						// TODO: Make it optional, and add different normalisation types n, n^2
						// TODO: Optimise
						if (sourceSemantic == MBS_BONEWEIGHTS)
						{
							if (ResultChannel.Format == MBF_NUINT8)
							{
								uint8* pData = (uint8*)pResultBuf;
								uint8 accum = 0;
								for (int32 i = 0; i < resultComponents; ++i)
								{
									accum += pData[i];
								}
								pData[0] += 255 - accum;
							}

							else if (ResultChannel.Format == MBF_NUINT16)
							{
								uint16* pData = (uint16*)pResultBuf;
								uint16 accum = 0;
								for (int32 i = 0; i < resultComponents; ++i)
								{
									accum += pData[i];
								}
								pData[0] += 65535 - accum;
							}
						}
					}

					pResultBuf += resultElemSize;
					pSourceBuf += sourceElemSize;
				}
			}
		}
	}


	//-------------------------------------------------------------------------------------------------
	void FormatBufferSet
	(
		const FMeshBufferSet& Source,
		FMeshBufferSet& Result,
		bool bKeepSystemBuffers,
		bool bIgnoreMissingChannels,
		bool bIsVertexBuffer, 
		uint32 IDPrefix = 0
	)
	{
		if (bIgnoreMissingChannels)
		{
			// Remove from the result the channels that are not present in the source, and re-pack the
			// offsets.
			for (int32 b = 0; b < Result.GetBufferCount(); ++b)
			{
				TArray<EMeshBufferSemantic> resultSemantics;
				TArray<int32> resultSemanticIndexs;
				TArray<EMeshBufferFormat> resultFormats;
				TArray<int32> resultComponentss;
				TArray<int32> resultOffsets;
				int32 offset = 0;

				// For every channel in this buffer
				for (int32 c = 0; c < Result.GetBufferChannelCount(b); ++c)
				{
					EMeshBufferSemantic resultSemantic;
					int32 resultSemanticIndex;
					EMeshBufferFormat resultFormat;
					int32 resultComponents;

					// Find this channel in the source mesh
					Result.GetChannel
					(
						b, c,
						&resultSemantic, &resultSemanticIndex,
						&resultFormat, &resultComponents,
						nullptr
					);

					int32 sourceBuffer;
					int32 sourceChannel;
					Source.FindChannel
					(resultSemantic, resultSemanticIndex, &sourceBuffer, &sourceChannel);

					if (sourceBuffer >= 0)
					{
						resultSemantics.Add(resultSemantic);
						resultSemanticIndexs.Add(resultSemanticIndex);
						resultFormats.Add(resultFormat);
						resultComponentss.Add(resultComponents);
						resultOffsets.Add(offset);

						offset += GetMeshFormatData(resultFormat).SizeInBytes * resultComponents;
					}
				}

				if (resultSemantics.IsEmpty())
				{
					Result.SetBuffer(b, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr);
				}
				else
				{
					Result.SetBuffer(b, offset, resultSemantics.Num(),
						&resultSemantics[0],
						&resultSemanticIndexs[0],
						&resultFormats[0],
						&resultComponentss[0],
						&resultOffsets[0]);
				}
			}
		}


		// For every vertex buffer in result
		int32 vCount = Source.GetElementCount();
		Result.SetElementCount(vCount);
		for (int32 b = 0; b < Result.GetBufferCount(); ++b)
		{
			MeshFormatBuffer(Source, Result.Buffers[b], 0, bIsVertexBuffer, IDPrefix);
		}


		// Detect internal system buffers and clone them unmodified.
		if (bKeepSystemBuffers)
		{
			for (int32 b = 0; b < Source.GetBufferCount(); ++b)
			{
				// Detect system buffers and clone them unmodified.
				if (Source.GetBufferChannelCount(b) == 1)
				{
					EMeshBufferSemantic sourceSemantic;
					int32 sourceSemanticIndex;
					EMeshBufferFormat sourceFormat;
					int32 sourceComponents;
					int32 sourceOffset;
					Source.GetChannel
					(
						b, 0,
						&sourceSemantic, &sourceSemanticIndex,
						&sourceFormat, &sourceComponents,
						&sourceOffset
					);

					if (sourceSemantic == MBS_LAYOUTBLOCK
						||
						(bIsVertexBuffer && sourceSemantic == MBS_VERTEXINDEX))
					{
						// Add it if it wasn't already there, which could happen if it was included in the format mesh.
						int32 AlreadyExistingBufIndex = -1;
						int32 AlreadyExistingChannelIndex = -1;
						Result.FindChannel(sourceSemantic, sourceSemanticIndex, &AlreadyExistingBufIndex, &AlreadyExistingChannelIndex);
						if (AlreadyExistingBufIndex==-1)
						{
							Result.AddBuffer(Source, b);
						}
						else
						{
							// Replace the buffer
							check(Result.Buffers[AlreadyExistingBufIndex].Channels.Num()==1);
							Result.Buffers[AlreadyExistingBufIndex] = Source.Buffers[b];
						}
					}
				}
			}
		}

	}



	//-------------------------------------------------------------------------------------------------
	void MeshFormat
	(
		Mesh* Result, 
		const Mesh* PureSource,
		const Mesh* Format,
		bool keepSystemBuffers,
		bool formatVertices,
		bool formatIndices,
		bool ignoreMissingChannels,
		bool& bOutSuccess
	)
	{
		MUTABLE_CPUPROFILER_SCOPE(MeshFormat);
		bOutSuccess = true;

		if (!PureSource) 
		{
			check(false);
			bOutSuccess = false;	
			return;
		}

		if (!Format)
		{
			check(false);
			bOutSuccess = false;
			return;
		}

		Ptr<const Mesh> Source = PureSource;

		Result->CopyFrom(*Format);
		Result->MeshIDPrefix = Source->MeshIDPrefix;

		// Make sure that the bone indices will fit in this format, or extend it.
		if (formatVertices)
		{
			const FMeshBufferSet& VertexBuffers = Source->GetVertexBuffers();

			const int32 BufferCount = VertexBuffers.GetBufferCount();
			for (int32 BufferIndex = 0; BufferIndex < BufferCount; ++BufferIndex)
			{
				const int32 ChannelCount = VertexBuffers.GetBufferChannelCount(BufferIndex);
				for (int32 ChannelIndex = 0; ChannelIndex < ChannelCount; ++ChannelIndex)
				{
					const FMeshBufferChannel& Channel = VertexBuffers.Buffers[BufferIndex].Channels[ChannelIndex];

					if (Channel.Semantic == MBS_BONEINDICES)
					{
						int32 resultBuf = 0;
						int32 resultChan = 0;
						FMeshBufferSet& formBuffs = Result->GetVertexBuffers();
						formBuffs.FindChannel(MBS_BONEINDICES, Channel.SemanticIndex, &resultBuf, &resultChan);
						if (resultBuf >= 0)
						{
							UntypedMeshBufferIteratorConst it(VertexBuffers, MBS_BONEINDICES, Channel.SemanticIndex);
							int32 maxBoneIndex = 0;
							for (int32 v = 0; v < VertexBuffers.GetElementCount(); ++v)
							{
								// If MAX_TOTAL_INFLUENCES ever changed, the next line would no longer work or compile and 
								// GetAsVec12i would need to be changed accordingly
								int32 va[MAX_TOTAL_INFLUENCES];
								it.GetAsInt32Vec(va, MAX_TOTAL_INFLUENCES);
								for (int32 c = 0; c < it.GetComponents(); ++c)
								{
									maxBoneIndex = FMath::Max(maxBoneIndex, va[c]);
								}
								++it;
							}

							EMeshBufferFormat& format = formBuffs.Buffers[resultBuf].Channels[resultChan].Format;
							if (maxBoneIndex > 0xffff && (format == MBF_UINT8 || format == MBF_UINT16))
							{
								format = MBF_UINT32;
								formBuffs.UpdateOffsets(resultBuf);
							}
							else if (maxBoneIndex > 0x7fff && (format == MBF_INT8 || format == MBF_INT16))
							{
								format = MBF_UINT32;
								formBuffs.UpdateOffsets(resultBuf);
							}
							else if (maxBoneIndex > 0xff && format == MBF_UINT8)
							{
								format = MBF_UINT16;
								formBuffs.UpdateOffsets(resultBuf);
							}
							else if (maxBoneIndex > 0x7f && format == MBF_INT8)
							{
								format = MBF_INT16;
								formBuffs.UpdateOffsets(resultBuf);
							}
						}
					}
				}
			}
		}

		if (formatVertices)
		{
			FormatBufferSet(Source->GetVertexBuffers(), Result->GetVertexBuffers(),
				keepSystemBuffers, ignoreMissingChannels, true, Source->MeshIDPrefix);
		}
		else
		{
			Result->VertexBuffers = Source->GetVertexBuffers();
		}

		if (formatIndices)
		{
			// \todo Make sure that the vertex indices will fit in this format, or extend it.
			FormatBufferSet(Source->GetIndexBuffers(), Result->GetIndexBuffers(), keepSystemBuffers,
				ignoreMissingChannels, false);
		}
		else
		{
			Result->IndexBuffers = Source->GetIndexBuffers();
		}

		// Copy the rest of the data
		Result->SetSkeleton(Source->GetSkeleton());
		Result->SetPhysicsBody(Source->GetPhysicsBody());

		Result->Layouts.Empty();
		for (const Ptr<const Layout>& Layout : Source->Layouts)
		{
			Result->Layouts.Add(Layout->Clone());
		}

		Result->Tags = Source->Tags;
		Result->StreamedResources = Source->StreamedResources;

		Result->AdditionalBuffers = Source->AdditionalBuffers;

		Result->BonePoses = Source->BonePoses;
		Result->BoneMap = Source->BoneMap;

		Result->SkeletonIDs = Source->SkeletonIDs;

		// A shallow copy is done here, it should not be a problem.
		Result->AdditionalPhysicsBodies = Source->AdditionalPhysicsBodies;

		Result->Surfaces = Source->Surfaces;

		Result->ResetStaticFormatFlags();
		Result->EnsureSurfaceData();
	}


	void MeshOptimizeBuffers( Mesh* InMesh )
	{
		if (!InMesh)
		{
			return;
		}

		FMeshBufferSet& VertexBuffers = InMesh->VertexBuffers;

		// Reduce the number of influences if possible
		constexpr int32 SemanticIndex = 0;
		
		UntypedMeshBufferIteratorConst WeightIt(VertexBuffers, MBS_BONEWEIGHTS, SemanticIndex);
		if (WeightIt.ptr())
		{
			int32 BufferInfluences = WeightIt.GetComponents();
			int32 RealInfluences = 0;

			for (int32 VertexIndex = 0; VertexIndex < VertexBuffers.GetElementCount(); ++VertexIndex)
			{
				int32 ThisVertexInfluences = 0;

				switch (WeightIt.GetFormat())
				{
				case MBF_NUINT8:
				{
					const uint8* Data = reinterpret_cast<const uint8*>(WeightIt.ptr());
					for (int32 InfluenceIndex = 0; InfluenceIndex < BufferInfluences; ++InfluenceIndex)
					{
						if (*Data>0)
						{
							++ThisVertexInfluences;
						}
						++Data;
					}
					break;
				}
				case MBF_NUINT16:
				{
					const uint16* Data = reinterpret_cast<const uint16*>(WeightIt.ptr());
					for (int32 InfluenceIndex = 0; InfluenceIndex < BufferInfluences; ++InfluenceIndex)
					{
						if (*Data > 0)
						{
							++ThisVertexInfluences;
						}
						++Data;
					}
					break;
				}

				default:
					// Unsupported
					check(false);
					break;
				}

				++WeightIt;

				RealInfluences = FMath::Max(RealInfluences,ThisVertexInfluences);
			}

			if (RealInfluences<BufferInfluences)
			{
				// Remove the useless influences from the buffer.

				// \todo: This is a generic innefficient way
				FMeshBufferSet NewVertexBuffers;
				NewVertexBuffers.Buffers = VertexBuffers.Buffers;

				for (FMeshBuffer& Buffer: NewVertexBuffers.Buffers)
				{
					int32 OffsetDelta = 0;
					for (FMeshBufferChannel& Channel : Buffer.Channels)
					{
						Channel.Offset += OffsetDelta;

						if (Channel.SemanticIndex == SemanticIndex
							&&
							(Channel.Semantic == MBS_BONEWEIGHTS || Channel.Semantic == MBS_BONEINDICES)
							)
						{
							Channel.ComponentCount = RealInfluences;
							OffsetDelta -= (BufferInfluences - RealInfluences) * GetMeshFormatData(Channel.Format).SizeInBytes;
						}
					}

					Buffer.ElementSize += OffsetDelta;
				}

				FormatBufferSet( VertexBuffers, NewVertexBuffers, true, false, true);

				InMesh->VertexBuffers = NewVertexBuffers;
			}
		}

	}
}
