// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchDatabaseEditorReflection.h"
#include "StructUtils/InstancedStruct.h"
#include "PoseSearchDatabaseAssetTree.h"
#include "PoseSearchDatabaseAssetTreeNode.h"
#include "PoseSearchDatabaseViewModel.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchSchema.h"

#define LOCTEXT_NAMESPACE "UPoseSearchDatabaseReflection"

void UPoseSearchDatabaseReflectionBase::SetSourceLink(
	const TWeakPtr<UE::PoseSearch::FDatabaseAssetTreeNode>& InWeakAssetTreeNode,
	const TSharedPtr<UE::PoseSearch::SDatabaseAssetTree>& InAssetTreeWidget)
{
	WeakAssetTreeNode = InWeakAssetTreeNode;
	AssetTreeWidget = InAssetTreeWidget;
}

void UPoseSearchDatabaseReflectionBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Skip changes during EPropertyChangeType::Interactive since they are always followed by a PostEditChangeProperty() call
	// with EPropertyChangeType::ValueSet holding the final values.
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
	{
		return;
	}
	
	if (ApplyChanges())
	{
		const bool bShouldRefreshView = (PropertyChangedEvent.Property == nullptr) || !PropertyChangedEvent.Property->IsA(FFloatProperty::StaticClass());
		AssetTreeWidget->FinalizeTreeChanges(true, bShouldRefreshView);
	}
}

bool UPoseSearchDatabaseSequenceReflection::ApplyChanges() 
{
	const float PlayLength = Sequence.GetPlayLength();
	const FFloatInterval ClampedSamplingRange(FMath::Clamp(Sequence.SamplingRange.Min, 0.0f, PlayLength), FMath::Clamp(Sequence.SamplingRange.Max, 0.0f, PlayLength));
	if (ClampedSamplingRange != Sequence.SamplingRange)
	{
		Sequence.SamplingRange = ClampedSamplingRange;
	}

	if (const TSharedPtr<UE::PoseSearch::FDatabaseAssetTreeNode> AssetTreeNode = WeakAssetTreeNode.Pin())
	{
		if (const TSharedPtr<UE::PoseSearch::FDatabaseViewModel> ViewModel = AssetTreeNode->EditorViewModel.Pin())
		{
			UPoseSearchDatabase* Database = ViewModel->GetPoseSearchDatabase();
			if (IsValid(Database))
			{
				if (FPoseSearchDatabaseSequence* DatabaseSequence = Database->GetMutableDatabaseAnimationAsset<FPoseSearchDatabaseSequence>(AssetTreeNode->SourceAssetIdx))
				{
					if (*DatabaseSequence != Sequence)
					{
						*DatabaseSequence = Sequence;
						Database->MarkPackageDirty();
						return true;
					}
				}
			}
		}
	}

	return false;
}

bool UPoseSearchDatabaseBlendSpaceReflection::ApplyChanges()
{
	if (const TSharedPtr<UE::PoseSearch::FDatabaseAssetTreeNode> AssetTreeNode = WeakAssetTreeNode.Pin())
	{
		if (const TSharedPtr<UE::PoseSearch::FDatabaseViewModel> ViewModel = AssetTreeNode->EditorViewModel.Pin())
		{
			UPoseSearchDatabase* Database = ViewModel->GetPoseSearchDatabase();
			if (IsValid(Database))
			{
				if (FPoseSearchDatabaseBlendSpace* DatabaseBlendSpace = Database->GetMutableDatabaseAnimationAsset<FPoseSearchDatabaseBlendSpace>(AssetTreeNode->SourceAssetIdx))
				{
					if (*DatabaseBlendSpace != BlendSpace)
					{
						*DatabaseBlendSpace = BlendSpace;
						Database->MarkPackageDirty();
						return true;
					}
				}
			}
		}
	}

	return false;
}

bool UPoseSearchDatabaseAnimCompositeReflection::ApplyChanges()
{
	const float PlayLength = AnimComposite.GetPlayLength();
	const FFloatInterval ClampedSamplingRange(FMath::Clamp(AnimComposite.SamplingRange.Min, 0.0f, PlayLength), FMath::Clamp(AnimComposite.SamplingRange.Max, 0.0f, PlayLength));
	if (ClampedSamplingRange != AnimComposite.SamplingRange)
	{
		AnimComposite.SamplingRange = ClampedSamplingRange;
	}

	if (const TSharedPtr<UE::PoseSearch::FDatabaseAssetTreeNode> AssetTreeNode = WeakAssetTreeNode.Pin())
	{
		if (const TSharedPtr<UE::PoseSearch::FDatabaseViewModel> ViewModel = AssetTreeNode->EditorViewModel.Pin())
		{
			UPoseSearchDatabase* Database = ViewModel->GetPoseSearchDatabase();
			if (IsValid(Database))
			{
				if (FPoseSearchDatabaseAnimComposite* DatabaseAnimComposite = Database->GetMutableDatabaseAnimationAsset<FPoseSearchDatabaseAnimComposite>(AssetTreeNode->SourceAssetIdx))
				{
					if (*DatabaseAnimComposite != AnimComposite)
					{
						*DatabaseAnimComposite = AnimComposite;
						Database->MarkPackageDirty();
						return true;
					}
				}
			}
		}
	}

	return false;
}

