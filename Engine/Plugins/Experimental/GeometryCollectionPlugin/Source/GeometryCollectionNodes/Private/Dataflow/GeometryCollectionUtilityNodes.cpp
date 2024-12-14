// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/GeometryCollectionUtilityNodes.h"
#include "Dataflow/DataflowCore.h"

#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionConvexUtility.h"
#include "GeometryCollection/Facades/CollectionHierarchyFacade.h"
#include "GeometryCollection/Facades/CollectionTransformSelectionFacade.h"

#include "Operations/MeshSelfUnion.h"
#include "MeshQueries.h"

#include "MeshSimplification.h"


//#include UE_INLINE_GENERATED_CPP_BY_NAME(GeometryCollectionUtilityNodes)

namespace UE::Dataflow
{

	void GeometryCollectionUtilityNodes()
	{
		static const FLinearColor CDefaultNodeBodyTintColor = FLinearColor(0.f, 0.f, 0.f, 0.5f);

		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMakeDataflowConvexDecompositionSettingsNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCreateLeafConvexHullsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FSimplifyConvexHullsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCreateNonOverlappingConvexHullsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGenerateClusterConvexHullsFromLeafHullsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGenerateClusterConvexHullsFromChildrenHullsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FClearConvexHullsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMergeConvexHullsDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FUpdateVolumeAttributesDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FGetConvexHullVolumeDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FFixTinyGeoDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FRecomputeNormalsInGeometryCollectionDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FResampleGeometryCollectionDataflowNode);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FValidateGeometryCollectionDataflowNode);
	}
}

FMakeDataflowConvexDecompositionSettingsNode::FMakeDataflowConvexDecompositionSettingsNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&MinSizeToDecompose);
	RegisterInputConnection(&MaxGeoToHullVolumeRatioToDecompose);
	RegisterInputConnection(&ErrorTolerance);
	RegisterInputConnection(&MaxHullsPerGeometry);
	RegisterInputConnection(&MinThicknessTolerance);
	RegisterInputConnection(&NumAdditionalSplits);
	RegisterOutputConnection(&DecompositionSettings);
}

void FMakeDataflowConvexDecompositionSettingsNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&DecompositionSettings))
	{
		FDataflowConvexDecompositionSettings OutSettings;
		OutSettings.MinSizeToDecompose = GetValue(Context, &MinSizeToDecompose);
		OutSettings.MaxGeoToHullVolumeRatioToDecompose = GetValue(Context, &MaxGeoToHullVolumeRatioToDecompose);
		OutSettings.ErrorTolerance = GetValue(Context, &ErrorTolerance);
		OutSettings.MaxHullsPerGeometry = GetValue(Context, &MaxHullsPerGeometry);
		OutSettings.MinThicknessTolerance = GetValue(Context, &MinThicknessTolerance);
		OutSettings.NumAdditionalSplits = GetValue(Context, &NumAdditionalSplits);
		SetValue(Context, OutSettings, &DecompositionSettings);
	}
}

FCreateLeafConvexHullsDataflowNode::FCreateLeafConvexHullsDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Collection);
	RegisterInputConnection(&OptionalSelectionFilter);
	RegisterInputConnection(&SimplificationDistanceThreshold);
	RegisterInputConnection(&ConvexDecompositionSettings);
	RegisterOutputConnection(&Collection);
}

void FCreateLeafConvexHullsDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Collection))
	{
		const FManagedArrayCollection& InCollection = GetValue(Context, &Collection);
		if (InCollection.NumElements(FGeometryCollection::TransformGroup) == 0)
		{
			SetValue(Context, InCollection, &Collection);
			return;
		}

		if (TUniquePtr<FGeometryCollection> GeomCollection = TUniquePtr<FGeometryCollection>(InCollection.NewCopy<FGeometryCollection>()))
		{
			TArray<int32> SelectedBones;
			bool bRestrictToSelection = false;
			if (IsConnected(&OptionalSelectionFilter))
			{
				const FDataflowTransformSelection& InOptionalSelectionFilter = GetValue<FDataflowTransformSelection>(Context, &OptionalSelectionFilter);
				bRestrictToSelection = true;
				SelectedBones = InOptionalSelectionFilter.AsArray();
				GeometryCollection::Facades::FCollectionTransformSelectionFacade SelectionFacade(InCollection);
				SelectionFacade.Sanitize(SelectedBones, /* bFavorParent */false);
			}

			float InSimplificationDistanceThreshold = GetValue(Context, &SimplificationDistanceThreshold);

			FGeometryCollectionConvexUtility::FLeafConvexHullSettings LeafSettings(InSimplificationDistanceThreshold, GenerateMethod);
			LeafSettings.IntersectFilters.OnlyIntersectIfComputedIsSmallerFactor = IntersectIfComputedIsSmallerByFactor;
			LeafSettings.IntersectFilters.MinExternalVolumeToIntersect = MinExternalVolumeToIntersect;
			FDataflowConvexDecompositionSettings InDecompSettings = GetValue(Context, &ConvexDecompositionSettings);
			LeafSettings.DecompositionSettings.MaxGeoToHullVolumeRatioToDecompose = InDecompSettings.MaxGeoToHullVolumeRatioToDecompose;
			LeafSettings.DecompositionSettings.MinGeoVolumeToDecompose = InDecompSettings.MinSizeToDecompose * InDecompSettings.MinSizeToDecompose * InDecompSettings.MinSizeToDecompose;
			LeafSettings.DecompositionSettings.ErrorTolerance = InDecompSettings.ErrorTolerance;
			LeafSettings.DecompositionSettings.MaxHullsPerGeometry = InDecompSettings.MaxHullsPerGeometry;
			LeafSettings.DecompositionSettings.MinThicknessTolerance = InDecompSettings.MinThicknessTolerance;
			LeafSettings.DecompositionSettings.NumAdditionalSplits = InDecompSettings.NumAdditionalSplits;
			LeafSettings.bComputeIntersectionsBeforeHull = bComputeIntersectionsBeforeHull;
			FGeometryCollectionConvexUtility::GenerateLeafConvexHulls(*GeomCollection, bRestrictToSelection, SelectedBones, LeafSettings);
			SetValue(Context, (const FManagedArrayCollection&)(*GeomCollection), &Collection);
		}
	}
}

FSimplifyConvexHullsDataflowNode::FSimplifyConvexHullsDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Collection);
	RegisterInputConnection(&OptionalSelectionFilter);
	RegisterInputConnection(&SimplificationAngleThreshold);
	RegisterInputConnection(&SimplificationDistanceThreshold);
	RegisterInputConnection(&MinTargetTriangleCount);
	RegisterOutputConnection(&Collection);
}

void FSimplifyConvexHullsDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Collection) && IsConnected(&Collection))
	{
		FManagedArrayCollection InCollection = GetValue(Context, &Collection);
		if (InCollection.NumElements(FGeometryCollection::TransformGroup) == 0)
		{
			SetValue(Context, MoveTemp(InCollection), &Collection);
			return;
		}

		TArray<int32> SelectedBones;
		bool bRestrictToSelection = false;
		if (IsConnected(&OptionalSelectionFilter))
		{
			const FDataflowTransformSelection& InOptionalSelectionFilter = GetValue(Context, &OptionalSelectionFilter);
			bRestrictToSelection = true;
			SelectedBones = InOptionalSelectionFilter.AsArray();
		}

		UE::FractureEngine::Convex::FSimplifyHullSettings Settings;
		Settings.SimplifyMethod = SimplifyMethod;
		Settings.ErrorTolerance = GetValue(Context, &SimplificationDistanceThreshold);
		Settings.AngleThreshold = GetValue(Context, &SimplificationAngleThreshold);
		Settings.bUseGeometricTolerance = true;
		Settings.bUseTargetTriangleCount = true;
		Settings.bUseExistingVertexPositions = bUseExistingVertices;
		Settings.TargetTriangleCount = GetValue(Context, &MinTargetTriangleCount);
		UE::FractureEngine::Convex::SimplifyConvexHulls(InCollection, Settings, bRestrictToSelection, SelectedBones);
		SetValue(Context, MoveTemp(InCollection), &Collection);
	}
}

