// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchDatabaseViewModel.h"
#include "AnimPreviewInstance.h"
#include "Animation/AnimComposite.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Animation/MirrorDataTable.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "StructUtils/InstancedStruct.h"
#include "Modules/ModuleManager.h"
#include "PoseSearch/MultiAnimAsset.h"
#include "PoseSearch/PoseSearchAnimNotifies.h"
#include "PoseSearch/PoseSearchContext.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDefines.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchHistory.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchTrajectoryTypes.h"
#include "PoseSearchDatabaseAssetTreeNode.h"
#include "PoseSearchDatabaseDataDetails.h"
#include "PoseSearchDatabasePreviewScene.h"
#include "PoseSearchEditor.h"
#include "PropertyEditorModule.h"

namespace UE::PoseSearch
{
#if ENABLE_ANIM_DEBUG
static TAutoConsoleVariable<float> CVarDatabasePreviewDebugDrawSamplerSize(TEXT("a.DatabasePreview.DebugDrawSamplerSize"), 0.f, TEXT("Debug Draw Sampler Positions Size"));
static TAutoConsoleVariable<float> CVarDatabasePreviewDebugDrawSamplerTimeOffset(TEXT("a.DatabasePreview.DebugDrawSamplerTimeOffset"), 0.f, TEXT("Debug Draw Sampler Positions At Time Offset"));
static TAutoConsoleVariable<float> CVarDatabasePreviewDebugDrawSamplerRootAxisLength(TEXT("a.DatabasePreview.DebugDrawSamplerRootAxisLength"), 0.f, TEXT("Debug Draw Sampler Root Axis Length"));
#endif

// FDatabasePreviewActor
bool FDatabasePreviewActor::SpawnPreviewActor(UWorld* World, const UPoseSearchDatabase* PoseSearchDatabase, int32 IndexAssetIdx, const FRole& Role, const FTransform& SamplerRootTransformOrigin, int32 PoseIdxForTimeOffset)
{
	check(PoseSearchDatabase && PoseSearchDatabase->Schema);
	const FSearchIndex& SearchIndex = PoseSearchDatabase->GetSearchIndex();

	USkeleton* Skeleton = PoseSearchDatabase->Schema->GetSkeleton(Role);
	if (!Skeleton)
	{
		UE_LOG(LogPoseSearchEditor, Error, TEXT("Couldn't spawn preview Actor because of missing Role '%s' in Schema '%s'"), *Role.ToString(), *PoseSearchDatabase->Schema->GetName());
		return false;
	}

	const FSearchIndexAsset& IndexAsset = SearchIndex.Assets[IndexAssetIdx];

	const FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAsset = PoseSearchDatabase->GetDatabaseAnimationAsset<FPoseSearchDatabaseAnimationAssetBase>(IndexAsset.GetSourceAssetIdx());
	UAnimationAsset* PreviewAsset = Cast<UAnimationAsset>(DatabaseAnimationAsset->GetAnimationAssetForRole(Role));
	if (!PreviewAsset)
	{
		return false;
	}

	ActorRole = Role;
	IndexAssetIndex = IndexAssetIdx;
	CurrentPoseIndex = INDEX_NONE;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ActorPtr = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	ActorPtr->SetFlags(RF_Transient);

	UDebugSkelMeshComponent* Mesh = NewObject<UDebugSkelMeshComponent>(ActorPtr.Get());
	Mesh->RegisterComponentWithWorld(World);

	UAnimPreviewInstance* AnimInstance = NewObject<UAnimPreviewInstance>(Mesh);
	Mesh->PreviewInstance = AnimInstance;
	AnimInstance->InitializeAnimation();

	USkeletalMesh* DatabasePreviewMesh = PoseSearchDatabase->PreviewMesh;
	Mesh->SetSkeletalMesh(DatabasePreviewMesh ? DatabasePreviewMesh : Skeleton->GetPreviewMesh(true));
	Mesh->EnablePreview(true, PreviewAsset);
		
	AnimInstance->SetAnimationAsset(PreviewAsset, IndexAsset.IsLooping(), 0.f);
	AnimInstance->SetBlendSpacePosition(IndexAsset.GetBlendParameters());
		
	if (IndexAsset.IsMirrored())
	{
		const UMirrorDataTable* MirrorDataTable = PoseSearchDatabase->Schema->GetMirrorDataTable(Role);
		AnimInstance->SetMirrorDataTable(MirrorDataTable);
	}

	const FMirrorDataCache MirrorDataCache(AnimInstance->GetMirrorDataTable(), AnimInstance->GetRequiredBonesOnAnyThread());
	
	Sampler.Init(PreviewAsset, SamplerRootTransformOrigin, IndexAsset.GetBlendParameters());

	PlayTimeOffset = 0.f;
	if (PoseIdxForTimeOffset >= 0)
	{
		PlayTimeOffset = PoseSearchDatabase->GetRealAssetTime(PoseIdxForTimeOffset) - IndexAsset.GetFirstSampleTime(PoseSearchDatabase->Schema->SampleRate);

		if (DatabaseAnimationAsset->GetNumRoles() > 1)
		{
			// @todo: implement support for UMultiAnimAsset. the transform should be centered to the origin of the multi character animation!
		}
		else
		{
			// centering the Sampler RootTransformOrigin at PlayTimeOffset time, to be able to "align" multiple actors from different animation frames when selected by the pose search debugger
			FTransform NewSamplerRootTransformOrigin = MirrorDataCache.MirrorTransform(Sampler.ExtractRootTransform(0.f));
			NewSamplerRootTransformOrigin.SetToRelativeTransform(MirrorDataCache.MirrorTransform(Sampler.ExtractRootTransform(PlayTimeOffset)));
			Sampler.SetRootTransformOrigin(NewSamplerRootTransformOrigin);
		}
	}

	AnimInstance->PlayAnim(IndexAsset.IsLooping(), 0.f);
	if (!ActorPtr->GetRootComponent())
	{
		ActorPtr->SetRootComponent(Mesh);
	}

	AnimInstance->SetPlayRate(0.f);

	// initializing Trajectory and TrajectorySpeed
	const int NumPoses = IndexAsset.GetNumPoses();
	Trajectory.Samples.SetNumUninitialized(NumPoses);
	TrajectorySpeed.SetNumUninitialized(NumPoses);

	if (NumPoses > 1)
	{
		for (int32 Index = 0; Index < NumPoses; ++Index)
		{
			const int32 IndexAssetPoseIdx = Index + IndexAsset.GetFirstPoseIdx();
			const float IndexAssetPoseTime = IndexAsset.GetTimeFromPoseIndex(IndexAssetPoseIdx, PoseSearchDatabase->Schema->SampleRate);
			const FTransform IndexAssetPoseTransform = MirrorDataCache.MirrorTransform(Sampler.ExtractRootTransform(IndexAssetPoseTime));

			Trajectory.Samples[Index].SetTransform(IndexAssetPoseTransform);
			Trajectory.Samples[Index].AccumulatedSeconds = IndexAssetPoseTime;
		}

		for (int32 Index = 1; Index < NumPoses; ++Index)
		{
			const float DeltaAccumulatedSeconds = FMath::Max(UE_KINDA_SMALL_NUMBER, Trajectory.Samples[Index].AccumulatedSeconds - Trajectory.Samples[Index - 1].AccumulatedSeconds);
			const FVector& Start = Trajectory.Samples[Index - 1].Position;
			const FVector& End = Trajectory.Samples[Index].Position;
			TrajectorySpeed[Index] = (Start - End).Length() / DeltaAccumulatedSeconds;
		}
		TrajectorySpeed[0] = TrajectorySpeed[1];
	}

	UE_LOG(LogPoseSearchEditor, Log, TEXT("Spawned preview Actor: %s"), *GetNameSafe(ActorPtr.Get()));
	return true;
}

void FDatabasePreviewActor::UpdatePreviewActor(const UPoseSearchDatabase* PoseSearchDatabase, float PlayTime, bool bQuantizeAnimationToPoseData)
{
	check(PoseSearchDatabase);

	const FSearchIndex& SearchIndex = PoseSearchDatabase->GetSearchIndex();

	UAnimPreviewInstance* AnimInstance = GetAnimPreviewInstanceInternal();
	if (!AnimInstance || IndexAssetIndex >= SearchIndex.Assets.Num())
	{
		return;
	}

	const UAnimationAsset* PreviewAsset = AnimInstance->GetAnimationAsset();
	if (!PreviewAsset)
	{
		return;
	}

	CurrentTime = 0.f;
	const FSearchIndexAsset& IndexAsset = SearchIndex.Assets[IndexAssetIndex];
	float CurrentPlayTime = PlayTime + IndexAsset.GetFirstSampleTime(PoseSearchDatabase->Schema->SampleRate) + PlayTimeOffset;
	FAnimationRuntime::AdvanceTime(false, CurrentPlayTime, CurrentTime, IndexAsset.GetLastSampleTime(PoseSearchDatabase->Schema->SampleRate));
			 
	// time to pose index
	CurrentPoseIndex = PoseSearchDatabase->GetPoseIndexFromTime(CurrentTime, IndexAsset);

	QuantizedTime = CurrentPoseIndex >= 0 ? PoseSearchDatabase->GetRealAssetTime(CurrentPoseIndex) : CurrentTime;
	if (bQuantizeAnimationToPoseData)
	{
		CurrentTime = QuantizedTime;
	}

	// SetPosition is in [0..1] range for blendspaces
	AnimInstance->SetPosition(Sampler.ToNormalizedTime(CurrentTime));
	AnimInstance->SetPlayRate(0.f);
	AnimInstance->SetBlendSpacePosition(IndexAsset.GetBlendParameters());

	check(ActorPtr != nullptr);
	ActorPtr->SetActorTransform(Trajectory.GetSampleAtTime(CurrentTime).GetTransform());
}

void FDatabasePreviewActor::Destroy()
{
	if (ActorPtr != nullptr)
	{
		ActorPtr->Destroy();
	}
}

bool FDatabasePreviewActor::DrawPreviewActors(TConstArrayView<FDatabasePreviewActor> PreviewActors, const UPoseSearchDatabase* PoseSearchDatabase, bool bDisplayRootMotionSpeed, bool bDisplayBlockTransition, TConstArrayView<float> QueryVector)
{
	using namespace UE::PoseSearch;

	UWorld* CommonWorld = nullptr;
	int32 CommonCurrentPoseIndex = INDEX_NONE;
#if DO_CHECK
	int32 CommonIndexAssetIndex = INDEX_NONE;
#endif // DO_CHECK

	TArray<const USkinnedMeshComponent*, TInlineAllocator<PreallocatedRolesNum>> Meshes;
	FRoleToIndex RoleToIndex;
	TArray<FArchivedPoseHistory, TInlineAllocator<PreallocatedRolesNum>> ArchivedPoseHistories;
	TArray<const IPoseHistory*, TInlineAllocator<PreallocatedRolesNum>> PoseHistories;

	const int32 NumPreviewActors = PreviewActors.Num();
	if (NumPreviewActors > PreallocatedRolesNum)
	{
		// reserve the needed amount of memory for containers
		Meshes.Reserve(NumPreviewActors);
		RoleToIndex.Reserve(NumPreviewActors);
		ArchivedPoseHistories.Reserve(NumPreviewActors);
		PoseHistories.Reserve(NumPreviewActors);
	}

	for (const FDatabasePreviewActor& PreviewActor : PreviewActors)
	{
		const FSearchIndex& SearchIndex = PoseSearchDatabase->GetSearchIndex();

		// This condition happens when the database got reindexed and the new valid SearchIndex has different cardinality for assets or poses.
		// Since we didn't refresh the PreviewActor IndexAssetIndex nor CurrentPoseIndex, now the PreviewActor is invalid
		// @todo: we should refresh the PreviewActor and restore it's preview time etc
		if (!SearchIndex.Assets.IsValidIndex(PreviewActor.GetIndexAssetIndex()) ||
			!SearchIndex.IsValidPoseIndex(PreviewActor.GetCurrentPoseIndex()))
		{
			return false;
		}

		const UDebugSkelMeshComponent* Mesh = PreviewActor.GetDebugSkelMeshComponent();
		if (!Mesh)
		{
			return false;
		}

		if (!CommonWorld)
		{
			CommonWorld = Mesh->GetWorld();
		}
		else if (CommonWorld != Mesh->GetWorld())
		{
			return false;
		}

		// making sure PreviewActors are consistent with each other
		if (CommonCurrentPoseIndex == INDEX_NONE)
		{
			CommonCurrentPoseIndex = PreviewActor.GetCurrentPoseIndex();
		}
		else if (CommonCurrentPoseIndex != PreviewActor.GetCurrentPoseIndex())
		{
			checkNoEntry();
			return false;
		}

#if DO_CHECK
		if (CommonIndexAssetIndex == INDEX_NONE)
		{
			CommonIndexAssetIndex = PreviewActor.GetIndexAssetIndex();
		}
		else if (CommonIndexAssetIndex != PreviewActor.GetIndexAssetIndex())
		{
			checkNoEntry();
			return false;
		}
#endif // DO_CHECK

		RoleToIndex.Add(PreviewActor.ActorRole) = Meshes.Num();
		Meshes.Add(Mesh);

		FArchivedPoseHistory& ArchivedPoseHistory = ArchivedPoseHistories.AddDefaulted_GetRef();
		ArchivedPoseHistory.Trajectory = PreviewActor.Trajectory;

		check(PoseSearchDatabase && PoseSearchDatabase->Schema);
		if (const USkeleton* Skeleton = PoseSearchDatabase->Schema->GetSkeleton(PreviewActor.ActorRole))
		{
			// reconstructing ArchivedPoseHistory::BoneToTransformMap and ArchivedPoseHistory::Entries ONLY for the root bone.
			// @todo: add more bones if needed
			const TArray<FTransform>& RefBonePose = Skeleton->GetReferenceSkeleton().GetRefBonePose();
			const FTransform& RefRootBone = RefBonePose[RootBoneIndexType];

			ArchivedPoseHistory.BoneToTransformMap.Add(RootBoneIndexType) = RootBoneIndexType;
			FPoseHistoryEntry& PoseHistoryEntry = ArchivedPoseHistory.Entries.AddDefaulted_GetRef();
			// saving space for the root bone only
			PoseHistoryEntry.SetNum(1, true);
			PoseHistoryEntry.SetComponentSpaceTransform(RootBoneIndexType, RefRootBone);
		}

		for (FPoseSearchQueryTrajectorySample& TrajectorySample : ArchivedPoseHistory.Trajectory.Samples)
		{
			TrajectorySample.AccumulatedSeconds -= PreviewActor.QuantizedTime;
		}

		PoseHistories.Add(&ArchivedPoseHistory);
	}

	UE::PoseSearch::FDebugDrawParams DrawParams(Meshes, PoseHistories, RoleToIndex, PoseSearchDatabase);
	DrawParams.DrawFeatureVector(CommonCurrentPoseIndex);

	if (!QueryVector.IsEmpty())
	{
		DrawParams.DrawFeatureVector(QueryVector);
	}

	for (const FDatabasePreviewActor& PreviewActor : PreviewActors)
	{
		const UDebugSkelMeshComponent* Mesh = PreviewActor.GetDebugSkelMeshComponent();
		const FSearchIndex& SearchIndex = PoseSearchDatabase->GetSearchIndex();
		const FSearchIndexAsset& IndexAsset = SearchIndex.Assets[PreviewActor.GetIndexAssetIndex()];
		const int32 SamplesNum = PreviewActor.Trajectory.Samples.Num();

		if (bDisplayRootMotionSpeed)
		{
			// @todo: should we be using PreviewActor.Trajectory.DebugDrawTrajectory instead?
			// drawing PreviewActor.Trajectory
			
			if (SamplesNum > 1)
			{
				for (int32 Index = 0; Index < SamplesNum; ++Index)
				{
					const FVector& EndDown = PreviewActor.Trajectory.Samples[Index].Position;
					const FVector EndUp = EndDown + (PreviewActor.TrajectorySpeed[Index] * FVector::UpVector);

					DrawParams.DrawLine(EndDown, EndUp, FColor::Black);
					if (Index > 0)
					{
						const FColor RootMotionColor = Index % 2 == 0 ? FColor::Purple : FColor::Orange;
						const FVector& StartDown = PreviewActor.Trajectory.Samples[Index - 1].Position;
						const FVector StartUp = StartDown + (PreviewActor.TrajectorySpeed[Index - 1] * FVector::UpVector);
						DrawParams.DrawLine(StartDown, EndDown, RootMotionColor);
						DrawParams.DrawLine(StartUp, EndUp, RootMotionColor);
					}
				}
			}
		}

		if (bDisplayBlockTransition)
		{
			const int NumPoses = IndexAsset.GetNumPoses();
			if (NumPoses == SamplesNum)
			{
				for (int32 Index = 0; Index < SamplesNum; ++Index)
				{
					const int32 IndexAssetPoseIdx = Index + IndexAsset.GetFirstPoseIdx();
					if (SearchIndex.PoseMetadata[IndexAssetPoseIdx].IsBlockTransition())
					{
						DrawParams.DrawPoint(PreviewActor.Trajectory.Samples[Index].Position, FColor::Red);
					}
					else
					{
						DrawParams.DrawPoint(PreviewActor.Trajectory.Samples[Index].Position, FColor::Green);
					}
				}
			}
		}

#if ENABLE_ANIM_DEBUG
		const float DebugDrawSamplerSize = CVarDatabasePreviewDebugDrawSamplerSize.GetValueOnAnyThread();
		if (DebugDrawSamplerSize > UE_KINDA_SMALL_NUMBER)
		{
			const float DebugDrawSamplerTimeOffset = CVarDatabasePreviewDebugDrawSamplerTimeOffset.GetValueOnAnyThread();
			
			const int32 NumDrawPasses = FMath::IsNearlyZero(DebugDrawSamplerTimeOffset) ? 1 : 2;

			FMemMark Mark(FMemStack::Get());
			FCompactPose Pose;
			FCSPose<FCompactPose> ComponentSpacePose;

			const FMirrorDataCache MirrorDataCache(Mesh->PreviewInstance->GetMirrorDataTable(), Mesh->PreviewInstance->GetRequiredBonesOnAnyThread());

			for (int32 DrawPass = 0; DrawPass < NumDrawPasses; ++DrawPass)
			{
				// drawing the pose extracted from the Sampler to visually compare with the pose features and the mesh drawing
				Pose.SetBoneContainer(&PreviewActor.GetAnimPreviewInstance()->GetRequiredBonesOnAnyThread());

				const float SamplerTime = DrawPass ? PreviewActor.CurrentTime + DebugDrawSamplerTimeOffset : PreviewActor.CurrentTime;
				const FColor DebugColor = DrawPass ? FColor::Blue : FColor::Red;

				PreviewActor.Sampler.ExtractPose(SamplerTime, Pose);
				MirrorDataCache.MirrorPose(Pose);
				ComponentSpacePose.InitPose(MoveTemp(Pose));

				const FTransform RootTransform = MirrorDataCache.MirrorTransform(PreviewActor.Sampler.ExtractRootTransform(SamplerTime));
				const float DebugDrawSamplerRootAxisLength = CVarDatabasePreviewDebugDrawSamplerRootAxisLength.GetValueOnAnyThread();
				if (DebugDrawSamplerRootAxisLength > 0.f)
				{
					DrawParams.DrawLine(RootTransform.GetTranslation(), RootTransform.GetTranslation() + RootTransform.GetScaledAxis(EAxis::X) * DebugDrawSamplerRootAxisLength, FColor::Red);
					DrawParams.DrawLine(RootTransform.GetTranslation(), RootTransform.GetTranslation() + RootTransform.GetScaledAxis(EAxis::Y) * DebugDrawSamplerRootAxisLength, FColor::Green);
					DrawParams.DrawLine(RootTransform.GetTranslation(), RootTransform.GetTranslation() + RootTransform.GetScaledAxis(EAxis::Z) * DebugDrawSamplerRootAxisLength, FColor::Blue);
				}

				for (int32 BoneIndex = 0; BoneIndex < ComponentSpacePose.GetPose().GetNumBones(); ++BoneIndex)
				{
					const FTransform BoneWorldTransforms = ComponentSpacePose.GetComponentSpaceTransform(FCompactPoseBoneIndex(BoneIndex)) * RootTransform;
					DrawParams.DrawPoint(BoneWorldTransforms.GetTranslation(), DebugColor, DebugDrawSamplerSize);
				}
			}
		}
#endif // ENABLE_ANIM_DEBUG
	}
	return true;
}

const UDebugSkelMeshComponent* FDatabasePreviewActor::GetDebugSkelMeshComponent() const
{
	if (ActorPtr != nullptr)
	{
		return Cast<UDebugSkelMeshComponent>(ActorPtr->GetRootComponent());
	}
	return nullptr;
}

const UAnimPreviewInstance* FDatabasePreviewActor::GetAnimPreviewInstance() const
{
	if (const UDebugSkelMeshComponent* Mesh = GetDebugSkelMeshComponent())
	{
		return Mesh->PreviewInstance.Get();
	}
	return nullptr;
}

UAnimPreviewInstance* FDatabasePreviewActor::GetAnimPreviewInstanceInternal()
{
	if (ActorPtr != nullptr)
	{
		if (UDebugSkelMeshComponent* Mesh = Cast<UDebugSkelMeshComponent>(ActorPtr->GetRootComponent()))
		{
			return Mesh->PreviewInstance.Get();
		}
	}
	return nullptr;
}

// FDatabaseViewModel
void FDatabaseViewModel::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(PoseSearchDatabasePtr);
}

