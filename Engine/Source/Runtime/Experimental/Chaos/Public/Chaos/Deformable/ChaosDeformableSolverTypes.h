// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/DebugDrawQueue.h"
#include "Chaos/Deformable/ChaosDeformableSolverProxy.h"
#include "Chaos/Deformable/ChaosDeformableCollisionsProxy.h"
#include "Chaos/PBDSoftsEvolutionFwd.h"
#include "CoreMinimal.h"

class UDeformableSolverComponent;
class FFleshCacheAdapter;

namespace Chaos::Softs
{
	struct FDeformableSolverProperties
	{
		FDeformableSolverProperties(
			int32 InNumSolverSubSteps = 2,
			int32 InNumSolverIterations = 5,
			bool InFixTimeStep = false,
			FSolverReal InTimeStepSize = (FSolverReal)0.05,
			bool InCacheToFile = false,
			bool InbEnableKinematics = true,
			bool InbUseFloor = true,
			bool InbUseGridBasedConstraints = false,
			FSolverReal InGridDx = (FSolverReal)1.,
			bool InbDoQuasistatics = false,
			FSolverReal InEMesh = (FSolverReal)100000.,
			bool InbDoBlended = false,
			FSolverReal InBlendedZeta = (FSolverReal).1,
			FSolverReal InDamping = (FSolverReal)0,
			bool InbEnableGravity = true,
			bool InbEnableCorotatedConstraints = true,
			bool InbEnablePositionTargets = true,
			bool InbUseGaussSeidelConstraints = false,
			bool InbUseSOR = true,
			FSolverReal InOmegaSOR = (FSolverReal)1.6,
			bool InbUseGSNeohookean = false,
			bool InbDoSpringCollision = false,
			bool InbDoInComponentSpringCollision = false,
			int32 InNRingExcluded = 1,
			FSolverReal InSpringCollisionSearchRadius = (FSolverReal)0,
			FSolverReal InSpringCollisionStiffness = (FSolverReal)500.,
			bool InbAllowSliding = true,
			bool InbDoSphereRepulsion = false,
			FSolverReal InSphereRepulsionRadius = (FSolverReal)0,
			FSolverReal InSphereRepulsionStiffness = (FSolverReal)500.,
			bool InbDoMuscleActivation = false,
			bool InbCollideWithFullMesh = false, 
			bool InbEnableDynamicSprings = true)
			: NumSolverSubSteps(InNumSolverSubSteps)
			, NumSolverIterations(InNumSolverIterations)
			, FixTimeStep(InFixTimeStep)
			, TimeStepSize(InTimeStepSize)
			, CacheToFile(InCacheToFile)
			, bEnableKinematics(InbEnableKinematics)
			, bUseFloor(InbUseFloor)
			, bUseGridBasedConstraints(InbUseGridBasedConstraints)
			, GridDx(InGridDx)
			, bDoQuasistatics(InbDoQuasistatics)
			, EMesh(InEMesh)
			, bDoBlended(InbDoBlended)
			, BlendedZeta(InBlendedZeta)
			, Damping(InDamping)
			, bEnableGravity(InbEnableGravity)
			, bEnableCorotatedConstraints(InbEnableCorotatedConstraints)
			, bEnablePositionTargets(InbEnablePositionTargets)
			, bUseGaussSeidelConstraints(InbUseGaussSeidelConstraints)
			, bUseSOR(InbUseSOR)
			, OmegaSOR(InOmegaSOR)
			, bUseGSNeohookean(InbUseGSNeohookean)
			, bDoSpringCollision(InbDoSpringCollision)
			, bDoInComponentSpringCollision(InbDoInComponentSpringCollision)
			, NRingExcluded(InNRingExcluded)
			, SpringCollisionSearchRadius(InSpringCollisionSearchRadius)
			, SpringCollisionStiffness(InSpringCollisionStiffness)
			, bAllowSliding(InbAllowSliding)
			, bDoSphereRepulsion(InbDoSphereRepulsion)
			, SphereRepulsionRadius(InSphereRepulsionRadius)
			, SphereRepulsionStiffness(InSphereRepulsionStiffness)
			, bDoMuscleActivation(InbDoMuscleActivation)
			, bCollideWithFullMesh(InbCollideWithFullMesh)
			, bEnableDynamicSprings(InbEnableDynamicSprings)
		{}