FCreateNonOverlappingConvexHullsDataflowNode::FCreateNonOverlappingConvexHullsDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Collection);
	RegisterInputConnection(&CanRemoveFraction);
	RegisterInputConnection(&SimplificationDistanceThreshold);
	RegisterInputConnection(&CanExceedFraction);
	RegisterInputConnection(&OverlapRemovalShrinkPercent);
	RegisterOutputConnection(&Collection);
}

void FCreateNonOverlappingConvexHullsDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA<FManagedArrayCollection>(&Collection) && IsConnected(&Collection))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);

		if (TUniquePtr<FGeometryCollection> GeomCollection = TUniquePtr<FGeometryCollection>(InCollection.NewCopy<FGeometryCollection>()))
		{
			float InCanRemoveFraction = GetValue<float>(Context, &CanRemoveFraction);
			float InCanExceedFraction = GetValue<float>(Context, &CanExceedFraction);
			float InSimplificationDistanceThreshold = GetValue<float>(Context, &SimplificationDistanceThreshold);
			float InOverlapRemovalShrinkPercent = GetValue<float>(Context, &OverlapRemovalShrinkPercent);

			FGeometryCollectionConvexUtility::FGeometryCollectionConvexData ConvexData = FGeometryCollectionConvexUtility::CreateNonOverlappingConvexHullData(GeomCollection.Get(), 
				InCanRemoveFraction, 
				InSimplificationDistanceThreshold, 
				InCanExceedFraction,
				(EConvexOverlapRemoval)OverlapRemovalMethod,
				InOverlapRemovalShrinkPercent);

			SetValue<const FManagedArrayCollection&>(Context, *GeomCollection, &Collection);
		}
	}
}

// local helper to convert the dataflow enum
static UE::Geometry::FNegativeSpaceSampleSettings::ESampleMethod ConvertNegativeSpaceSampleMethodDataflowEnum(ENegativeSpaceSampleMethodDataflowEnum SampleMethod)
{
	switch (SampleMethod)
	{
	case ENegativeSpaceSampleMethodDataflowEnum::Uniform:
		return UE::Geometry::FNegativeSpaceSampleSettings::ESampleMethod::Uniform;
	case ENegativeSpaceSampleMethodDataflowEnum::VoxelSearch:
		return UE::Geometry::FNegativeSpaceSampleSettings::ESampleMethod::VoxelSearch;
	}
	return UE::Geometry::FNegativeSpaceSampleSettings::ESampleMethod::Uniform;
}

FGenerateClusterConvexHullsFromLeafHullsDataflowNode::FGenerateClusterConvexHullsFromLeafHullsDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Collection);
	RegisterInputConnection(&ConvexCount);
	RegisterInputConnection(&ErrorTolerance);
	RegisterInputConnection(&OptionalSelectionFilter);
	RegisterInputConnection(&bProtectNegativeSpace);
	RegisterInputConnection(&TargetNumSamples);
	RegisterInputConnection(&MinSampleSpacing);
	RegisterInputConnection(&NegativeSpaceTolerance);
	RegisterInputConnection(&MinRadius);

	RegisterOutputConnection(&Collection);
	RegisterOutputConnection(&SphereCovering);
}

