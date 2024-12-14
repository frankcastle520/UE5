// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SkeletalRenderStatic.cpp: CPU skinned skeletal mesh rendering code.
=============================================================================*/

#include "SkeletalRenderStatic.h"
#include "RenderUtils.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/RenderCommandPipes.h"

#if RHI_RAYTRACING
#include "Engine/SkinnedAssetCommon.h"
#endif

FSkeletalMeshObjectStatic::FSkeletalMeshObjectStatic(USkinnedMeshComponent* InMeshComponent, FSkeletalMeshRenderData* InSkelMeshRenderData, ERHIFeatureLevel::Type InFeatureLevel)
	: FSkeletalMeshObject(InMeshComponent, InSkelMeshRenderData, InFeatureLevel)
{
	// create LODs to match the base mesh
	for (int32 LODIndex = 0; LODIndex < InSkelMeshRenderData->LODRenderData.Num(); LODIndex++)
	{
		new(LODs) FSkeletalMeshObjectLOD(InFeatureLevel, InSkelMeshRenderData, LODIndex);
	}

	InitResources(InMeshComponent);
	bSupportsStaticRelevance = true;
}

FSkeletalMeshObjectStatic::~FSkeletalMeshObjectStatic()
{
}

void FSkeletalMeshObjectStatic::InitResources(USkinnedMeshComponent* InMeshComponent)
{
	for( int32 LODIndex=0;LODIndex < LODs.Num();LODIndex++ )
	{
		FSkeletalMeshObjectLOD& SkelLOD = LODs[LODIndex];

		check(SkelLOD.SkelMeshRenderData);
		check(SkelLOD.SkelMeshRenderData->LODRenderData.IsValidIndex(LODIndex));

		FSkeletalMeshLODRenderData& LODData = SkelLOD.SkelMeshRenderData->LODRenderData[LODIndex];
		
		// Skip LODs that have their render data stripped
		if (LODData.GetNumVertices() > 0)
		{
			FSkelMeshComponentLODInfo* CompLODInfo = nullptr;
			if (InMeshComponent->LODInfo.IsValidIndex(LODIndex))
			{
				CompLODInfo = &InMeshComponent->LODInfo[LODIndex];
			}

			SkelLOD.InitResources(CompLODInfo);

#if RHI_RAYTRACING
			if (IsRayTracingAllowed() && SkelLOD.SkelMeshRenderData->bSupportRayTracing)
			{
				if (LODData.NumReferencingStaticSkeletalMeshObjects == 0)
				{
					FPositionVertexBuffer* PositionVertexBufferPtr = &LODData.StaticVertexBuffers.PositionVertexBuffer;
					FRawStaticIndexBuffer16or32Interface* IndexBufferPtr = LODData.MultiSizeIndexContainer.GetIndexBuffer();

					uint32 TrianglesCount = 0;
					for (int32 SectionIndex = 0; SectionIndex < LODData.RenderSections.Num(); SectionIndex++)
					{
						const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];
						TrianglesCount += Section.NumTriangles;
					}

					TArray<FSkelMeshRenderSection>* RenderSections = &LODData.RenderSections;
					ENQUEUE_RENDER_COMMAND(InitSkeletalRenderStaticRayTracingGeometry)(UE::RenderCommandPipe::SkeletalMesh,
						[PositionVertexBufferPtr, IndexBufferPtr, TrianglesCount, RenderSections,
						LODIndex = LODIndex, 
						SkelMeshRenderData = SkelLOD.SkelMeshRenderData, 
						&RayTracingGeometry = LODData.StaticRayTracingGeometry,
						&bReferencedByStaticSkeletalMeshObjects_RenderThread = LODData.bReferencedByStaticSkeletalMeshObjects_RenderThread](FRHICommandList& RHICmdList)
						{
							FRayTracingGeometryInitializer Initializer;
							static const FName DebugName("FSkeletalMeshObjectLOD");
							static int32 DebugNumber = 0;
							Initializer.DebugName = FDebugName(DebugName, DebugNumber++);
							Initializer.IndexBuffer = IndexBufferPtr->IndexBufferRHI;
							Initializer.TotalPrimitiveCount = TrianglesCount;
							Initializer.GeometryType = RTGT_Triangles;
							Initializer.bFastBuild = false;

							TArray<FRayTracingGeometrySegment> GeometrySections;
							GeometrySections.Reserve(RenderSections->Num());

							uint32 TotalNumVertices = 0;
							for (const FSkelMeshRenderSection& Section : *RenderSections)
							{
								TotalNumVertices += Section.GetNumVertices();
							}

							for (const FSkelMeshRenderSection& Section : *RenderSections)
							{
								FRayTracingGeometrySegment Segment;
								Segment.VertexBuffer = PositionVertexBufferPtr->VertexBufferRHI;
								Segment.VertexBufferElementType = VET_Float3;
								Segment.VertexBufferOffset = 0;
								Segment.VertexBufferStride = PositionVertexBufferPtr->GetStride();
								Segment.MaxVertices = TotalNumVertices;
								Segment.FirstPrimitive = Section.BaseIndex / 3;
								Segment.NumPrimitives = Section.NumTriangles;
								Segment.bEnabled = !Section.bDisabled && Section.bVisibleInRayTracing;
								GeometrySections.Add(Segment);
							}
							Initializer.Segments = GeometrySections;

							if (LODIndex < SkelMeshRenderData->CurrentFirstLODIdx) // According to GetMeshElementsConditionallySelectable(), non-resident LODs should just be skipped
							{
								Initializer.Type = ERayTracingGeometryInitializerType::StreamingDestination;
							}

							RayTracingGeometry.GroupHandle = SkelMeshRenderData->RayTracingGeometryGroupHandle;
							RayTracingGeometry.LODIndex = LODIndex;

							RayTracingGeometry.SetInitializer(Initializer);
							RayTracingGeometry.InitResource(RHICmdList);

							bReferencedByStaticSkeletalMeshObjects_RenderThread = true;
						}
					);
				}

				LODData.NumReferencingStaticSkeletalMeshObjects++;
			}
#endif
		}
	}
}