bool UPoseSearchDatabaseAnimMontageReflection::ApplyChanges()
{
	const float PlayLength = AnimMontage.GetPlayLength();
	const FFloatInterval ClampedSamplingRange(FMath::Clamp(AnimMontage.SamplingRange.Min, 0.0f, PlayLength), FMath::Clamp(AnimMontage.SamplingRange.Max, 0.0f, PlayLength));
	if (ClampedSamplingRange != AnimMontage.SamplingRange)
	{
		AnimMontage.SamplingRange = ClampedSamplingRange;
	}

	if (const TSharedPtr<UE::PoseSearch::FDatabaseAssetTreeNode> AssetTreeNode = WeakAssetTreeNode.Pin())
	{
		if (const TSharedPtr<UE::PoseSearch::FDatabaseViewModel> ViewModel = AssetTreeNode->EditorViewModel.Pin())
		{
			UPoseSearchDatabase* Database = ViewModel->GetPoseSearchDatabase();
			if (IsValid(Database))
			{
				if (FPoseSearchDatabaseAnimMontage* DatabaseAnimMontage = Database->GetMutableDatabaseAnimationAsset<FPoseSearchDatabaseAnimMontage>(AssetTreeNode->SourceAssetIdx))
				{
					if (*DatabaseAnimMontage != AnimMontage)
					{
						*DatabaseAnimMontage = AnimMontage;
						Database->MarkPackageDirty();
						return true;
					}
				}
			}
		}
	}

	return false;
}

bool UPoseSearchDatabaseMultiAnimAssetReflection::ApplyChanges()
{
	const float PlayLength = MultiAnimAsset.GetPlayLength();
	const FFloatInterval ClampedSamplingRange(FMath::Clamp(MultiAnimAsset.SamplingRange.Min, 0.0f, PlayLength), FMath::Clamp(MultiAnimAsset.SamplingRange.Max, 0.0f, PlayLength));
	if (ClampedSamplingRange != MultiAnimAsset.SamplingRange)
	{
		MultiAnimAsset.SamplingRange = ClampedSamplingRange;
	}

	if (const TSharedPtr<UE::PoseSearch::FDatabaseAssetTreeNode> AssetTreeNode = WeakAssetTreeNode.Pin())
	{
		if (const TSharedPtr<UE::PoseSearch::FDatabaseViewModel> ViewModel = AssetTreeNode->EditorViewModel.Pin())
		{
			UPoseSearchDatabase* Database = ViewModel->GetPoseSearchDatabase();
			if (IsValid(Database))
			{
				if (FPoseSearchDatabaseMultiAnimAsset* DatabaseMultiAnimAsset = Database->GetMutableDatabaseAnimationAsset<FPoseSearchDatabaseMultiAnimAsset>(AssetTreeNode->SourceAssetIdx))
				{
					if (*DatabaseMultiAnimAsset != MultiAnimAsset)
					{
						*DatabaseMultiAnimAsset = MultiAnimAsset;
						Database->MarkPackageDirty();
						return true;
					}
				}
			}
		}
	}

	return false;
}