void FGenerateClusterConvexHullsFromLeafHullsDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Collection) || Out->IsA(&SphereCovering))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		FDataflowSphereCovering Spheres;

		if (TUniquePtr<FGeometryCollection> GeomCollection = TUniquePtr<FGeometryCollection>(InCollection.NewCopy<FGeometryCollection>()))
		{
			TArray<int32> SelectionArray;
			bool bHasSelectionFilter = IsConnected(&OptionalSelectionFilter);
			if (bHasSelectionFilter)
			{
				const FDataflowTransformSelection& InOptionalSelectionFilter = GetValue<FDataflowTransformSelection>(Context, &OptionalSelectionFilter);
				SelectionArray = InOptionalSelectionFilter.AsArray();
				GeometryCollection::Facades::FCollectionTransformSelectionFacade SelectionFacade(InCollection);
				SelectionFacade.Sanitize(SelectionArray, /* bFavorParent */false);
			}

			bool bHasNegativeSpace = false;
			UE::Geometry::FSphereCovering NegativeSpace;
			if (GetValue(Context, &bProtectNegativeSpace))
			{
				UE::Geometry::FNegativeSpaceSampleSettings NegativeSpaceSettings;
				NegativeSpaceSettings.TargetNumSamples = GetValue(Context, &TargetNumSamples);
				NegativeSpaceSettings.MinRadius = GetValue(Context, &MinRadius);
				NegativeSpaceSettings.ReduceRadiusMargin = GetValue(Context, &NegativeSpaceTolerance);
				NegativeSpaceSettings.MinSpacing = GetValue(Context, &MinSampleSpacing);
				NegativeSpaceSettings.SampleMethod = ConvertNegativeSpaceSampleMethodDataflowEnum(SampleMethod);
				NegativeSpaceSettings.bRequireSearchSampleCoverage = bRequireSearchSampleCoverage;
				NegativeSpaceSettings.bOnlyConnectedToHull = bOnlyConnectedToHull;
				NegativeSpaceSettings.Sanitize();
				bHasNegativeSpace = UE::FractureEngine::Convex::ComputeConvexHullsNegativeSpace(*GeomCollection, NegativeSpace, NegativeSpaceSettings, bHasSelectionFilter, SelectionArray);
			}

			const int32 InConvexCount = GetValue(Context, &ConvexCount);
			const double InErrorToleranceInCm = GetValue(Context, &ErrorTolerance);
			FGeometryCollectionConvexUtility::FClusterConvexHullSettings HullMergeSettings(InConvexCount, InErrorToleranceInCm, bPreferExternalCollisionShapes);
			HullMergeSettings.AllowMergesMethod = AllowMerges;
			HullMergeSettings.EmptySpace = bHasNegativeSpace ? &NegativeSpace : nullptr;

			if (bHasSelectionFilter)
			{
				FGeometryCollectionConvexUtility::GenerateClusterConvexHullsFromLeafHulls(
					*GeomCollection,
					HullMergeSettings,
					SelectionArray
				);
			}
			else
			{
				FGeometryCollectionConvexUtility::GenerateClusterConvexHullsFromLeafHulls(
					*GeomCollection,
					HullMergeSettings
				);
			}

			SetValue(Context, static_cast<const FManagedArrayCollection>(*GeomCollection), &Collection);
			// Move the negative space to the output container at the end to be sure it is no longer needed
			Spheres.Spheres = MoveTemp(NegativeSpace);
		}
		else
		{
			UE_LOG(LogChaos, Error, TEXT("Error: Input collection could not be converted to a valid Geometry Collection"));
			SetValue(Context, InCollection, &Collection);
		}

		SetValue(Context, MoveTemp(Spheres), &SphereCovering);
	}
}

FGenerateClusterConvexHullsFromChildrenHullsDataflowNode::FGenerateClusterConvexHullsFromChildrenHullsDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Collection);
	RegisterInputConnection(&ConvexCount);
	RegisterInputConnection(&ErrorTolerance);
	RegisterInputConnection(&OptionalSelectionFilter);
	RegisterInputConnection(&bProtectNegativeSpace);
	RegisterInputConnection(&TargetNumSamples);
	RegisterInputConnection(&MinSampleSpacing);
	RegisterInputConnection(&NegativeSpaceTolerance);
	RegisterInputConnection(&MinRadius);

	RegisterOutputConnection(&Collection);
	RegisterOutputConnection(&SphereCovering);
}

