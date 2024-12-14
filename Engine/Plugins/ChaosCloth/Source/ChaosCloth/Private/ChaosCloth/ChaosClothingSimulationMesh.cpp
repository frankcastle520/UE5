// Copyright Epic Games, Inc. All Rights Reserved.
#include "ChaosCloth/ChaosClothingSimulationMesh.h"
#include "ChaosCloth/ChaosClothingSimulationSolver.h"
#include "ChaosCloth/ChaosWeightMapTarget.h"
#include "ChaosCloth/ChaosClothPrivate.h"
#include "ClothingSimulation.h"
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5
#include "Components/SkeletalMeshComponent.h"
#include "ClothingAsset.h"
#endif
#include "Components/SkinnedMeshComponent.h"
#include "Containers/ArrayView.h"
#include "Async/ParallelFor.h"
#include "SkeletalMeshTypes.h"  // For FMeshToMeshVertData
#if INTEL_ISPC
#include "ChaosClothingSimulationMesh.ispc.generated.h"
#endif

#if INTEL_ISPC && !UE_BUILD_SHIPPING
bool bChaos_SkinPhysicsMesh_ISPC_Enabled = CHAOS_SKIN_PHYSICS_MESH_ISPC_ENABLED_DEFAULT;
FAutoConsoleVariableRef CVarChaosSkinPhysicsMeshISPCEnabled(TEXT("p.Chaos.SkinPhysicsMesh.ISPC"), bChaos_SkinPhysicsMesh_ISPC_Enabled, TEXT("Whether to use ISPC optimizations on skinned physics meshes"));

static_assert(sizeof(ispc::FVector3f) == sizeof(Chaos::Softs::FSolverVec3), "sizeof(ispc::FVector3f) != sizeof(Chaos::Softs::FSolverVec3)");
static_assert(sizeof(ispc::FVector3f) == sizeof(FVector3f), "sizeof(ispc::FVector3f) != sizeof(FVector3f)");
static_assert(sizeof(ispc::FMatrix44f) == sizeof(FMatrix44f), "sizeof(ispc::FMatrix44f) != sizeof(FMatrix44f)");
static_assert(sizeof(ispc::FTransform3f) == sizeof(FTransform3f), "sizeof(ispc::FTransform3f) != sizeof(FTransform3f)");
static_assert(sizeof(ispc::FClothVertBoneData) == sizeof(FClothVertBoneData), "sizeof(ispc::FClothVertBoneData) != sizeof(Chaos::FClothVertBoneData)");
#endif

DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Skin Physics Mesh"), STAT_ChaosClothSkinPhysicsMesh, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Wrap Deform Mesh"), STAT_ChaosClothWrapDeformMesh, STATGROUP_ChaosCloth);
DECLARE_CYCLE_STAT(TEXT("Chaos Cloth Wrap Deform Cloth LOD"), STAT_ChaosClothWrapDeformClothLOD, STATGROUP_ChaosCloth);

