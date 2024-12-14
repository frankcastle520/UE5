// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Framework/Parallel.h"
#include "Chaos/XPBDCorotatedConstraints.h"
#include "GeometryCollection/Facades/CollectionMuscleActivationFacade.h"

namespace Chaos::Softs
{

	using Chaos::TVec3;
	typedef GeometryCollection::Facades::FMuscleActivationFacade Facade;
	typedef GeometryCollection::Facades::FMuscleActivationData Data;
	template <typename T, typename ParticleType>
	struct FMuscleActivationConstraints
	{
		//Handles muscle activation data
		FMuscleActivationConstraints(){}

		virtual ~FMuscleActivationConstraints() {}

		void Init(const FSolverParticles& InParticles, const T Dt) const
		{
		}

		void AddMuscles(const Facade& FMuscleActivation, int32 VertexOffset = 0, int32 ElementOffset = 0)
		{
			for (int MuscleIdx = 0; MuscleIdx < FMuscleActivation.NumMuscles(); MuscleIdx++)
			{
				Data MuscleActivationData = FMuscleActivation.GetMuscleActivationData(MuscleIdx);
				if (FMuscleActivation.IsValidGeometryIndex(MuscleActivationData.GeometryGroupIndex))
				{
					int32 OldSize = MuscleActivationElement.Num();
					MuscleActivationElement.SetNum(OldSize + 1);
					FiberDirectionMatrix.SetNum(OldSize + 1);
					ContractionVolumeScale.SetNum(OldSize + 1);
					for (int32 i = 0; i < MuscleActivationData.MuscleActivationElement.Num(); i++)
					{
						if (FMuscleActivation.IsValidElementIndex(MuscleActivationData.MuscleActivationElement[i]))
						{
							MuscleActivationElement[OldSize].Add(MuscleActivationData.MuscleActivationElement[i] + ElementOffset);
							FiberDirectionMatrix[OldSize].Add(MuscleActivationData.FiberDirectionMatrix[i]);
							ContractionVolumeScale[OldSize].Add(MuscleActivationData.ContractionVolumeScale[i]);
						}
					}
					MuscleActivationData.OriginInsertionPair[0] += VertexOffset;
					MuscleActivationData.OriginInsertionPair[1] += VertexOffset;
					OriginInsertionPair.Add(MuscleActivationData.OriginInsertionPair);
					OriginInsertionRestLength.Add(MuscleActivationData.OriginInsertionRestLength);
					Activation.Add(0.f);
				}
			}
		}

		void UpdateLengthBasedMuscleActivation(const ParticleType& InParticles)
		{
			float AverageFiberLengthScale = 0;
			for (int32 MuscleIdx = 0; MuscleIdx < MuscleActivationElement.Num(); MuscleIdx++)
			{
				if (ensureMsgf(MuscleActivationElement[MuscleIdx].Num() == FiberDirectionMatrix[MuscleIdx].Num(),
					TEXT("MuscleActivationElement[%d].Num() = %d, not equal to FiberDirectionMatrix[%d].Num() = %d"), MuscleIdx, MuscleActivationElement[MuscleIdx].Num(), MuscleIdx, FiberDirectionMatrix[MuscleIdx].Num()))
				{
					//Infer activation
					float CurrentFiberLengthScale = (InParticles.X(OriginInsertionPair[MuscleIdx][0]) - InParticles.X(OriginInsertionPair[MuscleIdx][1])).Size() / OriginInsertionRestLength[MuscleIdx];
					AverageFiberLengthScale += CurrentFiberLengthScale;
					Activation[MuscleIdx] = CurrentFiberLengthScale * CurrentFiberLengthScale;
				}
			}
		}

		void ApplyMuscleActivation(FXPBDCorotatedConstraints<T,ParticleType>& Constraints) const
		{
			for (int32 MuscleIdx = 0; MuscleIdx < MuscleActivationElement.Num(); MuscleIdx++)
			{
				if (ensureMsgf(MuscleActivationElement[MuscleIdx].Num() == FiberDirectionMatrix[MuscleIdx].Num(), 
					TEXT("MuscleActivationElement[%d].Num() = %d, not equal to FiberDirectionMatrix[%d].Num() = %d"), MuscleIdx, MuscleActivationElement[MuscleIdx].Num(), MuscleIdx, FiberDirectionMatrix[MuscleIdx].Num()))
				{
					for (int32 ElemIdx = 0; ElemIdx < MuscleActivationElement[MuscleIdx].Num(); ElemIdx++)
					{
						Constraints.ModifyDmInverseFromFiberLength(MuscleActivationElement[MuscleIdx][ElemIdx], Activation[MuscleIdx], FiberDirectionMatrix[MuscleIdx][ElemIdx], ContractionVolumeScale[MuscleIdx][ElemIdx]);
					}
				}
			}
		}
		
		TArray<TArray<int32>> MuscleActivationElement;
		TArray<FIntVector2> OriginInsertionPair;
		TArray<float> OriginInsertionRestLength;
		TArray<float> Activation;
		TArray<TArray<Chaos::PMatrix33d>> FiberDirectionMatrix;
		TArray<TArray<float>> ContractionVolumeScale;
	};


}// End namespace Chaos::Softs
