// Copyright Epic Games, Inc. All Rights Reserved. 

#include "Components/BaseDynamicMeshSceneProxy.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "PhysicsEngine/BodySetup.h"
#include "RayTracingDefinitions.h"
#include "RayTracingInstance.h"
#include "SceneInterface.h"
#include "PrimitiveUniformShaderParametersBuilder.h"
#include "SceneManagement.h"
#include "Engine/Engine.h"		// for GEngine definition
#include "MeshCardRepresentation.h"
#include "MeshCardBuild.h"
#include "DistanceFieldAtlas.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "MeshPaintVisualize.h"

#include "Implicit/SweepingMeshSDF.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Spatial/FastWinding.h"
#include "HAL/IConsoleManager.h"


static TAutoConsoleVariable<bool> CVarDynamicMeshComponent_AllowDistanceFieldGeneration(
	TEXT("geometry.DynamicMesh.AllowDistanceFieldGeneration"),
	1,
	TEXT("Whether to allow distance field generation for dynamic mesh components")
);

static TAutoConsoleVariable<bool> CVarDynamicMeshComponent_AllowMeshCardGeneration(
	TEXT("geometry.DynamicMesh.AllowMeshCardGeneration"),
	1,
	TEXT("Whether to allow mesh card generation for dynamic mesh components")
);


namespace UE::DynamicMesh
{
	static bool AllowDistanceFieldGeneration()
	{
		// we disallow distance fields on integrated devices to match FSceneRenderer::ShouldPrepareDistanceFieldScene, which notes that they are too likely to hang/fail on the associated large allocations
		return CVarDynamicMeshComponent_AllowDistanceFieldGeneration.GetValueOnAnyThread() && DoesProjectSupportDistanceFields() && !GRHIDeviceIsIntegrated;
	}

	static bool AllowLumenCardGeneration()
	{
		return CVarDynamicMeshComponent_AllowMeshCardGeneration.GetValueOnAnyThread() && FDataDrivenShaderPlatformInfo::GetSupportsLumenGI(GetFeatureLevelShaderPlatform(GMaxRHIFeatureLevel));
	}
}

FBaseDynamicMeshSceneProxy::FBaseDynamicMeshSceneProxy(UBaseDynamicMeshComponent* Component)
	: FPrimitiveSceneProxy(Component),
	ParentBaseComponent(Component),
	bEnableRaytracing(Component->GetEnableRaytracing()),
	bEnableViewModeOverrides(Component->GetViewModeOverridesEnabled()),
	bPreferStaticDrawPath(Component->GetMeshDrawPath() == EDynamicMeshDrawPath::StaticDraw)
{
	MeshRenderBufferSetConverter.ColorSpaceTransformMode = Component->GetVertexColorSpaceTransformMode();

	if (Component->GetColorOverrideMode() == EDynamicMeshComponentColorOverrideMode::Constant)
	{
		MeshRenderBufferSetConverter.ConstantVertexColor = Component->GetConstantOverrideColor();
		MeshRenderBufferSetConverter.bIgnoreVertexColors = true;
	}

	MeshRenderBufferSetConverter.bUsePerTriangleNormals = Component->GetFlatShadingEnabled();
	
	SetCollisionData();

	FMaterialRelevance MaterialRelevance = Component->GetMaterialRelevance(GetScene().GetFeatureLevel());
	bOpaqueOrMasked = MaterialRelevance.bOpaque;

	// set initial distance field flags based on whether we will have one, after its async build
	bool bWillHaveDistanceField = Component->GetDistanceFieldMode() != EDynamicMeshComponentDistanceFieldMode::NoDistanceField 
									&& UE::DynamicMesh::AllowDistanceFieldGeneration();
	bSupportsDistanceFieldRepresentation = bWillHaveDistanceField;
	bAffectDistanceFieldLighting = bWillHaveDistanceField;
	// note whether lumen is enabled will depend on the distance field flags (in some cases)
	UpdateVisibleInLumenScene();
}

FBaseDynamicMeshSceneProxy::~FBaseDynamicMeshSceneProxy()
{
	// destroy all existing renderbuffers
	for (FMeshRenderBufferSet* BufferSet : AllocatedBufferSets)
	{
		FMeshRenderBufferSet::DestroyRenderBufferSet(BufferSet);
	}
}

FMeshRenderBufferSet* FBaseDynamicMeshSceneProxy::AllocateNewRenderBufferSet()
{
	// should we hang onto these and destroy them in constructor? leaving to subclass seems risky?
	FMeshRenderBufferSet* RenderBufferSet = new FMeshRenderBufferSet(GetScene().GetFeatureLevel());

	RenderBufferSet->Material = UMaterial::GetDefaultMaterial(MD_Surface);
	RenderBufferSet->bEnableRaytracing = this->bEnableRaytracing && this->IsVisibleInRayTracing();

	AllocatedSetsLock.Lock();
	AllocatedBufferSets.Add(RenderBufferSet);
	AllocatedSetsLock.Unlock();

	return RenderBufferSet;
}

void FBaseDynamicMeshSceneProxy::ReleaseRenderBufferSet(FMeshRenderBufferSet* BufferSet)
{
	FScopeLock Lock(&AllocatedSetsLock);
	if (ensure(AllocatedBufferSets.Contains(BufferSet)))
	{
		AllocatedBufferSets.Remove(BufferSet);
		Lock.Unlock();

		FMeshRenderBufferSet::DestroyRenderBufferSet(BufferSet);
	}
}

int32 FBaseDynamicMeshSceneProxy::GetNumMaterials() const
{
	return ParentBaseComponent->GetNumMaterials();
}

UMaterialInterface* FBaseDynamicMeshSceneProxy::GetMaterial(int32 k) const
{
	UMaterialInterface* Material = ParentBaseComponent->GetMaterial(k);
	return (Material != nullptr) ? Material : UMaterial::GetDefaultMaterial(MD_Surface);
}

void FBaseDynamicMeshSceneProxy::UpdatedReferencedMaterials()
{
#if WITH_EDITOR
	TArray<UMaterialInterface*> Materials;
	ParentBaseComponent->GetUsedMaterials(Materials, true);

	// Temporarily disable material verification while the enqueued render command is in flight.
	// The original value for bVerifyUsedMaterials gets restored when the command is executed.
	// If we do not do this, material verification might spuriously fail in cases where the render command for changing
	// the verfifcation material is still in flight but the render thread is already trying to render the mesh.
	const uint8 bRestoreVerifyUsedMaterials = bVerifyUsedMaterials;
	bVerifyUsedMaterials = false;

	ENQUEUE_RENDER_COMMAND(FMeshRenderBufferSetDestroy)(
		[this, Materials, bRestoreVerifyUsedMaterials](FRHICommandListImmediate& RHICmdList)
	{
		this->SetUsedMaterialForVerification(Materials);
		this->bVerifyUsedMaterials = bRestoreVerifyUsedMaterials;
	});
#endif
}

FMaterialRenderProxy* FBaseDynamicMeshSceneProxy::GetEngineVertexColorMaterialProxy(FMeshElementCollector& Collector, const FEngineShowFlags& EngineShowFlags, bool bProxyIsSelected, bool bIsHovered)
{
	FMaterialRenderProxy* ForceOverrideMaterialProxy = nullptr;
#if UE_ENABLE_DEBUG_DRAWING
	if (bProxyIsSelected && EngineShowFlags.VertexColors && AllowDebugViewmodes())
	{
		// Note: static mesh renderer does something more complicated involving per-section selection, but whole component selection seems ok for now.
		if (FMaterialRenderProxy* VertexColorVisualizationMaterialInstance = MeshPaintVisualize::GetMaterialRenderProxy(bProxyIsSelected, bIsHovered))
		{
			Collector.RegisterOneFrameMaterialProxy(VertexColorVisualizationMaterialInstance);
			ForceOverrideMaterialProxy = VertexColorVisualizationMaterialInstance;
		}
	}
#endif
	return ForceOverrideMaterialProxy;
}