namespace Chaos
{

FClothingSimulationMesh::FClothingSimulationMesh(const FString& InDebugName)
#if !UE_BUILD_SHIPPING
	: DebugName(InDebugName)
#endif
{
}

FClothingSimulationMesh::~FClothingSimulationMesh() = default;

Softs::FSolverReal FClothingSimulationMesh::GetScale() const
{
	return GetComponentToWorldTransform().GetScale3D().GetMax();
}

bool FClothingSimulationMesh::WrapDeformLOD(
	int32 PrevLODIndex,
	int32 LODIndex,
	const FSolverVec3* Normals,
	const FSolverVec3* Positions,
	FSolverVec3* OutPositions) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FClothingSimulationMesh_WrapDeformLOD);
	SCOPE_CYCLE_COUNTER(STAT_ChaosClothWrapDeformMesh);

	const int32 NumLODsPassed = FMath::Abs(LODIndex - PrevLODIndex);
	if (NumLODsPassed != 1 || !IsValidLODIndex(PrevLODIndex) || !IsValidLODIndex(LODIndex))
	{
		return false;
	}

	const int32 NumPoints = GetNumPoints(LODIndex);
	const TConstArrayView<FMeshToMeshVertData> SkinData = (PrevLODIndex < LODIndex) ?
		GetTransitionUpSkinData(LODIndex) :
		GetTransitionDownSkinData(LODIndex);

	for (int32 Index = 0; Index < NumPoints; ++Index)  // TODO: Profile for parallel for
	{
		const FMeshToMeshVertData& VertData = SkinData[Index];

		const int32 VertIndex0 = (int32)VertData.SourceMeshVertIndices[0];  // Note: The source is uint16. Watch out for large mesh sections!
		const int32 VertIndex1 = (int32)VertData.SourceMeshVertIndices[1];
		const int32 VertIndex2 = (int32)VertData.SourceMeshVertIndices[2];

		OutPositions[Index] = 
			Positions[VertIndex0] * (FSolverReal)VertData.PositionBaryCoordsAndDist.X + Normals[VertIndex0] * (FSolverReal)VertData.PositionBaryCoordsAndDist.W +
			Positions[VertIndex1] * (FSolverReal)VertData.PositionBaryCoordsAndDist.Y + Normals[VertIndex1] * (FSolverReal)VertData.PositionBaryCoordsAndDist.W +
			Positions[VertIndex2] * (FSolverReal)VertData.PositionBaryCoordsAndDist.Z + Normals[VertIndex2] * (FSolverReal)VertData.PositionBaryCoordsAndDist.W;
	}

	return true;
}

bool FClothingSimulationMesh::WrapDeformLOD(
	int32 PrevLODIndex,
	int32 LODIndex,
	const Softs::FSolverVec3* Normals,
	const Softs::FPAndInvM* PositionAndInvMs,
	const Softs::FSolverVec3* Velocities,
	Softs::FPAndInvM* OutPositionAndInvMs0,
	Softs::FSolverVec3* OutPositions1,
	Softs::FSolverVec3* OutVelocities) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FClothingSimulationMesh_WrapDeformLOD);
	SCOPE_CYCLE_COUNTER(STAT_ChaosClothWrapDeformClothLOD);

	const int32 NumLODsPassed = FMath::Abs(LODIndex - PrevLODIndex);
	if (NumLODsPassed != 1 || !IsValidLODIndex(PrevLODIndex) || !IsValidLODIndex(LODIndex))
	{
		return false;
	}

	const int32 NumPoints = GetNumPoints(LODIndex);
	const TConstArrayView<FMeshToMeshVertData> SkinData = (PrevLODIndex < LODIndex) ?
		GetTransitionUpSkinData(LODIndex) :
		GetTransitionDownSkinData(LODIndex);

	for (int32 Index = 0; Index < NumPoints; ++Index)  // TODO: Profile for parallel for
	{
		const FMeshToMeshVertData& VertData = SkinData[Index];

		const int32 VertIndex0 = (int32)VertData.SourceMeshVertIndices[0];  // Note: The source is uint16. Watch out for large mesh sections!
		const int32 VertIndex1 = (int32)VertData.SourceMeshVertIndices[1];
		const int32 VertIndex2 = (int32)VertData.SourceMeshVertIndices[2];

		OutPositionAndInvMs0[Index].P = OutPositions1[Index] =
			PositionAndInvMs[VertIndex0].P * (FSolverReal)VertData.PositionBaryCoordsAndDist.X + Normals[VertIndex0] * (FSolverReal)VertData.PositionBaryCoordsAndDist.W +
			PositionAndInvMs[VertIndex1].P * (FSolverReal)VertData.PositionBaryCoordsAndDist.Y + Normals[VertIndex1] * (FSolverReal)VertData.PositionBaryCoordsAndDist.W +
			PositionAndInvMs[VertIndex2].P * (FSolverReal)VertData.PositionBaryCoordsAndDist.Z + Normals[VertIndex2] * (FSolverReal)VertData.PositionBaryCoordsAndDist.W;

		OutVelocities[Index] = 
			Velocities[VertIndex0] * (FSolverReal)VertData.PositionBaryCoordsAndDist.X +
			Velocities[VertIndex1] * (FSolverReal)VertData.PositionBaryCoordsAndDist.Y +
			Velocities[VertIndex2] * (FSolverReal)VertData.PositionBaryCoordsAndDist.Z;
	}

	return true;
}

