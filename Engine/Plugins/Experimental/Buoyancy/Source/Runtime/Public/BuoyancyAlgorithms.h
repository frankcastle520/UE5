// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Chaos/Core.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Chaos/AABB.h"
#include "WaterBodyComponent.h"

namespace Chaos
{
	class FPBDRigidsEvolutionGBF;
}

struct FBuoyancyParticleData;

namespace BuoyancyAlgorithms
{
	// Minimal struct containing essential data about a particular submersion
	struct FSubmersion
	{
		// Indicates the submerged particle
		Chaos::FPBDRigidParticleHandle* SubmergedParticle;

		// Total submerged volume
		float SubmergedVolume;

		// Effective submerged center of mass
		Chaos::FVec3 SubmergedCoM;
	};

	// Compute the effective volume of an entire particle based on its material
	// density and mass.
	Chaos::FRealSingle ComputeParticleVolume(const Chaos::FPBDRigidsEvolutionGBF& Evolution, const Chaos::FGeometryParticleHandle* Particle);

	// Compute the effective volume of a shape. This method must reflect the
	// maximum possible output value of the non-scaled ComputeSubmergedVolume.
	Chaos::FRealSingle ComputeShapeVolume(const Chaos::FGeometryParticleHandle* Particle);

	//
	void ScaleSubmergedVolume(const Chaos::FPBDRigidsEvolutionGBF& Evolution, const Chaos::FGeometryParticleHandle* Particle, Chaos::FRealSingle& SubmergedVol, Chaos::FRealSingle& TotalVol);

	// Compute an approximate volume and center of mass of particle B submerged in particle A,
	// adjusting for the volume of the object based on the material density and mass of the object
	bool ComputeSubmergedVolume(FBuoyancyParticleData& ParticleData, const Chaos::FPBDRigidsEvolutionGBF& Evolution, const Chaos::FGeometryParticleHandle* ParticleA, const Chaos::FGeometryParticleHandle* ParticleB, int32 NumSubdivisions, float MinVolume, float& SubmergedVol, Chaos::FVec3& SubmergedCoM, float& TotalVol);

	// Compute an approximate volume and center of mass of particle B submerged in particle A
	bool ComputeSubmergedVolume(FBuoyancyParticleData& ParticleData, const Chaos::FGeometryParticleHandle* ParticleA, const Chaos::FGeometryParticleHandle* ParticleB, int32 NumSubdivisions, float MinVolume, float& SubmergedVol, Chaos::FVec3& SubmergedCoM);

	// Compute submerged volume given a single waterlevel
	bool ComputeSubmergedVolume(FBuoyancyParticleData& ParticleData, const Chaos::FPBDRigidsEvolutionGBF& Evolution, const Chaos::FGeometryParticleHandle* SubmergedParticle, const Chaos::FGeometryParticleHandle* WaterParticle, const FVector& WaterX, const FVector& WaterN, int32 NumSubdivisions, float MinVolume, float& SubmergedVol, Chaos::FVec3& SubmergedCoM, float& TotalVol);

	bool ComputeSubmergedVolume(FBuoyancyParticleData& ParticleData, const Chaos::FGeometryParticleHandle* SubmergedParticle, const Chaos::FGeometryParticleHandle* WaterParticle, const FVector& WaterX, const FVector& WaterN, int32 NumSubdivisions, float MinVolume, float& SubmergedVol, Chaos::FVec3& SubmergedCoM);

	// Given an OOBB and a water level, generate another OOBB which is 1. entirely contained
	// within the input OOBB and 2. entirely contains the portion of the OOBB which is submerged
	// below the water level.
	bool FORCEINLINE ComputeSubmergedBounds(const FVector& SurfacePointLocal, const FVector& SurfaceNormalLocal, const Chaos::FAABB3& RigidBox, Chaos::FAABB3& OutSubmergedBounds);

	// Given a bounds object, recursively subdivide it in eighths to a fixed maximum depth and
	// a fixed minimum smallest subdivision volume.
	bool SubdivideBounds(const Chaos::FAABB3& Bounds, int32 NumSubdivisions, float MinVolume, TArray<Chaos::FAABB3>& OutBounds);