void UPoseSearchDatabaseStatistics::Initialize(const UPoseSearchDatabase* PoseSearchDatabase)
{
	static FText TimeFormat = LOCTEXT("TimeFormat", "{0} {0}|plural(one=Second,other=Seconds)");
	
	if (IsValid(PoseSearchDatabase))
	{
		const UE::PoseSearch::FSearchIndex& SearchIndex = PoseSearchDatabase->GetSearchIndex();
		// General Information
	
		AnimationSequences = PoseSearchDatabase->GetNumAnimationAssets();
			
		const int32 SampleRate = FMath::Max(1, PoseSearchDatabase->Schema->SampleRate);
		TotalAnimationPosesInFrames = SearchIndex.GetNumPoses();
		TotalAnimationPosesInTime = FText::Format(TimeFormat, static_cast<double>(TotalAnimationPosesInFrames) / SampleRate);
			
		uint32 NumOfSearchablePoses = 0;
		for (const UE::PoseSearch::FPoseMetadata& PoseMetadata : SearchIndex.PoseMetadata)
		{
			if (!PoseMetadata.IsBlockTransition())
			{
				++NumOfSearchablePoses;
			}
		}
		SearchableFrames = NumOfSearchablePoses;
		SearchableTime = FText::Format(TimeFormat, static_cast<double>(NumOfSearchablePoses) / SampleRate);

		SchemaCardinality = PoseSearchDatabase->Schema->SchemaCardinality;

		if (SchemaCardinality > 0)
		{
			const int32 TotalAnimationFeatureVectors = SearchIndex.GetNumValuesVectors(SchemaCardinality);
			PrunedFrames = TotalAnimationPosesInFrames - TotalAnimationFeatureVectors;
		}
		else
		{
			PrunedFrames = 0;
		}

		if (PoseSearchDatabase->GetNumberOfPrincipalComponents() > 0)
		{
			const int32 TotalAnimationPCAFeatureVectors = SearchIndex.GetNumPCAValuesVectors(PoseSearchDatabase->GetNumberOfPrincipalComponents());
			PrunedPCAFrames = TotalAnimationPosesInFrames - TotalAnimationPCAFeatureVectors;
		}
		else
		{
			PrunedPCAFrames = 0;
		}

		// Kinematic Information
	
		// using FText instead of meta = (ForceUnits = "cm/s") to keep properties consistent
		AverageSpeed = FText::Format(LOCTEXT("StatsAverageSpeed", "{0} cm/s"), SearchIndex.Stats.AverageSpeed);
		MaxSpeed = FText::Format(LOCTEXT("StatsMaxSpeed", "{0} cm/s"), SearchIndex.Stats.MaxSpeed);
		AverageAcceleration = FText::Format(LOCTEXT("StatsAverageAcceleration", "{0} cm/s²"), SearchIndex.Stats.AverageAcceleration);
		MaxAcceleration = FText::Format(LOCTEXT("StatsMaxAcceleration", "{0} cm/s²"), SearchIndex.Stats.MaxAcceleration);

		// Principal Component Analysis
			
		ExplainedVariance = SearchIndex.PCAExplainedVariance * 100.f;
			
		// Memory Information
			
		{
			uint32 SourceAnimAssetsSizeCookedEstimateInBytes = 0;
			TSet<const UObject*> Analyzed;
			const int32 NumAnimationAssets = PoseSearchDatabase->GetNumAnimationAssets();
			Analyzed.Reserve(NumAnimationAssets);
			for (int32 AnimationAssetIndex = 0; AnimationAssetIndex < NumAnimationAssets; ++AnimationAssetIndex)
			{
				if (const FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAssetBase = PoseSearchDatabase->GetDatabaseAnimationAsset<FPoseSearchDatabaseAnimationAssetBase>(AnimationAssetIndex))
				{
					bool bAlreadyAnalyzed = false;
					Analyzed.Add(DatabaseAnimationAssetBase->GetAnimationAsset(), &bAlreadyAnalyzed);
					if (!bAlreadyAnalyzed && DatabaseAnimationAssetBase->GetAnimationAsset())
					{
						SourceAnimAssetsSizeCookedEstimateInBytes += DatabaseAnimationAssetBase->GetApproxCookedSize();
					}
				}
			}

			const uint32 ValuesBytesSize = SearchIndex.Values.GetAllocatedSize();
			const uint32 PCAValuesBytesSize = SearchIndex.PCAValues.GetAllocatedSize();
			const uint32 KDTreeBytesSize = SearchIndex.KDTree.GetAllocatedSize();
			const uint32 VPTreeBytesSize = SearchIndex.VPTree.GetAllocatedSize();
			const uint32 ValuesVectorToPoseIndexesBytesSize = SearchIndex.ValuesVectorToPoseIndexes.GetAllocatedSize();
			const uint32 PCAValuesVectorToPoseIndexesBytesSize = SearchIndex.PCAValuesVectorToPoseIndexes.GetAllocatedSize();

			const uint32 PoseMetadataBytesSize = SearchIndex.PoseMetadata.GetAllocatedSize();
			const uint32 AssetsBytesSize = SearchIndex.Assets.GetAllocatedSize();
			const uint32 OtherBytesSize = SearchIndex.PCAProjectionMatrix.GetAllocatedSize() + SearchIndex.Mean.GetAllocatedSize() + SearchIndex.WeightsSqrt.GetAllocatedSize();
			const uint32 EstimatedDatabaseBytesSize = ValuesBytesSize + PCAValuesBytesSize + KDTreeBytesSize + VPTreeBytesSize + ValuesVectorToPoseIndexesBytesSize + PCAValuesVectorToPoseIndexesBytesSize + PoseMetadataBytesSize + AssetsBytesSize + OtherBytesSize + SourceAnimAssetsSizeCookedEstimateInBytes;
				
			ValuesSize = FText::AsMemory(ValuesBytesSize);
			PCAValuesSize = FText::AsMemory(PCAValuesBytesSize);
			KDTreeSize = FText::AsMemory(KDTreeBytesSize);
			VPTreeSize = FText::AsMemory(VPTreeBytesSize);
			PoseMetadataSize = FText::AsMemory(PoseMetadataBytesSize);
			AssetsSize = FText::AsMemory(AssetsBytesSize);
			EstimatedDatabaseSize = FText::AsMemory(EstimatedDatabaseBytesSize);
			SourceAnimAssetsSizeCookedEstimate = FText::AsMemory(SourceAnimAssetsSizeCookedEstimateInBytes);
		}
	}
}

#undef LOCTEXT_NAMESPACE