void FDatabaseViewModel::Initialize(UPoseSearchDatabase* InPoseSearchDatabase, const TSharedRef<FDatabasePreviewScene>& InPreviewScene, const TSharedRef<SDatabaseDataDetails>& InDatabaseDataDetails)
{
	PoseSearchDatabasePtr = InPoseSearchDatabase;
	PreviewScenePtr = InPreviewScene;
	DatabaseDataDetails = InDatabaseDataDetails;

	RemovePreviewActors();
}

void FDatabaseViewModel::BuildSearchIndex()
{
	using namespace UE::PoseSearch;
	FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(GetPoseSearchDatabase(), ERequestAsyncBuildFlag::NewRequest);
}

void FDatabaseViewModel::PreviewBackwardEnd()
{
	SetPlayTime(MinPreviewPlayLength, false);
}

void FDatabaseViewModel::PreviewBackwardStep()
{
	const float NewPlayTime = FMath::Clamp(PlayTime - StepDeltaTime, MinPreviewPlayLength, MaxPreviewPlayLength);
	SetPlayTime(NewPlayTime, false);
}

void FDatabaseViewModel::PreviewBackward()
{
	DeltaTimeMultiplier = -1.f;
}

void FDatabaseViewModel::PreviewPause()
{
	DeltaTimeMultiplier = 0.f;
}