	// Given a rigid particle and it's submerged CoM and Volume, compute delta velocities for
	// integrated buoyancy forces on an object
	bool ComputeBuoyantForce(const Chaos::FPBDRigidParticleHandle* RigidParticle, const float DeltaSeconds, const float WaterDensity, 
		const float WaterDrag, const Chaos::FVec3& GravityAccelVec, const Chaos::FVec3& SubmergedCoM, const float SubmergedVol, 
		const Chaos::FVec3& WaterVel, const Chaos::FVec3& WaterN, Chaos::FVec3& OutDeltaV, Chaos::FVec3& OutDeltaW);

	// given a particle, loop over the contained shapes and accumulate force/torque/submerged CoM values
	void ComputeSubmergedVolumeAndForcesForParticle(FBuoyancyParticleData& ParticleData,
		const Chaos::FGeometryParticleHandle* SubmergedParticle, const Chaos::FGeometryParticleHandle* WaterParticle,
		const FShallowWaterSimulationGrid& ShallowWaterGrid,
		const Chaos::FPBDRigidsEvolution& Evolution, const float DeltaSeconds, const float WaterDensity, const float WaterDrag,
		float& OutTotalSubmergedVol, Chaos::FVec3& OutTotalSubmergedCoM, Chaos::FVec3& OutTotalForce, Chaos::FVec3& OutTotalTorque);

	// given a shape, compute the submerged volume and accumulate forces
	// this is done in a single function call because of the iterative nature of the algorithm
	void ComputeSubmergedVolumeAndForcesForShape(
		const Chaos::FGeometryParticleHandle* SubmergedParticle,
		const Chaos::FPBDRigidsEvolution& Evolution, float DeltaSeconds, const float WaterDensity, const float WaterDrag,
		const Chaos::FImplicitObject* Implicit, const Chaos::FRigidTransform3& RelativeTransform, const int32 RootObjectIndex, const int32 ObjectIndex, const int32 LeafObjectIndex,
		const FShallowWaterSimulationGrid& ShallowWaterGrid, 
		Chaos::FVec3& OutWaterP, Chaos::FVec3& OutWaterN, 
		float& OutSubmergedVol, Chaos::FVec3& OutSubmergedCoM,
		Chaos::FVec3& OutForce, Chaos::FVec3& OutTorque);

	// find intersection points between a plane and aabbox
	void FindAllIntersectionPoints(const Chaos::FVec3& WaterP, const Chaos::FVec3& WaterN, const Chaos::FAABB3& LocalBox, const FVector WorldVertexPosition[8],
		TMap<int, FVector> &OutEdgeToIntersectionPointMap, TArray<FVector>& OutOrderedIntersectionPoints, FVector& OutIntersectionCenter);

	// sort intersection points by angle
	void SortIntersectionPointsByAngle(const Chaos::FVec3& WaterP, const Chaos::FVec3& WaterN, const Chaos::FVec3 &IntersectionCenter, 
		const TMap<int, FVector>& EdgeToIntersectionPointMap, TArray<FVector>& OutOrderedIntersectionPoints);

	// compute area and volume of a tet from a triangle and center point on mesh
	void FORCEINLINE ComputeTriangleAreaAndVolume(const FVector &V0, const FVector &V1, const FVector &V2,
		const FVector &MeshCenterPoint, FVector& OutTriangleBaryCenter, FVector& OutNormal, float& OutArea, float& OutVolume, bool DebugDraw = false);

	// compute the force the fluid exerts on a triangle
	void FORCEINLINE ComputeFluidForceForTriangle(const float WaterDrag,
		const float DeltaSeconds,
		const Chaos::FPBDRigidParticleHandle* RigidParticle, const FVector WorldCoM,
		const FVector &TriBaryCenter, const FVector &TriNormal, const float TriArea, const float TetVolume,
		const FVector &WaterVelocity, const FVector &WaterP, const FVector &WaterN,
		FVector& OutTotalWorldForce, FVector& OutTotalWorldTorque);

	// compute the force the buoyancy exerts on a shape
	void ComputeBuoyantForceForShape(const Chaos::FPBDRigidsEvolution& Evolution, const Chaos::FPBDRigidParticleHandle* RigidParticle, const float DeltaSeconds, const float WaterDensity,
		const Chaos::FVec3& SubmergedCoM, const float SubmergedVol, const Chaos::FVec3& WaterN, Chaos::FVec3& OutWorldBuoyantForce, Chaos::FVec3& OutWorldBuoyantTorque);
}
