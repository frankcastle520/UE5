// Copyright Epic Games, Inc. All Rights Reserved.

#include "Traits/SequencePlayer.h"

#include "AnimationRuntime.h"
#include "TraitCore/ExecutionContext.h"
#include "EvaluationVM/Tasks/PushAnimSequenceKeyframe.h"

namespace UE::AnimNext
{
	AUTO_REGISTER_ANIM_TRAIT(FSequencePlayerTrait)

	// Trait implementation boilerplate
	#define TRAIT_INTERFACE_ENUMERATOR(GeneratorMacro) \
		GeneratorMacro(IEvaluate) \
		GeneratorMacro(ITimeline) \
		GeneratorMacro(IUpdate) \
		GeneratorMacro(IGarbageCollection) \

	GENERATE_ANIM_TRAIT_IMPLEMENTATION(FSequencePlayerTrait, TRAIT_INTERFACE_ENUMERATOR, NULL_ANIM_TRAIT_INTERFACE_ENUMERATOR, NULL_ANIM_TRAIT_EVENT_ENUMERATOR)
	#undef TRAIT_INTERFACE_ENUMERATOR

	void FSequencePlayerTrait::FInstanceData::Construct(const FExecutionContext& Context, const FTraitBinding& Binding)
	{
		FTrait::FInstanceData::Construct(Context, Binding);

		IGarbageCollection::RegisterWithGC(Context, Binding);
	}

	void FSequencePlayerTrait::FInstanceData::Destruct(const FExecutionContext& Context, const FTraitBinding& Binding)
	{
		FTrait::FInstanceData::Destruct(Context, Binding);

		IGarbageCollection::UnregisterWithGC(Context, Binding);
	}

	void FSequencePlayerTrait::PreEvaluate(FEvaluateTraversalContext& Context, const TTraitBinding<IEvaluate>& Binding) const
	{
		const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();

		const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		const bool bInterpolate = true;

		FAnimNextAnimSequenceKeyframeTask Task = FAnimNextAnimSequenceKeyframeTask::MakeFromSampleTime(InstanceData->AnimSequence, InstanceData->InternalTimeAccumulator, bInterpolate);
		Task.bExtractTrajectory = true;	/*Output.AnimInstanceProxy->ShouldExtractRootMotion()*/

		Context.AppendTask(Task);
	}

	float FSequencePlayerTrait::GetPlayRate(FExecutionContext& Context, const TTraitBinding<ITimeline>& Binding) const
	{
		const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();
		return SharedData->GetPlayRate(Binding);
	}

	FTimelineProgress FSequencePlayerTrait::GetProgress(FExecutionContext& Context, const TTraitBinding<ITimeline>& Binding) const
	{
		const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();
		if (const UAnimSequence* AnimSeq = InstanceData->AnimSequence.Get())
		{
			return FTimelineProgress(InstanceData->InternalTimeAccumulator, AnimSeq->GetPlayLength());
		}

		return FTimelineProgress();
	}

	FTimelineProgress FSequencePlayerTrait::SimulateAdvanceBy(FExecutionContext& Context, const TTraitBinding<ITimeline>& Binding, float DeltaTime) const
	{
		const FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		if (UAnimSequence* AnimSeq = InstanceData->AnimSequence.Get())
		{
			const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();

			TTraitBinding<ITimeline> TimelineTrait;
			Binding.GetStackInterface(TimelineTrait);

			const float PlayRate = TimelineTrait.GetPlayRate(Context);
			const bool bIsLooping = SharedData->GetbLoop(Binding);
			const float SequenceLength = AnimSeq->GetPlayLength();

			float Position = InstanceData->InternalTimeAccumulator;
			FAnimationRuntime::AdvanceTime(bIsLooping, DeltaTime * PlayRate, Position, SequenceLength);

			return FTimelineProgress(Position, SequenceLength);
		}

		return FTimelineProgress();
	}

	FTimelineProgress FSequencePlayerTrait::AdvanceBy(FExecutionContext& Context, const TTraitBinding<ITimeline>& Binding, float DeltaTime) const
	{
		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		if (UAnimSequence* AnimSeq = InstanceData->AnimSequence.Get())
		{
			const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();

			TTraitBinding<ITimeline> TimelineTrait;
			Binding.GetStackInterface(TimelineTrait);

			const float PlayRate = TimelineTrait.GetPlayRate(Context);
			const bool bIsLooping = SharedData->GetbLoop(Binding);
			const float SequenceLength = AnimSeq->GetPlayLength();

			FAnimationRuntime::AdvanceTime(bIsLooping, DeltaTime * PlayRate, InstanceData->InternalTimeAccumulator, SequenceLength);

			return FTimelineProgress(InstanceData->InternalTimeAccumulator, SequenceLength);
		}

		return FTimelineProgress();
	}

	void FSequencePlayerTrait::AdvanceToRatio(FExecutionContext& Context, const TTraitBinding<ITimeline>& Binding, float ProgressRatio) const
	{
		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		if (UAnimSequence* AnimSeq = InstanceData->AnimSequence.Get())
		{
			const float SequenceLength = AnimSeq->GetPlayLength();

			InstanceData->InternalTimeAccumulator = FMath::Clamp(ProgressRatio, 0.0f, 1.0f) * SequenceLength;
		}
	}

	void FSequencePlayerTrait::OnBecomeRelevant(FUpdateTraversalContext& Context, const TTraitBinding<IUpdate>& Binding, const FTraitUpdateState& TraitState) const
	{
		const FSharedData* SharedData = Binding.GetSharedData<FSharedData>();
		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		// Cache the anim sequence we'll play during construction, we don't allow it to change afterwards
		InstanceData->AnimSequence = SharedData->GetAnimSequence(Binding);

		float InternalTimeAccumulator = 0.0f;
		if (UAnimSequence* AnimSeq = InstanceData->AnimSequence.Get())
		{
			const float StartPosition = SharedData->GetStartPosition(Binding);
			const float SequenceLength = AnimSeq->GetPlayLength();
			InternalTimeAccumulator = FMath::Clamp(StartPosition, 0.0f, SequenceLength);
		}

		InstanceData->InternalTimeAccumulator = InternalTimeAccumulator;
	}

	void FSequencePlayerTrait::PreUpdate(FUpdateTraversalContext& Context, const TTraitBinding<IUpdate>& Binding, const FTraitUpdateState& TraitState) const
	{
		// We just advance the timeline
		TTraitBinding<ITimeline> TimelineTrait;
		Binding.GetStackInterface(TimelineTrait);

		TimelineTrait.AdvanceBy(Context, TraitState.GetDeltaTime());
	}

	void FSequencePlayerTrait::AddReferencedObjects(const FExecutionContext& Context, const TTraitBinding<IGarbageCollection>& Binding, FReferenceCollector& Collector) const
	{
		IGarbageCollection::AddReferencedObjects(Context, Binding, Collector);

		FInstanceData* InstanceData = Binding.GetInstanceData<FInstanceData>();

		Collector.AddReferencedObject(InstanceData->AnimSequence);
	}
}