void FDatabaseViewModel::PreviewForward()
{
	DeltaTimeMultiplier = 1.f;
}

void FDatabaseViewModel::PreviewForwardStep()
{
	const float NewPlayTime = FMath::Clamp(PlayTime + StepDeltaTime, MinPreviewPlayLength, MaxPreviewPlayLength);
	SetPlayTime(NewPlayTime, false);
}

void FDatabaseViewModel::PreviewForwardEnd()
{
	SetPlayTime(MaxPreviewPlayLength, false);
}

UWorld* FDatabaseViewModel::GetWorld()
{
	check(PreviewScenePtr.IsValid());
	return PreviewScenePtr.Pin()->GetWorld();
}

void FDatabaseViewModel::OnPreviewActorClassChanged()
{
	// todo: implement
}

void FDatabaseViewModel::Tick(float DeltaSeconds)
{
	if (!PreviewActors.IsEmpty())
	{
		const float DeltaPlayTime = DeltaSeconds * DeltaTimeMultiplier;

		const UPoseSearchDatabase* Database = GetPoseSearchDatabase();
		if (EAsyncBuildIndexResult::Success == FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database, ERequestAsyncBuildFlag::ContinueRequest))
		{
			if (!FMath::IsNearlyZero(DeltaPlayTime))
			{
				PlayTime += DeltaPlayTime;
				PlayTime = FMath::Clamp(PlayTime, MinPreviewPlayLength, MaxPreviewPlayLength);

				for (TArray<FDatabasePreviewActor>& PreviewActorGroup : PreviewActors)
				{
					for (FDatabasePreviewActor& PreviewActor : PreviewActorGroup)
					{
						PreviewActor.UpdatePreviewActor(Database, PlayTime, bQuantizeAnimationToPoseData);
					}
				}
			}

			bool bShouldDrawQueryVector = ShouldDrawQueryVector();
			for (TArray<FDatabasePreviewActor>& PreviewActorGroup : PreviewActors)
			{
				bShouldDrawQueryVector &= !FDatabasePreviewActor::DrawPreviewActors(PreviewActorGroup, Database, bDisplayRootMotionSpeed, bDisplayBlockTransition, bShouldDrawQueryVector ? GetQueryVector() : TConstArrayView<float>());
			}
		}
	}
}