bool FBaseDynamicMeshSceneProxy::IsCollisionView(const FEngineShowFlags& EngineShowFlags, bool& bDrawSimpleCollision, bool& bDrawComplexCollision) const
{
	bDrawSimpleCollision = bDrawComplexCollision = false;

	bool bDrawCollisionView = (EngineShowFlags.CollisionVisibility || EngineShowFlags.CollisionPawn);

#if UE_ENABLE_DEBUG_DRAWING
	// If in a 'collision view' and collision is enabled
	FScopeLock Lock(&CachedCollisionLock);
	if (bHasCollisionData && bDrawCollisionView && IsCollisionEnabled())
	{
		// See if we have a response to the interested channel
		bool bHasResponse = EngineShowFlags.CollisionPawn && CollisionResponse.GetResponse(ECC_Pawn) != ECR_Ignore;
		bHasResponse |= EngineShowFlags.CollisionVisibility && CollisionResponse.GetResponse(ECC_Visibility) != ECR_Ignore;

		if(bHasResponse)
		{
			// Visibility uses complex and pawn uses simple. However, if UseSimpleAsComplex or UseComplexAsSimple is used we need to adjust accordingly
			bDrawComplexCollision = (EngineShowFlags.CollisionVisibility && CollisionTraceFlag != ECollisionTraceFlag::CTF_UseSimpleAsComplex) || (EngineShowFlags.CollisionPawn && CollisionTraceFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple);
			bDrawSimpleCollision  = (EngineShowFlags.CollisionPawn && CollisionTraceFlag != ECollisionTraceFlag::CTF_UseComplexAsSimple) || (EngineShowFlags.CollisionVisibility && CollisionTraceFlag == ECollisionTraceFlag::CTF_UseSimpleAsComplex);
		}
	}
#endif
	return bDrawCollisionView;
}

void FBaseDynamicMeshSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_BaseDynamicMeshSceneProxy_GetDynamicMeshElements);

	const FEngineShowFlags& EngineShowFlags = ViewFamily.EngineShowFlags;
	bool bIsWireframeViewMode = (AllowDebugViewmodes() && EngineShowFlags.Wireframe);
	bool bWantWireframeOnShaded = ParentBaseComponent->GetEnableWireframeRenderPass();
	bool bWireframe = bIsWireframeViewMode || bWantWireframeOnShaded;
	const bool bProxyIsSelected = IsSelected();


	TArray<FMeshRenderBufferSet*> Buffers;
	GetActiveRenderBufferSets(Buffers);

#if UE_ENABLE_DEBUG_DRAWING
	bool bDrawSimpleCollision = false, bDrawComplexCollision = false;
	const bool bDrawCollisionView = IsCollisionView(EngineShowFlags, bDrawSimpleCollision, bDrawComplexCollision);

	// If we're in a collision view, run the only draw the collision and return without drawing mesh normally
	if (bDrawCollisionView)
	{
		GetCollisionDynamicMeshElements(Buffers, EngineShowFlags, bDrawCollisionView, bDrawSimpleCollision, bDrawComplexCollision, bProxyIsSelected, Views, VisibilityMap, Collector);
		return;
	}
#endif

	// Get wireframe material proxy if requested and available, otherwise disable wireframe
	FMaterialRenderProxy* WireframeMaterialProxy = nullptr;
	if (bWireframe)
	{
		UMaterialInterface* WireframeMaterial = UBaseDynamicMeshComponent::GetDefaultWireframeMaterial_RenderThread();
		if (WireframeMaterial != nullptr)
		{
			FLinearColor UseWireframeColor = (bProxyIsSelected && (bWantWireframeOnShaded == false || bIsWireframeViewMode))  ?
				GEngine->GetSelectedMaterialColor() : ParentBaseComponent->WireframeColor;
			FColoredMaterialRenderProxy* WireframeMaterialInstance = new FColoredMaterialRenderProxy(
				WireframeMaterial->GetRenderProxy(), UseWireframeColor);
			Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);
			WireframeMaterialProxy = WireframeMaterialInstance;
		}
		else
		{
			bWireframe = false;
		}
	}

	FMaterialRenderProxy* ForceOverrideMaterialProxy = GetEngineVertexColorMaterialProxy(Collector, EngineShowFlags, bProxyIsSelected, IsHovered());
	// If engine show flags aren't setting vertex color, also check if the component requested custom vertex color modes for the dynamic mesh
	if (!ForceOverrideMaterialProxy)
	{
		const bool bVertexColor = ParentBaseComponent->ColorMode == EDynamicMeshComponentColorOverrideMode::VertexColors ||
			ParentBaseComponent->ColorMode == EDynamicMeshComponentColorOverrideMode::Polygroups ||
			ParentBaseComponent->ColorMode == EDynamicMeshComponentColorOverrideMode::Constant;
		if (bVertexColor)
		{
			ForceOverrideMaterialProxy = UBaseDynamicMeshComponent::GetDefaultVertexColorMaterial_RenderThread()->GetRenderProxy();
		}
	}

	ESceneDepthPriorityGroup DepthPriority = SDPG_World;


	FMaterialRenderProxy* SecondaryMaterialProxy = ForceOverrideMaterialProxy;
	if (ParentBaseComponent->HasSecondaryRenderMaterial() && ForceOverrideMaterialProxy == nullptr)
	{
		SecondaryMaterialProxy = ParentBaseComponent->GetSecondaryRenderMaterial()->GetRenderProxy();
	}
	bool bDrawSecondaryBuffers = ParentBaseComponent->GetSecondaryBuffersVisibility();

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];

			// Draw the mesh.
			for (FMeshRenderBufferSet* BufferSet : Buffers)
			{
				FMaterialRenderProxy* MaterialProxy = ForceOverrideMaterialProxy;
				if (!MaterialProxy)
				{
					UMaterialInterface* UseMaterial = BufferSet->Material;
					if (ParentBaseComponent->HasOverrideRenderMaterial(0))
					{
						UseMaterial = ParentBaseComponent->GetOverrideRenderMaterial(0);
					}
					MaterialProxy = UseMaterial->GetRenderProxy();
				}

				if (BufferSet->TriangleCount == 0)
				{
					continue;
				}

				// lock buffers so that they aren't modified while we are submitting them
				FScopeLock BuffersLock(&BufferSet->BuffersLock);

				// do we need separate one of these for each MeshRenderBufferSet?
				FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
				FPrimitiveUniformShaderParametersBuilder Builder;
				BuildUniformShaderParameters(Builder);
				DynamicPrimitiveUniformBuffer.Set(Collector.GetRHICommandList(), Builder);

				// If we want Wireframe-on-Shaded, we have to draw the solid. If View Mode Overrides are enabled, the solid
				// will be replaced with it's wireframe, so we might as well not. 
				bool bDrawSolidWithWireframe = ( bWantWireframeOnShaded && (bIsWireframeViewMode == false || bEnableViewModeOverrides == false) );

				if (BufferSet->IndexBuffer.Indices.Num() > 0)
				{
					if (bWireframe)
					{
						if (bDrawSolidWithWireframe)
						{
							DrawBatch(Collector, *BufferSet, BufferSet->IndexBuffer, MaterialProxy, /*bWireframe*/false, DepthPriority, ViewIndex, DynamicPrimitiveUniformBuffer);
						}
						DrawBatch(Collector, *BufferSet, BufferSet->IndexBuffer, WireframeMaterialProxy, /*bWireframe*/true, DepthPriority, ViewIndex, DynamicPrimitiveUniformBuffer);
					}
					else
					{
						DrawBatch(Collector, *BufferSet, BufferSet->IndexBuffer, MaterialProxy, /*bWireframe*/false, DepthPriority, ViewIndex, DynamicPrimitiveUniformBuffer);
					}
				}

				// draw secondary buffer if we have it, falling back to base material if we don't have the Secondary material
				FMaterialRenderProxy* UseSecondaryMaterialProxy = (SecondaryMaterialProxy != nullptr) ? SecondaryMaterialProxy : MaterialProxy;
				if (bDrawSecondaryBuffers && BufferSet->SecondaryIndexBuffer.Indices.Num() > 0 && UseSecondaryMaterialProxy != nullptr)
				{
					if (bWireframe)
					{
						if (bDrawSolidWithWireframe)
						{
							DrawBatch(Collector, *BufferSet, BufferSet->SecondaryIndexBuffer, UseSecondaryMaterialProxy, /*bWireframe*/false, DepthPriority, ViewIndex, DynamicPrimitiveUniformBuffer);
						}
						DrawBatch(Collector, *BufferSet, BufferSet->SecondaryIndexBuffer, UseSecondaryMaterialProxy, /*bWireframe*/true, DepthPriority, ViewIndex, DynamicPrimitiveUniformBuffer);
					}
					else
					{
						DrawBatch(Collector, *BufferSet, BufferSet->SecondaryIndexBuffer, UseSecondaryMaterialProxy, /*bWireframe*/false, DepthPriority, ViewIndex, DynamicPrimitiveUniformBuffer);
					}
				}
			}
		}
	}