bool FClothingSimulationMesh::WrapDeformLOD(
	int32 PrevLODIndex,
	int32 LODIndex,
	const TConstArrayView<Softs::FSolverVec3>& Positions,
	const TConstArrayView<Softs::FSolverVec3>& Normals,
	TArrayView<Softs::FSolverVec3>& OutPositions,
	TArrayView<Softs::FSolverVec3>& OutNormals) const
{

	TRACE_CPUPROFILER_EVENT_SCOPE(FClothingSimulationMesh_WrapDeformLOD);
	SCOPE_CYCLE_COUNTER(STAT_ChaosClothWrapDeformMesh);

	const int32 NumLODsPassed = FMath::Abs(LODIndex - PrevLODIndex);
	if (NumLODsPassed != 1 || !IsValidLODIndex(PrevLODIndex) || !IsValidLODIndex(LODIndex))
	{
		return false;
	}

	const int32 NumPoints = OutPositions.Num();

	const TConstArrayView<FMeshToMeshVertData> SkinData = (PrevLODIndex < LODIndex) ?
		GetTransitionUpSkinData(LODIndex) :
		GetTransitionDownSkinData(LODIndex);

	for (int32 Index = 0; Index < NumPoints; ++Index)  // TODO: Profile for parallel for
	{
		const FMeshToMeshVertData& VertData = SkinData[Index];

		const int32 VertIndex0 = (int32)VertData.SourceMeshVertIndices[0];  // Note: The source is uint16. Watch out for large mesh sections!
		const int32 VertIndex1 = (int32)VertData.SourceMeshVertIndices[1];
		const int32 VertIndex2 = (int32)VertData.SourceMeshVertIndices[2];

		OutPositions[Index] =
			Positions[VertIndex0] * (FSolverReal)VertData.PositionBaryCoordsAndDist.X + Normals[VertIndex0] * (FSolverReal)VertData.PositionBaryCoordsAndDist.W +
			Positions[VertIndex1] * (FSolverReal)VertData.PositionBaryCoordsAndDist.Y + Normals[VertIndex1] * (FSolverReal)VertData.PositionBaryCoordsAndDist.W +
			Positions[VertIndex2] * (FSolverReal)VertData.PositionBaryCoordsAndDist.Z + Normals[VertIndex2] * (FSolverReal)VertData.PositionBaryCoordsAndDist.W;

		OutNormals[Index] =
			(Normals[VertIndex0] * (FSolverReal)VertData.PositionBaryCoordsAndDist.X +
			Normals[VertIndex1] * (FSolverReal)VertData.PositionBaryCoordsAndDist.Y +
			Normals[VertIndex2] * (FSolverReal)VertData.PositionBaryCoordsAndDist.Z).GetSafeNormal();
	}

	return true;
}

// Inline function used to force the unrolling of the skinning loop, LWC: note skinning is all done in float to match the asset data type
FORCEINLINE static void AddInfluence(FVector3f& OutPosition, FVector3f& OutNormal, const FVector3f& RefParticle, const FVector3f& RefNormal, const FMatrix44f& BoneMatrix, const float Weight)
{
	OutPosition += BoneMatrix.TransformPosition(RefParticle) * Weight;
	OutNormal += BoneMatrix.TransformVector(RefNormal) * Weight;
}