void FDatabaseViewModel::RemovePreviewActors()
{
	PlayTime = 0.f;
	DeltaTimeMultiplier = 1.f;
	MaxPreviewPlayLength = 0.f;
	MinPreviewPlayLength = 0.f;
	bIsEditorSelection = true;
	bDrawQueryVector = false;

	for (TArray<FDatabasePreviewActor>& PreviewActorGroup : PreviewActors)
	{
		for (FDatabasePreviewActor& PreviewActor : PreviewActorGroup)
		{
			PreviewActor.Destroy();
		}
	}

	PreviewActors.Reset();
}

void FDatabaseViewModel::AddSequenceToDatabase(UAnimSequence* AnimSequence)
{
	if (UPoseSearchDatabase* Database = GetPoseSearchDatabase())
	{
		Database->Modify();
		
		FPoseSearchDatabaseSequence NewAsset;
		NewAsset.Sequence = AnimSequence;
		Database->AddAnimationAsset(FInstancedStruct::Make(NewAsset));
	}
}

void FDatabaseViewModel::AddBlendSpaceToDatabase(UBlendSpace* BlendSpace)
{
	if (UPoseSearchDatabase* Database = GetPoseSearchDatabase())
	{
		Database->Modify();
		
		FPoseSearchDatabaseBlendSpace NewAsset;
		NewAsset.BlendSpace = BlendSpace;
		Database->AddAnimationAsset(FInstancedStruct::Make(NewAsset));
	}
}