void FGenerateClusterConvexHullsFromChildrenHullsDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Collection) || Out->IsA(&SphereCovering))
	{
		const FManagedArrayCollection& InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		FDataflowSphereCovering Spheres;

		if (TUniquePtr<FGeometryCollection> GeomCollection = TUniquePtr<FGeometryCollection>(InCollection.NewCopy<FGeometryCollection>()))
		{
			TArray<int32> SelectionArray;
			bool bHasSelectionFilter = IsConnected(&OptionalSelectionFilter);
			if (bHasSelectionFilter)
			{
				const FDataflowTransformSelection& InOptionalSelectionFilter = GetValue<FDataflowTransformSelection>(Context, &OptionalSelectionFilter);
				SelectionArray = InOptionalSelectionFilter.AsArray();
				GeometryCollection::Facades::FCollectionTransformSelectionFacade SelectionFacade(InCollection);
				SelectionFacade.Sanitize(SelectionArray, /* bFavorParent */false);
			}

			bool bHasNegativeSpace = false;
			UE::Geometry::FSphereCovering NegativeSpace;
			if (GetValue(Context, &bProtectNegativeSpace))
			{
				UE::Geometry::FNegativeSpaceSampleSettings NegativeSpaceSettings;
				NegativeSpaceSettings.TargetNumSamples = GetValue(Context, &TargetNumSamples);
				NegativeSpaceSettings.MinRadius = GetValue(Context, &MinRadius);
				NegativeSpaceSettings.ReduceRadiusMargin = GetValue(Context, &NegativeSpaceTolerance);
				NegativeSpaceSettings.MinSpacing = GetValue(Context, &MinSampleSpacing);
				NegativeSpaceSettings.SampleMethod = ConvertNegativeSpaceSampleMethodDataflowEnum(SampleMethod);
				NegativeSpaceSettings.bRequireSearchSampleCoverage = bRequireSearchSampleCoverage;
				NegativeSpaceSettings.bOnlyConnectedToHull = bOnlyConnectedToHull;
				NegativeSpaceSettings.Sanitize();
				bHasNegativeSpace = UE::FractureEngine::Convex::ComputeConvexHullsNegativeSpace(*GeomCollection, NegativeSpace, NegativeSpaceSettings, bHasSelectionFilter, SelectionArray);
			}

			const int32 InConvexCount = GetValue(Context, &ConvexCount);
			const double InErrorToleranceInCm = GetValue(Context, &ErrorTolerance);
			FGeometryCollectionConvexUtility::FClusterConvexHullSettings HullMergeSettings(InConvexCount, InErrorToleranceInCm, bPreferExternalCollisionShapes);
			HullMergeSettings.AllowMergesMethod = EAllowConvexMergeMethod::Any; // Note: Only 'Any' is supported for this node currently
			HullMergeSettings.EmptySpace = bHasNegativeSpace ? &NegativeSpace : nullptr;

			if (bHasSelectionFilter)
			{
				FGeometryCollectionConvexUtility::GenerateClusterConvexHullsFromChildrenHulls(
					*GeomCollection,
					HullMergeSettings,
					SelectionArray
				);
			}
			else
			{
				FGeometryCollectionConvexUtility::GenerateClusterConvexHullsFromChildrenHulls(
					*GeomCollection,
					HullMergeSettings
				);
			}

			SetValue<const FManagedArrayCollection&>(Context, *GeomCollection, &Collection);
			// Move the negative space to the output container at the end to be sure it is no longer needed
			Spheres.Spheres = MoveTemp(NegativeSpace);
		}
		else
		{
			UE_LOG(LogChaos, Error, TEXT("Error: Input collection could not be converted to a valid Geometry Collection"));
			SetValue(Context, InCollection, &Collection);
		}
		
		SetValue(Context, MoveTemp(Spheres), &SphereCovering);
	}
}