#if UE_ENABLE_DEBUG_DRAWING
	GetCollisionDynamicMeshElements(Buffers, EngineShowFlags, bDrawCollisionView, bDrawSimpleCollision, bDrawComplexCollision, bProxyIsSelected, Views, VisibilityMap, Collector);
#endif
}

void FBaseDynamicMeshSceneProxy::GetCollisionDynamicMeshElements(TArray<FMeshRenderBufferSet*>& Buffers, 
	const FEngineShowFlags& EngineShowFlags, bool bDrawCollisionView, bool bDrawSimpleCollision, bool bDrawComplexCollision, bool bProxyIsSelected,
	const TArray<const FSceneView*>& Views, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
#if UE_ENABLE_DEBUG_DRAWING
	FScopeLock Lock(&CachedCollisionLock);

	if (!bHasCollisionData)
	{
		return;
	}

	// Note: This is closely following StaticMeshRender.cpp's collision rendering code, from its GetDynamicMeshElements() implementation
	FColor SimpleCollisionColor = FColor(157, 149, 223, 255);
	FColor ComplexCollisionColor = FColor(0, 255, 255, 255);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		if (VisibilityMap & (1 << ViewIndex))
		{
			const FSceneView* View = Views[ViewIndex];

			if(AllowDebugViewmodes())
			{
				// Should we draw the mesh wireframe to indicate we are using the mesh as collision
				bool bDrawComplexWireframeCollision = (EngineShowFlags.Collision && IsCollisionEnabled() && CollisionTraceFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple);

				// If drawing complex collision as solid or wireframe
				if (bHasComplexMeshData && (bDrawComplexWireframeCollision || (bDrawCollisionView && bDrawComplexCollision)))
				{
					bool bDrawWireframe = !bDrawCollisionView;

					UMaterial* MaterialToUse = UMaterial::GetDefaultMaterial(MD_Surface);
					FLinearColor DrawCollisionColor = GetWireframeColor();
					// Collision view modes draw collision mesh as solid
					if(bDrawCollisionView)
					{
						MaterialToUse = GEngine->ShadedLevelColorationUnlitMaterial;
					}
					// Wireframe, choose color based on complex or simple
					else
					{
						MaterialToUse = GEngine->WireframeMaterial;
						DrawCollisionColor = (CollisionTraceFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple) ? SimpleCollisionColor : ComplexCollisionColor;
					}
					// Create colored proxy
					FColoredMaterialRenderProxy* CollisionMaterialInstance = new FColoredMaterialRenderProxy(MaterialToUse->GetRenderProxy(), DrawCollisionColor);
					Collector.RegisterOneFrameMaterialProxy(CollisionMaterialInstance);

					// Draw the mesh with collision materials
					for (FMeshRenderBufferSet* BufferSet : Buffers)
					{

						if (BufferSet->TriangleCount == 0)
						{
							continue;
						}

						// lock buffers so that they aren't modified while we are submitting them
						FScopeLock BuffersLock(&BufferSet->BuffersLock);

						// do we need separate one of these for each MeshRenderBufferSet?
						FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
						FPrimitiveUniformShaderParametersBuilder Builder;
						BuildUniformShaderParameters(Builder);
						DynamicPrimitiveUniformBuffer.Set(Collector.GetRHICommandList(), Builder);

						if (BufferSet->IndexBuffer.Indices.Num() > 0)
						{
							DrawBatch(Collector, *BufferSet, BufferSet->IndexBuffer, CollisionMaterialInstance, bDrawWireframe, SDPG_World, ViewIndex, DynamicPrimitiveUniformBuffer);
						}
					}
				}
			}

			// Draw simple collision as wireframe if 'show collision', collision is enabled, and we are not using the complex as the simple
			const bool bDrawSimpleWireframeCollision = (EngineShowFlags.Collision && IsCollisionEnabled() && CollisionTraceFlag != ECollisionTraceFlag::CTF_UseComplexAsSimple);

			if((bDrawSimpleCollision || bDrawSimpleWireframeCollision))
			{
				if (ParentBaseComponent->GetBodySetup())
				{
					// Avoid zero scaling, otherwise GeomTransform below will assert
					if (FMath::Abs(GetLocalToWorld().Determinant()) > UE_SMALL_NUMBER)
					{
						const bool bDrawSolid = !bDrawSimpleWireframeCollision;

						if (AllowDebugViewmodes() && bDrawSolid)
						{
							// Make a material for drawing solid collision stuff
							FColoredMaterialRenderProxy* SolidMaterialInstance = new FColoredMaterialRenderProxy(
								GEngine->ShadedLevelColorationUnlitMaterial->GetRenderProxy(),
								GetWireframeColor()
							);

							Collector.RegisterOneFrameMaterialProxy(SolidMaterialInstance);

							FTransform GeomTransform(GetLocalToWorld());
							CachedAggGeom.GetAggGeom(GeomTransform, GetWireframeColor().ToFColor(true), SolidMaterialInstance, false, true, AlwaysHasVelocity(), ViewIndex, Collector);
						}
						// wireframe
						else
						{
							FTransform GeomTransform(GetLocalToWorld());
							CachedAggGeom.GetAggGeom(GeomTransform, GetSelectionColor(SimpleCollisionColor, bProxyIsSelected, IsHovered()).ToFColor(true), NULL, bOwnerIsNull, false, AlwaysHasVelocity(), ViewIndex, Collector);
						}

						// Note: if dynamic mesh component could have nav collision data, we'd also draw that here (see the similar code in StaticMeshRenderer.cpp)
					}
				}
			}
		}
	}
#endif // UE_ENABLE_DEBUG_DRAWING

}

void FBaseDynamicMeshSceneProxy::DrawBatch(FMeshElementCollector& Collector, const FMeshRenderBufferSet& RenderBuffers, const FDynamicMeshIndexBuffer32& IndexBuffer, FMaterialRenderProxy* UseMaterial, bool bWireframe, ESceneDepthPriorityGroup DepthPriority, int ViewIndex, FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer) const
{
	FMeshBatch& Mesh = Collector.AllocateMesh();
	FMeshBatchElement& BatchElement = Mesh.Elements[0];
	BatchElement.IndexBuffer = &IndexBuffer;
	Mesh.bWireframe = bWireframe;
	//Mesh.bDisableBackfaceCulling = bWireframe;		// todo: doing this would be more consistent w/ other meshes in wireframe mode, but it is problematic for modeling tools - perhaps should be configurable
	Mesh.VertexFactory = &RenderBuffers.VertexFactory;
	Mesh.MaterialRenderProxy = UseMaterial;

	BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

	BatchElement.FirstIndex = 0;
	BatchElement.NumPrimitives = IndexBuffer.Indices.Num() / 3;
	BatchElement.MinVertexIndex = 0;
	BatchElement.MaxVertexIndex = RenderBuffers.PositionVertexBuffer.GetNumVertices() - 1;
	Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
	Mesh.Type = PT_TriangleList;
	Mesh.DepthPriorityGroup = DepthPriority;
	// if this is a wireframe draw pass then we do not want to apply View Mode Overrides
	Mesh.bCanApplyViewModeOverrides = (bWireframe) ? false : this->bEnableViewModeOverrides;
	Collector.AddMesh(ViewIndex, Mesh);
}