void FDatabaseViewModel::AddAnimCompositeToDatabase(UAnimComposite* AnimComposite)
{
	if (UPoseSearchDatabase* Database = GetPoseSearchDatabase())
	{
		Database->Modify();

		FPoseSearchDatabaseAnimComposite NewAsset;
		NewAsset.AnimComposite = AnimComposite;
		Database->AddAnimationAsset(FInstancedStruct::Make(NewAsset));
	}
}

void FDatabaseViewModel::AddAnimMontageToDatabase(UAnimMontage* AnimMontage)
{
	if (UPoseSearchDatabase* Database = GetPoseSearchDatabase())
	{
		Database->Modify();
		
		FPoseSearchDatabaseAnimMontage NewAsset;
		NewAsset.AnimMontage = AnimMontage;
		Database->AddAnimationAsset(FInstancedStruct::Make(NewAsset));
	}
}

void FDatabaseViewModel::AddMultiAnimAssetToDatabase(UMultiAnimAsset* MultiAnimAsset)
{
	if (UPoseSearchDatabase* Database = GetPoseSearchDatabase())
	{
		Database->Modify();
		
		FPoseSearchDatabaseMultiAnimAsset NewAsset;
		NewAsset.MultiAnimAsset = MultiAnimAsset;
		Database->AddAnimationAsset(FInstancedStruct::Make(NewAsset));
	}
}

bool FDatabaseViewModel::DeleteFromDatabase(int32 AnimationAssetIndex)
{
	if (UPoseSearchDatabase* Database = GetPoseSearchDatabase())
	{
		if (const FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAssetBase = Database->GetDatabaseAnimationAsset<FPoseSearchDatabaseAnimationAssetBase>(AnimationAssetIndex))
		{
			Database->Modify();
			
			if (DatabaseAnimationAssetBase->IsSynchronizedWithExternalDependency())
			{
				if (UAnimSequenceBase* AnimSequenceBase = Cast<UAnimSequenceBase>(DatabaseAnimationAssetBase->GetAnimationAsset()))
				{
					bool bModified = false;
					for (int32 NotifyIndex = AnimSequenceBase->Notifies.Num() - 1; NotifyIndex >= 0; --NotifyIndex)
					{
						const FAnimNotifyEvent& NotifyEvent = AnimSequenceBase->Notifies[NotifyIndex];
						if (const UAnimNotifyState_PoseSearchBranchIn* PoseSearchBranchIn = Cast<UAnimNotifyState_PoseSearchBranchIn>(NotifyEvent.NotifyStateClass))
						{
							if (PoseSearchBranchIn->Database == Database && PoseSearchBranchIn->GetBranchInId() == DatabaseAnimationAssetBase->BranchInId)
							{
								if (!bModified)
								{
									AnimSequenceBase->Modify();
									bModified = true;
								}

								AnimSequenceBase->Notifies.RemoveAt(NotifyIndex);
							}
						}
					}

					if (bModified)
					{
						AnimSequenceBase->RefreshCacheData();
					}
				}
				else
				{
					UE_LOG(LogPoseSearchEditor, Error, TEXT("found DatabaseAnimationAssetBase with valid BranchInId, but invalid AnimSequenceBase in %s"), *Database->GetName());
				}
			}

			Database->RemoveAnimationAssetAt(AnimationAssetIndex);

			return true;
		}
	}

	return false;
}