FMergeConvexHullsDataflowNode::FMergeConvexHullsDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Collection);
	RegisterInputConnection(&MaxConvexCount);
	RegisterInputConnection(&ErrorTolerance);
	RegisterInputConnection(&OptionalSelectionFilter);
	RegisterInputConnection(&bProtectNegativeSpace);
	RegisterInputConnection(&TargetNumSamples);
	RegisterInputConnection(&MinSampleSpacing);
	RegisterInputConnection(&NegativeSpaceTolerance);
	RegisterInputConnection(&MinRadius);

	RegisterOutputConnection(&Collection);
	RegisterOutputConnection(&SphereCovering);
}

void FClearConvexHullsDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Collection))
	{
		FManagedArrayCollection InCollection = GetValue(Context, &Collection);

		if (!IsConnected(&Collection) || !FGeometryCollectionConvexUtility::HasConvexHullData(&InCollection))
		{
			SetValue(Context, MoveTemp(InCollection), &Collection);
			return;
		}

		GeometryCollection::Facades::FCollectionTransformSelectionFacade SelectionFacade(InCollection);

		TArray<int32> ToClear;
		if (IsConnected(&TransformSelection))
		{ 
			const FDataflowTransformSelection& InTransformSelection = GetValue<FDataflowTransformSelection>(Context, &TransformSelection);
			ToClear = InTransformSelection.AsArray();

			SelectionFacade.Sanitize(ToClear, /* bFavorParent */false);
		}
		else
		{
			ToClear = SelectionFacade.SelectAll();
		}

		FGeometryCollectionConvexUtility::RemoveConvexHulls(&InCollection, ToClear);
		SetValue(Context, MoveTemp(InCollection), &Collection);
	}
}

void FMergeConvexHullsDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Collection) || Out->IsA(&SphereCovering))
	{
		FManagedArrayCollection InCollection = GetValue<FManagedArrayCollection>(Context, &Collection);
		FDataflowSphereCovering Spheres;

		TArray<int32> SelectionArray;
		bool bHasSelectionFilter = IsConnected(&OptionalSelectionFilter);
		if (bHasSelectionFilter)
		{
			const FDataflowTransformSelection& InOptionalSelectionFilter = GetValue<FDataflowTransformSelection>(Context, &OptionalSelectionFilter);
			SelectionArray = InOptionalSelectionFilter.AsArray();
			GeometryCollection::Facades::FCollectionTransformSelectionFacade SelectionFacade(InCollection);
			SelectionFacade.Sanitize(SelectionArray, /* bFavorParent */false);
		}

		bool bHasPrecomputedNegativeSpace = false;
		UE::Geometry::FSphereCovering NegativeSpace;
		bool bInProtectNegativeSpace = GetValue(Context, &bProtectNegativeSpace);
		UE::Geometry::FNegativeSpaceSampleSettings NegativeSpaceSettings;
		if (bInProtectNegativeSpace)
		{
			NegativeSpaceSettings.TargetNumSamples = GetValue(Context, &TargetNumSamples);
			NegativeSpaceSettings.MinRadius = GetValue(Context, &MinRadius);
			NegativeSpaceSettings.ReduceRadiusMargin = GetValue(Context, &NegativeSpaceTolerance);
			NegativeSpaceSettings.MinSpacing = GetValue(Context, &MinSampleSpacing);
			NegativeSpaceSettings.SampleMethod = ConvertNegativeSpaceSampleMethodDataflowEnum(SampleMethod);
			NegativeSpaceSettings.bRequireSearchSampleCoverage = bRequireSearchSampleCoverage;
			NegativeSpaceSettings.bOnlyConnectedToHull = bOnlyConnectedToHull;
			NegativeSpaceSettings.Sanitize();
		}
		if (bInProtectNegativeSpace && !bComputeNegativeSpacePerBone)
		{
			bHasPrecomputedNegativeSpace = UE::FractureEngine::Convex::ComputeConvexHullsNegativeSpace(InCollection, NegativeSpace, NegativeSpaceSettings, bHasSelectionFilter, SelectionArray, false);
		}

		const int32 InMaxConvexCount = GetValue(Context, &MaxConvexCount);
		const double InErrorToleranceInCm = GetValue(Context, &ErrorTolerance);
		FGeometryCollectionConvexUtility::FMergeConvexHullSettings HullMergeSettings;
		HullMergeSettings.EmptySpace = bHasPrecomputedNegativeSpace ? &NegativeSpace : nullptr;
		HullMergeSettings.ErrorToleranceInCm = InErrorToleranceInCm;
		HullMergeSettings.MaxConvexCount = InMaxConvexCount;
		HullMergeSettings.ComputeEmptySpacePerBoneSettings = (bInProtectNegativeSpace && bComputeNegativeSpacePerBone) ? &NegativeSpaceSettings : nullptr;

		UE::Geometry::FSphereCovering UsedNegativeSpace;
		FGeometryCollectionConvexUtility::MergeHullsOnTransforms(InCollection, HullMergeSettings, bHasSelectionFilter, SelectionArray, &UsedNegativeSpace);

		SetValue(Context, MoveTemp(InCollection), &Collection);

		Spheres.Spheres = MoveTemp(UsedNegativeSpace);
		SetValue(Context, MoveTemp(Spheres), &SphereCovering);
	}
}


