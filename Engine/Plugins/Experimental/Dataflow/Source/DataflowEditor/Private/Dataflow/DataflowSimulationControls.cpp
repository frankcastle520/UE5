// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowSimulationControls.h"

#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Chaos/CacheCollection.h"
#include "Chaos/CacheManagerActor.h"
#include "Chaos/Adapters/CacheAdapter.h"
#include "Dataflow/DataflowContent.h"
#include "Dataflow/DataflowObject.h"
#include "Features/IModularFeatures.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "BonePose.h"
#include "AnimationRuntime.h"

#define LOCTEXT_NAMESPACE "DataflowSimulationGenerator"

namespace UE::Dataflow
{
	bool ShouldResetWorld(const TObjectPtr<UDataflow>& SimulationGraph, const TObjectPtr<UWorld>& SimulationWorld, UE::Dataflow::FTimestamp& LastTimeStamp)
	{
		if(const TSharedPtr<UE::Dataflow::FGraph> DataflowGraph = SimulationGraph->GetDataflow())
		{
			UE::Dataflow::FTimestamp MaxTimeStamp = UE::Dataflow::FTimestamp::Invalid;
			for(const TSharedPtr<FDataflowNode>& TerminalNode : DataflowGraph->GetFilteredNodes(FDataflowTerminalNode::StaticType()))
			{
				MaxTimeStamp.Value = FMath::Max(MaxTimeStamp.Value, TerminalNode->GetTimestamp().Value);
			}
			if(MaxTimeStamp.Value > LastTimeStamp.Value)
			{
				LastTimeStamp = MaxTimeStamp.Value;
				return true;
			}
		}
		return false;
	}
	
	TObjectPtr<AActor> SpawnSimulatedActor(const TSubclassOf<AActor>& ActorClass,
		const TObjectPtr<AChaosCacheManager>& CacheManager, const TObjectPtr<UChaosCacheCollection>& CacheCollection,
		const bool bIsRecording, const TObjectPtr<UDataflowBaseContent>& DataflowContent, const FTransform& ActorTransform)
	{
		if(CacheManager)
		{
			const FString BaseName = CacheCollection ? CacheCollection->GetName() : TEXT("CacheActor");
			const uint32 CacheCollectionPathHash = CacheCollection ? GetTypeHash(CacheCollection->GetPathName()) : 0;
			const uint32 TerminalAssetPathHash = (DataflowContent && DataflowContent->GetTerminalAsset()) ? GetTypeHash(DataflowContent->GetTerminalAsset()->GetPathName()) : 0;
			const uint32 CacheActorHash = HashCombineFast(CacheCollectionPathHash, TerminalAssetPathHash);
			const FString CacheActorName = FString::Printf(TEXT("%s_%08X"), *BaseName, CacheActorHash);

			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Name = FName(CacheActorName);
			SpawnParameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
			SpawnParameters.Owner = CacheManager.Get(); 
			SpawnParameters.bDeferConstruction = true;
			
			TObjectPtr<AActor> PreviewActor = CacheManager->GetWorld()->SpawnActor<AActor>(ActorClass, SpawnParameters);
			if(PreviewActor)
			{
				// Link the editor content properties to the BP actor one 
				DataflowContent->SetActorProperties(PreviewActor);

				// Finish spawning
				PreviewActor->FinishSpawning(ActorTransform, true);
			}

			CacheManager->CacheCollection = CacheCollection;
			CacheManager->StartMode = EStartMode::Timed;
			CacheManager->CacheMode = bIsRecording ? ECacheMode::Record : ECacheMode::None;
			
			// Get the implementation of our adapters for identifying compatible components
			IModularFeatures&                      ModularFeatures = IModularFeatures::Get();
			TArray<Chaos::FComponentCacheAdapter*> Adapters = ModularFeatures.GetModularFeatureImplementations<Chaos::FComponentCacheAdapter>(Chaos::FComponentCacheAdapter::FeatureName);

			if(PreviewActor)
			{
				TInlineComponentArray<UPrimitiveComponent*> PrimComponents;
				PreviewActor->GetComponents(PrimComponents);
	
				for(UPrimitiveComponent* PrimComponent : PrimComponents)
				{
					if(Chaos::FAdapterUtil::GetBestAdapterForClass(PrimComponent->GetClass(), false))
					{
						const FName ChannelName(PrimComponent->GetName());
						CacheManager->FindOrAddObservedComponent(PrimComponent, ChannelName, true);
					}
				}
			}
			return PreviewActor;
		}
		return nullptr;
	}
	