void FDatabaseViewModel::SetDisableReselection(int32 AnimationAssetIndex, bool bEnabled)
{
	if (UPoseSearchDatabase* Database = GetPoseSearchDatabase())
	{
		if (FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAsset = Database->GetMutableDatabaseAnimationAsset<FPoseSearchDatabaseAnimationAssetBase>(AnimationAssetIndex))
		{
			Database->Modify();
			
			DatabaseAnimationAsset->SetDisableReselection(bEnabled);
		}
	}
}

bool FDatabaseViewModel::IsDisableReselection(int32 AnimationAssetIndex) const
{
	if (const UPoseSearchDatabase* Database = GetPoseSearchDatabase())
	{
		if (const FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAsset = Database->GetDatabaseAnimationAsset<FPoseSearchDatabaseAnimationAssetBase>(AnimationAssetIndex))
		{
			return DatabaseAnimationAsset->IsDisableReselection();
		}
	}

	return false;
}

void FDatabaseViewModel::SetIsEnabled(int32 AnimationAssetIndex, bool bEnabled)
{
	if (UPoseSearchDatabase* Database = GetPoseSearchDatabase())
	{
		if (FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAsset = Database->GetMutableDatabaseAnimationAsset<FPoseSearchDatabaseAnimationAssetBase>(AnimationAssetIndex))
		{
			Database->Modify();

			DatabaseAnimationAsset->SetIsEnabled(bEnabled);
		}
	}
}

bool FDatabaseViewModel::IsEnabled(int32 AnimationAssetIndex) const
{
	if (const UPoseSearchDatabase* Database = GetPoseSearchDatabase())
	{
		if (const FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAsset = Database->GetDatabaseAnimationAsset<FPoseSearchDatabaseAnimationAssetBase>(AnimationAssetIndex))
		{
			return DatabaseAnimationAsset->IsEnabled();
		}
	}

	return false;
}

bool FDatabaseViewModel::SetAnimationAsset(int32 AnimationAssetIndex, UObject* AnimAsset)
{
	if (AnimAsset)
	{
		if (UPoseSearchDatabase* Database = GetPoseSearchDatabase())
		{
			if (FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAsset = Database->GetMutableDatabaseAnimationAsset<FPoseSearchDatabaseAnimationAssetBase>(AnimationAssetIndex))
			{
				// Ensure that our target database item matches the input object's class.
				const UClass* AssetClass = AnimAsset->GetClass();
				if (AssetClass->IsChildOf(DatabaseAnimationAsset->GetAnimationAssetStaticClass()))
				{
					if (AssetClass->IsChildOf( UAnimSequence::StaticClass()))
					{
						Database->Modify();
						FPoseSearchDatabaseSequence* DatabaseSequenceAsset = static_cast<FPoseSearchDatabaseSequence*>(DatabaseAnimationAsset);
						DatabaseSequenceAsset->Sequence = Cast<UAnimSequence>(AnimAsset);
						return true;
					}
					
					if (AssetClass->IsChildOf(UAnimComposite::StaticClass()))
					{
						Database->Modify();
						FPoseSearchDatabaseAnimComposite* DatabaseCompositeAsset = static_cast<FPoseSearchDatabaseAnimComposite*>(DatabaseAnimationAsset);
						DatabaseCompositeAsset->AnimComposite = Cast<UAnimComposite>(AnimAsset);
						return true;
					}
					
					if (AssetClass->IsChildOf(UAnimMontage::StaticClass()))
					{
						Database->Modify();
						FPoseSearchDatabaseAnimMontage* DatabaseMontageAsset = static_cast<FPoseSearchDatabaseAnimMontage*>(DatabaseAnimationAsset);
						DatabaseMontageAsset->AnimMontage = Cast<UAnimMontage>(AnimAsset);
						return true;
					}
					
					if (AssetClass->IsChildOf(UBlendSpace::StaticClass()))
					{
						Database->Modify();
						FPoseSearchDatabaseBlendSpace* DatabaseBlendSpaceAsset = static_cast<FPoseSearchDatabaseBlendSpace*>(DatabaseAnimationAsset);
						DatabaseBlendSpaceAsset->BlendSpace = Cast<UBlendSpace>(AnimAsset);
						return true;
					}
					
					if (AssetClass->IsChildOf(UMultiAnimAsset::StaticClass()))
					{
						Database->Modify();
						FPoseSearchDatabaseMultiAnimAsset* DatabaseMultiAnimAssetAsset = static_cast<FPoseSearchDatabaseMultiAnimAsset*>(DatabaseAnimationAsset);
						DatabaseMultiAnimAssetAsset->MultiAnimAsset = Cast<UMultiAnimAsset>(AnimAsset);
						return true;
					}
				}
			}
		}
	}
	
	return false;
}

void FDatabaseViewModel::SetMirrorOption(int32 AnimationAssetIndex, EPoseSearchMirrorOption InMirrorOption)
{
	if (UPoseSearchDatabase* Database = GetPoseSearchDatabase())
	{
		if (FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAsset = Database->GetMutableDatabaseAnimationAsset<FPoseSearchDatabaseAnimationAssetBase>(AnimationAssetIndex))
		{
			Database->Modify();

			DatabaseAnimationAsset->MirrorOption = InMirrorOption;
		}
	}
}