void FClothingSimulationMesh::SkinPhysicsMesh(int32 LODIndex, const FReal LocalSpaceScale, const FVec3& LocalSpaceLocation, TArrayView<Softs::FSolverVec3>& OutPositions, TArrayView<Softs::FSolverVec3>& OutNormals) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FClothingSimulationMesh_SkinPhysicsMesh);
	SCOPE_CYCLE_COUNTER(STAT_ChaosClothSkinPhysicsMesh);
	SCOPE_CYCLE_COUNTER(STAT_ClothSkinPhysMesh);

	FTransform ComponentToLocalSpaceReal = GetComponentToWorldTransform();
	ComponentToLocalSpaceReal.AddToTranslation(-LocalSpaceLocation);
	check(LocalSpaceScale > UE_SMALL_NUMBER);
	const FReal LocalSpaceScaleInv = 1. / LocalSpaceScale;
	ComponentToLocalSpaceReal.MultiplyScale3D(FVec3(LocalSpaceScaleInv));
	ComponentToLocalSpaceReal.ScaleTranslation(LocalSpaceScaleInv);
	const FTransform3f ComponentToLocalSpace(ComponentToLocalSpaceReal);  // LWC: Now in local space, therefore it is safe to use single precision which is the asset data format

	const int32* const RESTRICT BoneMap = GetBoneMap().GetData();
	const FMatrix44f* const RESTRICT RefToLocalMatrices = GetRefToLocalMatrices().GetData();

	check(IsValidLODIndex(LODIndex));
	const uint32 NumPoints = GetNumPoints(LODIndex);
	check(NumPoints == OutPositions.Num());
	check(NumPoints == OutNormals.Num());
	const TConstArrayView<FClothVertBoneData> BoneData = GetBoneData(LODIndex);
	const TConstArrayView<FVector3f> Positions = GetPositions(LODIndex);
	const TConstArrayView<FVector3f> Normals = GetNormals(LODIndex);

#if INTEL_ISPC
	if (bChaos_SkinPhysicsMesh_ISPC_Enabled)
	{
		ispc::SkinPhysicsMesh(
			(ispc::FVector3f*)OutPositions.GetData(),
			(ispc::FVector3f*)OutNormals.GetData(),
			(ispc::FVector3f*)Positions.GetData(),
			(ispc::FVector3f*)Normals.GetData(),
			(ispc::FClothVertBoneData*)BoneData.GetData(),
			BoneMap,
			(ispc::FMatrix44f*)RefToLocalMatrices,
			(ispc::FTransform3f&)ComponentToLocalSpace,
			NumPoints);
	}
	else
#endif
	{
		static const uint32 MinParallelVertices = 500;  // 500 seems to be the lowest threshold still giving gains even on profiled assets that are only using a small number of influences

		ParallelFor(NumPoints, [&BoneData, &Positions, &Normals, &ComponentToLocalSpace, BoneMap, RefToLocalMatrices, &OutPositions, &OutNormals](uint32 VertIndex)
		{
			const uint16* const RESTRICT BoneIndices = BoneData[VertIndex].BoneIndices;
			const float* const RESTRICT BoneWeights = BoneData[VertIndex].BoneWeights;
	
			// WARNING - HORRIBLE UNROLLED LOOP + JUMP TABLE BELOW
			// done this way because this is a pretty tight and performance critical loop. essentially
			// rather than checking each influence we can just jump into this switch and fall through
			// everything to compose the final skinned data
			const FVector3f& RefParticle = Positions[VertIndex];
			const FVector3f& RefNormal = Normals[VertIndex];

			FVector3f Position(ForceInitToZero);
			FVector3f Normal(ForceInitToZero);
			switch (BoneData[VertIndex].NumInfluences)
			{
			default:  // Intentional fall through
			case 12: AddInfluence(Position, Normal, RefParticle, RefNormal, RefToLocalMatrices[BoneMap[BoneIndices[11]]], BoneWeights[11]);  // Intentional fall through
			case 11: AddInfluence(Position, Normal, RefParticle, RefNormal, RefToLocalMatrices[BoneMap[BoneIndices[10]]], BoneWeights[10]);  // Intentional fall through
			case 10: AddInfluence(Position, Normal, RefParticle, RefNormal, RefToLocalMatrices[BoneMap[BoneIndices[ 9]]], BoneWeights[ 9]);  // Intentional fall through
			case  9: AddInfluence(Position, Normal, RefParticle, RefNormal, RefToLocalMatrices[BoneMap[BoneIndices[ 8]]], BoneWeights[ 8]);  // Intentional fall through
			case  8: AddInfluence(Position, Normal, RefParticle, RefNormal, RefToLocalMatrices[BoneMap[BoneIndices[ 7]]], BoneWeights[ 7]);  // Intentional fall through
			case  7: AddInfluence(Position, Normal, RefParticle, RefNormal, RefToLocalMatrices[BoneMap[BoneIndices[ 6]]], BoneWeights[ 6]);  // Intentional fall through
			case  6: AddInfluence(Position, Normal, RefParticle, RefNormal, RefToLocalMatrices[BoneMap[BoneIndices[ 5]]], BoneWeights[ 5]);  // Intentional fall through
			case  5: AddInfluence(Position, Normal, RefParticle, RefNormal, RefToLocalMatrices[BoneMap[BoneIndices[ 4]]], BoneWeights[ 4]);  // Intentional fall through
			case  4: AddInfluence(Position, Normal, RefParticle, RefNormal, RefToLocalMatrices[BoneMap[BoneIndices[ 3]]], BoneWeights[ 3]);  // Intentional fall through
			case  3: AddInfluence(Position, Normal, RefParticle, RefNormal, RefToLocalMatrices[BoneMap[BoneIndices[ 2]]], BoneWeights[ 2]);  // Intentional fall through
			case  2: AddInfluence(Position, Normal, RefParticle, RefNormal, RefToLocalMatrices[BoneMap[BoneIndices[ 1]]], BoneWeights[ 1]);  // Intentional fall through
			case  1: AddInfluence(Position, Normal, RefParticle, RefNormal, RefToLocalMatrices[BoneMap[BoneIndices[ 0]]], BoneWeights[ 0]);  // Intentional fall through
			case  0: break;
			}

			OutPositions[VertIndex] = FSolverVec3(ComponentToLocalSpace.TransformPosition(Position));
			OutNormals[VertIndex] = FSolverVec3(ComponentToLocalSpace.TransformVector(Normal).GetSafeNormal());

		}, NumPoints > MinParallelVertices ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread);
	}
}