FUpdateVolumeAttributesDataflowNode::FUpdateVolumeAttributesDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Collection);
	RegisterOutputConnection(&Collection);
}

void FUpdateVolumeAttributesDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Collection))
	{
		FManagedArrayCollection InCollection = GetValue(Context, &Collection);
		if (InCollection.NumElements(FGeometryCollection::TransformGroup) > 0)
		{
			FGeometryCollectionConvexUtility::SetVolumeAttributes(&InCollection);
		}
		SetValue(Context, MoveTemp(InCollection), &Collection);
	}
}


FGetConvexHullVolumeDataflowNode::FGetConvexHullVolumeDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
{
	RegisterInputConnection(&Collection);
	RegisterInputConnection(&TransformSelection);
	RegisterOutputConnection(&Volume);
}

void FGetConvexHullVolumeDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Volume))
	{
		float VolumeSum = 0;

		if (!IsConnected(&Collection) || !IsConnected(&TransformSelection))
		{
			SetValue(Context, VolumeSum, &Volume);
			return;
		}

		const FManagedArrayCollection& InCollection = GetValue(Context, &Collection);
		const FDataflowTransformSelection& InSelection = GetValue(Context, &TransformSelection);

		if (!FGeometryCollectionConvexUtility::HasConvexHullData(&InCollection))
		{
			SetValue(Context, VolumeSum, &Volume);
			return;
		}

		const int32 NumTransforms = InCollection.NumElements(FGeometryCollection::TransformGroup);
		TArray<int32> SelectionToSum = InSelection.AsArray();
		GeometryCollection::Facades::FCollectionTransformSelectionFacade SelectionFacade(InCollection);
		SelectionFacade.Sanitize(SelectionToSum);
		if (NumTransforms == 0 || SelectionToSum.Num() == 0)
		{
			SetValue(Context, VolumeSum, &Volume);
			return;
		}

		const TManagedArray<TSet<int32>>& TransformToConvexIndices = InCollection.GetAttribute<TSet<int32>>("TransformToConvexIndices", FTransformCollection::TransformGroup);
		const TManagedArray<Chaos::FConvexPtr>& ConvexHulls = InCollection.GetAttribute<Chaos::FConvexPtr>(FGeometryCollection::ConvexHullAttribute, FGeometryCollection::ConvexGroup);

		Chaos::Facades::FCollectionHierarchyFacade HierarchyFacade(InCollection);

		auto IterateHulls = [this, &TransformToConvexIndices, &HierarchyFacade](TArray<int32>& SelectionToSum, TFunctionRef<void(int32)> ProcessFn)
		{
			while (!SelectionToSum.IsEmpty())
			{
				int32 TransformIdx = SelectionToSum.Pop(EAllowShrinking::No);
				if (!bSumChildrenForClustersWithoutHulls || !TransformToConvexIndices[TransformIdx].IsEmpty())
				{
					ProcessFn(TransformIdx);
				}
				else if (const TSet<int32>* Children = HierarchyFacade.FindChildren(TransformIdx))
				{
					SelectionToSum.Append(Children->Array());
				}
			}
		};

		if (!bVolumeOfUnion)
		{
			IterateHulls(SelectionToSum, [&VolumeSum, &ConvexHulls, &TransformToConvexIndices](int32 TransformIdx)
				{
					for (int32 ConvexIdx : TransformToConvexIndices[TransformIdx])
					{
						VolumeSum += ConvexHulls[ConvexIdx]->GetVolume();
					}
				});
		}
		else
		{
			TArray<int32> SelectedBones;
			SelectedBones.Reserve(SelectionToSum.Num());
			IterateHulls(SelectionToSum, [&SelectedBones](int32 TransformIdx)
				{
					SelectedBones.Add(TransformIdx);
				});
			UE::Geometry::FDynamicMesh3 Mesh;
			UE::FractureEngine::Convex::GetConvexHullsAsDynamicMesh(InCollection, Mesh, true, SelectedBones);
			UE::Geometry::FMeshSelfUnion Union(&Mesh);
			// Disable quality-related features, since we just want the volume
			Union.TryToImproveTriQualityThreshold = -1;
			Union.bWeldSharedEdges = false;
			Union.Compute();
			VolumeSum = UE::Geometry::TMeshQueries<UE::Geometry::FDynamicMesh3>::GetVolumeNonWatertight(Mesh);
		}
		
		SetValue(Context, VolumeSum, &Volume);
	}
}

void FFixTinyGeoDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Collection))
	{
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

		if (InTransformSelection.AnySelected())
		{
			FManagedArrayCollection InCollection = GetValue(Context, &Collection);

			FFractureEngineUtility::FixTinyGeo(InCollection,
				InTransformSelection,
				MergeType,
				bOnFractureLevel,
				SelectionMethod,
				MinVolumeCubeRoot,
				RelativeVolume,
				UseBoneSelection,
				bOnlyClusters,
				NeighborSelection,
				bOnlyToConnected,
				bOnlySameParent);

			SetValue(Context, MoveTemp(InCollection), &Collection);

			return;
		}

		SafeForwardInput(Context, &Collection, &Collection);
	}
}

void FRecomputeNormalsInGeometryCollectionDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Collection))
	{
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

		if (InTransformSelection.AnySelected())
		{
			FManagedArrayCollection InCollection = GetValue(Context, &Collection);

			FFractureEngineUtility::RecomputeNormalsInGeometryCollection(InCollection,
				InTransformSelection,
				bOnlyTangents,
				bRecomputeSharpEdges,
				SharpEdgeAngleThreshold,
				bOnlyInternalSurfaces);

			SetValue(Context, MoveTemp(InCollection), &Collection);

			return;
		}

		SafeForwardInput(Context, &Collection, &Collection);
	}
}

void FResampleGeometryCollectionDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Collection))
	{
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

		if (InTransformSelection.AnySelected())
		{
			FManagedArrayCollection InCollection = GetValue(Context, &Collection);

			FFractureEngineUtility::ResampleGeometryCollection(InCollection,
				InTransformSelection,
				GetValue(Context, &CollisionSampleSpacing));

			SetValue(Context, MoveTemp(InCollection), &Collection);

			return;
		}

		SafeForwardInput(Context, &Collection, &Collection);
	}
}

void FValidateGeometryCollectionDataflowNode::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if (Out->IsA(&Collection))
	{
		FManagedArrayCollection InCollection = GetValue(Context, &Collection);

		FFractureEngineUtility::ValidateGeometryCollection(InCollection,
			bRemoveUnreferencedGeometry,
			bRemoveClustersOfOne,
			bRemoveDanglingClusters);

		SetValue(Context, MoveTemp(InCollection), &Collection);
	}
}