EPoseSearchMirrorOption FDatabaseViewModel::GetMirrorOption(int32 AnimationAssetIndex)
{
	if (const UPoseSearchDatabase* Database = GetPoseSearchDatabase())
	{
		if (const FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAsset = Database->GetDatabaseAnimationAsset<FPoseSearchDatabaseAnimationAssetBase>(AnimationAssetIndex))
		{
			return DatabaseAnimationAsset->MirrorOption;
		}
	}

	return EPoseSearchMirrorOption::MirroredOnly;
}

int32 FDatabaseViewModel::SetSelectedNode(int32 PoseIdx, bool bClearSelection, bool bDrawQuery, TConstArrayView<float> InQueryVector)
{
	int32 SelectedSourceAssetIdx = INDEX_NONE;

	if (bClearSelection)
	{
		RemovePreviewActors();
	}

	bIsEditorSelection = false;
	bDrawQueryVector = bDrawQuery;
	QueryVector = InQueryVector;

	if (const UPoseSearchDatabase* Database = GetPoseSearchDatabase())
	{
		if (EAsyncBuildIndexResult::Success == FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database, ERequestAsyncBuildFlag::ContinueRequest))
		{
			const FSearchIndex& SearchIndex = Database->GetSearchIndex();
			if (SearchIndex.PoseMetadata.IsValidIndex(PoseIdx))
			{
				const uint32 IndexAssetIndex = SearchIndex.PoseMetadata[PoseIdx].GetAssetIndex();
				if (SearchIndex.Assets.IsValidIndex(IndexAssetIndex))
				{
					const FSearchIndexAsset& IndexAsset = SearchIndex.Assets[IndexAssetIndex];
					const FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAsset = Database->GetDatabaseAnimationAsset<FPoseSearchDatabaseAnimationAssetBase>(IndexAsset.GetSourceAssetIdx());
					check(DatabaseAnimationAsset);
					int32 PreviewActorGroupIndex = INDEX_NONE;
					for (int32 RoleIndex = 0; RoleIndex < DatabaseAnimationAsset->GetNumRoles(); ++RoleIndex)
					{
						FDatabasePreviewActor PreviewActor;
						const UE::PoseSearch::FRole Role = DatabaseAnimationAsset->GetRole(RoleIndex);
						const FTransform& RootTransformOrigin = DatabaseAnimationAsset->GetRootTransformOriginForRole(Role);
						if (PreviewActor.SpawnPreviewActor(GetWorld(), Database, IndexAssetIndex, Role, RootTransformOrigin, PoseIdx))
						{
							if (PreviewActorGroupIndex == INDEX_NONE)
							{
								PreviewActorGroupIndex = PreviewActors.AddDefaulted();
							}

							MaxPreviewPlayLength = FMath::Max(MaxPreviewPlayLength, IndexAsset.GetLastSampleTime(Database->Schema->SampleRate) - PreviewActor.GetPlayTimeOffset());
							MinPreviewPlayLength = FMath::Min(MinPreviewPlayLength, IndexAsset.GetFirstSampleTime(Database->Schema->SampleRate) - PreviewActor.GetPlayTimeOffset());
							PreviewActors[PreviewActorGroupIndex].Add(PreviewActor);
							SelectedSourceAssetIdx = IndexAsset.GetSourceAssetIdx();
						}
					}
				}
			}

			DatabaseDataDetails.Pin()->Reconstruct();

			for (TArray<FDatabasePreviewActor>& PreviewActorGroup : PreviewActors)
			{
				for (FDatabasePreviewActor& PreviewActor : PreviewActorGroup)
				{
					PreviewActor.UpdatePreviewActor(Database, PlayTime, bQuantizeAnimationToPoseData);
				}
			}

			SetPlayTime(0.f, false);
		}
	}

	ProcessSelectedActor(nullptr);

	return SelectedSourceAssetIdx;
}

void FDatabaseViewModel::SetSelectedNodes(const TArrayView<TSharedPtr<FDatabaseAssetTreeNode>>& InSelectedNodes)
{
	RemovePreviewActors();

	if (const UPoseSearchDatabase* Database = GetPoseSearchDatabase())
	{
		if (EAsyncBuildIndexResult::Success == FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database, ERequestAsyncBuildFlag::ContinueRequest))
		{
			TMap<int32, int32> AssociatedAssetIndices;
			for (int32 i = 0; i < InSelectedNodes.Num(); ++i)
			{
				AssociatedAssetIndices.FindOrAdd(InSelectedNodes[i]->SourceAssetIdx) = i;
			}

			const FSearchIndex& SearchIndex = Database->GetSearchIndex();
			for (int32 IndexAssetIndex = 0; IndexAssetIndex < SearchIndex.Assets.Num(); ++IndexAssetIndex)
			{
				const FSearchIndexAsset& IndexAsset = SearchIndex.Assets[IndexAssetIndex];
				if (AssociatedAssetIndices.Find(IndexAsset.GetSourceAssetIdx()))
				{
					const FPoseSearchDatabaseAnimationAssetBase* DatabaseAnimationAsset = Database->GetDatabaseAnimationAsset<FPoseSearchDatabaseAnimationAssetBase>(IndexAsset.GetSourceAssetIdx());
					check(DatabaseAnimationAsset);
					int32 PreviewActorGroupIndex = INDEX_NONE;
					for (int32 RoleIndex = 0; RoleIndex < DatabaseAnimationAsset->GetNumRoles(); ++RoleIndex)
					{
						FDatabasePreviewActor PreviewActor;
						const UE::PoseSearch::FRole Role = DatabaseAnimationAsset->GetRole(RoleIndex);
						const FTransform RootTransformOrigin = DatabaseAnimationAsset->GetRootTransformOriginForRole(Role);
						if (PreviewActor.SpawnPreviewActor(GetWorld(), Database, IndexAssetIndex, Role, RootTransformOrigin))
						{
							if (PreviewActorGroupIndex == INDEX_NONE)
							{
								PreviewActorGroupIndex = PreviewActors.AddDefaulted();
							}

							MaxPreviewPlayLength = FMath::Max(MaxPreviewPlayLength, IndexAsset.GetLastSampleTime(Database->Schema->SampleRate) - IndexAsset.GetFirstSampleTime(Database->Schema->SampleRate));
							PreviewActors[PreviewActorGroupIndex].Add(PreviewActor);
						}
					}
				}
			}

			DatabaseDataDetails.Pin()->Reconstruct();
			for (TArray<FDatabasePreviewActor>& PreviewActorGroup : PreviewActors)
			{
				for (FDatabasePreviewActor& PreviewActor : PreviewActorGroup)
				{
					PreviewActor.UpdatePreviewActor(Database, PlayTime, bQuantizeAnimationToPoseData);
				}
			}
		}

		ProcessSelectedActor(nullptr);
	}
}