		int32 NumSolverSubSteps = 5;
		int32 NumSolverIterations = 5;
		bool FixTimeStep = false;
		FSolverReal TimeStepSize = (FSolverReal)0.05;
		bool CacheToFile = false;
		bool bEnableKinematics = true;
		bool bUseFloor = true;
		bool bUseGridBasedConstraints = false;
		FSolverReal GridDx = (FSolverReal)1.;
		bool bDoQuasistatics = false;
		FSolverReal EMesh = (FSolverReal)100000.;
		bool bDoBlended = false;
		FSolverReal BlendedZeta = (FSolverReal)0.;
		FSolverReal Damping = (FSolverReal)0.;
		bool bEnableGravity = true;
		bool bEnableCorotatedConstraints = true;
		bool bEnablePositionTargets = true;
		bool bUseGaussSeidelConstraints = false;
		bool bUseSOR = true;
		FSolverReal OmegaSOR = (FSolverReal)1.6;
		bool bUseGSNeohookean = false;
		bool bDoSpringCollision = false;
		bool bDoInComponentSpringCollision = false;
		int32 NRingExcluded = 1;
		FSolverReal SpringCollisionSearchRadius = (FSolverReal)0;
		FSolverReal SpringCollisionStiffness = (FSolverReal)500.;
		bool bAllowSliding = true;
		bool bDoSphereRepulsion = false;
		FSolverReal SphereRepulsionRadius = (FSolverReal)0;
		FSolverReal SphereRepulsionStiffness = (FSolverReal)500.;
		bool bDoMuscleActivation = false;
		bool bCollideWithFullMesh = false;
		bool bEnableDynamicSprings = true; 
	};


	/*Data Transfer*/
	typedef TSharedPtr<const FThreadingProxy::FBuffer> FDataMapValue; // Buffer Pointer
	typedef TMap<FThreadingProxy::FKey, FDataMapValue > FDeformableDataMap; // <const UObject*,FBufferSharedPtr>

	struct FDeformablePackage {
		FDeformablePackage()
		{}

		FDeformablePackage(int32 InFrame, FDeformableDataMap&& InMap)
			: Frame(InFrame)
			, ObjectMap(InMap)
		{}

		int32 Frame = INDEX_NONE;
		FDeformableDataMap ObjectMap;
	};

	/* Accessor for the Game Thread*/
	class FGameThreadAccessor
	{
	public:
//		friend class UDeformableSolverComponent;
//		friend class FFleshCacheAdapter;
//#if PLATFORM_WINDOWS
//	protected:
//#endif
		FGameThreadAccessor() {}
	};


	/* Accessor for the Physics Thread*/
	class FPhysicsThreadAccessor
	{
	public:
//		friend class UDeformableSolverComponent;
//		friend class FFleshCacheAdapter;
//#if PLATFORM_WINDOWS
//	protected:
//#endif
		FPhysicsThreadAccessor() {}
	};


	struct FDeformableDebugParams
	{				
		bool bDoDrawTetrahedralParticles = false;
		bool bDoDrawKinematicParticles = false;
		bool bDoDrawTransientKinematicParticles = false;
		bool bDoDrawRigidCollisionGeometry = false;
		FSolverReal ParticleRadius = 5.f;

		bool IsDebugDrawingEnabled()
		{ 
#if WITH_EDITOR
			// p.Chaos.DebugDraw.Enabled 1
			return Chaos::FDebugDrawQueue::GetInstance().IsDebugDrawingEnabled();
#else
			return false;
#endif
		}
	};

	struct FDeformableXPBDCorotatedParams
	{
		int32 XPBDCorotatedBatchSize = 5;
		int32 XPBDCorotatedBatchThreshold = 5;

	};


}; // namesapce Chaos::Softs