void FSkeletalMeshObjectStatic::ReleaseResources()
{
	for( int32 LODIndex=0;LODIndex < LODs.Num();LODIndex++ )
	{
		FSkeletalMeshObjectLOD& SkelLOD = LODs[LODIndex];
		
		// Skip LODs that have their render data stripped
		if (SkelLOD.SkelMeshRenderData->LODRenderData[LODIndex].GetNumVertices() > 0)
		{
#if RHI_RAYTRACING
			if (IsRayTracingAllowed())
			{
				if (SkelLOD.SkelMeshRenderData->LODRenderData[LODIndex].NumReferencingStaticSkeletalMeshObjects > 0)
				{
					SkelLOD.SkelMeshRenderData->LODRenderData[LODIndex].NumReferencingStaticSkeletalMeshObjects--;

					if (SkelLOD.SkelMeshRenderData->LODRenderData[LODIndex].NumReferencingStaticSkeletalMeshObjects == 0)
					{
						ENQUEUE_RENDER_COMMAND(ResetStaticRayTracingGeometryFlag)(UE::RenderCommandPipe::SkeletalMesh,
							[&bReferencedByStaticSkeletalMeshObjects_RenderThread = SkelLOD.SkelMeshRenderData->LODRenderData[LODIndex].bReferencedByStaticSkeletalMeshObjects_RenderThread]
						{
							bReferencedByStaticSkeletalMeshObjects_RenderThread = false;
						});

						BeginReleaseResource(&SkelLOD.SkelMeshRenderData->LODRenderData[LODIndex].StaticRayTracingGeometry, &UE::RenderCommandPipe::SkeletalMesh);
					}
				}
			}
#endif

			SkelLOD.ReleaseResources();
		}
	}
}

const FVertexFactory* FSkeletalMeshObjectStatic::GetSkinVertexFactory(const FSceneView* View, int32 LODIndex, int32 ChunkIdx, ESkinVertexFactoryMode VFMode) const
{
	return &LODs[LODIndex].VertexFactory; 
}

