// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/BoundingVolumeHierarchy.h"
#include "Chaos/PBDSpringConstraintsBase.h"
#include "Chaos/XPBDCorotatedConstraints.h"
#include "ChaosStats.h"
#include "Chaos/ImplicitQRSVD.h"
#include "Chaos/GraphColoring.h"
#include "Chaos/NewtonCorotatedCache.h"
#include "Chaos/Framework/Parallel.h"
#include "Chaos/Utilities.h"
#include "Chaos/DebugDrawQueue.h"
#include "Chaos/XPBDWeakConstraints.h"
#include "Chaos/Triangle.h"
#include "Chaos/TriangleCollisionPoint.h"
#include "Chaos/TriangleMesh.h"
#include <unordered_map>
#include "Chaos/HierarchicalSpatialHash.h"
#include "Chaos/PBDSelfCollisionSphereConstraints.h"

namespace Chaos::Softs
{
	using Chaos::TVec3;
	template <typename T, typename ParticleType>
	class FGaussSeidelSphereRepulsionConstraints
	{
	public:
		//TODO(Yushan): Add unit tests for Gauss Seidel Sphere Repulsion Constraints
		FGaussSeidelSphereRepulsionConstraints(FSolverReal InRadius, FSolverReal InStiffness, const ParticleType& InParticles, const FDeformableXPBDWeakConstraintParams& InParams): Radius(InRadius), Stiffness(InStiffness), DebugDrawParams(InParams)
		{
			const TArrayCollectionArray<FPAndInvM>& PAndInvM = InParticles.GetPAndInvM();
			ReferencePositions.SetNum(PAndInvM.Num());
			for (int32 i = 0; i < PAndInvM.Num(); ++i)
			{
				ReferencePositions[i] = PAndInvM[i].P;
			}
		}

		virtual ~FGaussSeidelSphereRepulsionConstraints(){}

		//Energy = k/2*(2r-d)^2
		//Residual = de/dx = -force = -k*(2r-d)*dd/dx
		//Hessian = de2/dx2 = k*dd/dx*dd/dx-k*(2r-d)*(-dd/dx*dd/dx^T+I)/d
		void AddSphereRepulsionResidual(const ParticleType& InParticles, const int32 p, const T Dt, TVec3<T>& res)
		{

		}

		void AddSphereRepulsionHessian(const int32 p, const T Dt, Chaos::PMatrix<T, 3, 3>& ParticleHessian)
		{

		}

		void AddSphereRepulsionResidualAndHessian(const ParticleType& InParticles, const int32 ConstraintIndex, const int32 LocalIndex, const T Dt, TVec3<T>& ParticleResidual, Chaos::PMatrix<T, 3, 3>& ParticleHessian)
		{
			Chaos::TVec3<T> x0 = InParticles.P(Constraints[ConstraintIndex][0]);
			Chaos::TVec3<T> x1 = InParticles.P(Constraints[ConstraintIndex][1]);
			Chaos::TVec3<T> normal = (x1 - x0).GetSafeNormal();
			normal = (LocalIndex ? normal : -normal); //dd/dx
			T dist = (x1 - x0).Size();
			FSolverReal penetration = 2 * Radius - dist; //2r-d

			if (penetration > T(0)) {
				T dist_inv = T(1) / (dist+T(1e-12));
				Chaos::PMatrix<T, 3, 3> outer = Chaos::PMatrix<T, 3, 3>::OuterProduct(normal, normal);
				Chaos::PMatrix<T, 3, 3> A = (Chaos::PMatrix<T, 3, 3>::Identity - outer) * dist_inv; //(-dd/dx*dd/dx^T+I)/d
				ParticleHessian += Dt * Dt * ConstraintStiffness[ConstraintIndex] * (outer - penetration * A);
				ParticleResidual += -Dt * Dt * penetration * ConstraintStiffness[ConstraintIndex] * normal;
			}
		}

		void VisualizeAllBindings(const FSolverParticles& InParticles, const T Dt) const
		{
#if WITH_EDITOR
			auto DoubleVert = [](Chaos::TVec3<T> V) { return FVector3d(V.X, V.Y, V.Z); };
			for (int32 i = 0; i < Constraints.Num(); i++)
			{
				FVector3d SourcePos = DoubleVert(InParticles.P(Constraints[i][0]));
				FVector3d TargetPos = DoubleVert(InParticles.P(Constraints[i][1]));

				float ParticleThickness = DebugDrawParams.DebugParticleWidth;
				float LineThickness = DebugDrawParams.DebugLineWidth;

				Chaos::FDebugDrawQueue::GetInstance().DrawDebugPoint(SourcePos, FColor::Red, false, Dt, 0, ParticleThickness);
				Chaos::FDebugDrawQueue::GetInstance().DrawDebugPoint(TargetPos, FColor::Red, false, Dt, 0, ParticleThickness);
				Chaos::FDebugDrawQueue::GetInstance().DrawDebugLine(SourcePos, TargetPos, FColor::Green, false, Dt, 0, LineThickness);
			}
#endif
		}