void FClothingSimulationMesh::Update(
	FClothingSimulationSolver* Solver,
	int32 PrevLODIndex,
	int32 LODIndex,
	int32 PrevParticleRangeId,
	int32 ParticleRangeId)
{
	check(Solver);

	// Exit if any inputs are missing or not ready, and if the LOD is invalid
	if (!IsValidLODIndex(LODIndex))
	{
		return;
	}

	// Skin current LOD positions
	const FReal LocalSpaceScale = Solver->GetLocalSpaceScale();
	const FVec3& LocalSpaceLocation = Solver->GetLocalSpaceLocation();
	TArrayView<FSolverVec3> OutPositions = Solver->GetAnimationPositionsView(ParticleRangeId);
	TArrayView<FSolverVec3> OutNormals = Solver->GetAnimationNormalsView(ParticleRangeId);
	
	SkinPhysicsMesh(LODIndex, LocalSpaceScale, LocalSpaceLocation, OutPositions, OutNormals);

	// Update old positions after LOD Switching
	if (LODIndex != PrevLODIndex)
	{
		// TODO: Using the more accurate skinning method here would require double buffering the context at the skeletal mesh level
		const TConstArrayView<FSolverVec3> SrcWrapPositions = Solver->GetOldAnimationPositionsView(PrevParticleRangeId);
		const TConstArrayView<FSolverVec3> SrcWrapNormals = Solver->GetOldAnimationNormalsView(PrevParticleRangeId);
		TArrayView<FSolverVec3> OutOldPositions = Solver->GetOldAnimationPositionsView(ParticleRangeId);
		TArrayView<FSolverVec3> OutOldNormals = Solver->GetOldAnimationNormalsView(ParticleRangeId);

		const bool bValidWrap = WrapDeformLOD(PrevLODIndex, LODIndex, SrcWrapPositions, SrcWrapNormals, OutOldPositions, OutOldNormals);
		if (!bValidWrap)
		{
			// The previous LOD is invalid, reset old positions with the new LOD
			for (int32 Index = 0; Index < OutOldPositions.Num(); ++Index)
			{
				OutOldPositions[Index] = OutPositions[Index];
				OutOldNormals[Index] = OutNormals[Index];
			}
		}
	}
}

}  // End namespace Chaos