const FVertexFactory* FSkeletalMeshObjectStatic::GetStaticSkinVertexFactory(int32 LODIndex, int32 ChunkIdx, ESkinVertexFactoryMode VFMode) const
{
	return &LODs[LODIndex].VertexFactory;
}

TArray<FTransform>* FSkeletalMeshObjectStatic::GetComponentSpaceTransforms() const
{
	return nullptr;
}

const TArray<FMatrix44f>& FSkeletalMeshObjectStatic::GetReferenceToLocalMatrices() const
{
	static TArray<FMatrix44f> ReferenceToLocalMatrices;
	return ReferenceToLocalMatrices;
}

int32 FSkeletalMeshObjectStatic::GetLOD() const
{
	// WorkingMinDesiredLODLevel can be a LOD that's not loaded, so need to clamp it to the first loaded LOD
	return FMath::Max<int32>(WorkingMinDesiredLODLevel, SkeletalMeshRenderData->CurrentFirstLODIdx);
}

void FSkeletalMeshObjectStatic::FSkeletalMeshObjectLOD::InitResources(FSkelMeshComponentLODInfo* CompLODInfo)
{
	check(SkelMeshRenderData);
	check(SkelMeshRenderData->LODRenderData.IsValidIndex(LODIndex));

	FSkeletalMeshLODRenderData& LODData = SkelMeshRenderData->LODRenderData[LODIndex];
	
	FPositionVertexBuffer* PositionVertexBufferPtr = &LODData.StaticVertexBuffers.PositionVertexBuffer;
	FStaticMeshVertexBuffer* StaticMeshVertexBufferPtr = &LODData.StaticVertexBuffers.StaticMeshVertexBuffer;
	
	// If we have a vertex color override buffer (and it's the right size) use it
	if (CompLODInfo &&
		CompLODInfo->OverrideVertexColors &&
		CompLODInfo->OverrideVertexColors->GetNumVertices() == PositionVertexBufferPtr->GetNumVertices())
	{
		ColorVertexBuffer = CompLODInfo->OverrideVertexColors;
	}
	else
	{
		ColorVertexBuffer = &LODData.StaticVertexBuffers.ColorVertexBuffer;
	}

	FLocalVertexFactory* VertexFactoryPtr = &VertexFactory;
	FColorVertexBuffer* ColorVertexBufferPtr = ColorVertexBuffer;

	ENQUEUE_RENDER_COMMAND(InitSkeletalMeshStaticSkinVertexFactory)(UE::RenderCommandPipe::SkeletalMesh,
		[VertexFactoryPtr, PositionVertexBufferPtr, StaticMeshVertexBufferPtr, ColorVertexBufferPtr](FRHICommandList& RHICmdList)
		{
			FLocalVertexFactory::FDataType Data;
			PositionVertexBufferPtr->InitResource(RHICmdList);
			StaticMeshVertexBufferPtr->InitResource(RHICmdList);
			ColorVertexBufferPtr->InitResource(RHICmdList);

			PositionVertexBufferPtr->BindPositionVertexBuffer(VertexFactoryPtr, Data);
			StaticMeshVertexBufferPtr->BindTangentVertexBuffer(VertexFactoryPtr, Data);
			StaticMeshVertexBufferPtr->BindPackedTexCoordVertexBuffer(VertexFactoryPtr, Data);
			StaticMeshVertexBufferPtr->BindLightMapVertexBuffer(VertexFactoryPtr, Data, 0);
			ColorVertexBufferPtr->BindColorVertexBuffer(VertexFactoryPtr, Data);

			VertexFactoryPtr->SetData(RHICmdList, Data);
			VertexFactoryPtr->InitResource(RHICmdList);
		});

	bResourcesInitialized = true;
}

/** 
 * Release rendering resources for this LOD 
 */
void FSkeletalMeshObjectStatic::FSkeletalMeshObjectLOD::ReleaseResources()
{	
	BeginReleaseResource(&VertexFactory, &UE::RenderCommandPipe::SkeletalMesh);

#if RHI_RAYTRACING
	BeginReleaseResource(&RayTracingGeometry, &UE::RenderCommandPipe::SkeletalMesh);
#endif // RHI_RAYTRACING

	bResourcesInitialized = false;
}

