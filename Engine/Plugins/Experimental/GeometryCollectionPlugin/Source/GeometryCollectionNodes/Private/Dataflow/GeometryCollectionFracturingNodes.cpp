// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/GeometryCollectionFracturingNodes.h"
#include "Dataflow/DataflowCore.h"

#include "Engine/StaticMesh.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionEngineUtility.h"
#include "GeometryCollection/GeometryCollectionEngineConversion.h"
#include "Logging/LogMacros.h"
#include "Templates/SharedPointer.h"
#include "UObject/UnrealTypePrivate.h"
#include "DynamicMeshToMeshDescription.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "StaticMeshAttributes.h"
#include "DynamicMeshEditor.h"
#include "Operations/MeshBoolean.h"

#include "EngineGlobals.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/GeometryCollectionClusteringUtility.h"
#include "GeometryCollection/GeometryCollectionConvexUtility.h"
#include "Voronoi/Voronoi.h"
#include "PlanarCut.h"
#include "GeometryCollection/GeometryCollectionProximityUtility.h"
#include "FractureEngineClustering.h"
#include "FractureEngineSelection.h"
#include "GeometryCollection/Facades/CollectionTransformSelectionFacade.h"
#include "GeometryCollection/Facades/CollectionBoundsFacade.h"
#include "Dataflow/DataflowSelection.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "UDynamicMesh.h"
#include "MeshDescription.h"
#include "DynamicMeshToMeshDescription.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "StaticMeshAttributes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GeometryCollectionFracturingNodes)

namespace UE::Dataflow
{

	void GeometryCollectionFracturingNodes()
	{
		static const FLinearColor CDefaultNodeBodyTintColor = FLinearColor(0.f, 0.f, 0.f, 0.5f);

		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FUniformScatterPointsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FUniformScatterPointsDataflowNode_v2);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FRadialScatterPointsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FRadialScatterPointsDataflowNode_v2);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGridScatterPointsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FVoronoiFractureDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FVoronoiFractureDataflowNode_v2);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FPlaneCutterDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FPlaneCutterDataflowNode_v2);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FExplodedViewDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FSliceCutterDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FBrickCutterDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMeshCutterDataflowNode);

		// GeometryCollection|Fracture
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY_NODE_COLORS_BY_CATEGORY("GeometryCollection|Fracture", FLinearColor(1.f, 1.f, 0.8f), CDefaultNodeBodyTintColor);
	}
}

void FUniformScatterPointsDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<FVector>>(&Points))
	{
		const FBox& BBox = GetValue<FBox>(Context, &BoundingBox);
		if (BBox.GetVolume() > 0.f)
		{
			FRandomStream RandStream(GetValue<float>(Context, &RandomSeed));

			const FVector Extent(BBox.Max - BBox.Min);
			const int32 NumPoints = RandStream.RandRange(GetValue<int32>(Context, &MinNumberOfPoints), GetValue<int32>(Context, &MaxNumberOfPoints));

			TArray<FVector> PointsArr;
			PointsArr.Reserve(NumPoints);
			for (int32 Idx = 0; Idx < NumPoints; ++Idx)
			{
				PointsArr.Emplace(BBox.Min + FVector(RandStream.FRand(), RandStream.FRand(), RandStream.FRand()) * Extent);
			}

			SetValue(Context, MoveTemp(PointsArr), &Points);
		}
		else
		{
			// ERROR: Invalid BoundingBox input
			SetValue(Context, TArray<FVector>(), &Points);
		}
	}
}

void FUniformScatterPointsDataflowNode_v2::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<FVector>>(&Points))
	{
		const FBox& BBox = GetValue<FBox>(Context, &BoundingBox);
		if (BBox.GetVolume() > 0.f)
		{
			FRandomStream RandStream(GetValue<int32>(Context, &RandomSeed));

			const FVector Extent(BBox.Max - BBox.Min);
			const int32 NumPoints = RandStream.RandRange(GetValue<int32>(Context, &MinNumberOfPoints), GetValue<int32>(Context, &MaxNumberOfPoints));

			TArray<FVector> PointsArr;
			PointsArr.Reserve(NumPoints);
			for (int32 Idx = 0; Idx < NumPoints; ++Idx)
			{
				PointsArr.Emplace(BBox.Min + FVector(RandStream.FRand(), RandStream.FRand(), RandStream.FRand()) * Extent);
			}

			SetValue(Context, MoveTemp(PointsArr), &Points);
		}
		else
		{
			// ERROR: Invalid BoundingBox input
			SetValue(Context, TArray<FVector>(), &Points);
		}
	}
}

void FRadialScatterPointsDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<FVector>>(&Points))
	{
		const FVector::FReal RadialStep = GetValue<float>(Context, &Radius) / GetValue<int32>(Context, &RadialSteps);
		const FVector::FReal AngularStep = 2 * PI / GetValue<int32>(Context, &AngularSteps);

		FRandomStream RandStream(GetValue<float>(Context, &RandomSeed));
		FVector UpVector(GetValue<FVector>(Context, &Normal));
		UpVector.Normalize();
		FVector BasisX, BasisY;
		UpVector.FindBestAxisVectors(BasisX, BasisY);

		TArray<FVector> PointsArr;

		FVector::FReal Len = RadialStep * .5;
		for (int32 ii = 0; ii < GetValue<int32>(Context, &RadialSteps); ++ii, Len += RadialStep)
		{
			FVector::FReal Angle = FMath::DegreesToRadians(GetValue<float>(Context, &AngleOffset));
			for (int32 kk = 0; kk < AngularSteps; ++kk, Angle += AngularStep)
			{
				FVector RotatingOffset = Len * (FMath::Cos(Angle) * BasisX + FMath::Sin(Angle) * BasisY);
				PointsArr.Emplace(GetValue<FVector>(Context, &Center) + RotatingOffset + (RandStream.VRand() * RandStream.FRand() * Variability));
			}
		}

		SetValue(Context, MoveTemp(PointsArr), &Points);
	}
}

void FRadialScatterPointsDataflowNode_v2::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<FVector>>(&Points))
	{
		const FBox InBoundingBox = GetValue<FBox>(Context, &BoundingBox);
		const FVector InCenter = GetValue<FVector>(Context, &Center);
		const FVector InNormal = GetValue<FVector>(Context, &Normal);
		const int32 InRandomSeed = GetValue<int32>(Context, &RandomSeed);
		const int32 InAngularSteps = GetValue<int32>(Context, &AngularSteps);
		const float InAngleOffset = GetValue<float>(Context, &AngleOffset);
		const float InAngularNoise = GetValue<float>(Context, &AngularNoise);
		const float InRadius = GetValue<float>(Context, &Radius);
		const int32 InRadialSteps = GetValue<int32>(Context, &RadialSteps);
		const float InRadialStepExponent = GetValue<float>(Context, &RadialStepExponent);
		const float InRadialMinStep = GetValue<float>(Context, &RadialMinStep);
		const float InRadialNoise = GetValue<float>(Context, &RadialNoise);
		const float InRadialVariability = GetValue<float>(Context, &RadialVariability);
		const float InAngularVariability = GetValue<float>(Context, &AngularVariability);
		const float InAxialVariability = GetValue<float>(Context, &AxialVariability);

		TArray<FVector> PointsArr;

		const FVector::FReal AngularStep = 2 * PI / InAngularSteps;

		FVector CenterVal(InBoundingBox.GetCenter() + InCenter);

		FRandomStream RandStream(InRandomSeed);
		FVector UpVector(InNormal);
		UpVector.Normalize();
		FVector BasisX, BasisY;
		UpVector.FindBestAxisVectors(BasisX, BasisY);

		// Precompute consistent noise for each angular step
		TArray<FVector::FReal> AngleStepOffsets;
		AngleStepOffsets.SetNumUninitialized(InAngularSteps);
		for (int32 AngleIdx = 0; AngleIdx < InAngularSteps; ++AngleIdx)
		{
			AngleStepOffsets[AngleIdx] = FMath::DegreesToRadians(RandStream.FRandRange(-1, 1) * InAngularNoise);
		}

		// Compute radial positions following an (idx+1)^exp curve, and then re-normalize back to the Radius range
		TArray<FVector::FReal> RadialPositions;
		RadialPositions.SetNumUninitialized(InRadialSteps);
		FVector::FReal StepOffset = 0;
		for (int32 RadIdx = 0; RadIdx < InRadialSteps; ++RadIdx)
		{
			FVector::FReal RadialPos = FMath::Pow(RadIdx + 1, InRadialStepExponent) + StepOffset;
			if (RadIdx == 0)
			{
				// Note we bring the first point a half-step toward the center, and shift all subsequent points accordingly
				// so that for Exponent==1, the step from center to first boundary is the same distance as the step between each boundary
				// (this is only necessary because there is no Voronoi site at the center)
				RadialPos *= .5;
				StepOffset = -RadialPos;
			}

			RadialPositions[RadIdx] = RadialPos;
		}
		// Normalize positions so that the diagram fits in the target radius
		FVector::FReal RadialPosNorm = InRadius / RadialPositions.Last();
		for (FVector::FReal& RadialPos : RadialPositions)
		{
			RadialPos = RadialPos * RadialPosNorm;
		}
		// Add radial noise 
		for (int32 RadIdx = 0; RadIdx < InRadialSteps; ++RadIdx)
		{
			FVector::FReal& RadialPos = RadialPositions[RadIdx];
			// Offset by RadialNoise, but don't allow noise to take the value below 0
			RadialPos += RandStream.FRandRange(-FMath::Min(RadialPos, InRadialNoise), InRadialNoise);
		}
		// make sure the positions remain in increasing order
		RadialPositions.Sort();
		// Adjust positions so they are never closer than the RadialMinStep
		FVector::FReal LastRadialPos = 0;
		for (int32 RadIdx = 0; RadIdx < InRadialSteps; ++RadIdx)
		{
			FVector::FReal MinStep = InRadialMinStep;
			if (RadIdx == 0)
			{
				MinStep *= .5;
			}
			if (RadialPositions[RadIdx] - LastRadialPos < MinStep)
			{
				RadialPositions[RadIdx] = LastRadialPos + MinStep;
			}
			LastRadialPos = RadialPositions[RadIdx];
		}

		// Add a bit of noise to work around failure case in Voro++
		// TODO: fix the failure case in Voro++ and remove this
		float MinRadialVariability = InRadius > 1.f ? .0001f : 0.f;
		float UseRadialVariability = FMath::Max(MinRadialVariability, InRadialVariability);

		// Create the radial Voronoi sites
		for (int32 ii = 0; ii < InRadialSteps; ++ii)
		{
			FVector::FReal Len = RadialPositions[ii];
			FVector::FReal Angle = FMath::DegreesToRadians(InAngleOffset);
			for (int32 kk = 0; kk < InAngularSteps; ++kk, Angle += AngularStep)
			{
				// Add the global noise and the per-point noise into the angle
				FVector::FReal UseAngle = Angle + AngleStepOffsets[kk] + FMath::DegreesToRadians(RandStream.FRand() * InAngularVariability);
				// Add per point noise into the radial position
				FVector::FReal UseRadius = Len + FVector::FReal(RandStream.FRand() * UseRadialVariability);
				FVector RotatingOffset = UseRadius * (FMath::Cos(UseAngle) * BasisX + FMath::Sin(UseAngle) * BasisY);
				PointsArr.Emplace(CenterVal + RotatingOffset + UpVector * (RandStream.FRandRange(-1, 1) * InAxialVariability));
			}
		}

		SetValue(Context, MoveTemp(PointsArr), &Points);
	}
}

void FGridScatterPointsDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<TArray<FVector>>(&Points))
	{
		const FBox& BBox = GetValue<FBox>(Context, &BoundingBox);
		if (BBox.GetVolume() > 0.f)
		{
			const FVector Extent(BBox.Max - BBox.Min);
			const int32 NumPointsInX = GetValue<int32>(Context, &NumberOfPointsInX);
			const int32 NumPointsInY = GetValue<int32>(Context, &NumberOfPointsInY);
			const int32 NumPointsInZ = GetValue<int32>(Context, &NumberOfPointsInZ);
			
			if (NumPointsInX >= 2 && NumPointsInY >= 2 && NumPointsInZ >= 2)
			{
				const int32 NumPoints = NumPointsInX * NumPointsInY * NumPointsInZ;
				const float dX = Extent.X / (float)NumPointsInX;
				const float dY = Extent.Y / (float)NumPointsInY;
				const float dZ = Extent.Z / (float)NumPointsInZ;

				FRandomStream RandStream(GetValue<int32>(Context, &RandomSeed));

				TArray<FVector> PointsArr;
				PointsArr.Reserve(NumPoints);
				for (int32 Idx_X = 0; Idx_X < NumPointsInX; ++Idx_X)
				{
					for (int32 Idx_Y = 0; Idx_Y < NumPointsInY; ++Idx_Y)
					{
						for (int32 Idx_Z = 0; Idx_Z < NumPointsInZ; ++Idx_Z)
						{
							FVector RandomDisplacement = FVector(RandStream.FRandRange(-1.f, 1.f) * GetValue<float>(Context, &MaxRandomDisplacementX),
								RandStream.FRandRange(-1.f, 1.f) * GetValue<float>(Context, &MaxRandomDisplacementY),
								RandStream.FRandRange(-1.f, 1.f) * GetValue<float>(Context, &MaxRandomDisplacementZ));

							PointsArr.Emplace(BBox.Min.X + 0.5f * dX + (float)Idx_X * dX + RandomDisplacement.X,
								BBox.Min.Y + 0.5f * dY + (float)Idx_Y * dY + RandomDisplacement.Y,
								BBox.Min.Z + 0.5f * dZ + (float)Idx_Z * dZ + RandomDisplacement.Z);
						}
					}
				}

				SetValue(Context, MoveTemp(PointsArr), &Points);
			}
			else
			{
				// ERROR: Invalid number of points
				SetValue(Context, TArray<FVector>(), &Points);
			}
		}
		else
		{
			// ERROR: Invalid BoundingBox input
			SetValue(Context, TArray<FVector>(), &Points);
		}
	}
}

void FVoronoiFractureDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		const FDataflowTransformSelection& InTransformSelection = GetValue<FDataflowTransformSelection>(Context, &TransformSelection);

		if (IsConnected<FDataflowTransformSelection>(&TransformSelection))
		{
			if (InTransformSelection.AnySelected())
			{
				FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

				FFractureEngineFracturing::VoronoiFracture(InCollection,
					InTransformSelection,
					GetValue<TArray<FVector>>(Context, &Points),
					FTransform::Identity,
					(int32)GetValue<float>(Context, &RandomSeed),
					GetValue<float>(Context, &ChanceToFracture),
					true,
					GetValue<float>(Context, &Grout),
					GetValue<float>(Context, &Amplitude),
					GetValue<float>(Context, &Frequency),
					GetValue<float>(Context, &Persistence),
					GetValue<float>(Context, &Lacunarity),
					GetValue<int32>(Context, &OctaveNumber),
					GetValue<float>(Context, &PointSpacing),
					AddSamplesForCollision,
					GetValue<float>(Context, &CollisionSampleSpacing));

				SetValue(Context, MoveTemp(InCollection), &Collection);

				return;
			}
		}

		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		SetValue(Context, InCollection, &Collection);
	}
}

void FVoronoiFractureDataflowNode_v2::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection) ||
		Out->IsA<FDataflowTransformSelection>(&TransformSelection) ||
		Out->IsA<FDataflowTransformSelection>(&NewGeometryTransformSelection))
	{
		FDataflowTransformSelection InTransformSelection = GetValue<FDataflowTransformSelection>(Context, &TransformSelection);

		//
		// If not connected select everything by default
		//
		if (!IsConnected<FDataflowTransformSelection>(&TransformSelection))
		{
			const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

			GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
			const TArray<int32>& SelectionArr = TransformSelectionFacade.SelectAll();

			FDataflowTransformSelection NewTransformSelection;
			NewTransformSelection.Initialize(InCollection.NumElements(FGeometryCollection::TransformGroup), false);
			NewTransformSelection.SetFromArray(SelectionArr);

			InTransformSelection = NewTransformSelection;
		}

		if (InTransformSelection.AnySelected())
		{
			FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

			int32 ResultGeometryIndex = FFractureEngineFracturing::VoronoiFracture(InCollection,
				InTransformSelection,
				GetValue<TArray<FVector>>(Context, &Points),
				GetValue<FTransform>(Context, &Transform),
				0, // RandomSeed is not used in Voronoi fracture, it is used in the source point generation
				GetValue<float>(Context, &ChanceToFracture),
				SplitIslands,
				GetValue<float>(Context, &Grout),
				GetValue<float>(Context, &Amplitude),
				GetValue<float>(Context, &Frequency),
				GetValue<float>(Context, &Persistence),
				GetValue<float>(Context, &Lacunarity),
				GetValue<int32>(Context, &OctaveNumber),
				GetValue<float>(Context, &PointSpacing),
				AddSamplesForCollision,
				GetValue<float>(Context, &CollisionSampleSpacing));

			FDataflowTransformSelection NewSelection;
			FDataflowTransformSelection OriginalSelection;

			if (ResultGeometryIndex != INDEX_NONE)
			{
				if (InCollection.HasAttribute("TransformIndex", FGeometryCollection::GeometryGroup))
				{
					const TManagedArray<int32>& GeometryToTransformIndices = InCollection.GetAttribute<int32>("TransformIndex", FGeometryCollection::GeometryGroup);

					const int32 NumTransforms = InCollection.NumElements(FGeometryCollection::TransformGroup);
					NewSelection.Initialize(NumTransforms, false);
					OriginalSelection.Initialize(NumTransforms, false);

					// The newly fractured pieces are added to the end of the transform array (starting position is ResultGeometryIndex)
					for (int32 GeometryIdx = ResultGeometryIndex; GeometryIdx < GeometryToTransformIndices.Num(); ++GeometryIdx)
					{
						const int32 TransformIdx = GeometryToTransformIndices[GeometryIdx];
						NewSelection.SetSelected(TransformIdx);
					}

					for (int32 TransformIdx = 0; TransformIdx < InTransformSelection.Num(); ++TransformIdx)
					{
						if (InTransformSelection.IsSelected(TransformIdx))
						{
							OriginalSelection.SetSelected(TransformIdx);
						}
					}
				}
			}

			SetValue(Context, MoveTemp(InCollection), &Collection);
			SetValue(Context, OriginalSelection, &TransformSelection);
			SetValue(Context, NewSelection, &NewGeometryTransformSelection);

			return;
		}

		SafeForwardInput(Context, &Collection, &Collection);
		SetValue(Context, InTransformSelection, &TransformSelection);
		SetValue(Context, FDataflowTransformSelection(), &NewGeometryTransformSelection);
	}
}

void FPlaneCutterDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		const FDataflowTransformSelection& InTransformSelection = GetValue<FDataflowTransformSelection>(Context, &TransformSelection);

		if (IsConnected<FDataflowTransformSelection>(&TransformSelection))
		{
			if (InTransformSelection.AnySelected())
			{
				FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

				FFractureEngineFracturing::PlaneCutter(InCollection,
					InTransformSelection,
					GetValue<FBox>(Context, &BoundingBox),
					FTransform::Identity,
					NumPlanes,
					(int32)GetValue<float>(Context, &RandomSeed),
					1.f,
					true,
					GetValue<float>(Context, &Grout),
					GetValue<float>(Context, &Amplitude),
					GetValue<float>(Context, &Frequency),
					GetValue<float>(Context, &Persistence),
					GetValue<float>(Context, &Lacunarity),
					GetValue<int32>(Context, &OctaveNumber),
					GetValue<float>(Context, &PointSpacing),
					GetValue<bool>(Context, &AddSamplesForCollision),
					GetValue<float>(Context, &CollisionSampleSpacing));

				SetValue(Context, MoveTemp(InCollection), &Collection);

				return;
			}
		}

		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		SetValue(Context, InCollection, &Collection);
	}
}

void FPlaneCutterDataflowNode_v2::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection) ||
		Out->IsA<FDataflowTransformSelection>(&TransformSelection) ||
		Out->IsA<FDataflowTransformSelection>(&NewGeometryTransformSelection))
	{
		FDataflowTransformSelection InTransformSelection = GetValue<FDataflowTransformSelection>(Context, &TransformSelection);

		//
		// If not connected select everything by default
		//
		if (!IsConnected<FDataflowTransformSelection>(&TransformSelection))
		{
			const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

			GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
			const TArray<int32>& SelectionArr = TransformSelectionFacade.SelectAll();

			FDataflowTransformSelection NewTransformSelection;
			NewTransformSelection.Initialize(InCollection.NumElements(FGeometryCollection::TransformGroup), false);
			NewTransformSelection.SetFromArray(SelectionArr);

			InTransformSelection = NewTransformSelection;
		}

		if (InTransformSelection.AnySelected())
		{
			FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

			int32 ResultGeometryIndex = FFractureEngineFracturing::PlaneCutter(InCollection,
				InTransformSelection,
				GetValue<FBox>(Context, &BoundingBox),
				GetValue<FTransform>(Context, &Transform),
				NumPlanes,
				GetValue<int32>(Context, &RandomSeed),
				GetValue<float>(Context, &ChanceToFracture),
				SplitIslands,
				GetValue<float>(Context, &Grout),
				GetValue<float>(Context, &Amplitude),
				GetValue<float>(Context, &Frequency),
				GetValue<float>(Context, &Persistence),
				GetValue<float>(Context, &Lacunarity),
				GetValue<int32>(Context, &OctaveNumber),
				GetValue<float>(Context, &PointSpacing),
				AddSamplesForCollision,
				GetValue<float>(Context, &CollisionSampleSpacing));

			FDataflowTransformSelection NewSelection;
			FDataflowTransformSelection OriginalSelection;

			if (ResultGeometryIndex != INDEX_NONE)
			{
				if (InCollection.HasAttribute("TransformIndex", FGeometryCollection::GeometryGroup))
				{
					const TManagedArray<int32>& TransformIndices = InCollection.GetAttribute<int32>("TransformIndex", FGeometryCollection::GeometryGroup);

					NewSelection.Initialize(TransformIndices.Num(), false);
					OriginalSelection.Initialize(TransformIndices.Num(), false);

					// The newly fractured pieces are added to the end of the transform array (starting position is ResultGeometryIndex)
					for (int32 Idx = ResultGeometryIndex; Idx < TransformIndices.Num(); ++Idx)
					{
						int32 BoneIdx = TransformIndices[Idx];
						NewSelection.SetSelected(BoneIdx);
					}

					for (int32 Idx = 0; Idx < InTransformSelection.Num(); ++Idx)
					{
						if (InTransformSelection.IsSelected(Idx))
						{
							OriginalSelection.SetSelected(Idx);
						}
					}
				}
			}

			SetValue(Context, MoveTemp(InCollection), &Collection);
			SetValue(Context, OriginalSelection, &TransformSelection);
			SetValue(Context, NewSelection, &NewGeometryTransformSelection);

			return;
		}

		SafeForwardInput(Context, &Collection, &Collection);
		SetValue(Context, InTransformSelection, &TransformSelection);
		SetValue(Context, FDataflowTransformSelection(), &NewGeometryTransformSelection);
	}
}

void FExplodedViewDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		FFractureEngineFracturing::GenerateExplodedViewAttribute(InCollection, GetValue<FVector>(Context, &Scale), GetValue<float>(Context, &UniformScale));

		SetValue(Context, MoveTemp(InCollection), &Collection);
	}
}

void FSliceCutterDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection) ||
		Out->IsA<FDataflowTransformSelection>(&TransformSelection) ||
		Out->IsA<FDataflowTransformSelection>(&NewGeometryTransformSelection))
	{
		FDataflowTransformSelection InTransformSelection = GetValue<FDataflowTransformSelection>(Context, &TransformSelection);

		//
		// If not connected select everything by default
		//
		if (!IsConnected<FDataflowTransformSelection>(&TransformSelection))
		{
			const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

			GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
			const TArray<int32>& SelectionArr = TransformSelectionFacade.SelectAll();

			FDataflowTransformSelection NewTransformSelection;
			NewTransformSelection.Initialize(InCollection.NumElements(FGeometryCollection::TransformGroup), false);
			NewTransformSelection.SetFromArray(SelectionArr);

			InTransformSelection = NewTransformSelection;
		}

		if (InTransformSelection.AnySelected())
		{
			FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

			int32 ResultGeometryIndex = FFractureEngineFracturing::SliceCutter(InCollection,
				InTransformSelection,
				GetValue<FBox>(Context, &BoundingBox),
				GetValue<int32>(Context, &SlicesX),
				GetValue<int32>(Context, &SlicesY),
				GetValue<int32>(Context, &SlicesZ),
				GetValue<float>(Context, &SliceAngleVariation),
				GetValue<float>(Context, &SliceOffsetVariation),
				GetValue<int32>(Context, &RandomSeed),
				GetValue<float>(Context, &ChanceToFracture),
				SplitIslands,
				GetValue<float>(Context, &Grout),
				GetValue<float>(Context, &Amplitude),
				GetValue<float>(Context, &Frequency),
				GetValue<float>(Context, &Persistence),
				GetValue<float>(Context, &Lacunarity),
				GetValue<int32>(Context, &OctaveNumber),
				GetValue<float>(Context, &PointSpacing),
				AddSamplesForCollision,
				GetValue<float>(Context, &CollisionSampleSpacing));

			FDataflowTransformSelection NewSelection;
			FDataflowTransformSelection OriginalSelection;

			if (ResultGeometryIndex != INDEX_NONE)
			{
				if (InCollection.HasAttribute("TransformIndex", FGeometryCollection::GeometryGroup))
				{
					const TManagedArray<int32>& TransformIndices = InCollection.GetAttribute<int32>("TransformIndex", FGeometryCollection::GeometryGroup);

					NewSelection.Initialize(TransformIndices.Num(), false);
					OriginalSelection.Initialize(TransformIndices.Num(), false);

					// The newly fractured pieces are added to the end of the transform array (starting position is ResultGeometryIndex)
					for (int32 Idx = ResultGeometryIndex; Idx < TransformIndices.Num(); ++Idx)
					{
						int32 BoneIdx = TransformIndices[Idx];
						NewSelection.SetSelected(BoneIdx);
					}

					for (int32 Idx = 0; Idx < InTransformSelection.Num(); ++Idx)
					{
						if (InTransformSelection.IsSelected(Idx))
						{
							OriginalSelection.SetSelected(Idx);
						}
					}
				}
			}

			SetValue(Context, MoveTemp(InCollection), &Collection);
			SetValue(Context, OriginalSelection, &TransformSelection);
			SetValue(Context, NewSelection, &NewGeometryTransformSelection);

			return;
		}

		SafeForwardInput(Context, &Collection, &Collection);
		SetValue(Context, InTransformSelection, &TransformSelection);
		SetValue(Context, FDataflowTransformSelection(), &NewGeometryTransformSelection);
	}
}

void FBrickCutterDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection) ||
		Out->IsA<FDataflowTransformSelection>(&TransformSelection) ||
		Out->IsA<FDataflowTransformSelection>(&NewGeometryTransformSelection))
	{
		FDataflowTransformSelection InTransformSelection = GetValue<FDataflowTransformSelection>(Context, &TransformSelection);
		//
		// If not connected select everything by default
		//
		if (!IsConnected<FDataflowTransformSelection>(&TransformSelection))
		{
			const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

			GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
			const TArray<int32>& SelectionArr = TransformSelectionFacade.SelectAll();

			FDataflowTransformSelection NewTransformSelection;
			NewTransformSelection.Initialize(InCollection.NumElements(FGeometryCollection::TransformGroup), false);
			NewTransformSelection.SetFromArray(SelectionArr);

			InTransformSelection = NewTransformSelection;
		}

		FBox InBoundingBox = GetValue<FBox>(Context, &BoundingBox);
		//
		// If not connected set bounds to collection bounds
		//
		if (!IsConnected<FBox>(&BoundingBox))
		{
			const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

			GeometryCollection::Facades::FBoundsFacade BoundsFacade(InCollection);
			const FBox& BoundingBoxInCollectionSpace = BoundsFacade.GetBoundingBoxInCollectionSpace();

			InBoundingBox = BoundingBoxInCollectionSpace;
		}

		if (InTransformSelection.AnySelected())
		{
			FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

			int32 ResultGeometryIndex = FFractureEngineFracturing::BrickCutter(InCollection,
				InTransformSelection,
				InBoundingBox,
				GetValue<FTransform>(Context, &Transform),
				Bond,
				GetValue<float>(Context, &BrickLength),
				GetValue<float>(Context, &BrickHeight),
				GetValue<float>(Context, &BrickDepth),
				GetValue<int32>(Context, &RandomSeed),
				GetValue<float>(Context, &ChanceToFracture),
				SplitIslands,
				GetValue<float>(Context, &Grout),
				GetValue<float>(Context, &Amplitude),
				GetValue<float>(Context, &Frequency),
				GetValue<float>(Context, &Persistence),
				GetValue<float>(Context, &Lacunarity),
				GetValue<int32>(Context, &OctaveNumber),
				GetValue<float>(Context, &PointSpacing),
				AddSamplesForCollision,
				GetValue<float>(Context, &CollisionSampleSpacing));

			FDataflowTransformSelection NewSelection;
			FDataflowTransformSelection OriginalSelection;

			if (ResultGeometryIndex != INDEX_NONE)
			{
				if (InCollection.HasAttribute("TransformIndex", FGeometryCollection::GeometryGroup))
				{
					const TManagedArray<int32>& TransformIndices = InCollection.GetAttribute<int32>("TransformIndex", FGeometryCollection::GeometryGroup);

					NewSelection.Initialize(TransformIndices.Num(), false);
					OriginalSelection.Initialize(TransformIndices.Num(), false);

					// The newly fractured pieces are added to the end of the transform array (starting position is ResultGeometryIndex)
					for (int32 Idx = ResultGeometryIndex; Idx < TransformIndices.Num(); ++Idx)
					{
						int32 BoneIdx = TransformIndices[Idx];
						NewSelection.SetSelected(BoneIdx);
					}

					for (int32 Idx = 0; Idx < InTransformSelection.Num(); ++Idx)
					{
						if (InTransformSelection.IsSelected(Idx))
						{
							OriginalSelection.SetSelected(Idx);
						}
					}
				}
			}

			SetValue(Context, MoveTemp(InCollection), &Collection);
			SetValue(Context, OriginalSelection, &TransformSelection);
			SetValue(Context, NewSelection, &NewGeometryTransformSelection);

			return;
		}

		SafeForwardInput(Context, &Collection, &Collection);
		SetValue(Context, InTransformSelection, &TransformSelection);
		SetValue(Context, FDataflowTransformSelection(), &NewGeometryTransformSelection);
	}
}

void FMeshCutterDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Collection) ||
		Out->IsA(&TransformSelection) ||
		Out->IsA(&NewGeometryTransformSelection))
	{
#if WITH_EDITORONLY_DATA
		FDataflowTransformSelection InTransformSelection = GetValue(Context, &TransformSelection);
		//
		// If not connected select everything by default
		//
		if (!IsConnected(&TransformSelection))
		{
			const FManagedArrayCollection& InCollection = GetValue(Context, &Collection);

			GeometryCollection::Facades::FCollectionTransformSelectionFacade TransformSelectionFacade(InCollection);
			const TArray<int32>& SelectionArr = TransformSelectionFacade.SelectAll();

			FDataflowTransformSelection NewTransformSelection;
			NewTransformSelection.Initialize(InCollection.NumElements(FGeometryCollection::TransformGroup), false);
			NewTransformSelection.SetFromArray(SelectionArr);

			InTransformSelection = NewTransformSelection;
		}

		FBox InBoundingBox = GetValue(Context, &BoundingBox);
		//
		// If not connected set bounds to collection bounds
		//
		if (!IsConnected(&BoundingBox))
		{
			const FManagedArrayCollection& InCollection = GetValue(Context, &Collection);

			GeometryCollection::Facades::FBoundsFacade BoundsFacade(InCollection);
			const FBox& BoundingBoxInCollectionSpace = BoundsFacade.GetBoundingBoxInCollectionSpace();

			InBoundingBox = BoundingBoxInCollectionSpace;
		}

		if (InTransformSelection.AnySelected())
		{
			FManagedArrayCollection InCollection = GetValue(Context, &Collection);

			if (TObjectPtr<UStaticMesh> InCuttingMesh = GetValue(Context, &CuttingStaticMesh))
			{
				if (FMeshDescription* MeshDescription = bUseHiRes ? InCuttingMesh->GetHiResMeshDescription() : InCuttingMesh->GetMeshDescription(LODLevel))
				{
					// If HiRes is empty then use LoRes
					if (bUseHiRes && MeshDescription->Vertices().Num() == 0)
					{
						MeshDescription = InCuttingMesh->GetMeshDescription(LODLevel);
					}

					if (MeshDescription->Vertices().Num() > 0)
					{
						TObjectPtr<UDynamicMesh> NewMesh = NewObject<UDynamicMesh>();
						NewMesh->Reset();

						UE::Geometry::FDynamicMesh3& DynCuttingMesh = NewMesh->GetMeshRef();
						{
							FMeshDescriptionToDynamicMesh ConverterToDynamicMesh;
							ConverterToDynamicMesh.Convert(MeshDescription, DynCuttingMesh);
						}

						const int32 InRandomSeed = GetValue(Context, &RandomSeed);
						const int32 InNumberToScatter = GetValue(Context, &NumberToScatter);
						const int32 InGridX = GetValue(Context, &GridX);
						const int32 InGridY = GetValue(Context, &GridY);
						const int32 InGridZ = GetValue(Context, &GridZ);
						const float InVariability = GetValue(Context, &Variability);
						const float InMinScaleFactor = GetValue(Context, &MinScaleFactor);
						const float InMaxScaleFactor = GetValue(Context, &MaxScaleFactor);
						const float InRollRange = GetValue(Context, &RollRange);
						const float InPitchRange = GetValue(Context, &PitchRange);
						const float InYawRange = GetValue(Context, &YawRange);
						const FTransform InTransform = GetValue(Context, &Transform);
						const float InChanceToFracture = GetValue(Context, &ChanceToFracture);
						const float InCollisionSampleSpacing = GetValue(Context, &CollisionSampleSpacing);

						TArray<FTransform> MeshTransforms;

						if (CutDistribution == EMeshCutterCutDistribution::SingleCut)
						{
							MeshTransforms.Add(InTransform);
						}
						else
						{
							FFractureEngineFracturing::GenerateMeshTransforms(MeshTransforms,
								InBoundingBox,
								InRandomSeed,
								CutDistribution,
								InNumberToScatter,
								InGridX,
								InGridY,
								InGridZ,
								InVariability,
								InMinScaleFactor,
								InMaxScaleFactor,
								bRandomOrientation,
								InRollRange,
								InPitchRange,
								InYawRange);
						}

						int32 ResultGeometryIndex = FFractureEngineFracturing::MeshCutter(MeshTransforms,
							InCollection,
							InTransformSelection,
							DynCuttingMesh,
							InTransform,
							InRandomSeed,
							InChanceToFracture,
							SplitIslands,
							InCollisionSampleSpacing);

						FDataflowTransformSelection NewSelection;
						FDataflowTransformSelection OriginalSelection;

						if (ResultGeometryIndex != INDEX_NONE)
						{
							if (InCollection.HasAttribute("TransformIndex", FGeometryCollection::GeometryGroup))
							{
								const TManagedArray<int32>& TransformIndices = InCollection.GetAttribute<int32>("TransformIndex", FGeometryCollection::GeometryGroup);

								NewSelection.Initialize(TransformIndices.Num(), false);
								OriginalSelection.Initialize(TransformIndices.Num(), false);

								// The newly fractured pieces are added to the end of the transform array (starting position is ResultGeometryIndex)
								for (int32 Idx = ResultGeometryIndex; Idx < TransformIndices.Num(); ++Idx)
								{
									int32 BoneIdx = TransformIndices[Idx];
									NewSelection.SetSelected(BoneIdx);
								}

								for (int32 Idx = 0; Idx < InTransformSelection.Num(); ++Idx)
								{
									if (InTransformSelection.IsSelected(Idx))
									{
										OriginalSelection.SetSelected(Idx);
									}
								}
							}
						}

						SetValue(Context, MoveTemp(InCollection), &Collection);
						SetValue(Context, OriginalSelection, &TransformSelection);
						SetValue(Context, NewSelection, &NewGeometryTransformSelection);

						return;
					}
				}
			}
		}

		SafeForwardInput(Context, &Collection, &Collection);
		SetValue(Context, InTransformSelection, &TransformSelection);
		SetValue(Context, FDataflowTransformSelection(), &NewGeometryTransformSelection);
#else
		ensureMsgf(false, TEXT("FMeshCutterDataflowNode is an editor only node."));
#endif
	}
}