	void SetupSkeletonAnimation(const TObjectPtr<AActor>& PreviewActor, const bool bSkeletalMeshVisibility)
	{
		if(PreviewActor)
		{
			TInlineComponentArray<UPrimitiveComponent*> PrimComponents;
			PreviewActor->GetComponents(PrimComponents);

			for(UPrimitiveComponent* PrimComponent : PrimComponents)
			{
				if(USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(PrimComponent))
				{
					SkeletalMeshComponent->SetVisibility(bSkeletalMeshVisibility);
					SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
					SkeletalMeshComponent->InitAnim(true);
					
					if(UAnimSingleNodeInstance* AnimNodeInstance = SkeletalMeshComponent->GetSingleNodeInstance())
					{
						// Setup the animation instance
						AnimNodeInstance->SetAnimationAsset(SkeletalMeshComponent->AnimationData.AnimToPlay);
						AnimNodeInstance->InitializeAnimation();

						// Update the anim data
						SkeletalMeshComponent->AnimationData.PopulateFrom(AnimNodeInstance);
#if WITH_EDITOR
						SkeletalMeshComponent->ValidateAnimation();
#endif

						// Stop the animation 
						AnimNodeInstance->SetLooping(true);
						AnimNodeInstance->SetPlaying(false);
						
					}
				}
			}
		}
	}

	static void FillAnimationDatas(const UAnimSequenceBase* AnimSequence, const float CurrentTime, USkeletalMesh* InSkeletalMesh, TArray<FTransform>& ComponentSpaceTransforms)
	{
		const FAnimExtractContext ExtractionContext(FMath::Clamp(CurrentTime, 0., AnimSequence->GetPlayLength()));

		if(const FReferenceSkeleton* ReferenceSkeleton = &InSkeletalMesh->GetRefSkeleton())
		{
			const int32 NumBones = ReferenceSkeleton->GetNum();

			TArray<FBoneIndexType> BoneIndices;
			BoneIndices.SetNumUninitialized(NumBones);
			for (int32 Index = 0; Index < NumBones; ++Index)
			{
				BoneIndices[Index] = StaticCast<FBoneIndexType>(Index);
			}

			FBoneContainer BoneContainer;
			BoneContainer.SetUseRAWData(true);
			BoneContainer.InitializeTo(BoneIndices, UE::Anim::FCurveFilterSettings(), *InSkeletalMesh);
		
			FCompactPose CompactPose;
			CompactPose.SetBoneContainer(&BoneContainer);

			FBlendedCurve BlendedCurve;
			BlendedCurve.InitFrom(BoneContainer);

			UE::Anim::FStackAttributeContainer TempAttributes;
			FAnimationPoseData AnimationPoseData(CompactPose, BlendedCurve, TempAttributes);
			AnimSequence->GetAnimationPose(AnimationPoseData, ExtractionContext);
		
			FAnimationRuntime::FillUpComponentSpaceTransforms(*ReferenceSkeleton, AnimationPoseData.GetPose().GetBones(), ComponentSpaceTransforms);
		}
	}