bool FBaseDynamicMeshSceneProxy::AllowStaticDrawPath(const FSceneView* View) const
{
	bool bAllowDebugViews = AllowDebugViewmodes();
	if (!bAllowDebugViews)
	{
		return true;
	}
	const FEngineShowFlags& EngineShowFlags = View->Family->EngineShowFlags;
	bool bWantWireframeOnShaded = ParentBaseComponent->GetEnableWireframeRenderPass();
	bool bWireframe = EngineShowFlags.Wireframe || bWantWireframeOnShaded;
	if (bWireframe)
	{
		return false;
	}
	bool bDrawSimpleCollision = false, bDrawComplexCollision = false;
	bool bDrawCollisionView = IsCollisionView(EngineShowFlags, bDrawSimpleCollision, bDrawComplexCollision); // check for the full collision views
	bool bDrawCollisionFlags = EngineShowFlags.Collision && IsCollisionEnabled(); // check for single component collision rendering
	bool bDrawCollision = bDrawCollisionFlags || bDrawSimpleCollision || bDrawCollisionView;
	if (bDrawCollision)
	{
		return false;
	}
	bool bIsSelected = IsSelected();
	bool bColorOverrides = (bIsSelected && EngineShowFlags.VertexColors) || (ParentBaseComponent->ColorMode != EDynamicMeshComponentColorOverrideMode::None);
	return !bColorOverrides;
}


void FBaseDynamicMeshSceneProxy::DrawStaticElements(FStaticPrimitiveDrawInterface* PDI)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_BaseDynamicMeshSceneProxy_DrawStaticElements);

	if (!bPreferStaticDrawPath)
	{
		return;
	}

	UMaterialInterface* UseSecondaryMaterial = nullptr;
	if (ParentBaseComponent->HasSecondaryRenderMaterial())
	{
		UseSecondaryMaterial = ParentBaseComponent->GetSecondaryRenderMaterial();
	}
	bool bDrawSecondaryBuffers = ParentBaseComponent->GetSecondaryBuffersVisibility();

	ESceneDepthPriorityGroup DepthPriority = SDPG_World;

	TArray<FMeshRenderBufferSet*> Buffers;
	GetActiveRenderBufferSets(Buffers);
	PDI->ReserveMemoryForMeshes(Buffers.Num());

	// Draw the mesh.
	int32 SectionIndexCounter = 0;
	for (FMeshRenderBufferSet* BufferSet : Buffers)
	{
		if (BufferSet->TriangleCount == 0)
		{
			continue;
		}

		UMaterialInterface* UseMaterial = BufferSet->Material;
		if (ParentBaseComponent->HasOverrideRenderMaterial(0))
		{
			UseMaterial = ParentBaseComponent->GetOverrideRenderMaterial(0);
		}
		FMaterialRenderProxy* MaterialProxy = UseMaterial->GetRenderProxy();

		// lock buffers so that they aren't modified while we are submitting them
		FScopeLock BuffersLock(&BufferSet->BuffersLock);

		FMeshBatch MeshBatch;

		FMeshBatchElement& BatchElement = MeshBatch.Elements[0];
		BatchElement.IndexBuffer = &BufferSet->IndexBuffer;
		MeshBatch.VertexFactory = &BufferSet->VertexFactory;
		MeshBatch.MaterialRenderProxy = MaterialProxy;

		BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();
		BatchElement.NumPrimitives = BufferSet->IndexBuffer.Indices.Num() / 3;
		BatchElement.FirstIndex = 0;
		BatchElement.MinVertexIndex = 0;
		BatchElement.MaxVertexIndex = BufferSet->PositionVertexBuffer.GetNumVertices() - 1;
		MeshBatch.ReverseCulling = IsLocalToWorldDeterminantNegative();
		MeshBatch.Type = PT_TriangleList;
		MeshBatch.DepthPriorityGroup = DepthPriority;
		MeshBatch.bCanApplyViewModeOverrides = this->bEnableViewModeOverrides;
		MeshBatch.LODIndex = 0;
		MeshBatch.SegmentIndex = SectionIndexCounter;
		MeshBatch.MeshIdInPrimitive = SectionIndexCounter;
		SectionIndexCounter++;

		MeshBatch.LCI = nullptr; // lightmap cache interface (allowed to be null)
		MeshBatch.CastShadow = true;
		MeshBatch.bUseForMaterial = true;
		MeshBatch.bDitheredLODTransition = false;
		MeshBatch.bUseForDepthPass = true;
		MeshBatch.bUseAsOccluder = ShouldUseAsOccluder();

		PDI->DrawMesh(MeshBatch, FLT_MAX);
	}

}


void FBaseDynamicMeshSceneProxy::SetCollisionData()
{
#if UE_ENABLE_DEBUG_DRAWING
	FScopeLock Lock(&CachedCollisionLock);
	bHasCollisionData = true;
	bOwnerIsNull = ParentBaseComponent->GetOwner() == nullptr;
	bHasComplexMeshData = false;
	if (UBodySetup* BodySetup = ParentBaseComponent->GetBodySetup())
	{
		CollisionTraceFlag = BodySetup->GetCollisionTraceFlag();
		CachedAggGeom = BodySetup->AggGeom;
		
		if (IInterface_CollisionDataProvider* CDP = Cast<IInterface_CollisionDataProvider>(ParentBaseComponent))
		{
			bHasComplexMeshData = CDP->ContainsPhysicsTriMeshData(BodySetup->bMeshCollideAll);
		}
	}
	else
	{
		CachedAggGeom = FKAggregateGeom();
	}
	CollisionResponse = ParentBaseComponent->GetCollisionResponseToChannels();
#endif
}

#if RHI_RAYTRACING

bool FBaseDynamicMeshSceneProxy::IsRayTracingRelevant() const 
{
	return true;
}

bool FBaseDynamicMeshSceneProxy::HasRayTracingRepresentation() const
{
	return true;
}


