// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDSpringConstraintsBase.h"
#include "ChaosStats.h"
#include "Chaos/ImplicitQRSVD.h"
#include "Chaos/GraphColoring.h"
#include "Chaos/Framework/Parallel.h"
#include "Chaos/Deformable/ChaosDeformableSolverTypes.h"
#include "Chaos/Vector.h"


namespace Chaos::Softs
{
	template <typename T, typename ParticleType>
	class FGaussSeidelLinearCodimensionalConstraints
	{

	public:
		FGaussSeidelLinearCodimensionalConstraints(
			const ParticleType& InParticles,
			const TArray<TVector<int32, 3>>& InMesh,
			const T& EMesh = (T)10.0,
			const T& NuMesh = (T).3
		)
			: MeshConstraints(InMesh)
		{
			Measure.Init((T)0., MeshConstraints.Num());
			Lambda = EMesh * NuMesh / (((T)1. + NuMesh) * ((T)1. - (T)2. * NuMesh));
			Mu = EMesh / ((T)2. * ((T)1. + NuMesh));

			MuElementArray.Init(Mu, MeshConstraints.Num());
			LambdaElementArray.Init(Lambda, MeshConstraints.Num());

			InitializeCodimensionData(InParticles);
		}

		FGaussSeidelLinearCodimensionalConstraints(
			const ParticleType& InParticles,
			const TArray<TVector<int32, 3>>& InMesh,
			const TArray<T>& EMeshArray,
			const T& NuMesh = (T).3
		)
			: MeshConstraints(InMesh)
		{
			Measure.Init((T)0., MeshConstraints.Num());

			InitializeCodimensionData(InParticles);
			LambdaElementArray.Init((T)0., MeshConstraints.Num());
			MuElementArray.Init((T)0., MeshConstraints.Num());
			
			for (int32 e = 0; e < InMesh.Num(); e++)
			{
				LambdaElementArray[e] = EMeshArray[e] * NuMesh / (((T)1. + NuMesh) * ((T)1. - (T)2. * NuMesh));
				MuElementArray[e] = EMeshArray[e] / ((T)2. * ((T)1. + NuMesh));
			}
		}

		virtual ~FGaussSeidelLinearCodimensionalConstraints() {}

		PMatrix<T, 3, 3> DsInit(const int32 E, const ParticleType& InParticles)  
		{
			PMatrix<T, 3, 3> Result((T)0.);
			for (int32 i = 0; i < 3; i++) 
			{
				for (int32 c = 0; c < 3; c++) 
				{
					Result.SetAt(c, i, InParticles.X(MeshConstraints[E][i + 1])[c] - InParticles.X(MeshConstraints[E][0])[c]);
				}
			}
			return Result;
		}

		PMatrix<T, 3, 2> Ds(const int32 E, const ParticleType& InParticles) const 
		{
			if (INDEX_NONE < E && E< MeshConstraints.Num()
				&& INDEX_NONE < MeshConstraints[E][0] && MeshConstraints[E][0] < (int32)InParticles.Size()
				&& INDEX_NONE < MeshConstraints[E][1] && MeshConstraints[E][1] < (int32)InParticles.Size()
				&& INDEX_NONE < MeshConstraints[E][2] && MeshConstraints[E][2] < (int32)InParticles.Size())
			{
				const TVec3<T> P1P0 = InParticles.P(MeshConstraints[E][1]) - InParticles.P(MeshConstraints[E][0]);
				const TVec3<T> P2P0 = InParticles.P(MeshConstraints[E][2]) - InParticles.P(MeshConstraints[E][0]);

				return PMatrix<T, 3, 2>(
					P1P0[0], P1P0[1], P1P0[2],
					P2P0[0], P2P0[1], P2P0[2]);
			}
			else
			{
				return PMatrix<T, 3, 2>(
					(T)0., (T)0, (T)0,
					(T)0, (T)0, (T)0);
			}
		}

		PMatrix<T, 3, 2> F(const int32 E, const ParticleType& InParticles) const 
		{
			if (INDEX_NONE < E && E < DmInverse.Num())
			{
				return Ds(E, InParticles) * DmInverse[E];
			}
			else
			{
				return PMatrix<T, 3, 2>(
					(T)0., (T)0, (T)0,
					(T)0, (T)0, (T)0);
			}
		}

		TArray<TArray<int32>> GetConstraintsArray() const
		{
			TArray<TArray<int32>> Constraints;
			Constraints.SetNum(MeshConstraints.Num());
			for (int32 i = 0; i < MeshConstraints.Num(); i++)
			{
				Constraints[i].SetNum(3);
				for (int32 j = 0; j < 3; j++)
				{
					Constraints[i][j] = MeshConstraints[i][j];
				}
			}
			return Constraints;
		}

	protected:

		void InitializeCodimensionData(const ParticleType& Particles)
		{
			Measure.Init((T)0., MeshConstraints.Num());
			DmInverse.Init(PMatrix<FSolverReal, 2, 2>(0.f, 0.f, 0.f), MeshConstraints.Num());
			for (int32 e = 0; e < MeshConstraints.Num(); e++)
			{
				check(MeshConstraints[e][0] < (int32)Particles.Size() && MeshConstraints[e][0] > INDEX_NONE);
				check(MeshConstraints[e][1] < (int32)Particles.Size() && MeshConstraints[e][1] > INDEX_NONE);
				check(MeshConstraints[e][2] < (int32)Particles.Size() && MeshConstraints[e][2] > INDEX_NONE);
				if (MeshConstraints[e][0] < (int32)Particles.Size() && MeshConstraints[e][0] > INDEX_NONE
					&& MeshConstraints[e][1] < (int32)Particles.Size() && MeshConstraints[e][1] > INDEX_NONE
					&& MeshConstraints[e][2] < (int32)Particles.Size() && MeshConstraints[e][2] > INDEX_NONE)
				{
					const TVec3<T> X1X0 = Particles.GetX(MeshConstraints[e][1]) - Particles.GetX(MeshConstraints[e][0]);
					const TVec3<T> X2X0 = Particles.GetX(MeshConstraints[e][2]) - Particles.GetX(MeshConstraints[e][0]);
					PMatrix<T, 2, 2> Dm((T)0., (T)0., (T)0.);
					Dm.M[0] = X1X0.Size();
					Dm.M[2] = X1X0.Dot(X2X0) / Dm.M[0];
					Dm.M[3] = Chaos::TVector<T, 3>::CrossProduct(X1X0, X2X0).Size() / Dm.M[0];
					Measure[e] = Chaos::TVector<T, 3>::CrossProduct(X1X0, X2X0).Size() / (T)2.;
					ensureMsgf(Measure[e] > (T)0., TEXT("Degenerate triangle detected"));

					PMatrix<T, 2, 2> DmInv = Dm.Inverse();
					DmInverse[e] = DmInv;
				}
			}
		}

		static T SafeRecip(const T Len, const T Fallback)
		{
			if (Len > (T)UE_SMALL_NUMBER)
			{
				return (T)1. / Len;
			}
			return Fallback;
		}

	public:
		void AddHyperelasticResidualAndHessian(const ParticleType& Particles, const int32 ElementIndex, const int32 ElementIndexLocal, const T Dt, TVec3<T>& ParticleResidual, Chaos::PMatrix<T, 3, 3>& ParticleHessian)
		{
			check(ElementIndex < DmInverse.Num() && ElementIndex > INDEX_NONE);
			check(ElementIndex < MuElementArray.Num());
			check(ElementIndex < Measure.Num());
			check(ElementIndex < LambdaElementArray.Num());
			check(ElementIndexLocal < 3 && ElementIndexLocal > INDEX_NONE);
			if (ElementIndexLocal < 3 
				&& ElementIndexLocal > INDEX_NONE
				&& ElementIndex < Measure.Num()
				&& ElementIndex < MuElementArray.Num()
				&& ElementIndex < LambdaElementArray.Num()
				&& ElementIndex < DmInverse.Num() 
				&& ElementIndex > INDEX_NONE)
			{
				const Chaos::PMatrix<T, 2, 2> DmInvT = DmInverse[ElementIndex].GetTransposed(), DmInv = DmInverse[ElementIndex]; 
				const Chaos::PMatrix<T, 3, 2> Fe = F(ElementIndex, Particles);

				PMatrix<T, 3, 2> Pe(TVec3<T>((T)0.), TVec3<T>((T)0.)), ForceTerm(TVec3<T>((T)0.), TVec3<T>((T)0.));
				Pe = T(2) * MuElementArray[ElementIndex] * Fe; 

				ForceTerm = -Measure[ElementIndex] * Pe * DmInverse[ElementIndex].GetTransposed();

				Chaos::TVector<T, 3> Dx((T)0.);
				if (ElementIndexLocal > 0)
				{
					for (int32 c = 0; c < 3; c++)
					{
						Dx[c] += ForceTerm.M[ElementIndexLocal * 3 - 3 + c];
					}
				}
				else
				{
					for (int32 c = 0; c < 3; c++)
					{
						for (int32 h = 0; h < 2; h++)
						{
							Dx[c] -= ForceTerm.M[h * 3 + c];
						}
					}
				}

				Dx *= Dt * Dt;
				ParticleResidual -= Dx;
				T Coeff = Dt * Dt * Measure[ElementIndex];

				if (ElementIndexLocal == 0) 
				{
					T DmInvsum = T(0);
					for (int32 nu = 0; nu < 2; nu++) 
					{
						T localDmsum = T(0);
						for (int32 k = 0; k < 2; k++) 
						{
							localDmsum += DmInv.M[nu * 2 + k];
						}
						DmInvsum += localDmsum * localDmsum;
					}
					for (int32 alpha = 0; alpha < 3; alpha++) 
					{
						ParticleHessian.SetAt(alpha, alpha, ParticleHessian.GetAt(alpha, alpha) + Coeff * T(2) * MuElementArray[ElementIndex] * DmInvsum);
					}
				}
				else 
				{
					T DmInvsum = T(0);
					for (int32 nu = 0; nu < 2; nu++)
					{
						DmInvsum += DmInv.GetAt(ElementIndexLocal - 1, nu) * DmInv.GetAt(ElementIndexLocal - 1, nu);
					}
					for (int32 alpha = 0; alpha < 3; alpha++)
					{
						ParticleHessian.SetAt(alpha, alpha, ParticleHessian.GetAt(alpha, alpha) + Dt * Dt * Measure[ElementIndex] * T(2) * MuElementArray[ElementIndex] * DmInvsum);
					}
				}
			}
		}

	protected:
		mutable TArray<FSolverMatrix22> DmInverse;

		//material constants calculated from E:
		T Mu;
		T Lambda;
		TArray<T> MuElementArray;
		TArray<T> LambdaElementArray;
		TArray<T> AlphaJArray;

		TArray<TVector<int32, 3>> MeshConstraints;
		mutable TArray<T> Measure;
	};

}  // End namespace Chaos::Softs