	void ComputeSkeletonAnimation(const TObjectPtr<AActor>& PreviewActor, const float SimulationTime)
	{
		if(PreviewActor)
		{
			TInlineComponentArray<UPrimitiveComponent*> PrimComponents;
			PreviewActor->GetComponents(PrimComponents);

			// Update all the animation time 
			for(UPrimitiveComponent* PrimComponent : PrimComponents)
			{
				if(USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(PrimComponent))
				{
					if(UAnimSingleNodeInstance* SingleNodeInstance = Cast<UAnimSingleNodeInstance>(SkeletalMeshComponent->GetAnimInstance()))
					{
						if(const UAnimSequenceBase* AnimSequence = Cast<UAnimSequenceBase>(SingleNodeInstance->GetAnimationAsset()))
						{
							TArray<FTransform> ComponentSpaceTransforms = SkeletalMeshComponent->GetComponentSpaceTransforms();
							FillAnimationDatas(AnimSequence, SimulationTime, SkeletalMeshComponent->GetSkeletalMeshAsset(), ComponentSpaceTransforms);
							
							SkeletalMeshComponent->GetEditableComponentSpaceTransforms() = ComponentSpaceTransforms;
							SkeletalMeshComponent->ApplyEditedComponentSpaceTransforms();
						}
					}
				}
			}
		}
	}

	void UpdateSkeletonAnimation(const TObjectPtr<AActor>& PreviewActor, const float SimulationTime)
	{
		if(PreviewActor)
		{
			TInlineComponentArray<UPrimitiveComponent*> PrimComponents;
			PreviewActor->GetComponents(PrimComponents);

			// Update all the animation time 
			for(UPrimitiveComponent* PrimComponent : PrimComponents)
			{
				if(USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(PrimComponent))
				{
					SkeletalMeshComponent->SetPosition(SimulationTime);
					SkeletalMeshComponent->TickAnimation(0.f, false /*bNeedsValidRootMotion*/);
					SkeletalMeshComponent->RefreshBoneTransforms(nullptr /*TickFunction*/);

					SkeletalMeshComponent->RefreshFollowerComponents();
					SkeletalMeshComponent->UpdateComponentToWorld();
					SkeletalMeshComponent->FinalizeBoneTransform();
					SkeletalMeshComponent->MarkRenderTransformDirty();
					SkeletalMeshComponent->MarkRenderDynamicDataDirty();
				}
			}
		}
	}

	void StartSkeletonAnimation(const TObjectPtr<AActor>& PreviewActor)
	{
		if(PreviewActor)
		{
			TInlineComponentArray<UPrimitiveComponent*> PrimComponents;
			PreviewActor->GetComponents(PrimComponents);

			for(UPrimitiveComponent* PrimComponent : PrimComponents)
			{
				if(const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(PrimComponent))
				{
					if(UAnimSingleNodeInstance* AnimNodeInstance = SkeletalMeshComponent->GetSingleNodeInstance())
					{
						AnimNodeInstance->SetPlaying(true);
					}
				}
			}
		}
	}
	
	void PauseSkeletonAnimation(const TObjectPtr<AActor>& PreviewActor)
	{
		if(PreviewActor)
		{
			TInlineComponentArray<UPrimitiveComponent*> PrimComponents;
			PreviewActor->GetComponents(PrimComponents);

			for(UPrimitiveComponent* PrimComponent : PrimComponents)
			{
				if(const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(PrimComponent))
				{
					if(UAnimSingleNodeInstance* AnimNodeInstance = SkeletalMeshComponent->GetSingleNodeInstance())
					{
						AnimNodeInstance->SetPlaying(false);
					}
				}
			}
		}
	}
	
	void StepSkeletonAnimation(const TObjectPtr<AActor>& PreviewActor)
	{
		if(PreviewActor)
		{
			TInlineComponentArray<UPrimitiveComponent*> PrimComponents;
			PreviewActor->GetComponents(PrimComponents);

			for(UPrimitiveComponent* PrimComponent : PrimComponents)
			{
				if(const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(PrimComponent))
				{
					if(UAnimSingleNodeInstance* AnimNodeInstance = SkeletalMeshComponent->GetSingleNodeInstance())
					{
						AnimNodeInstance->SetPlaying(false);
						AnimNodeInstance->StepForward();
					}
				}
			}
		}
	}
	
}

#undef LOCTEXT_NAMESPACE