void FBaseDynamicMeshSceneProxy::GetDynamicRayTracingInstances(FRayTracingInstanceCollector& Collector)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_BaseDynamicMeshSceneProxy_GetDynamicRayTracingInstances);

	ESceneDepthPriorityGroup DepthPriority = SDPG_World;

	TArray<FMeshRenderBufferSet*> Buffers;
	GetActiveRenderBufferSets(Buffers);

	// will use this material instead of any others below, if it becomes non-null
	UMaterialInterface* ForceOverrideMaterial = nullptr;
	const bool bVertexColor = ParentBaseComponent->ColorMode == EDynamicMeshComponentColorOverrideMode::VertexColors ||
		ParentBaseComponent->ColorMode == EDynamicMeshComponentColorOverrideMode::Polygroups ||
		ParentBaseComponent->ColorMode == EDynamicMeshComponentColorOverrideMode::Constant;
	if (bVertexColor)
	{
		ForceOverrideMaterial = UBaseDynamicMeshComponent::GetDefaultVertexColorMaterial_RenderThread();
	}

	UMaterialInterface* UseSecondaryMaterial = ForceOverrideMaterial;
	if (ParentBaseComponent->HasSecondaryRenderMaterial() && ForceOverrideMaterial == nullptr)
	{
		UseSecondaryMaterial = ParentBaseComponent->GetSecondaryRenderMaterial();
	}
	bool bDrawSecondaryBuffers = ParentBaseComponent->GetSecondaryBuffersVisibility();

	// is it safe to share this between primary and secondary raytracing batches?
	FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
	FPrimitiveUniformShaderParametersBuilder Builder;
	BuildUniformShaderParameters(Builder);
	DynamicPrimitiveUniformBuffer.Set(Collector.GetRHICommandList(), Builder);

	// Draw the active buffer sets
	for (FMeshRenderBufferSet* BufferSet : Buffers)
	{
		UMaterialInterface* UseMaterial = BufferSet->Material;
		if (ParentBaseComponent->HasOverrideRenderMaterial(0))
		{
			UseMaterial = ParentBaseComponent->GetOverrideRenderMaterial(0);
		}
		if (ForceOverrideMaterial)
		{
			UseMaterial = ForceOverrideMaterial;
		}
		FMaterialRenderProxy* MaterialProxy = UseMaterial->GetRenderProxy();

		if (BufferSet->TriangleCount == 0)
		{
			continue;
		}
		if (BufferSet->bIsRayTracingDataValid == false)
		{
			continue;
		}

		// Lock buffers so that they aren't modified while we are submitting them.
		FScopeLock BuffersLock(&BufferSet->BuffersLock);

		// draw primary index buffer
		if (BufferSet->IndexBuffer.Indices.Num() > 0
			&& BufferSet->PrimaryRayTracingGeometry.IsValid())
		{
			ensure(BufferSet->PrimaryRayTracingGeometry.Initializer.IndexBuffer.IsValid());
			DrawRayTracingBatch(Collector, *BufferSet, BufferSet->IndexBuffer, BufferSet->PrimaryRayTracingGeometry, MaterialProxy, DepthPriority, DynamicPrimitiveUniformBuffer);
		}

		// draw secondary index buffer if we have it, falling back to base material if we don't have the Secondary material
		FMaterialRenderProxy* UseSecondaryMaterialProxy = (UseSecondaryMaterial != nullptr) ? UseSecondaryMaterial->GetRenderProxy() : MaterialProxy;
		if (bDrawSecondaryBuffers
			&& BufferSet->SecondaryIndexBuffer.Indices.Num() > 0
			&& UseSecondaryMaterialProxy != nullptr
			&& BufferSet->SecondaryRayTracingGeometry.IsValid())
		{
			ensure(BufferSet->SecondaryRayTracingGeometry.Initializer.IndexBuffer.IsValid());
			DrawRayTracingBatch(Collector, *BufferSet, BufferSet->SecondaryIndexBuffer, BufferSet->SecondaryRayTracingGeometry, UseSecondaryMaterialProxy, DepthPriority, DynamicPrimitiveUniformBuffer);
		}
	}
}

void FBaseDynamicMeshSceneProxy::DrawRayTracingBatch(FRayTracingInstanceCollector& Collector, const FMeshRenderBufferSet& RenderBuffers, const FDynamicMeshIndexBuffer32& IndexBuffer, FRayTracingGeometry& RayTracingGeometry, FMaterialRenderProxy* UseMaterialProxy, ESceneDepthPriorityGroup DepthPriority, FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer) const
{
	ensure(RayTracingGeometry.Initializer.IndexBuffer.IsValid());

	FRayTracingInstance RayTracingInstance;
	RayTracingInstance.Geometry = &RayTracingGeometry;
	RayTracingInstance.InstanceTransforms.Add(GetLocalToWorld());

	uint32 SectionIdx = 0;
	FMeshBatch MeshBatch;

	MeshBatch.VertexFactory = &RenderBuffers.VertexFactory;
	MeshBatch.SegmentIndex = 0;
	MeshBatch.MaterialRenderProxy = UseMaterialProxy;
	MeshBatch.Type = PT_TriangleList;
	MeshBatch.DepthPriorityGroup = DepthPriority;
	MeshBatch.bCanApplyViewModeOverrides = this->bEnableViewModeOverrides;
	MeshBatch.CastRayTracedShadow = IsShadowCast(Collector.GetReferenceView());

	FMeshBatchElement& BatchElement = MeshBatch.Elements[0];
	BatchElement.IndexBuffer = &IndexBuffer;
	BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;
	BatchElement.FirstIndex = 0;
	BatchElement.NumPrimitives = IndexBuffer.Indices.Num() / 3;
	BatchElement.MinVertexIndex = 0;
	BatchElement.MaxVertexIndex = RenderBuffers.PositionVertexBuffer.GetNumVertices() - 1;

	RayTracingInstance.Materials.Add(MeshBatch);

	Collector.AddRayTracingInstance(MoveTemp(RayTracingInstance));
}

#endif // RHI_RAYTRACING






const FCardRepresentationData* FBaseDynamicMeshSceneProxy::GetMeshCardRepresentation() const
{
	if (MeshCards.IsValid() && bMeshCardsValid)
	{
		return MeshCards.Get();
	}
	return nullptr;
}

namespace UE::Local
{
	// Same as LumenMeshCards::GetAxisAlignedDirection
	FVector3f GetAxisAlignedDirection(int32 AxisAlignedDirectionIndex, int32& AxisIndex)
	{
		AxisIndex = AxisAlignedDirectionIndex / 2;
		FVector3f Direction(0.0f, 0.0f, 0.0f);
		Direction[AxisIndex] = AxisAlignedDirectionIndex & 1 ? 1.0f : -1.0f;
		return Direction;
	}
}

void FBaseDynamicMeshSceneProxy::UpdateLumenCardsFromBounds()
{
	bMeshCardsValid = false;
	if (!bVisibleInLumenScene || !UE::DynamicMesh::AllowLumenCardGeneration())
	{
		MeshCards.Reset();
		return;
	}

	FBox Box = ParentBaseComponent->GetLocalBounds().GetBox();

	if (MeshCards.IsValid() == false)
	{
		MeshCards = MakePimpl<FCardRepresentationData>();
	}

	*MeshCards = FCardRepresentationData();		 // increments ID
	FMeshCardsBuildData& CardData = MeshCards->MeshCardsBuildData;

	CardData.Bounds = Box;


	struct FCardDirection
	{
		int DirectionIndex;
		FVector AxisZ;
		FVector AxisX;
		FVector AxisY;
		int AxisZIndex;
	};
	TArray<FCardDirection> CardDirections;
	for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
	{
		FCardDirection Direction;
		Direction.DirectionIndex = DirectionIndex;
		Direction.AxisZ = (FVector)UE::Local::GetAxisAlignedDirection(DirectionIndex, Direction.AxisZIndex);
		Direction.AxisZ.FindBestAxisVectors(Direction.AxisX, Direction.AxisY);
		Direction.AxisX = FVector::CrossProduct(Direction.AxisZ, Direction.AxisY);
		Direction.AxisX.Normalize();
		CardDirections.Add(Direction);
	}

	FVector3d Center = Box.GetCenter();
	FVector3d Extents = Box.GetExtent();
	float CardOffset = 5.0;

	CardData.CardBuildData.SetNum(CardDirections.Num());
	for (int32 CardIndex = 0; CardIndex < CardDirections.Num(); ++CardIndex)
	{
		FCardDirection Direction = CardDirections[CardIndex];
		FLumenCardOBBf OBB;
		OBB.AxisZ = (FVector3f)Direction.AxisZ;
		OBB.AxisX = (FVector3f)Direction.AxisX;
		OBB.AxisY = (FVector3f)Direction.AxisY;
	
		// project 3D mesh extents onto the specific axes of this CardOBB  (this just reshuffles them but the combinatorics are messy)
		double ExtentX = FMathd::Abs(Direction.AxisX.Dot( Extents ));
		double ExtentY = FMathd::Abs(Direction.AxisY.Dot( Extents ));
		double ExtentZ = FMathd::Abs(Direction.AxisZ.Dot( Extents ));

		// Translate the box along the AxisZ axis so the box center is at the middle of the axis-face 
		FVector3d LocalCenter = Center;
		LocalCenter +=  ExtentZ * Direction.AxisZ;

		// hardcoding the card box to cover half the mesh bounds along Z (and full mesh box along X and Y)
		double CardExtentZ = ExtentZ * 0.5;	

		// shift the card box center so that the +Z face lies on the mesh box face, then bump it forward a bit
		LocalCenter += (-CardExtentZ + CardOffset) * Direction.AxisZ;

		// set up the box for the card
		OBB.Extent = (FVector3f)FVector3d(ExtentX, ExtentY, CardExtentZ + CardOffset*0.5);
		OBB.Origin = (FVector3f)LocalCenter;

		CardData.CardBuildData[CardIndex].OBB = OBB;
		CardData.CardBuildData[CardIndex].AxisAlignedDirectionIndex = Direction.DirectionIndex;
	}

	bMeshCardsValid = true;
}