		void Init(const FSolverParticles& InParticles, const T Dt) const
		{
			if (DebugDrawParams.bVisualizeBindings)
			{
				VisualizeAllBindings(InParticles, Dt);
			}

		}

		void UpdateSphereRepulsionConstraints(const FSolverParticles& Particles, const TArray<int32>& SurfaceVertices, const TArray<int32>& ComponentIndex)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(STAT_GaussSeidelSphereRepulsionConstraints);
			Constraints.Reset();
			ConstraintStiffness.Reset();
			if (SurfaceVertices.Num() == 0)
			{
				return;
			}

			// Build Spatial
			TArray<FSphereSpatialEntry> Entries;
			const FSolverReal Diameter = 2.f * Radius;
			TConstArrayView<FSolverVec3> Points = Particles.XArray();

			Entries.Reset(SurfaceVertices.Num());
			for (int32 Index = 0; Index < SurfaceVertices.Num(); ++Index)
			{
				Entries.Add({ &Points, SurfaceVertices[Index]});
			}
			
			TSpatialHashGridPoints<int32, FSolverReal> SpatialHash(Diameter);
			SpatialHash.InitializePoints(Entries);

			const FSolverReal DiamSq = FMath::Square(Diameter);
			constexpr int32 CellRadius = 1; // We set the cell size of the spatial hash such that we only need to look 1 cell away to find proximities.
			constexpr int32 MaxNumExpectedConnectionsPerParticle = 3;
			const int32 MaxNumExpectedConnections = MaxNumExpectedConnectionsPerParticle * Entries.Num();

			Constraints = SpatialHash.FindAllSelfProximities(CellRadius, MaxNumExpectedConnections,
				[this, &Particles, DiamSq, &ComponentIndex](const int32 i1, const int32 i2)
				{
					if (ComponentIndex[i1] == ComponentIndex[i2])
					{
						return false;
					}
					const FSolverReal CombinedMass = Particles.InvM(i1) + Particles.InvM(i2);
					if (CombinedMass < (FSolverReal)1e-7)
					{
						return false;
					}
					if (FSolverVec3::DistSquared(this->ReferencePositions[i1], this->ReferencePositions[i2]) < DiamSq)
					{
						return false;
					}
					return true;
				}
			);

			for (const TVec2<int32>& CollisionPair : Constraints)
			{
				T TotalMass = T(0);
				if (Particles.InvM(CollisionPair[0]) > (FSolverReal)1e-7)
				{
					TotalMass += Particles.M(CollisionPair[0]);
				}
				if (Particles.InvM(CollisionPair[1]) > (FSolverReal)1e-7)
				{
					TotalMass += Particles.M(CollisionPair[1]);
				}
				ConstraintStiffness.Add(Stiffness*TotalMass/T(2));
			}
		}

		void ReturnSphereRepulsionConstraints(TArray<TArray<int32>>& ExtraConstraints, TArray<TArray<int32>>& ExtraIncidentElements, TArray<TArray<int32>>& ExtraIncidentElementsLocal)
		{
			ExtraConstraints.Init(TArray<int32>(), Constraints.Num());
			for (int32 i = 0; i < Constraints.Num(); i++)
			{
				ExtraConstraints[i].SetNum(2);
				ExtraConstraints[i][0] = Constraints[i][0];
				ExtraConstraints[i][1] = Constraints[i][1];
			}
			ExtraIncidentElements = Chaos::Utilities::ComputeIncidentElements(ExtraConstraints, &ExtraIncidentElementsLocal);
		}

		protected:
			TArray<TVec2<int32>> Constraints;
			FSolverReal Radius = FSolverReal(0);
			FSolverReal Stiffness = FSolverReal(0);
			TArray<T> ConstraintStiffness;
		private:
			struct FSphereSpatialEntry
			{
				const TConstArrayView<FSolverVec3>* Points;
				int32 Index;

				FSolverVec3 X() const
				{
					return (*Points)[Index];
				}

				template<typename TPayloadType>
				int32 GetPayload(int32) const
				{
					return Index;
				}
			};
			TArray<FSolverVec3> ReferencePositions;
			FDeformableXPBDWeakConstraintParams DebugDrawParams;
	};


}// End namespace Chaos::Softs