void FDatabaseViewModel::ProcessSelectedActor(AActor* Actor)
{
	SelectedActorIndexAssetIndex = INDEX_NONE;

	for (const TArray<FDatabasePreviewActor>& PreviewActorGroup : PreviewActors)
	{
		for (const FDatabasePreviewActor& PreviewActor : PreviewActorGroup)
		{
			if (PreviewActor.GetActor() == Actor)
			{
				SelectedActorIndexAssetIndex = PreviewActor.GetIndexAssetIndex();
				return;
			}
		}
	}
}

void FDatabaseViewModel::SetDrawQueryVector(bool bValue)
{
	if (bDrawQueryVector != bValue)
	{
		bDrawQueryVector = bValue;
		DatabaseDataDetails.Pin()->Reconstruct();
	}
}

const FSearchIndexAsset* FDatabaseViewModel::GetSelectedActorIndexAsset() const
{
	if (SelectedActorIndexAssetIndex >= 0)
	{
		const UPoseSearchDatabase* Database = GetPoseSearchDatabase();
		if (EAsyncBuildIndexResult::Success == FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database, ERequestAsyncBuildFlag::ContinueRequest))
		{
			const FSearchIndex& SearchIndex = Database->GetSearchIndex();
			if (SearchIndex.Assets.IsValidIndex(SelectedActorIndexAssetIndex))
			{
				return &SearchIndex.Assets[SelectedActorIndexAssetIndex];
			}
		}
	}
	return nullptr;
}

TRange<double> FDatabaseViewModel::GetPreviewPlayRange() const
{
	constexpr double ViewRangeSlack = 0.2;
	return TRange<double>(MinPreviewPlayLength - ViewRangeSlack, MaxPreviewPlayLength + ViewRangeSlack);
}

float FDatabaseViewModel::GetPlayTime() const
{
	return PlayTime;
}

void FDatabaseViewModel::SetPlayTime(float NewPlayTime, bool bInTickPlayTime)
{
	NewPlayTime = FMath::Clamp(NewPlayTime, MinPreviewPlayLength, MaxPreviewPlayLength);
	DeltaTimeMultiplier = bInTickPlayTime ? DeltaTimeMultiplier : 0.f;

	if (!FMath::IsNearlyEqual(PlayTime, NewPlayTime))
	{
		PlayTime = NewPlayTime;
		
		if (const UPoseSearchDatabase* Database = GetPoseSearchDatabase())
		{
			if (EAsyncBuildIndexResult::Success == FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database, ERequestAsyncBuildFlag::ContinueRequest))
			{
				for (TArray<FDatabasePreviewActor>& PreviewActorGroup : PreviewActors)
				{
					for (FDatabasePreviewActor& PreviewActor : PreviewActorGroup)
					{
						PreviewActor.UpdatePreviewActor(Database, PlayTime, bQuantizeAnimationToPoseData);
					}
				}
			}
		}
	}
}

bool FDatabaseViewModel::GetAnimationTime(int32 SourceAssetIdx, float& CurrentPlayTime, FVector& BlendParameters) const
{
	if (const UPoseSearchDatabase* Database = GetPoseSearchDatabase())
	{
		if (EAsyncBuildIndexResult::Success == FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database, ERequestAsyncBuildFlag::ContinueRequest))
		{
			const FSearchIndex& SearchIndex = Database->GetSearchIndex();
			for (const TArray<FDatabasePreviewActor>& PreviewActorGroup : PreviewActors)
			{
				for (const FDatabasePreviewActor& PreviewActor : PreviewActorGroup)
				{
					if (PreviewActor.GetIndexAssetIndex() >= 0 && PreviewActor.GetIndexAssetIndex() < SearchIndex.Assets.Num())
					{
						const FSearchIndexAsset& IndexAsset = SearchIndex.Assets[PreviewActor.GetIndexAssetIndex()];
						if (IndexAsset.GetSourceAssetIdx() == SourceAssetIdx)
						{
							CurrentPlayTime = PreviewActor.GetSampler().ToNormalizedTime(PlayTime + IndexAsset.GetFirstSampleTime(Database->Schema->SampleRate) + PreviewActor.GetPlayTimeOffset());
							BlendParameters = IndexAsset.GetBlendParameters();
							return true;
						}
					}
				}
			}

			for (const FSearchIndexAsset& IndexAsset : SearchIndex.Assets)
			{
				if (IndexAsset.GetSourceAssetIdx() == SourceAssetIdx)
				{
					CurrentPlayTime = PlayTime + IndexAsset.GetFirstSampleTime(Database->Schema->SampleRate);
					BlendParameters = IndexAsset.GetBlendParameters();

					const bool bIsBlendSpace = Database->GetDatabaseAnimationAsset<FPoseSearchDatabaseBlendSpace>(IndexAsset) != nullptr;
					if (bIsBlendSpace && !FMath::IsNearlyEqual(MaxPreviewPlayLength, MinPreviewPlayLength))
					{
						CurrentPlayTime = (CurrentPlayTime - MaxPreviewPlayLength) / (MaxPreviewPlayLength - MinPreviewPlayLength);
					}
					return true;
				}
			}
		}
	}

	CurrentPlayTime = 0.f;
	BlendParameters = FVector::ZeroVector;
	return false;
}
}