void FBaseDynamicMeshSceneProxy::GetDistanceFieldAtlasData(const class FDistanceFieldVolumeData*& OutDistanceFieldData, float& SelfShadowBias) const
{
	if (DistanceField.IsValid() && bDistanceFieldValid)
	{
		OutDistanceFieldData = DistanceField.Get();
		SelfShadowBias = 0.0;
	}
	else
	{
		OutDistanceFieldData = nullptr;
		SelfShadowBias = 0.0;
	}
}


void FBaseDynamicMeshSceneProxy::GetDistanceFieldInstanceData(TArray<FRenderTransform>& InstanceLocalToPrimitiveTransforms) const
{
	check(InstanceLocalToPrimitiveTransforms.IsEmpty());
	if (DistanceField.IsValid() && bDistanceFieldValid)
	{
		InstanceLocalToPrimitiveTransforms.Add(FRenderTransform::Identity);
	}
}

bool FBaseDynamicMeshSceneProxy::HasDistanceFieldRepresentation() const 
{
	return CastsDynamicShadow() && AffectsDistanceFieldLighting() && bDistanceFieldValid && DistanceField.IsValid();
}

bool FBaseDynamicMeshSceneProxy::HasDynamicIndirectShadowCasterRepresentation() const
{
	return bCastsDynamicIndirectShadow && FBaseDynamicMeshSceneProxy::HasDistanceFieldRepresentation();
}






static int32 ComputeLinearVoxelIndex(FIntVector VoxelCoordinate, FIntVector VolumeDimensions)
{
	return (VoxelCoordinate.Z * VolumeDimensions.Y + VoxelCoordinate.Y) * VolumeDimensions.X + VoxelCoordinate.X;
}

static bool DynamicMesh_GenerateSignedDistanceFieldVolumeData(
	const FDynamicMesh3& Mesh,
	float DistanceFieldResolutionScale,
	bool bGenerateAsIfTwoSided,
	FDistanceFieldVolumeData& VolumeDataOut,
	FProgressCancel& Progress)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DynamicMesh_GenerateSignedDistanceFieldVolumeData);

	if (!UE::DynamicMesh::AllowDistanceFieldGeneration())
	{
		return false;
	}

	if ( DistanceFieldResolutionScale <= 0 )
	{
		return false;
	}

	const double StartTime = FPlatformTime::Seconds();

	UE::Geometry::FDynamicMeshAABBTree3 Spatial(&Mesh, true);
	if ( Progress.Cancelled() ) { return false; }
	UE::Geometry::FAxisAlignedBox3d MeshBounds = Spatial.GetBoundingBox();
	UE::Geometry::TFastWindingTree<FDynamicMesh3> WindingTree(&Spatial, true);
	if ( Progress.Cancelled() ) { return false; }

	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DistanceFields.MaxPerMeshResolution"));
	const int32 PerMeshMax = CVar->GetValueOnAnyThread();

	// Meshes with explicit artist-specified scale can go higher
	const int32 MaxNumBlocksOneDim = FMath::Min<int32>(FMath::DivideAndRoundNearest(DistanceFieldResolutionScale <= 1 ? PerMeshMax / 2 : PerMeshMax, DistanceField::UniqueDataBrickSize), DistanceField::MaxIndirectionDimension - 1);

	static const auto CVarDensity = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.DistanceFields.DefaultVoxelDensity"));
	const float VoxelDensity = CVarDensity->GetValueOnAnyThread();

	const float NumVoxelsPerLocalSpaceUnit = VoxelDensity * DistanceFieldResolutionScale;
	FBox3f LocalSpaceMeshBounds = (FBox3f)MeshBounds;

	// Make sure the mesh bounding box has positive extents to handle planes
	{
		FVector3f MeshBoundsCenter = LocalSpaceMeshBounds.GetCenter();
		FVector3f MeshBoundsExtent = FVector3f::Max(LocalSpaceMeshBounds.GetExtent(), FVector3f(1.0f, 1.0f, 1.0f));
		LocalSpaceMeshBounds.Min = MeshBoundsCenter - MeshBoundsExtent;
		LocalSpaceMeshBounds.Max = MeshBoundsCenter + MeshBoundsExtent;
	}

	// We sample on voxel corners and use central differencing for gradients, so a box mesh using two-sided materials whose vertices lie on LocalSpaceMeshBounds produces a zero gradient on intersection
	// Expand the mesh bounds by a fraction of a voxel to allow room for a pullback on the hit location for computing the gradient.
	// Only expand for two sided meshes as this adds significant Mesh SDF tracing cost
	if (bGenerateAsIfTwoSided)
	{
		const FVector3f DesiredDimensions = FVector3f(LocalSpaceMeshBounds.GetSize() * FVector3f(NumVoxelsPerLocalSpaceUnit / (float)DistanceField::UniqueDataBrickSize));
		const FIntVector Mip0IndirectionDimensions = FIntVector(
			FMath::Clamp(FMath::RoundToInt(DesiredDimensions.X), 1, MaxNumBlocksOneDim),
			FMath::Clamp(FMath::RoundToInt(DesiredDimensions.Y), 1, MaxNumBlocksOneDim),
			FMath::Clamp(FMath::RoundToInt(DesiredDimensions.Z), 1, MaxNumBlocksOneDim));

		const float CentralDifferencingExpandInVoxels = .25f;
		const FVector3f TexelObjectSpaceSize = LocalSpaceMeshBounds.GetSize() / FVector3f(Mip0IndirectionDimensions * DistanceField::UniqueDataBrickSize - FIntVector(2 * CentralDifferencingExpandInVoxels));
		LocalSpaceMeshBounds = LocalSpaceMeshBounds.ExpandBy(TexelObjectSpaceSize);
	}

	// The tracing shader uses a Volume space that is normalized by the maximum extent, to keep Volume space within [-1, 1], we must match that behavior when encoding
	const float LocalToVolumeScale = 1.0f / LocalSpaceMeshBounds.GetExtent().GetMax();

	const FVector3f DesiredDimensions = FVector3f(LocalSpaceMeshBounds.GetSize() * FVector3f(NumVoxelsPerLocalSpaceUnit / (float)DistanceField::UniqueDataBrickSize));
	const FIntVector Mip0IndirectionDimensions = FIntVector(
		FMath::Clamp(FMath::RoundToInt(DesiredDimensions.X), 1, MaxNumBlocksOneDim),
		FMath::Clamp(FMath::RoundToInt(DesiredDimensions.Y), 1, MaxNumBlocksOneDim),
		FMath::Clamp(FMath::RoundToInt(DesiredDimensions.Z), 1, MaxNumBlocksOneDim));

	TArray<uint8> StreamableMipData;

	struct FDistanceFieldBrick
	{
		FDistanceFieldBrick(
			float InLocalSpaceTraceDistance,
			FBox3f InVolumeBounds,
			float InLocalToVolumeScale,
			FVector2f InDistanceFieldToVolumeScaleBias,
			FIntVector InBrickCoordinate,
			FIntVector InIndirectionSize)
			:
			LocalSpaceTraceDistance(InLocalSpaceTraceDistance),
			VolumeBounds(InVolumeBounds),
			LocalToVolumeScale(InLocalToVolumeScale),
			DistanceFieldToVolumeScaleBias(InDistanceFieldToVolumeScaleBias),
			BrickCoordinate(InBrickCoordinate),
			IndirectionSize(InIndirectionSize),
			BrickMaxDistance(MIN_uint8),
			BrickMinDistance(MAX_uint8)
		{}

		float LocalSpaceTraceDistance;
		FBox3f VolumeBounds;
		float LocalToVolumeScale;
		FVector2f DistanceFieldToVolumeScaleBias;
		FIntVector BrickCoordinate;
		FIntVector IndirectionSize;

		// Output
		uint8 BrickMaxDistance;
		uint8 BrickMinDistance;
		TArray<uint8> DistanceFieldVolume;
	};


	for (int32 MipIndex = 0; MipIndex < DistanceField::NumMips; MipIndex++)
	{
		if ( Progress.Cancelled() ) { return false; }

		const FIntVector IndirectionDimensions = FIntVector(
			FMath::DivideAndRoundUp(Mip0IndirectionDimensions.X, 1 << MipIndex),
			FMath::DivideAndRoundUp(Mip0IndirectionDimensions.Y, 1 << MipIndex),
			FMath::DivideAndRoundUp(Mip0IndirectionDimensions.Z, 1 << MipIndex));

		// Expand to guarantee one voxel border for gradient reconstruction using bilinear filtering
		const FVector3f TexelObjectSpaceSize = LocalSpaceMeshBounds.GetSize() / FVector3f(IndirectionDimensions * DistanceField::UniqueDataBrickSize - FIntVector(2 * DistanceField::MeshDistanceFieldObjectBorder));
		const FBox3f DistanceFieldVolumeBounds = LocalSpaceMeshBounds.ExpandBy(TexelObjectSpaceSize);

		const FVector3f IndirectionVoxelSize = DistanceFieldVolumeBounds.GetSize() / FVector3f(IndirectionDimensions);
		const float IndirectionVoxelRadius = IndirectionVoxelSize.Size();

		const FVector3f VolumeSpaceDistanceFieldVoxelSize = IndirectionVoxelSize * LocalToVolumeScale / FVector3f(DistanceField::UniqueDataBrickSize);
		const float MaxDistanceForEncoding = VolumeSpaceDistanceFieldVoxelSize.Size() * DistanceField::BandSizeInVoxels;
		const float LocalSpaceTraceDistance = MaxDistanceForEncoding / LocalToVolumeScale;
		const FVector2f DistanceFieldToVolumeScaleBias(2.0f * MaxDistanceForEncoding, -MaxDistanceForEncoding);

		TArray<FDistanceFieldBrick> BricksToCompute;
		BricksToCompute.Reserve(IndirectionDimensions.X * IndirectionDimensions.Y * IndirectionDimensions.Z / 8);
		for (int32 ZIndex = 0; ZIndex < IndirectionDimensions.Z; ZIndex++)
		{
			for (int32 YIndex = 0; YIndex < IndirectionDimensions.Y; YIndex++)
			{
				for (int32 XIndex = 0; XIndex < IndirectionDimensions.X; XIndex++)
				{
					BricksToCompute.Emplace(
						LocalSpaceTraceDistance,
						DistanceFieldVolumeBounds,
						LocalToVolumeScale,
						DistanceFieldToVolumeScaleBias,
						FIntVector(XIndex, YIndex, ZIndex),
						IndirectionDimensions);
				}
			}
		}

		if ( Progress.Cancelled() ) { return false; }

		// compute bricks now
		for ( FDistanceFieldBrick& Brick : BricksToCompute )
		{
			const FVector3f BrickIndirectionVoxelSize = Brick.VolumeBounds.GetSize() / FVector3f(Brick.IndirectionSize);
			const FVector3f DistanceFieldVoxelSize = BrickIndirectionVoxelSize / FVector3f(DistanceField::UniqueDataBrickSize);
			const FVector3f BrickMinPosition = Brick.VolumeBounds.Min + FVector3f(Brick.BrickCoordinate) * BrickIndirectionVoxelSize;

			Brick.DistanceFieldVolume.Empty(DistanceField::BrickSize * DistanceField::BrickSize * DistanceField::BrickSize);
			Brick.DistanceFieldVolume.AddZeroed(DistanceField::BrickSize * DistanceField::BrickSize * DistanceField::BrickSize);

			for (int32 ZIndex = 0; ZIndex < DistanceField::BrickSize; ZIndex++)
			{
				for (int32 YIndex = 0; YIndex < DistanceField::BrickSize; YIndex++)
				{
					if ( Progress.Cancelled() ) { return false; }

					for (int32 XIndex = 0; XIndex < DistanceField::BrickSize; XIndex++)
					{
						const FVector3f VoxelPosition = FVector3f(XIndex, YIndex, ZIndex) * DistanceFieldVoxelSize + BrickMinPosition;
						const int32 Index = (ZIndex * DistanceField::BrickSize * DistanceField::BrickSize + YIndex * DistanceField::BrickSize + XIndex);

						float MinLocalSpaceDistance = LocalSpaceTraceDistance;

						double NearestDistSqr = 0;
						int32 NearestTriangleID = Spatial.FindNearestTriangle((FVector3d)VoxelPosition, NearestDistSqr, 
							UE::Geometry::IMeshSpatial::FQueryOptions(LocalSpaceTraceDistance));
						if (NearestTriangleID != IndexConstants::InvalidID)
						{
							const float ClosestDistance = FMath::Sqrt(NearestDistSqr);
							MinLocalSpaceDistance = FMath::Min(MinLocalSpaceDistance, ClosestDistance);

							// found closest point within search radius
							double IsoThreshold = 0.5;
							bool bInside = WindingTree.IsInside((FVector3d)VoxelPosition, 0.5);
							if ( bInside )
							{
								MinLocalSpaceDistance *= -1;
							}
						}
						else
						{
							// no closest point...
							MinLocalSpaceDistance = LocalSpaceTraceDistance;
						}

						// Transform to the tracing shader's Volume space
						const float VolumeSpaceDistance = MinLocalSpaceDistance * LocalToVolumeScale;
						// Transform to the Distance Field texture's space
						const float RescaledDistance = (VolumeSpaceDistance - DistanceFieldToVolumeScaleBias.Y) / DistanceFieldToVolumeScaleBias.X;
						check(DistanceField::DistanceFieldFormat == PF_G8);
						const uint8 QuantizedDistance = FMath::Clamp<int32>(FMath::FloorToInt(RescaledDistance * 255.0f + .5f), 0, 255);
						Brick.DistanceFieldVolume[Index] = QuantizedDistance;
						Brick.BrickMaxDistance = FMath::Max(Brick.BrickMaxDistance, QuantizedDistance);
						Brick.BrickMinDistance = FMath::Min(Brick.BrickMinDistance, QuantizedDistance);

					} // X iteration 
				} // Y iteration
			} // Z iteration

			
		}  // Bricks iteration


		FSparseDistanceFieldMip& OutMip = VolumeDataOut.Mips[MipIndex];
		TArray<uint32> IndirectionTable;
		IndirectionTable.Empty(IndirectionDimensions.X * IndirectionDimensions.Y * IndirectionDimensions.Z);
		IndirectionTable.AddUninitialized(IndirectionDimensions.X * IndirectionDimensions.Y * IndirectionDimensions.Z);

		for (int32 i = 0; i < IndirectionTable.Num(); i++)
		{
			IndirectionTable[i] = DistanceField::InvalidBrickIndex;
		} 

		TArray<FDistanceFieldBrick*> ValidBricks;
		ValidBricks.Reserve(BricksToCompute.Num());

		for (int32 k = 0; k < BricksToCompute.Num(); k++)
		{
			const FDistanceFieldBrick& ComputedBrick = BricksToCompute[k];
			if (ComputedBrick.BrickMinDistance < MAX_uint8 && ComputedBrick.BrickMaxDistance > MIN_uint8)
			{
				ValidBricks.Add(&BricksToCompute[k]);
			}
		}

		const uint32 NumBricks = ValidBricks.Num();
		const uint32 BrickSizeBytes = DistanceField::BrickSize * DistanceField::BrickSize * DistanceField::BrickSize * GPixelFormats[DistanceField::DistanceFieldFormat].BlockBytes;

		TArray<uint8> DistanceFieldBrickData;
		DistanceFieldBrickData.Empty(BrickSizeBytes * NumBricks);
		DistanceFieldBrickData.AddUninitialized(BrickSizeBytes * NumBricks);

		if ( Progress.Cancelled() ) { return false; }

		for (int32 BrickIndex = 0; BrickIndex < ValidBricks.Num(); BrickIndex++)
		{
			const FDistanceFieldBrick& Brick = *ValidBricks[BrickIndex];
			const int32 IndirectionIndex = ComputeLinearVoxelIndex(Brick.BrickCoordinate, IndirectionDimensions);
			IndirectionTable[IndirectionIndex] = BrickIndex;

			check(BrickSizeBytes == Brick.DistanceFieldVolume.Num() * Brick.DistanceFieldVolume.GetTypeSize());
			FPlatformMemory::Memcpy(&DistanceFieldBrickData[BrickIndex * BrickSizeBytes], Brick.DistanceFieldVolume.GetData(), Brick.DistanceFieldVolume.Num() * Brick.DistanceFieldVolume.GetTypeSize());
		}

		const int32 IndirectionTableBytes = IndirectionTable.Num() * IndirectionTable.GetTypeSize();
		const int32 MipDataBytes = IndirectionTableBytes + DistanceFieldBrickData.Num();

		if (MipIndex == DistanceField::NumMips - 1)
		{
			VolumeDataOut.AlwaysLoadedMip.Empty(MipDataBytes);
			VolumeDataOut.AlwaysLoadedMip.AddUninitialized(MipDataBytes);

			FPlatformMemory::Memcpy(&VolumeDataOut.AlwaysLoadedMip[0], IndirectionTable.GetData(), IndirectionTableBytes);

			if (DistanceFieldBrickData.Num() > 0)
			{
				FPlatformMemory::Memcpy(&VolumeDataOut.AlwaysLoadedMip[IndirectionTableBytes], DistanceFieldBrickData.GetData(), DistanceFieldBrickData.Num());
			}
		}
		else
		{
			OutMip.BulkOffset = StreamableMipData.Num();
			StreamableMipData.AddUninitialized(MipDataBytes);
			OutMip.BulkSize = StreamableMipData.Num() - OutMip.BulkOffset;
			checkf(OutMip.BulkSize > 0, TEXT("DynamicMeshComponent - BulkSize was 0 with %ux%ux%u indirection"), IndirectionDimensions.X, IndirectionDimensions.Y, IndirectionDimensions.Z);

			FPlatformMemory::Memcpy(&StreamableMipData[OutMip.BulkOffset], IndirectionTable.GetData(), IndirectionTableBytes);

			if (DistanceFieldBrickData.Num() > 0)
			{
				FPlatformMemory::Memcpy(&StreamableMipData[OutMip.BulkOffset + IndirectionTableBytes], DistanceFieldBrickData.GetData(), DistanceFieldBrickData.Num());
			}
		}
	
		if ( Progress.Cancelled() ) { return false; }

		OutMip.IndirectionDimensions = IndirectionDimensions;
		OutMip.DistanceFieldToVolumeScaleBias = DistanceFieldToVolumeScaleBias;
		OutMip.NumDistanceFieldBricks = NumBricks;

		// Account for the border voxels we added
		const FVector3f VirtualUVMin = FVector3f(DistanceField::MeshDistanceFieldObjectBorder) / FVector3f(IndirectionDimensions * DistanceField::UniqueDataBrickSize);
		const FVector3f VirtualUVSize = FVector3f(IndirectionDimensions * DistanceField::UniqueDataBrickSize - FIntVector(2 * DistanceField::MeshDistanceFieldObjectBorder)) / FVector3f(IndirectionDimensions * DistanceField::UniqueDataBrickSize);
		
		const FVector3f VolumePositionExtent = LocalSpaceMeshBounds.GetExtent() * LocalToVolumeScale;

		// [-VolumePositionExtent, VolumePositionExtent] -> [VirtualUVMin, VirtualUVMin + VirtualUVSize]
		OutMip.VolumeToVirtualUVScale = VirtualUVSize / (2 * VolumePositionExtent);
		OutMip.VolumeToVirtualUVAdd = VolumePositionExtent * OutMip.VolumeToVirtualUVScale + VirtualUVMin;
	}

	VolumeDataOut.bMostlyTwoSided = bGenerateAsIfTwoSided;
	VolumeDataOut.LocalSpaceMeshBounds = LocalSpaceMeshBounds;

	if ( Progress.Cancelled() ) { return false; }

	VolumeDataOut.StreamableMips.Lock(LOCK_READ_WRITE);
	uint8* Ptr = (uint8*)VolumeDataOut.StreamableMips.Realloc(StreamableMipData.Num());
	FMemory::Memcpy(Ptr, StreamableMipData.GetData(), StreamableMipData.Num());
	VolumeDataOut.StreamableMips.Unlock();
	VolumeDataOut.StreamableMips.SetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload);

	const float BuildTime = (float)(FPlatformTime::Seconds() - StartTime);
		 
	if (BuildTime > 1.0f)
	{
		UE_LOG(LogGeometry, Log, TEXT("DynamicMeshComponent - Finished distance field build in %.1fs - %ux%ux%u sparse distance field, %.1fMb total, %.1fMb always loaded, %u%% occupied, %u triangles"),
			BuildTime,
			Mip0IndirectionDimensions.X * DistanceField::UniqueDataBrickSize,
			Mip0IndirectionDimensions.Y * DistanceField::UniqueDataBrickSize,
			Mip0IndirectionDimensions.Z * DistanceField::UniqueDataBrickSize,
			(VolumeDataOut.GetResourceSizeBytes() + VolumeDataOut.StreamableMips.GetBulkDataSize()) / 1024.0f / 1024.0f,
			(VolumeDataOut.AlwaysLoadedMip.GetAllocatedSize()) / 1024.0f / 1024.0f,
			FMath::RoundToInt(100.0f * VolumeDataOut.Mips[0].NumDistanceFieldBricks / (float)(Mip0IndirectionDimensions.X * Mip0IndirectionDimensions.Y * Mip0IndirectionDimensions.Z)),
			Mesh.TriangleCount());
	}

	return true;
}


TUniquePtr<FDistanceFieldVolumeData> FBaseDynamicMeshSceneProxy::ComputeDistanceFieldForMesh(
	const FDynamicMesh3& Mesh, 
	FProgressCancel& Progress,
	float DistanceFieldResolutionScale, 
	bool bGenerateAsIfTwoSided)
{
	TUniquePtr<FDistanceFieldVolumeData> NewDistanceField = MakeUnique<FDistanceFieldVolumeData>();
	bool bCompleted = DynamicMesh_GenerateSignedDistanceFieldVolumeData( Mesh, 
		DistanceFieldResolutionScale, bGenerateAsIfTwoSided, *NewDistanceField, Progress);
	if (bCompleted)
	{
		return NewDistanceField;
	}
	return TUniquePtr<FDistanceFieldVolumeData>();
}

void FBaseDynamicMeshSceneProxy::SetNewDistanceField(TSharedPtr<FDistanceFieldVolumeData> NewDistanceField, bool bInInitialize)
{
	if (DistanceField.IsValid() && NewDistanceField.IsValid() && DistanceField.Get() == NewDistanceField.Get())
	{
		checkSlow(false); // we don't expect this to be called when no work needs to be done
		return;
	}

	// wait for end of frame
	if (!bInInitialize)
	{
		// Note this requires us to be on the game thread
		check(IsInGameThread());
		FlushRenderingCommands();
	}

	DistanceField = NewDistanceField;
	bDistanceFieldValid = DistanceField.IsValid();
	bSupportsDistanceFieldRepresentation = bDistanceFieldValid;
	bAffectDistanceFieldLighting = bDistanceFieldValid;

	// lumen visibility may change depending on the presence of a valid distance field
	UpdateVisibleInLumenScene();
}





