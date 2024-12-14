// Copyright Epic Games, Inc. All Rights Reserved.
#include "AnimLayers.h"
#include "ControlRig.h"
#include "ISequencer.h"
#include "MVVM/Selection/Selection.h"
#include "MVVM/ViewModels/SequencerEditorViewModel.h"
#include "MVVM/ViewModels/SequencerOutlinerViewModel.h"
#include "MVVM/ViewModels/SequencerTrackAreaViewModel.h"
#include "MVVM/ViewModels/SequencerEditorViewModel.h"
#include "MVVM/ViewModels/ObjectBindingModel.h"
#include "LevelSequence.h"
#include "ControlRigSequencerEditorLibrary.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "ILevelSequenceEditorToolkit.h"
#include "LevelSequencePlayer.h"
#include "EditMode/ControlRigEditMode.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "MovieScene.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "Editor/EditorEngine.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/Selection.h"
#include "ScopedTransaction.h"
#include "EditMode/ControlRigEditMode.h"
#include "EditorModeManager.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneBoolChannel.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneIntegerChannel.h"
#include "Channels/MovieSceneByteChannel.h"
#include "ISequencerPropertyKeyedStatus.h"
#include "BakingAnimationKeySettings.h"
#include "Algo/Accumulate.h"
#include "MovieSceneToolHelpers.h"

#define LOCTEXT_NAMESPACE "AnimLayers"


static TArray<FGuid> GetSelectedOutlinerGuids(ISequencer* SequencerPtr)
{
	using namespace UE::Sequencer;
	TArray<FGuid> SelectedObjects;// = SequencerPtr->GetViewModel()->GetSelection()->GetBoundObjectsGuids(); //this is not exposed :(

	for (TViewModelPtr<FObjectBindingModel> ObjectBindingNode : SequencerPtr->GetViewModel()->GetSelection()->Outliner.Filter<FObjectBindingModel>())
	{
		FGuid Guid = ObjectBindingNode->GetObjectGuid();
		SelectedObjects.Add(Guid);
	}
	return SelectedObjects;
}

struct FControlRigAndControlsAndTrack
{
	UMovieSceneControlRigParameterTrack* Track;
	UControlRig* ControlRig;
	TArray<FName> Controls;
};

struct FObjectAndTrack
{
	UMovieScenePropertyTrack* Track;
	UObject* BoundObject;
};

static void GetSelectedControlRigsAndBoundObjects(ISequencer* SequencerPtr, TArray<FControlRigAndControlsAndTrack>& OutSelectedCRs, TArray<FObjectAndTrack>& OutBoundObjects)
{
	if (SequencerPtr == nullptr || SequencerPtr->GetViewModel() == nullptr)
	{
		return;
	}
	ULevelSequence* LevelSequence = Cast<ULevelSequence>(SequencerPtr->GetFocusedMovieSceneSequence());
	if (LevelSequence == nullptr)
	{
		return;
	}

	using namespace UE::Sequencer;
	TArray<FGuid> SelectedObjects = GetSelectedOutlinerGuids(SequencerPtr);;// = SequencerPtr->GetViewModel()->GetSelection()->GetBoundObjectsGuids();

	UMovieScene* MovieScene = LevelSequence->GetMovieScene();
	if (MovieScene)
	{
		const TArray<FMovieSceneBinding>& Bindings = MovieScene->GetBindings();
		for (const FMovieSceneBinding& Binding : Bindings)
		{
			bool bHaveControlRig = false;
			TArray<UMovieSceneTrack*> CRTracks = MovieScene->FindTracks(UMovieSceneControlRigParameterTrack::StaticClass(), Binding.GetObjectGuid(), NAME_None);
			for (UMovieSceneTrack* AnyOleTrack : CRTracks)
			{
				if (UMovieSceneControlRigParameterTrack* Track = Cast<UMovieSceneControlRigParameterTrack>(AnyOleTrack))
				{
					if (UControlRig* ControlRig = Track->GetControlRig())
					{
						FControlRigAndControlsAndTrack CRControls;
						CRControls.ControlRig = ControlRig;
						CRControls.Track = Track;
						CRControls.Controls = ControlRig->CurrentControlSelection();
						if (CRControls.Controls.Num() > 0)
						{
							bHaveControlRig = true;
							OutSelectedCRs.Add(CRControls);
						}
					}
				}
			}
			//if we have control rig controls don't add the base skel mesh for now
			if (bHaveControlRig == false && SelectedObjects.Contains(Binding.GetObjectGuid()))
			{
				TArray<UMovieSceneTrack*> Tracks = MovieScene->FindTracks(UMovieScenePropertyTrack::StaticClass(), Binding.GetObjectGuid(), NAME_None);
				if (Tracks.Num() < 1)
				{
					continue;
				}
				const FMovieSceneSequenceID& SequenceID = SequencerPtr->State.FindSequenceId(LevelSequence);

				for (UMovieSceneTrack* AnyOleTrack : Tracks)
				{
					if (UMovieScenePropertyTrack* Track = Cast<UMovieScenePropertyTrack>(AnyOleTrack))
					{
						const FMovieSceneBlendTypeField SupportedBlendTypes = Track->GetSupportedBlendTypes();
						if (SupportedBlendTypes.Num() == 0)
						{
							continue;
						}
						const TArrayView<TWeakObjectPtr<UObject>>& BoundObjects = SequencerPtr->FindBoundObjects(Binding.GetObjectGuid(), SequenceID);
						for (const TWeakObjectPtr<UObject> CurrentBoundObject : BoundObjects)
						{
							if (UObject* BoundObject = CurrentBoundObject.Get())
							{
								FObjectAndTrack ObjectAndTrack;
								ObjectAndTrack.BoundObject = BoundObject;
								ObjectAndTrack.Track = Track;
								OutBoundObjects.Add(ObjectAndTrack);
							}
						}
					}
				}
			}
		}
	}
}

bool FAnimLayerSelectionSet::MergeWithAnotherSelection(const FAnimLayerSelectionSet& Selection)
{
	if (BoundObject.IsValid() && (BoundObject.Get() == Selection.BoundObject.Get()))
	{
		for (const TPair<FName, FAnimLayerPropertyAndChannels>& Incoming : Selection.Names)
		{
			FAnimLayerPropertyAndChannels& Channels = Names.FindOrAdd(Incoming.Key);
			Channels.Channels |= (Incoming.Value.Channels);
		}
		return true;
	}
	return false;
}

FAnimLayerState::FAnimLayerState() : bKeyed(ECheckBoxState::Unchecked), bActive(true),  bLock(false), Weight(1.0), Type((uint32)EAnimLayerType::Base)
{
	Name = FText(LOCTEXT("BaseLayer", "Base Layer"));
}

FText FAnimLayerState::AnimLayerTypeToText() const
{
	if ((EAnimLayerType)Type == EAnimLayerType::Additive)
	{
		return FText(LOCTEXT("Additive", "Additive"));
	}
	else if ((EAnimLayerType)Type == EAnimLayerType::Override)
	{
		return FText(LOCTEXT("Override", "Override"));
	}
	return FText(LOCTEXT("Base", "Base"));
}


UAnimLayer::UAnimLayer(class FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	WeightProxy = NewObject<UAnimLayerWeightProxy>(this, TEXT("Weight"), RF_Transactional);
}

void UAnimLayer::SetKey(TSharedPtr<ISequencer>& Sequencer, const IPropertyHandle& KeyedPropertyHandle)
{
	FScopedTransaction PropertyChangedTransaction(LOCTEXT("KeyAnimLayerWeight", "Key Anim Layer Weight"), !GIsTransacting);
	bool bAnythingKeyed = false;

	for (const TPair<TWeakObjectPtr<UObject>,FAnimLayerItem>& Pair : AnimLayerItems)
	{
		if (Pair.Key != nullptr)
		{
			for (const FAnimLayerSectionItem& SectionItem : Pair.Value.SectionItems)
			{
				if (SectionItem.Section.IsValid() && SectionItem.Section.Get()->TryModify())
				{
					FMovieSceneFloatChannel* FloatChannel = nullptr;
					if (UMovieSceneControlRigParameterSection* CRSection = Cast<UMovieSceneControlRigParameterSection>(SectionItem.Section.Get()))
					{
						FloatChannel = &CRSection->Weight;
					}
					else if (UMovieScene3DTransformSection* LayerSection = Cast<UMovieScene3DTransformSection>(SectionItem.Section.Get()))
					{
						FloatChannel = LayerSection->GetWeightChannel();
					}
					if (FloatChannel)
					{
						//don't add key if there!
						FFrameTime LocalTime = Sequencer->GetLocalTime().Time;
						const TRange<FFrameNumber> FrameRange = TRange<FFrameNumber>(LocalTime.FrameNumber);
						TArray<FFrameNumber> KeyTimes;
						FloatChannel->GetKeys(FrameRange, &KeyTimes, nullptr);
						if (KeyTimes.Num() == 0)
						{
							float Value = State.Weight;
							FloatChannel->AddCubicKey(LocalTime.FrameNumber, Value, ERichCurveTangentMode::RCTM_SmartAuto);
							bAnythingKeyed = true;
						}
					}
				}
			}
		}
	}

	if(bAnythingKeyed == false)
	{
		PropertyChangedTransaction.Cancel();
	}
}

EPropertyKeyedStatus UAnimLayer::GetPropertyKeyedStatus(TSharedPtr<ISequencer>& Sequencer, const IPropertyHandle& PropertyHandle)
{
	EPropertyKeyedStatus KeyedStatus = EPropertyKeyedStatus::NotKeyed;

	if (Sequencer.IsValid() == false || Sequencer->GetFocusedMovieSceneSequence() == nullptr)
	{
		return KeyedStatus;
	}
	const TRange<FFrameNumber> FrameRange = TRange<FFrameNumber>(Sequencer->GetLocalTime().Time.FrameNumber);
	int32 NumKeyed = 0;
	int32 NumToCheck = 0;
	for (const TPair<TWeakObjectPtr<UObject>,FAnimLayerItem>& Pair : AnimLayerItems)
	{
		if (Pair.Key != nullptr)
		{
			for (const FAnimLayerSectionItem& SectionItem : Pair.Value.SectionItems)
			{
				if (SectionItem.Section.IsValid())
				{
					FMovieSceneFloatChannel* FloatChannel = nullptr;
					if (UMovieSceneControlRigParameterSection* CRSection = Cast<UMovieSceneControlRigParameterSection>(SectionItem.Section.Get()))
					{
						FloatChannel = &CRSection->Weight;
					}
					else if (UMovieScene3DTransformSection* LayerSection = Cast<UMovieScene3DTransformSection>(SectionItem.Section.Get()))
					{
						FloatChannel = LayerSection->GetWeightChannel();
					}
					if (FloatChannel)
					{
						++NumToCheck;
						EPropertyKeyedStatus NewKeyedStatus = EPropertyKeyedStatus::NotKeyed;
						if (FloatChannel->GetNumKeys() > 0)
						{
							TArray<FFrameNumber> KeyTimes;
							FloatChannel->GetKeys(FrameRange, &KeyTimes, nullptr);
							if (KeyTimes.Num() > 0)
							{
								++NumKeyed;
								NewKeyedStatus = EPropertyKeyedStatus::PartiallyKeyed;
							}
							else
							{
								NewKeyedStatus = EPropertyKeyedStatus::KeyedInOtherFrame;
							}
						}
						KeyedStatus = FMath::Max(KeyedStatus, NewKeyedStatus);
					}
				}
			}
		}
	}
	if (KeyedStatus == EPropertyKeyedStatus::PartiallyKeyed && NumToCheck == NumKeyed)
	{
		KeyedStatus = EPropertyKeyedStatus::KeyedInFrame;
	}
	return KeyedStatus;
}

ECheckBoxState UAnimLayer::GetKeyed() const
{
	TOptional<ECheckBoxState> CurrentVal;
	for (const TPair<TWeakObjectPtr<UObject>, FAnimLayerItem>& Pair : AnimLayerItems)
	{
		if (Pair.Key != nullptr)
		{
			for (const FAnimLayerSectionItem& SectionItem : Pair.Value.SectionItems)
			{
				if (SectionItem.Section.IsValid())
				{
					if (UMovieSceneTrack* Track = Cast<UMovieSceneTrack>(SectionItem.Section.Get()->GetTypedOuter< UMovieSceneTrack>()))
					{
						if (UMovieSceneControlRigParameterTrack* ControlRigTrack = Cast<UMovieSceneControlRigParameterTrack>(Track))
						{
							for (const TPair<FName, FAnimLayerPropertyAndChannels>& ControlName : SectionItem.AnimLayerSet.Names)
							{
								if (ControlRigTrack->GetSectionToKey(ControlName.Key) == SectionItem.Section.Get())
								{
									if (CurrentVal.IsSet() && CurrentVal.GetValue() != ECheckBoxState::Checked)
									{
										CurrentVal = ECheckBoxState::Undetermined;
									}
									if (CurrentVal.IsSet() == false)
									{
										CurrentVal = ECheckBoxState::Checked;
									}
								}
								else
								{
									if (CurrentVal.IsSet() && CurrentVal.GetValue() != ECheckBoxState::Unchecked)
									{
										CurrentVal = ECheckBoxState::Undetermined;
									}
									if (CurrentVal.IsSet() == false)
									{
										CurrentVal = ECheckBoxState::Unchecked;
									}
								}
							}
						}
						else
						{
							if (Track->GetSectionToKey() == SectionItem.Section.Get() ||
								(Track->GetAllSections().Num() == 1 && (Track->GetAllSections()[0] == SectionItem.Section.Get())))
							{
								if (CurrentVal.IsSet() && CurrentVal.GetValue() != ECheckBoxState::Checked)
								{
									CurrentVal = ECheckBoxState::Undetermined;
								}
								if (CurrentVal.IsSet() == false)
								{
									CurrentVal = ECheckBoxState::Checked;
								}
							}
							else
							{
								if (CurrentVal.IsSet() && CurrentVal.GetValue() != ECheckBoxState::Unchecked)
								{
									CurrentVal = ECheckBoxState::Undetermined;
								}
								if (CurrentVal.IsSet() == false)
								{
									CurrentVal = ECheckBoxState::Unchecked;
								}
							}
						}
					}
				}
			}
		}
	}
	if (CurrentVal.IsSet())
	{
		if (State.bKeyed != CurrentVal.GetValue() && CurrentVal.GetValue() == ECheckBoxState::Checked)
		{
			SetSectionToKey();
		}
		State.bKeyed = CurrentVal.GetValue();
	}
	else
	{
		//has no sections and is base so it's keyed, else it's not
		State.bKeyed = State.Type == EAnimLayerType::Base ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return State.bKeyed;
}

void UAnimLayer::SetSectionToKey() const
{

	for (const TPair<TWeakObjectPtr<UObject>, FAnimLayerItem>& Pair : AnimLayerItems)
	{
		if (Pair.Key != nullptr)
		{
			for (const FAnimLayerSectionItem& SectionItem : Pair.Value.SectionItems)
			{
				if (SectionItem.Section.IsValid())
				{
					if (UMovieSceneTrack* Track = Cast<UMovieSceneTrack>(SectionItem.Section.Get()->GetTypedOuter< UMovieSceneTrack>()))
					{
						Track->Modify();
						if (UMovieSceneControlRigParameterTrack* ControlRigTrack = Cast<UMovieSceneControlRigParameterTrack>(Track))
						{
							for (const TPair<FName, FAnimLayerPropertyAndChannels>& ControlName : SectionItem.AnimLayerSet.Names)
							{
								ControlRigTrack->SetSectionToKey(SectionItem.Section.Get(), ControlName.Key);
							}
						}
						else
						{
							Track->SetSectionToKey(SectionItem.Section.Get());
						}
					}
				}
			}
		}
	}
}

void UAnimLayer::SetKeyed()
{
	State.bKeyed = ECheckBoxState::Checked;
	SetSectionToKey();

}

bool UAnimLayer::GetActive() const
{
	TOptional<bool> CurrentVal;
	for (const TPair<TWeakObjectPtr<UObject>, FAnimLayerItem>& Pair : AnimLayerItems)
	{
		if (Pair.Key != nullptr)
		{
			for (const FAnimLayerSectionItem& SectionItem : Pair.Value.SectionItems)
			{
				if (SectionItem.Section.IsValid())
				{
					const bool bActive = SectionItem.Section.Get()->IsActive();
					if (CurrentVal.IsSet() && CurrentVal.GetValue() != bActive)
					{
						SectionItem.Section.Get()->SetIsActive(CurrentVal.GetValue());
					}
					if (CurrentVal.IsSet() == false)
					{
						CurrentVal = bActive;
					}
				}
			}
		}
	}
	if (CurrentVal.IsSet())
	{
		State.bActive = CurrentVal.GetValue();
	}
	return State.bActive;
}

void UAnimLayer::SetActive(bool bInActive)
{
	const FScopedTransaction Transaction(LOCTEXT("SetActive_Transaction", "Set Active"), !GIsTransacting);
	Modify();
	State.bActive = bInActive;
	for (const TPair<TWeakObjectPtr<UObject>, FAnimLayerItem>& Pair : AnimLayerItems)
	{
		if (Pair.Key != nullptr)
		{
			for (const FAnimLayerSectionItem& SectionItem : Pair.Value.SectionItems)
			{
				if (SectionItem.Section.IsValid())
				{
					SectionItem.Section->Modify();
					SectionItem.Section.Get()->SetIsActive(State.bActive);
				}
			}
		}
	}
}

bool UAnimLayer::AddSelectedInSequencer() 
{
	TSharedPtr<ISequencer> SequencerPtr = UAnimLayers::GetSequencerFromAsset();
	if (SequencerPtr.IsValid() == false)
	{
		return false;
	}
	TArray<FControlRigAndControlsAndTrack> SelectedCRs;
	TArray<FObjectAndTrack> SelectedBoundObjects;
	GetSelectedControlRigsAndBoundObjects(SequencerPtr.Get(), SelectedCRs, SelectedBoundObjects);
	if (SelectedCRs.Num() <= 0 && SelectedBoundObjects.Num() <= 0)
	{
		return false;
	}
	bool bAddedSomething = false;
	const FScopedTransaction Transaction(LOCTEXT("AddSelectedAnimLayer_Transaction", "Add Selected"),!GIsTransacting);
	Modify();
	for (FControlRigAndControlsAndTrack& CRControls : SelectedCRs)
	{
		if (FAnimLayerItem* ExistingAnimLayerItem = AnimLayerItems.Find(CRControls.ControlRig))
		{
			for (FAnimLayerSectionItem& SectionItem : ExistingAnimLayerItem->SectionItems)
			{
				if (UMovieSceneControlRigParameterSection* CRSection = Cast<UMovieSceneControlRigParameterSection>(SectionItem.Section))
				{
					for (FName& ControlName : CRControls.Controls)
					{
						if (SectionItem.AnimLayerSet.Names.Contains(ControlName) == false)
						{
							FAnimLayerPropertyAndChannels Channels;
							Channels.Name = ControlName;
							Channels.Channels = (uint32)EControlRigContextChannelToKey::AllTransform;
							SectionItem.AnimLayerSet.Names.Add(ControlName, Channels);
						}
					}
					TArray<FName> AllControls;
					SectionItem.AnimLayerSet.Names.GenerateKeyArray(AllControls);
					UAnimLayers::SetUpControlRigSection(CRSection, AllControls);
					bAddedSomething = true;
				}
			}
		}
		else
		{
			//add new section
			FAnimLayerItem AnimLayerItem;
			FAnimLayerSectionItem SectionItem;
			SectionItem.AnimLayerSet.BoundObject = CRControls.ControlRig;
			for (FName& ControlName : CRControls.Controls)
			{
				FAnimLayerPropertyAndChannels Channels;
				Channels.Name = ControlName;
				Channels.Channels = (uint32)EControlRigContextChannelToKey::AllTransform;
				SectionItem.AnimLayerSet.Names.Add(ControlName, Channels);
			}
			// Add a new section that starts and ends at the same time
			TGuardValue<bool> GuardSetSection(CRControls.Track->bSetSectionToKeyPerControl, false);
			if (UMovieSceneControlRigParameterSection* NewSection = Cast<UMovieSceneControlRigParameterSection>(CRControls.Track->CreateNewSection()))
			{
				ensureAlwaysMsgf(NewSection->HasAnyFlags(RF_Transactional), TEXT("CreateNewSection must return an instance with RF_Transactional set! (pass RF_Transactional to NewObject)"));
				NewSection->SetFlags(RF_Transactional);
				NewSection->SetTransformMask(FMovieSceneTransformMask{ EMovieSceneTransformChannel::All });
				FMovieSceneFloatChannel* FloatChannel = &NewSection->Weight;
				SectionItem.Section = NewSection;
				AnimLayerItem.SectionItems.Add(SectionItem);
				AnimLayerItems.Add(CRControls.ControlRig, AnimLayerItem);
				UAnimLayers::SetUpSectionDefaults(SequencerPtr.Get(), this, CRControls.Track, NewSection,FloatChannel);
				UAnimLayers::SetUpControlRigSection(NewSection, CRControls.Controls);
				bAddedSomething = true;
			}
		}
	}

	for (FObjectAndTrack& ObjectAndTrack : SelectedBoundObjects)
	{
		FAnimLayerItem& AnimLayerItem = AnimLayerItems.FindOrAdd(ObjectAndTrack.BoundObject);
		FAnimLayerSectionItem SectionItem;
		SectionItem.AnimLayerSet.BoundObject = ObjectAndTrack.BoundObject;
		/* if we ever support channels
		for (FName& ControlName : SelectedControls)
		{
			FAnimLayerPropertyAndChannels Channels;
			Channels.Name = ControlName;
			Channels.Channels = (uint32)EControlRigContextChannelToKey::AllTransform;
			AnimLayerItem.AnimLayerSet.Names.Add(ControlName, Channels);

		}*/
		// Add a new section that starts and ends at the same time
		ObjectAndTrack.Track->Modify();
		if (UMovieSceneSection* NewSection = ObjectAndTrack.Track->CreateNewSection())
		{
			ensureAlwaysMsgf(NewSection->HasAnyFlags(RF_Transactional), TEXT("CreateNewSection must return an instance with RF_Transactional set! (pass RF_Transactional to NewObject)"));
			NewSection->SetFlags(RF_Transactional);
			FMovieSceneFloatChannel* FloatChannel = nullptr;
			if (UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(NewSection))
			{
				TransformSection->SetMask(FMovieSceneTransformMask{ EMovieSceneTransformChannel::All });
				FloatChannel = TransformSection->GetWeightChannel();
			}
			SectionItem.Section = NewSection;
			AnimLayerItem.SectionItems.Add(SectionItem);
			UAnimLayers::SetUpSectionDefaults(SequencerPtr.Get(), this, ObjectAndTrack.Track, NewSection, FloatChannel);
			bAddedSomething = true;
		}
	}
	if (bAddedSomething)
	{
		if (UAnimLayers* AnimLayers = UAnimLayers::GetAnimLayers(SequencerPtr.Get()))
		{
			AnimLayers->SetUpBaseLayerSections();
		}
		SetKeyed();
	}
	SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
	return true;
}

bool UAnimLayer::RemoveAnimLayerItem(UObject* InObject)
{
	if (FAnimLayerItem* Item =  AnimLayerItems.Find(InObject))
	{
		for (const FAnimLayerSectionItem& SectionItem : Item->SectionItems)
		{
			if (SectionItem.Section.IsValid())
			{
				if (UMovieSceneTrack* Track = SectionItem.Section->GetTypedOuter<UMovieSceneTrack>())
				{
					if (Track->GetAllSections().Find(SectionItem.Section.Get()) != 0)
					{
						Track->Modify();
						Track->RemoveSection(*(SectionItem.Section.Get()));
					}
				}
			}
		}
		AnimLayerItems.Remove(InObject);
		return true;
	}
	return false;
	
}
bool UAnimLayer::RemoveSelectedInSequencer() 
{
	TSharedPtr<ISequencer> SequencerPtr = UAnimLayers::GetSequencerFromAsset();
	if (SequencerPtr.IsValid() == false)
	{
		return false;
	}

	if (UAnimLayers* AnimLayers = UAnimLayers::GetAnimLayers(SequencerPtr.Get()))
	{
		if (AnimLayers->AnimLayers[0] == this)
		{
			return false;
		}
	}

	TArray<FControlRigAndControlsAndTrack> SelectedCRs;
	TArray<FObjectAndTrack> SelectedBoundObjects;
	GetSelectedControlRigsAndBoundObjects(SequencerPtr.Get(), SelectedCRs, SelectedBoundObjects);
	if (SelectedCRs.Num() <= 0 && SelectedBoundObjects.Num() < 0)
	{
		return false;
	}
	bool bRemovedSomething = false;
	UAnimLayer& AnimLayer = *this;
	const FScopedTransaction Transaction(LOCTEXT("RemoveSelected_Transaction", "Remove Selected"), !GIsTransacting);
	Modify();
	for (FControlRigAndControlsAndTrack& CRControls : SelectedCRs)
	{
		if (FAnimLayerItem* ExistingAnimLayerItem = AnimLayer.AnimLayerItems.Find(CRControls.ControlRig))
		{
			for (FAnimLayerSectionItem& SectionItem : ExistingAnimLayerItem->SectionItems)
			{
				if (SectionItem.Section.IsValid())
				{
					if (UMovieSceneControlRigParameterSection* CRSection = Cast<UMovieSceneControlRigParameterSection>(SectionItem.Section))
					{
						for (FName& ControlName : CRControls.Controls)
						{
							if (SectionItem.AnimLayerSet.Names.Contains(ControlName) == true)
							{
								SectionItem.AnimLayerSet.Names.Remove(ControlName);
							}
							TArray<FName> ControlNames;
							SectionItem.AnimLayerSet.Names.GetKeys(ControlNames);
							UAnimLayers::SetUpControlRigSection(CRSection, ControlNames);
							bRemovedSomething = true;
						}
					}
					if (SectionItem.AnimLayerSet.Names.Num() == 0)
					{
						AnimLayer.RemoveAnimLayerItem(CRControls.ControlRig);
						bRemovedSomething = true;
						break;
					}
				}
			}
		}
	}
	for (FObjectAndTrack& ObjectAndTrack : SelectedBoundObjects)
	{
		if (AnimLayer.AnimLayerItems.Find(ObjectAndTrack.BoundObject))
		{
			AnimLayer.RemoveAnimLayerItem(ObjectAndTrack.BoundObject);
			bRemovedSomething = true;
		}
	}
	if (bRemovedSomething)
	{
		if (UAnimLayers* AnimLayers = UAnimLayers::GetAnimLayers(SequencerPtr.Get()))
		{
			AnimLayers->SetUpBaseLayerSections();
		}
	}
	if (AnimLayer.AnimLayerItems.Num() == 0)
	{
		if (UAnimLayers* AnimLayers = UAnimLayers::GetAnimLayers(SequencerPtr.Get()))
		{
			int32 Index = AnimLayers->GetAnimLayerIndex(this);
			if (Index != INDEX_NONE)
			{
				AnimLayers->DeleteAnimLayer(SequencerPtr.Get(), Index);
			}
		}
	}
	
	SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemRemoved);
	return true;

}

void UAnimLayer::GetAnimLayerObjects(FAnimLayerObjects& InLayerObjects) const
{
	for (const TPair<TWeakObjectPtr<UObject>,FAnimLayerItem>& Pair : AnimLayerItems)
	{
		if (Pair.Key != nullptr)
		{
			for (const FAnimLayerSectionItem& SectionItem : Pair.Value.SectionItems)
			{
				if (SectionItem.Section.IsValid())
				{
					if (Pair.Key->IsA<UControlRig>())
					{
						if (UMovieSceneControlRigParameterSection* Section = Cast<UMovieSceneControlRigParameterSection>(SectionItem.Section.Get()))
						{
							FAnimLayerControlRigObject ControlRigObject;
							ControlRigObject.ControlRig = Cast<UControlRig>(Pair.Key.Get());
							for (const TPair<FName, FAnimLayerPropertyAndChannels>& ControlName : SectionItem.AnimLayerSet.Names)
							{
								ControlRigObject.ControlNames.Add(ControlName.Key);
							}
							InLayerObjects.ControlRigObjects.Add(ControlRigObject);
						}
					}
					else if (IsAccepableNonControlRigObject(Pair.Key.Get()))
					{
						FAnimLayerSceneObject SceneObject;
						SceneObject.SceneObjectOrComponent = Pair.Key;
						InLayerObjects.SceneObjects.Add(SceneObject);
					}
				}
			}
		}
	}
}

bool UAnimLayer::IsAccepableNonControlRigObject(UObject* InObject)
{
	return (InObject->IsA<AActor>() || InObject->IsA<USceneComponent>());
}

ECheckBoxState UAnimLayer::GetSelected() const
{
	FAnimLayerObjects LayerObjects;
	GetAnimLayerObjects(LayerObjects);
	TOptional<ECheckBoxState>  SelectionState;
	for (const FAnimLayerControlRigObject& ControlRigObject : LayerObjects.ControlRigObjects)
	{
		if (ControlRigObject.ControlRig.IsValid())
		{
			TArray<FName> SelectedControls = ControlRigObject.ControlRig->CurrentControlSelection();
			for (const FName& ControlName : ControlRigObject.ControlNames)
			{
				if (SelectedControls.Contains(ControlName))
				{
					if (SelectionState.IsSet() == false)
					{
						SelectionState = ECheckBoxState::Checked;
					}
					else if (SelectionState.GetValue() != ECheckBoxState::Checked)
					{
						SelectionState = ECheckBoxState::Undetermined;
					}
				}
				else
				{
					if (SelectionState.IsSet() == false)
					{
						SelectionState = ECheckBoxState::Unchecked;
					}
					else if (SelectionState.GetValue() != ECheckBoxState::Unchecked)
					{
						SelectionState = ECheckBoxState::Undetermined;
					}
				}
			}
		}
		else
		{
			if (SelectionState.IsSet())
			{
				if (SelectionState.GetValue() == ECheckBoxState::Checked)
				{
					SelectionState = ECheckBoxState::Undetermined;
				}
			}
			else
			{
				SelectionState = ECheckBoxState::Unchecked;
			}
		}
	}
	USelection* ComponentSelection = GEditor->GetSelectedComponents();
	TArray<TWeakObjectPtr<UObject>> SelectedComponents;
	ComponentSelection->GetSelectedObjects(SelectedComponents);
	USelection* ActorSelection = GEditor->GetSelectedActors();
	TArray<TWeakObjectPtr<UObject>> SelectedActors;
	ActorSelection->GetSelectedObjects(SelectedActors);

	for (const FAnimLayerSceneObject& SceneObject : LayerObjects.SceneObjects)
	{
		if (SceneObject.SceneObjectOrComponent.IsValid() &&
			SceneObject.SceneObjectOrComponent->IsA<AActor>())
		{
			if (SelectedActors.Contains(SceneObject.SceneObjectOrComponent))
			{
				if (SelectionState.IsSet() == false)
				{
					SelectionState = ECheckBoxState::Checked;
				}
				else if (SelectionState.GetValue() != ECheckBoxState::Checked)
				{
					SelectionState = ECheckBoxState::Undetermined;
				}
			}
			else
			{
				if (SelectionState.IsSet() == false)
				{
					SelectionState = ECheckBoxState::Unchecked;
				}
				else if (SelectionState.GetValue() != ECheckBoxState::Unchecked)
				{
					SelectionState = ECheckBoxState::Undetermined;
				}
			}
		}
		else if (SceneObject.SceneObjectOrComponent.IsValid() &&
			SceneObject.SceneObjectOrComponent->IsA<USceneComponent>())
		{
			if (SelectedComponents.Contains(SceneObject.SceneObjectOrComponent))
			{
				if (SelectionState.IsSet() == false)
				{
					SelectionState = ECheckBoxState::Checked;
				}
				else if (SelectionState.GetValue() != ECheckBoxState::Checked)
				{
					SelectionState = ECheckBoxState::Undetermined;
				}
			}
			else
			{
				if (SelectionState.IsSet() == false)
				{
					SelectionState = ECheckBoxState::Unchecked;
				}
				else if (SelectionState.GetValue() != ECheckBoxState::Unchecked)
				{
					SelectionState = ECheckBoxState::Undetermined;
				}
			}

		}
	}
	if (SelectionState.IsSet())
	{
		return SelectionState.GetValue();
	}
	return ECheckBoxState::Unchecked;
}

void UAnimLayer::SetSelected(bool bInSelected, bool bClearSelection)
{
	if (!GEditor)
	{
		return;
	}
	FAnimLayerObjects LayerObjects;
	GetAnimLayerObjects(LayerObjects);
	if (LayerObjects.ControlRigObjects.Num() <= 0 && LayerObjects.SceneObjects.Num() <= 0)
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("SetSelected_Transaction", "Set Selection"), !GIsTransacting);
	Modify();
	State.bSelected = bInSelected ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	if (bClearSelection)
	{
		if ((GEditor->GetSelectedActorCount() || GEditor->GetSelectedComponentCount()))
		{
			GEditor->SelectNone(false, true);
			GEditor->NoteSelectionChange();
		}

		TArray<UControlRig*> ControlRigs;
		FControlRigEditMode* ControlRigEditMode = static_cast<FControlRigEditMode*>(GLevelEditorModeTools().GetActiveMode(FControlRigEditMode::ModeName));
		if (ControlRigEditMode)
		{
			ControlRigs = ControlRigEditMode->GetControlRigsArray(false /*bIsVisible*/);
			for (UControlRig* ControlRig : ControlRigs)
			{
				ControlRig->ClearControlSelection();
			}
		}
		if (bInSelected == false)
		{
			return; //clearing and not selecting so we are done.
		}
	}

	for (const FAnimLayerSceneObject& SceneObject : LayerObjects.SceneObjects)
	{
		if (AActor* Actor = Cast<AActor>(SceneObject.SceneObjectOrComponent.Get()))
		{
			GEditor->SelectActor(Actor, bInSelected, true);
		}
		else if (UActorComponent* Component = Cast<UActorComponent>(SceneObject.SceneObjectOrComponent.Get()))
		{
			GEditor->SelectComponent(Component, bInSelected, true);
		}
	}

	for (const FAnimLayerControlRigObject& ControlRigObject : LayerObjects.ControlRigObjects)
	{
		if (ControlRigObject.ControlRig.IsValid())
		{
			for (const FName& ControlName : ControlRigObject.ControlNames)
			{
				ControlRigObject.ControlRig->SelectControl(ControlName, bInSelected);
			}
		}
	}	
}

bool UAnimLayer::GetLock() const
{
	TOptional<bool> CurrentVal;
	for (const TPair<TWeakObjectPtr<UObject>,FAnimLayerItem>& Pair : AnimLayerItems)
	{
		if (Pair.Key != nullptr)
		{
			for (const FAnimLayerSectionItem& SectionItem : Pair.Value.SectionItems)
			{
				if (SectionItem.Section.IsValid())
				{
					const bool bIsLocked = SectionItem.Section.Get()->IsLocked();
					if (CurrentVal.IsSet() && CurrentVal.GetValue() != bIsLocked)
					{
						SectionItem.Section.Get()->SetIsLocked(CurrentVal.GetValue());
					}
					if (CurrentVal.IsSet() == false)
					{
						CurrentVal = bIsLocked;
					}
				}
			}
		}
	}
	if (CurrentVal.IsSet())
	{
		State.bLock = CurrentVal.GetValue();
	}
	return State.bLock;
}

void UAnimLayer::SetLock(bool bInLock)
{
	const FScopedTransaction Transaction(LOCTEXT("SetLock_Transaction", "Set Lock"), !GIsTransacting);
	Modify();
	State.bLock = bInLock;
	for (const TPair<TWeakObjectPtr<UObject>,FAnimLayerItem>& Pair : AnimLayerItems)
	{
		if (Pair.Key != nullptr)
		{
			for (const FAnimLayerSectionItem& SectionItem : Pair.Value.SectionItems)
			{
				if (SectionItem.Section.IsValid())
				{

					SectionItem.Section->Modify();
					SectionItem.Section.Get()->SetIsLocked(State.bLock);
				}
			}
		}
	}
}

FText UAnimLayer::GetName() const
{
	return State.Name;
}

void UAnimLayer::SetName(const FText& InName)
{
	const FScopedTransaction Transaction(LOCTEXT("SetName_Transaction", "Set Name"), !GIsTransacting);
	Modify();
	State.Name = InName;

	for (const TPair<TWeakObjectPtr<UObject>,FAnimLayerItem>& Pair : AnimLayerItems)
	{
		if (Pair.Key != nullptr)
		{
			for (const FAnimLayerSectionItem& SectionItem : Pair.Value.SectionItems)
			{
				if (SectionItem.Section.IsValid())
				{
					if (UMovieSceneNameableTrack* NameableTrack = Cast<UMovieSceneNameableTrack>(SectionItem.Section.Get()->GetTypedOuter< UMovieSceneNameableTrack>()))
					{
						NameableTrack->Modify();
						NameableTrack->SetTrackRowDisplayName(State.Name, SectionItem.Section.Get()->GetRowIndex());
					}
				}
			}
		}
	}
}

double UAnimLayer::GetWeight() const
{
	TSharedPtr<ISequencer> SequencerPtr = UAnimLayers::GetSequencerFromAsset();
	if (SequencerPtr)
	{
		TOptional<float> DifferentWeightValue;
		for (const TPair<TWeakObjectPtr<UObject>,FAnimLayerItem>& Pair : AnimLayerItems)
		{
			if (Pair.Key != nullptr)
			{
				for (const FAnimLayerSectionItem& SectionItem : Pair.Value.SectionItems)
				{
					if (SectionItem.Section.IsValid())
					{
						FMovieSceneFloatChannel* FloatChannel = nullptr;
						if (UMovieSceneControlRigParameterSection* CRSection = Cast<UMovieSceneControlRigParameterSection>(SectionItem.Section.Get()))
						{
							FloatChannel = &CRSection->Weight;
						}
						else if (UMovieScene3DTransformSection* LayerSection = Cast<UMovieScene3DTransformSection>(SectionItem.Section.Get()))
						{
							FloatChannel = LayerSection->GetWeightChannel();
						}
						if (FloatChannel)
						{
							const FFrameNumber CurrentTime = SequencerPtr->GetLocalTime().Time.FloorToFrame();
							float Value = 0.f;
							FloatChannel->Evaluate(CurrentTime, Value);
							if (State.Weight != Value || WeightProxy->Weight != Value)
							{
								DifferentWeightValue = Value;
							}

						}
					}
				}
			}
		}
		if (DifferentWeightValue.IsSet())
		{
			State.Weight = DifferentWeightValue.GetValue();
			WeightProxy->Weight = State.Weight;
		}
	}
	return State.Weight;
}

static void SetFloatWeightValue(float InValue, ISequencer * Sequencer, UMovieSceneSection* OwningSection,FMovieSceneFloatChannel* Channel)
{
	using namespace UE::MovieScene;
	using namespace UE::Sequencer;


	if (!OwningSection)
	{
		return;
	}
	OwningSection->SetFlags(RF_Transactional);;

	if (!OwningSection->TryModify() || !Channel || !Sequencer)
	{
		return;
	}

	const bool  bAutoSetTrackDefaults = Sequencer->GetAutoSetTrackDefaults();	
	const FFrameNumber CurrentTime = Sequencer->GetLocalTime().Time.FloorToFrame();

	EMovieSceneKeyInterpolation Interpolation = GetInterpolationMode(Channel, CurrentTime, Sequencer->GetKeyInterpolation());

	TArray<FKeyHandle> KeysAtCurrentTime;
	Channel->GetKeys(TRange<FFrameNumber>(CurrentTime), nullptr, &KeysAtCurrentTime);

	if (KeysAtCurrentTime.Num() > 0)
	{
		AssignValue(Channel, KeysAtCurrentTime[0], InValue);
	}
	else
	{
		bool bHasAnyKeys = Channel->GetNumKeys() != 0;

		if (bHasAnyKeys || bAutoSetTrackDefaults == false)
		{
			// When auto setting track defaults are disabled, add a key even when it's empty so that the changed
			// value is saved and is propagated to the property.
			AddKeyToChannel(Channel, CurrentTime, InValue, Interpolation);
			bHasAnyKeys = Channel->GetNumKeys() != 0;
		}

		if (bHasAnyKeys)
		{
			TRange<FFrameNumber> KeyRange = TRange<FFrameNumber>(CurrentTime);
			TRange<FFrameNumber> SectionRange = OwningSection->GetRange();

			if (!SectionRange.Contains(KeyRange))
			{
				OwningSection->SetRange(TRange<FFrameNumber>::Hull(KeyRange, SectionRange));
			}
		}
	}

	// Always update the default value when auto-set default values is enabled so that the last changes
	// are always saved to the track.
	if (bAutoSetTrackDefaults)
	{
		SetChannelDefault(Channel, InValue);
	}

	Channel->AutoSetTangents();
	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
}

void UAnimLayer::SetWeight(double InWeight)
{
	State.Weight = InWeight;
	TSharedPtr<ISequencer> SequencerPtr = UAnimLayers::GetSequencerFromAsset();
	if (SequencerPtr)
	{
		for (const TPair<TWeakObjectPtr<UObject>,FAnimLayerItem>& Pair : AnimLayerItems)
		{
			if (Pair.Key != nullptr)
			{
				for (const FAnimLayerSectionItem& SectionItem : Pair.Value.SectionItems)
				{
					if (SectionItem.Section.IsValid())
					{
						if (SectionItem.Section.Get()->TryModify())
						{
							FMovieSceneFloatChannel* FloatChannel = nullptr;
							if (UMovieSceneControlRigParameterSection* CRSection = Cast<UMovieSceneControlRigParameterSection>(SectionItem.Section.Get()))
							{
								FloatChannel = &CRSection->Weight;
							}
							else if (UMovieScene3DTransformSection* LayerSection = Cast<UMovieScene3DTransformSection>(SectionItem.Section.Get()))
							{
								FloatChannel = LayerSection->GetWeightChannel();
							}
							if (FloatChannel)
							{
								float WeightValue = (float)InWeight;
								SetFloatWeightValue(WeightValue, SequencerPtr.Get(), SectionItem.Section.Get(), FloatChannel);
							}
						}
					}
				}
			}
		}
	}
}

void UAnimLayerWeightProxy::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);
#if WITH_EDITOR
	if (UAnimLayer* AnimLayer = GetTypedOuter<UAnimLayer>())
	{
		if (PropertyChangedEvent.Property && (
			PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet ||
			PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive ||
			PropertyChangedEvent.ChangeType == EPropertyChangeType::Unspecified)
			)
		{
			//set values
			FProperty* Property = PropertyChangedEvent.Property;
			FProperty* MemberProperty = nullptr;
			if ((Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimLayerWeightProxy, Weight)) ||
				(MemberProperty && MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimLayerWeightProxy, Weight)))
			{
				Modify();
				AnimLayer->SetWeight(Weight);
			}
		}
	}
#endif
}

#if WITH_EDITOR
void UAnimLayerWeightProxy::PostEditUndo()
{
	if (UAnimLayer* AnimLayer = GetTypedOuter<UAnimLayer>())
	{
		AnimLayer->SetWeight(Weight);
	}
}
#endif
EAnimLayerType UAnimLayer::GetType() const
{
	TOptional<EMovieSceneBlendType> CurrentVal;
	for (const TPair<TWeakObjectPtr<UObject>,FAnimLayerItem>& Pair : AnimLayerItems)
	{
		if (Pair.Key != nullptr)
		{
			for (const FAnimLayerSectionItem& SectionItem : Pair.Value.SectionItems)
			{
				if (SectionItem.Section.IsValid())
				{
					const EMovieSceneBlendType BlendType = SectionItem.Section.Get()->GetBlendType().BlendType;
					if (CurrentVal.IsSet() && CurrentVal.GetValue() != BlendType)
					{
						SectionItem.Section.Get()->SetBlendType(CurrentVal.GetValue());
					}
					if (CurrentVal.IsSet() == false)
					{
						CurrentVal = BlendType;
					}
				}
			}
		}
	}
	if (CurrentVal.IsSet())
	{
		switch (CurrentVal.GetValue())
		{
		case EMovieSceneBlendType::Additive:
			State.Type = (int32)EAnimLayerType::Additive;
			break;
		case EMovieSceneBlendType::Override:
			State.Type = (int32)EAnimLayerType::Override;
			break;
		case EMovieSceneBlendType::Absolute:
			State.Type = (int32)EAnimLayerType::Base;
			break;
		}
	}

	return (EAnimLayerType)(State.Type);
}

static void SetDefaultsForOverride(UMovieSceneSection* InSection)
{
	if (InSection->IsA<UMovieSceneControlRigParameterSection>())
	{
		return; //control rig sections already handle this
	}
	TSharedPtr<ISequencer> SequencerPtr = UAnimLayers::GetSequencerFromAsset();
	if (SequencerPtr.IsValid() == false)
	{
		return;
	}
	FFrameNumber FrameNumber = SequencerPtr->GetLocalTime().Time.GetFrame();
	if (UMovieSceneTrack* OwnerTrack = InSection->GetTypedOuter<UMovieSceneTrack>())
	{
		TArray<UMovieSceneSection*> TrackSections = OwnerTrack->GetAllSections();
		int32 SectionIndex = TrackSections.Find((InSection));
		if (SectionIndex != INDEX_NONE)
		{
			InSection->Modify();
			TrackSections.SetNum(SectionIndex); //this will gives us up to the section 
			TArray<UMovieSceneSection*> Sections;
			TArray<UMovieSceneSection*> AbsoluteSections;
			MovieSceneToolHelpers::SplitSectionsByBlendType(EMovieSceneBlendType::Absolute, TrackSections, Sections, AbsoluteSections);
			TArrayView<FMovieSceneFloatChannel*> BaseFloatChannels = InSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
			TArrayView<FMovieSceneDoubleChannel*> BaseDoubleChannels = InSection->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
			if (BaseDoubleChannels.Num() > 0)
			{
				int32 NumChannels = BaseDoubleChannels.Num();
				const int32 StartIndex = 0;
				const int32 EndIndex = NumChannels - 1;
				TArray<double> ChannelValues = MovieSceneToolHelpers::GetChannelValues<FMovieSceneDoubleChannel,
					double>(StartIndex,EndIndex, Sections, AbsoluteSections, FrameNumber);
				for (int32 Index = 0; Index < NumChannels; ++Index)
				{
					FMovieSceneDoubleChannel* DoubleChannel = BaseDoubleChannels[Index];
					const double Value = ChannelValues[Index];
					DoubleChannel->SetDefault(Value);
				}
			}
			else if (BaseFloatChannels.Num() > 0)
			{
				int32 NumChannels = BaseFloatChannels.Num();
				const int32 StartIndex = 0;
				const int32 EndIndex = NumChannels - 1;
				TArray<float> ChannelValues = MovieSceneToolHelpers::GetChannelValues<FMovieSceneFloatChannel,
					float>(StartIndex, EndIndex, Sections, AbsoluteSections, FrameNumber);
				for (int32 Index = 0; Index < NumChannels; ++Index)
				{
					FMovieSceneFloatChannel* FloatChannel = BaseFloatChannels[Index];
					const float Value = ChannelValues[Index];
					FloatChannel->SetDefault(Value);
				}
			}
			SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
		}
	}
}

void UAnimLayer::SetType(EAnimLayerType LayerType)
{
	const FScopedTransaction Transaction(LOCTEXT("SetType_Transaction", "Set Type"), !GIsTransacting);
	Modify();

	State.Type = (uint32)(LayerType);
	for (const TPair<TWeakObjectPtr<UObject>,FAnimLayerItem>& Pair : AnimLayerItems)
	{
		if (Pair.Key != nullptr)
		{
			for (const FAnimLayerSectionItem& SectionItem : Pair.Value.SectionItems)
			{
				if (SectionItem.Section.IsValid())
				{
					switch (LayerType)
					{
					case EAnimLayerType::Additive:
						SectionItem.Section.Get()->SetBlendType(EMovieSceneBlendType::Additive);
						break;
					case EAnimLayerType::Override:
						SectionItem.Section.Get()->SetBlendType(EMovieSceneBlendType::Override);
						SetDefaultsForOverride(SectionItem.Section.Get());
						break;
					case EAnimLayerType::Base:
						SectionItem.Section.Get()->SetBlendType(EMovieSceneBlendType::Absolute);
						break;
					}
				}
			}
		}
	}
}

UAnimLayers::UAnimLayers(class FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

#if WITH_EDITOR
void UAnimLayers::PostEditUndo()
{
	AnimLayerListChangedBroadcast();
}
#endif

TSharedPtr<ISequencer> UAnimLayers::GetSequencerFromAsset()
{
	ULevelSequence* LevelSequence = ULevelSequenceEditorBlueprintLibrary::GetCurrentLevelSequence();
	IAssetEditorInstance* AssetEditor = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(LevelSequence, false);
	ILevelSequenceEditorToolkit* LevelSequenceEditor = static_cast<ILevelSequenceEditorToolkit*>(AssetEditor);
	TSharedPtr<ISequencer> SequencerPtr = LevelSequenceEditor ? LevelSequenceEditor->GetSequencer() : nullptr;
	return SequencerPtr;
}

void UAnimLayers::AddBaseLayer()
{
	UAnimLayer* AnimLayer = NewObject<UAnimLayer>(this, TEXT("BaseLayer"), RF_Transactional);
	AnimLayer->State.Type = EAnimLayerType::Base;
	AnimLayer->State.bKeyed = ECheckBoxState::Checked;
	AnimLayers.Add(AnimLayer);
}

UAnimLayers* UAnimLayers::GetAnimLayers(ISequencer* SequencerPtr)
{
	if (!SequencerPtr)
	{
		return nullptr;
	}
	ULevelSequence* LevelSequence = Cast<ULevelSequence>(SequencerPtr->GetFocusedMovieSceneSequence());
	return UAnimLayers::GetAnimLayers(LevelSequence);
}

UAnimLayers* UAnimLayers::GetAnimLayers(ULevelSequence* LevelSequence)
{
	if(!LevelSequence)
	{
		return nullptr;
	}
	if (LevelSequence && LevelSequence->GetClass()->ImplementsInterface(UInterface_AssetUserData::StaticClass()))
	{
		if (IInterface_AssetUserData* AssetUserDataInterface = Cast< IInterface_AssetUserData >(LevelSequence))
		{
			UAnimLayers* AnimLayers = AssetUserDataInterface->GetAssetUserData< UAnimLayers >();
			if (!AnimLayers)
			{
				AnimLayers = NewObject<UAnimLayers>(LevelSequence, NAME_None, RF_Public | RF_Transactional);
				AssetUserDataInterface->AddAssetUserData(AnimLayers);
			}
			return AnimLayers;
		}
	}
	return nullptr;
}

int32 UAnimLayers::GetAnimLayerIndex(UAnimLayer* InAnimLayer) const
{
	if (InAnimLayer != nullptr)
	{
		return AnimLayers.Find(InAnimLayer);
	}
	return INDEX_NONE;
}

bool UAnimLayers::DeleteAnimLayer(ISequencer* SequencerPtr, int32 Index)
{
	if (Index >= 1 && Index < AnimLayers.Num()) //don't delete base
	{
		if (UAnimLayer* AnimLayer = AnimLayers[Index])
		{
			const FScopedTransaction Transaction(LOCTEXT("DeleteAnimLayer_Transaction", "Delete Anim Layer"), !GIsTransacting);
			Modify();
			for (const TPair<TWeakObjectPtr<UObject>, FAnimLayerItem>& Pair : AnimLayer->AnimLayerItems)
			{
				if (Pair.Key != nullptr)
				{
					for (const FAnimLayerSectionItem& SectionItem : Pair.Value.SectionItems)
					{
						if (SectionItem.Section.IsValid())
						{
							if (UMovieSceneTrack* Track = SectionItem.Section->GetTypedOuter<UMovieSceneTrack>())
							{
								if (Track->GetAllSections().Find(SectionItem.Section.Get()) != 0)
								{
									Track->Modify();
									Track->RemoveSection(*(SectionItem.Section.Get()));
								}
							}
						}
					}
				}
			}
			AnimLayers.RemoveAt(Index);
			SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemRemoved);
		}
		if (UAnimLayer* BaseAnimLayer = AnimLayers[0]) //set base as keyed
		{
			BaseAnimLayer->SetKeyed();
		}
		AnimLayerListChangedBroadcast();
	}
	else
	{
		return false;
	}
	return true;
}

static void CopySectionIntoAnother(UMovieSceneSection* ToSection, UMovieSceneSection* FromSection)
{
	const FFrameNumber Min = TNumericLimits<FFrameNumber>::Lowest();
	const FFrameNumber Max = TNumericLimits<FFrameNumber>::Max();
	TRange<FFrameNumber> Range(Min, Max);
	TArray<UMovieSceneSection*> AbsoluteSections;
	TArray<UMovieSceneSection*> AdditiveSections;

	AdditiveSections.Add(ToSection);
	AdditiveSections.Add(FromSection);
	

	FMovieSceneChannelProxy& ChannelProxy = ToSection->GetChannelProxy();
	for (const FMovieSceneChannelEntry& Entry : ToSection->GetChannelProxy().GetAllEntries())
	{
		const FName ChannelTypeName = Entry.GetChannelTypeName();

		if (ChannelTypeName == FMovieSceneFloatChannel::StaticStruct()->GetFName())
		{
			TArrayView<FMovieSceneFloatChannel*> BaseFloatChannels = ToSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
			const int StartIndex = 0;
			const int EndIndex = BaseFloatChannels.Num() - 1;
			MovieSceneToolHelpers::MergeSections<FMovieSceneFloatChannel>(ToSection, AbsoluteSections, AdditiveSections, 
				StartIndex, EndIndex, Range);
		}
		else if (ChannelTypeName == FMovieSceneDoubleChannel::StaticStruct()->GetFName())
		{
			TArrayView<FMovieSceneDoubleChannel*> BaseDoubleChannels = ToSection->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
			const int StartIndex = 0;
			const int EndIndex = BaseDoubleChannels.Num() - 1;
			MovieSceneToolHelpers::MergeSections<FMovieSceneDoubleChannel>(ToSection, AbsoluteSections, AdditiveSections, 
				StartIndex, EndIndex, Range);
		}
	}
}

int32 UAnimLayers::DuplicateAnimLayer(ISequencer* SequencerPtr, int32 Index)
{
	int32 NewIndex = INDEX_NONE;
	if (Index >= 1 && Index < AnimLayers.Num()) //don't duplicate base
	{
		if (UAnimLayer* ExistingAnimLayer = AnimLayers[Index])
		{
			if (ExistingAnimLayer->AnimLayerItems.Num() == 0)
			{
				UE_LOG(LogControlRig, Error, TEXT("Anim Layers: Can not duplicate empty layer"));
				return INDEX_NONE;
			}
			const FScopedTransaction Transaction(LOCTEXT("DuplicateAnimLayer_Transaction", "Duplicate Anim Layer"), !GIsTransacting);
			Modify();
			UAnimLayer* NewAnimLayer = NewObject<UAnimLayer>(this, NAME_None, RF_Transactional);
			NewAnimLayer->SetType(ExistingAnimLayer->GetType());
			bool bItemAdded = false;
			for (const TPair<TWeakObjectPtr<UObject>,FAnimLayerItem>& Pair : ExistingAnimLayer->AnimLayerItems)
			{
				if (Pair.Key != nullptr)
				{
					for (const FAnimLayerSectionItem& SectionItem : Pair.Value.SectionItems)
					{
						if (SectionItem.Section.IsValid())
						{
							if (UMovieSceneTrack* Track = SectionItem.Section->GetTypedOuter<UMovieSceneTrack>())
							{
								Track->Modify();
								if (UMovieSceneControlRigParameterSection* Section = Cast<UMovieSceneControlRigParameterSection>(SectionItem.Section.Get()))
								{
									if (UControlRig* ControlRig = Cast<UControlRig>(Pair.Key))
									{
										FAnimLayerItem AnimLayerItem;
										FAnimLayerSectionItem NewSectionItem;
										NewSectionItem.AnimLayerSet.BoundObject = ControlRig;
										NewSectionItem.AnimLayerSet = SectionItem.AnimLayerSet;
										// Add a new section that starts and ends at the same time
										if (UMovieSceneControlRigParameterTrack* CRTack = Cast<UMovieSceneControlRigParameterTrack>(Track))
										{
											TGuardValue<bool> GuardSetSection(CRTack->bSetSectionToKeyPerControl, false);
											if (UMovieSceneControlRigParameterSection* NewSection = Cast<UMovieSceneControlRigParameterSection>(Track->CreateNewSection()))
											{
												if (bItemAdded == false)
												{
													NewAnimLayer->State.Weight = 1.0;
													NewAnimLayer->State.Type = (EAnimLayerType::Additive);
													bItemAdded = true;
												}
												ensureAlwaysMsgf(NewSection->HasAnyFlags(RF_Transactional), TEXT("CreateNewSection must return an instance with RF_Transactional set! (pass RF_Transactional to NewObject)"));
												NewSection->SetFlags(RF_Transactional);
												NewSection->SetTransformMask(FMovieSceneTransformMask{ EMovieSceneTransformChannel::All });
												FMovieSceneFloatChannel* FloatChannel = &NewSection->Weight;
												NewSectionItem.Section = NewSection;
												AnimLayerItem.SectionItems.Add(NewSectionItem);
												NewAnimLayer->AnimLayerItems.Add(ControlRig, AnimLayerItem);
												SetUpSectionDefaults(SequencerPtr, NewAnimLayer, Track, NewSection, FloatChannel);
												NewSection->SetBlendType(Section->GetBlendType().Get());
												TArray<FName> ControlNames;
												NewSectionItem.AnimLayerSet.Names.GetKeys(ControlNames);
												SetUpControlRigSection(NewSection, ControlNames);
												//current copy keys
												CopySectionIntoAnother(NewSection, Section);
											}
										}
									}
								}
								else
								{
									FAnimLayerItem AnimLayerItem;
									FAnimLayerSectionItem NewSectionItem;
									NewSectionItem.AnimLayerSet.BoundObject = Pair.Key;
									if (UMovieSceneSection* NewSection = Track->CreateNewSection())
									{
										if (bItemAdded == false)
										{
											NewAnimLayer->State.Weight = 1.0;
											NewAnimLayer->State.Type = (EAnimLayerType::Additive);
											bItemAdded = true;
										}

										ensureAlwaysMsgf(NewSection->HasAnyFlags(RF_Transactional), TEXT("CreateNewSection must return an instance with RF_Transactional set! (pass RF_Transactional to NewObject)"));
										NewSection->SetFlags(RF_Transactional);
										FMovieSceneFloatChannel* FloatChannel = nullptr;
										if (UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(NewSection))
										{
											TransformSection->SetMask(FMovieSceneTransformMask{ EMovieSceneTransformChannel::All });
											FloatChannel = TransformSection->GetWeightChannel();
										}
										NewSectionItem.Section = NewSection;
										AnimLayerItem.SectionItems.Add(NewSectionItem);
										NewAnimLayer->AnimLayerItems.Add(Pair.Key, AnimLayerItem);
										SetUpSectionDefaults(SequencerPtr, NewAnimLayer, Track, NewSection, FloatChannel);
										NewSection->SetBlendType(SectionItem.Section->GetBlendType().Get());
										//current copy keys
										CopySectionIntoAnother(NewSection, SectionItem.Section.Get());
									}
								}
							}
						}
					}
				}
			}
			if (bItemAdded)
			{
				FString ExistingName = ExistingAnimLayer->GetName().ToString();
				FString NewLayerName = FString::Printf(TEXT("%s_Duplicate"), *ExistingName);

				FText LayerText;
				LayerText = LayerText.FromString(NewLayerName);
				NewAnimLayer->SetName(LayerText); //need items/sections to be added so we can change their track row names
				NewIndex = AnimLayers.Add(NewAnimLayer);
				NewAnimLayer->SetKeyed();
				AnimLayerListChangedBroadcast();
				// no need to since it's a dup SetUpBaseLayerSections();
			}
		}
	}
	SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
	return NewIndex;
}

static void AddNamesToMask(FAnimLayerSectionItem* Owner, UMovieSceneControlRigParameterSection* CRSection,const FAnimLayerSelectionSet& NewSet)
{
	bool bNameAdded = false;
	for (TPair<FName, FAnimLayerPropertyAndChannels> NameAndChannels : NewSet.Names)
	{
		if (Owner->AnimLayerSet.Names.Contains(NameAndChannels.Key) == false)
		{
			bNameAdded = true;
			Owner->AnimLayerSet.Names.Add(NameAndChannels);
		}
	}
	if (bNameAdded)
	{
		TArray<FName> AllControls;
		Owner->AnimLayerSet.Names.GenerateKeyArray(AllControls);
		UAnimLayers::SetUpControlRigSection(CRSection, AllControls);
	}
}

FAnimLayerSectionItem* FAnimLayerItem::FindMatchingSectionItem(UMovieSceneSection* InMovieSceneSection)
{
	if (InMovieSceneSection)
	{
		for (FAnimLayerSectionItem& CurrentItem : SectionItems)
		{
			if (CurrentItem.Section.IsValid())
			{
				UMovieSceneTrack* InTrack = InMovieSceneSection->GetTypedOuter<UMovieSceneTrack>();
				UMovieSceneTrack* CurrentTrack = CurrentItem.Section->GetTypedOuter<UMovieSceneTrack>();
				if (CurrentTrack && InTrack && CurrentTrack == InTrack)
				{
					return &CurrentItem;
				}
			}
		}
	}
	return nullptr;
}

template<typename ChannelType>
void AddKeyToChannel(ChannelType* Channel, EMovieSceneKeyInterpolation DefaultInterpolation, const FFrameNumber& FrameNumber, double Value)
{
	switch (DefaultInterpolation)
	{
	case EMovieSceneKeyInterpolation::Linear:
		Channel->AddLinearKey(FrameNumber, Value);
		break;
	case EMovieSceneKeyInterpolation::Constant:
		Channel->AddConstantKey(FrameNumber, Value);
		break;
	case  EMovieSceneKeyInterpolation::Auto:
		Channel->AddCubicKey(FrameNumber, Value, ERichCurveTangentMode::RCTM_Auto);
		break;
	case  EMovieSceneKeyInterpolation::SmartAuto:
	default:
		Channel->AddCubicKey(FrameNumber, Value, ERichCurveTangentMode::RCTM_SmartAuto);
		break;
	}
}

bool UAnimLayers::SetPassthroughKey(ISequencer* InSequencer, int32 InIndex)
{
	if (InSequencer == nullptr || InIndex <= 0 || InIndex >= AnimLayers.Num())
	{
		return false;
	}
	FFrameNumber FrameNumber = InSequencer->GetLocalTime().Time.GetFrame();
	EMovieSceneKeyInterpolation DefaultInterpolation = InSequencer->GetKeyInterpolation();
	if (UAnimLayer* AnimLayer = AnimLayers[InIndex].Get())
	{
		for (TPair<TWeakObjectPtr<UObject>, FAnimLayerItem>& Pair : AnimLayer->AnimLayerItems)
		{
			if (Pair.Key != nullptr)
			{
				for (FAnimLayerSectionItem& SectionItem : Pair.Value.SectionItems)
				{
					if (SectionItem.Section.IsValid())
					{
						if (UMovieSceneTrack* OwnerTrack = SectionItem.Section->GetTypedOuter<UMovieSceneTrack>())
						{
							TArray<UMovieSceneSection*> TrackSections = OwnerTrack->GetAllSections();
							int32 SectionIndex = TrackSections.Find((SectionItem.Section.Get()));
							if (SectionIndex != INDEX_NONE)
							{
								const FScopedTransaction Transaction(LOCTEXT("SetPassthroughKey_Transaction", "Set Passthrough Key"), !GIsTransacting);
								SectionItem.Section->Modify();
								TrackSections.SetNum(SectionIndex); //this will gives us up to the section 
								TArray<UMovieSceneSection*> Sections;
								TArray<UMovieSceneSection*> AbsoluteSections;
								MovieSceneToolHelpers::SplitSectionsByBlendType(EMovieSceneBlendType::Absolute, TrackSections, Sections, AbsoluteSections);
								TArrayView<FMovieSceneFloatChannel*> BaseFloatChannels = SectionItem.Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
								TArrayView<FMovieSceneDoubleChannel*> BaseDoubleChannels = SectionItem.Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
								if (BaseDoubleChannels.Num() > 0)
								{
									if (SectionItem.Section->GetBlendType().IsValid() &&
										SectionItem.Section->GetBlendType() == EMovieSceneBlendType::Additive)
									{
										const double Value = 0.0;
										for (FMovieSceneDoubleChannel* DoubleChannel : BaseDoubleChannels)
										{
											AddKeyToChannel(DoubleChannel, FrameNumber, Value, DefaultInterpolation);
										}
									}
									else if (SectionItem.Section->GetBlendType().IsValid() &&
										SectionItem.Section->GetBlendType() == EMovieSceneBlendType::Override)
									{
										int32 NumChannels = BaseDoubleChannels.Num();
										const int32 StartIndex = 0;
										const int32 EndIndex = NumChannels - 1;
										TArray<double> ChannelValues = MovieSceneToolHelpers::GetChannelValues<FMovieSceneDoubleChannel,
											double>(StartIndex, EndIndex, Sections,AbsoluteSections,FrameNumber);
										for (int32 Index = 0; Index < NumChannels; ++Index)
										{
											FMovieSceneDoubleChannel* DoubleChannel = BaseDoubleChannels[Index];
											const double Value = ChannelValues[Index];
											AddKeyToChannel(DoubleChannel, FrameNumber, Value, DefaultInterpolation);
										}
									}
								}
								else if (BaseFloatChannels.Num() > 0)
								{
									int32 NumChannels = SectionItem.Section->IsA<UMovieSceneControlRigParameterSection>() ?
										BaseFloatChannels.Num() - 1 : BaseFloatChannels.Num();// skip weight if CR section
									const int32 StartIndex = 0;
									const int32 EndIndex = NumChannels - 1;
									if (SectionItem.Section->GetBlendType().IsValid() &&
										SectionItem.Section->GetBlendType() == EMovieSceneBlendType::Additive)
									{
										const float Value = 0.0;
										for (int32 Index = 0; Index < NumChannels; ++Index)
										{
											FMovieSceneFloatChannel* FloatChannel = BaseFloatChannels[Index];
											AddKeyToChannel(FloatChannel, FrameNumber, Value, DefaultInterpolation);
										}
									}
									else if (SectionItem.Section->GetBlendType().IsValid() &&
										SectionItem.Section->GetBlendType() == EMovieSceneBlendType::Override)
									{
										TArray<float> ChannelValues = MovieSceneToolHelpers::GetChannelValues<FMovieSceneFloatChannel,
											float>(StartIndex,EndIndex, Sections,AbsoluteSections, FrameNumber);
										for (int32 Index = 0; Index < NumChannels; ++Index)
										{
											FMovieSceneFloatChannel* FloatChannel = BaseFloatChannels[Index];
											const float Value = ChannelValues[Index];
											AddKeyToChannel(FloatChannel, FrameNumber, Value, DefaultInterpolation);
										}
									}
								}
								InSequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
							}
						}
						else
						{
							return false;
						}
					}
				}
			}
		}
	}
	else
	{
		return false;
	}
	return true;
}

static void MergeControlRigSections(UMovieSceneControlRigParameterSection* BaseSection, UMovieSceneControlRigParameterSection* Section, const TRange<FFrameNumber>& Range)
{
	if (!BaseSection || !Section)
	{
		return;
	}
	TArrayView<FMovieSceneFloatChannel*> BaseFloatChannels = BaseSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	if (BaseFloatChannels.Num() > 0)
	{
		//need to go through each control and merge that
		TArray<FRigControlElement*> Controls;
		UControlRig* ControlRig = BaseSection->GetControlRig();
		if (ControlRig == nullptr)
		{
			return;
		}
		ControlRig->GetControlsInOrder(Controls);
		URigHierarchy* Hierarchy = ControlRig->GetHierarchy();
		if (Hierarchy == nullptr)
		{
			return;
		}

		for (int32 LocalControlIndex = 0; LocalControlIndex < Controls.Num(); ++LocalControlIndex)
		{
			FRigControlElement* ControlElement = Controls[LocalControlIndex];
			check(ControlElement);
			if (!Hierarchy->IsAnimatable(ControlElement))
			{
				continue;
			}

			if (FChannelMapInfo* pChannelIndex = BaseSection->ControlChannelMap.Find(ControlElement->GetFName()))
			{
				const int32 ChannelIndex = pChannelIndex->ChannelIndex;
				bool bMaskKeyOut = (BaseSection->GetControlNameMask(ControlElement->GetFName()) == false)
					|| (Section->GetControlNameMask(ControlElement->GetFName()) == false);

				if (bMaskKeyOut)
				{
					continue;
				}

				switch (ControlElement->Settings.ControlType)
				{

					case ERigControlType::Float:
					case ERigControlType::ScaleFloat:
					{
						int32 StartIndex = ChannelIndex;
						int32 EndIndex = ChannelIndex;
						MovieSceneToolHelpers::MergeSections<FMovieSceneFloatChannel>(BaseSection,
							Section, StartIndex, EndIndex, Range);
						break;
					}
					case ERigControlType::Vector2D:
					{
						int32 StartIndex = ChannelIndex;
						int32 EndIndex = ChannelIndex + 1;
						MovieSceneToolHelpers::MergeSections<FMovieSceneFloatChannel>(BaseSection,
							Section, StartIndex, EndIndex, Range);
						break;
					}
					case ERigControlType::Position:
					case ERigControlType::Scale:
					case ERigControlType::Rotator:
					{
						int32 StartIndex = ChannelIndex;
						int32 EndIndex = ChannelIndex + 2;
						MovieSceneToolHelpers::MergeSections<FMovieSceneFloatChannel>(BaseSection,
							Section, StartIndex, EndIndex, Range);
						break;
					}

					case ERigControlType::Transform:
					case ERigControlType::TransformNoScale:
					case ERigControlType::EulerTransform:
					{
						EMovieSceneTransformChannel BaseChannelMask = BaseSection->GetTransformMask().GetChannels();
						EMovieSceneTransformChannel ChannelMask = Section->GetTransformMask().GetChannels();

						const bool bDoAllTransform = EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::AllTransform);
						if (bDoAllTransform)
						{
							if (ControlElement->Settings.ControlType == ERigControlType::TransformNoScale)
							{
								int32 StartIndex = ChannelIndex;
								int32 EndIndex = ChannelIndex + 5;
								MovieSceneToolHelpers::MergeSections<FMovieSceneFloatChannel>(BaseSection,
									Section, StartIndex, EndIndex, Range);
							}
							else
							{
								int32 StartIndex = ChannelIndex;
								int32 EndIndex = ChannelIndex + 8;
								MovieSceneToolHelpers::MergeSections<FMovieSceneFloatChannel>(BaseSection,
									Section, StartIndex, EndIndex, Range);
							}
						}
						else
						{
							if (EnumHasAllFlags(BaseChannelMask, EMovieSceneTransformChannel::TranslationX)
								 && EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::TranslationX))
							{
								int32 StartIndex = ChannelIndex;
								int32 EndIndex = StartIndex;
								MovieSceneToolHelpers::MergeSections<FMovieSceneFloatChannel>(BaseSection,
									Section, StartIndex, EndIndex, Range);
							}
							if (EnumHasAllFlags(BaseChannelMask, EMovieSceneTransformChannel::TranslationY)
								&& EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::TranslationY))
							{
								int32 StartIndex = ChannelIndex + 1;
								int32 EndIndex = StartIndex;
								MovieSceneToolHelpers::MergeSections<FMovieSceneFloatChannel>(BaseSection,
									Section, StartIndex, EndIndex, Range);
							}
							if (EnumHasAllFlags(BaseChannelMask, EMovieSceneTransformChannel::TranslationZ)
								&& EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::TranslationZ))
							{
								int32 StartIndex = ChannelIndex + 2;
								int32 EndIndex = StartIndex;
								MovieSceneToolHelpers::MergeSections<FMovieSceneFloatChannel>(BaseSection,
									Section, StartIndex, EndIndex, Range);
							}
							if (EnumHasAllFlags(BaseChannelMask, EMovieSceneTransformChannel::RotationX)
								&& EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::RotationX))
							{
								int32 StartIndex = ChannelIndex + 3;
								int32 EndIndex = StartIndex;
								MovieSceneToolHelpers::MergeSections<FMovieSceneFloatChannel>(BaseSection,
									Section, StartIndex, EndIndex, Range);
							}
							if (EnumHasAllFlags(BaseChannelMask, EMovieSceneTransformChannel::RotationY)
								&& EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::RotationY))
							{
								int32 StartIndex = ChannelIndex + 4;
								int32 EndIndex = StartIndex;
								MovieSceneToolHelpers::MergeSections<FMovieSceneFloatChannel>(BaseSection,
									Section, StartIndex, EndIndex, Range);
							}
							if (EnumHasAllFlags(BaseChannelMask, EMovieSceneTransformChannel::RotationZ)
								&& EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::RotationZ))
							{
								int32 StartIndex = ChannelIndex + 5;
								int32 EndIndex = StartIndex;
								MovieSceneToolHelpers::MergeSections<FMovieSceneFloatChannel>(BaseSection,
									Section, StartIndex, EndIndex, Range);
							}
							if (ControlElement->Settings.ControlType != ERigControlType::TransformNoScale)
							{
								if (EnumHasAllFlags(BaseChannelMask, EMovieSceneTransformChannel::ScaleX)
									&& EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::ScaleX))
								{
									int32 StartIndex = ChannelIndex + 6;
									int32 EndIndex = StartIndex;
									MovieSceneToolHelpers::MergeSections<FMovieSceneFloatChannel>(BaseSection,
										Section, StartIndex, EndIndex, Range);
								}
								if (EnumHasAllFlags(BaseChannelMask, EMovieSceneTransformChannel::ScaleY)
									&& EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::ScaleY))
								{
									int32 StartIndex = ChannelIndex + 7;
									int32 EndIndex = StartIndex;
									MovieSceneToolHelpers::MergeSections<FMovieSceneFloatChannel>(BaseSection,
										Section, StartIndex, EndIndex, Range);
								}
								if (EnumHasAllFlags(BaseChannelMask, EMovieSceneTransformChannel::ScaleZ)
									&& EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::ScaleZ))
								{
									int32 StartIndex = ChannelIndex + 8;
									int32 EndIndex = StartIndex;
									MovieSceneToolHelpers::MergeSections<FMovieSceneFloatChannel>(BaseSection,
										Section, StartIndex, EndIndex, Range);
								}
							}
						}
						break;
					}
					default:
						break;
				}
			}
		}
	}
}

static void MergeTransformSections(UMovieScene3DTransformSection* BaseSection, UMovieScene3DTransformSection* Section, const TRange<FFrameNumber>& Range)
{
	if (!BaseSection || !Section)
	{
		return;
	}
	TArrayView<FMovieSceneDoubleChannel*> BaseDoubleChannels = BaseSection->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
	if (BaseDoubleChannels.Num() > 0)
	{
		EMovieSceneTransformChannel BaseChannelMask = BaseSection->GetMask().GetChannels();
		EMovieSceneTransformChannel ChannelMask = Section->GetMask().GetChannels();

		const bool bDoAllTransform = EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::AllTransform);
		if (bDoAllTransform)
		{
			int32 StartIndex = 0;
			int32 EndIndex = BaseDoubleChannels.Num() - 1;
			MovieSceneToolHelpers::MergeSections<FMovieSceneDoubleChannel>(BaseSection,
				Section, StartIndex, EndIndex, Range);
		}
		else
		{
			const int32 ChannelIndex = 0;
			if (EnumHasAllFlags(BaseChannelMask, EMovieSceneTransformChannel::TranslationX)
				&& EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::TranslationX))
			{
				int32 StartIndex = ChannelIndex;
				int32 EndIndex = StartIndex;
				MovieSceneToolHelpers::MergeSections<FMovieSceneDoubleChannel>(BaseSection,
					Section, StartIndex, EndIndex, Range);
			}
			if (EnumHasAllFlags(BaseChannelMask, EMovieSceneTransformChannel::TranslationY)
				&& EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::TranslationY))
			{
				int32 StartIndex = ChannelIndex + 1;
				int32 EndIndex = StartIndex;
				MovieSceneToolHelpers::MergeSections<FMovieSceneDoubleChannel>(BaseSection,
					Section, StartIndex, EndIndex, Range);
			}
			if (EnumHasAllFlags(BaseChannelMask, EMovieSceneTransformChannel::TranslationZ)
				&& EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::TranslationZ))
			{
				int32 StartIndex = ChannelIndex + 2;
				int32 EndIndex = StartIndex;
				MovieSceneToolHelpers::MergeSections<FMovieSceneDoubleChannel>(BaseSection,
					Section, StartIndex, EndIndex, Range);
			}
			if (EnumHasAllFlags(BaseChannelMask, EMovieSceneTransformChannel::RotationX)
				&& EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::RotationX))
			{
				int32 StartIndex = ChannelIndex + 3;
				int32 EndIndex = StartIndex;
				MovieSceneToolHelpers::MergeSections<FMovieSceneDoubleChannel>(BaseSection,
					Section, StartIndex, EndIndex, Range);
			}
			if (EnumHasAllFlags(BaseChannelMask, EMovieSceneTransformChannel::RotationY)
				&& EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::RotationY))
			{
				int32 StartIndex = ChannelIndex + 4;
				int32 EndIndex = StartIndex;
				MovieSceneToolHelpers::MergeSections<FMovieSceneDoubleChannel>(BaseSection,
					Section, StartIndex, EndIndex, Range);
			}
			if (EnumHasAllFlags(BaseChannelMask, EMovieSceneTransformChannel::RotationZ)
				&& EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::RotationZ))
			{
				int32 StartIndex = ChannelIndex +  5;
				int32 EndIndex = StartIndex;
				MovieSceneToolHelpers::MergeSections<FMovieSceneDoubleChannel>(BaseSection,
					Section, StartIndex, EndIndex, Range);
			}
			if (EnumHasAllFlags(BaseChannelMask, EMovieSceneTransformChannel::ScaleX)
				&& EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::ScaleX))
			{
				int32 StartIndex = ChannelIndex + 6;
				int32 EndIndex = StartIndex;
				MovieSceneToolHelpers::MergeSections<FMovieSceneFloatChannel>(BaseSection,
					Section, StartIndex, EndIndex, Range);
			}
			if (EnumHasAllFlags(BaseChannelMask, EMovieSceneTransformChannel::ScaleY)
				&& EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::ScaleY))
			{
				int32 StartIndex = ChannelIndex + 7;
				int32 EndIndex = StartIndex;
				MovieSceneToolHelpers::MergeSections<FMovieSceneDoubleChannel>(BaseSection,
					Section, StartIndex, EndIndex, Range);
			}
			if (EnumHasAllFlags(BaseChannelMask, EMovieSceneTransformChannel::ScaleZ)
				&& EnumHasAllFlags(ChannelMask, EMovieSceneTransformChannel::ScaleZ))
			{
				int32 StartIndex = ChannelIndex + 8;
				int32 EndIndex = StartIndex;
				MovieSceneToolHelpers::MergeSections<FMovieSceneFloatChannel>(BaseSection,
					Section, StartIndex, EndIndex, Range);
			}
		}
	}
}

bool UAnimLayers::MergeAnimLayers(ISequencer* InSequencer, const TArray<int32>& Indices, const FBakingAnimationKeySettings* InSettings)
{
	if (InSequencer == nullptr)
	{
		return false;
	}
	TArray<UAnimLayer*> LayersToMerge;
	const FFrameNumber Min = InSettings ? InSettings->StartFrame : TNumericLimits<FFrameNumber>::Lowest();
	const FFrameNumber Max = InSettings ? InSettings->EndFrame : TNumericLimits<FFrameNumber>::Max();
	TRange<FFrameNumber> Range(Min, Max);
	FScopedTransaction Transaction(LOCTEXT("Merge Anim Layers", "Merge Anim Layers"), !GIsTransacting);

	TArray<int32> SortedIndices = Indices;
	//we go backwards to the first one
	SortedIndices.Sort([](const int32& Index1, const int32& Index2) {
		return  Index1 > Index2;
		});

	for (int32 Index : SortedIndices)
	{
		if (Index >= 0 && Index < AnimLayers.Num())
		{
			if (UAnimLayer* AnimLayer = AnimLayers[Index].Get())
			{
				LayersToMerge.Add(AnimLayer);
			}
		}
	}
	if (LayersToMerge.Num() < 1)
	{
		return false;
	}
	Modify();

	for (int32 Index = 0; Index < LayersToMerge.Num() -1; ++Index)
	{
		UAnimLayer* BaseLayer = LayersToMerge[Index + 1];
		UAnimLayer* AnimLayer = LayersToMerge[Index];
		BaseLayer->Modify();
		for (TPair<TWeakObjectPtr<UObject>, FAnimLayerItem>& Pair : AnimLayer->AnimLayerItems)
		{
			if (Pair.Key != nullptr)
			{
				for (FAnimLayerSectionItem& SectionItem : Pair.Value.SectionItems)
				{
					if (SectionItem.Section.IsValid())
					{
						FAnimLayerSectionItem* BaseSectionItem = nullptr;
						if (FAnimLayerItem* Owner = BaseLayer->AnimLayerItems.Find(Pair.Key))
						{
							BaseSectionItem = Owner->FindMatchingSectionItem(SectionItem.Section.Get());

						}
						if (BaseSectionItem && BaseSectionItem->Section.IsValid())
						{
							if (SectionItem.Section->IsActive())//active sections merge them
							{
								//if transform or control rig section we need to handle masking
								if (UMovieSceneControlRigParameterSection* BaseCRSection = Cast< UMovieSceneControlRigParameterSection>(BaseSectionItem->Section.Get()))
								{
									UMovieSceneControlRigParameterSection* CRSection = Cast< UMovieSceneControlRigParameterSection>(SectionItem.Section.Get());
									MergeControlRigSections(BaseCRSection, CRSection, Range);
								}
								else if (UMovieScene3DTransformSection* BaseTRSection = Cast< UMovieScene3DTransformSection>(BaseSectionItem->Section.Get()))
								{
									UMovieScene3DTransformSection* TRSection = Cast< UMovieScene3DTransformSection>(SectionItem.Section.Get());
									MergeTransformSections(BaseTRSection, TRSection, Range);
								}
								else
								{
									TArrayView<FMovieSceneFloatChannel*> BaseFloatChannels = BaseSectionItem->Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
									TArrayView<FMovieSceneDoubleChannel*> BaseDoubleChannels = BaseSectionItem->Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
									if (BaseDoubleChannels.Num() > 0)
									{
										int32 StartIndex = 0;
										int32 EndIndex = BaseDoubleChannels.Num() - 1;
										MovieSceneToolHelpers::MergeSections<FMovieSceneDoubleChannel>(BaseSectionItem->Section.Get(),
											SectionItem.Section.Get(), StartIndex, EndIndex, Range);

									}
									else if (BaseFloatChannels.Num() > 0)
									{
										int32 StartIndex = 0;
										int32 EndIndex = BaseFloatChannels.Num() - 1;
										MovieSceneToolHelpers::MergeSections<FMovieSceneFloatChannel>(BaseSectionItem->Section.Get(),
											SectionItem.Section.Get(), StartIndex, EndIndex, Range);
									}
								}
							}
							if (BaseLayer != AnimLayers[0]) //if not base layer
							{
								if (UMovieSceneControlRigParameterSection* CRSection = Cast<UMovieSceneControlRigParameterSection>(BaseSectionItem->Section))
								{
									if (SectionItem.AnimLayerSet.Names.Num() > 0)
									{
										AddNamesToMask(BaseSectionItem, CRSection, SectionItem.AnimLayerSet);
										if (SortedIndices[0] != 0) //if not base then make sure mask is set up
										{
											TArray<FName> AllControls;
											BaseSectionItem->AnimLayerSet.Names.GenerateKeyArray(AllControls);
											UAnimLayers::SetUpControlRigSection(CRSection, AllControls);
										}
									}
								}
							}
						}
						else //okay this object doesn't exist in the first layer we are merging into so we need to move it to the other one 
						{
							FAnimLayerItem AnimLayerItem;
							FAnimLayerSectionItem NewSectionItem;
							NewSectionItem.AnimLayerSet.BoundObject = Pair.Key;
							NewSectionItem.AnimLayerSet.Names = SectionItem.AnimLayerSet.Names;
							NewSectionItem.Section = SectionItem.Section;
							if (UMovieSceneNameableTrack* NameableTrack = Cast<UMovieSceneNameableTrack>(SectionItem.Section.Get()->GetTypedOuter< UMovieSceneNameableTrack>()))
							{
								NameableTrack->Modify();
								NameableTrack->SetTrackRowDisplayName(BaseLayer->State.Name, SectionItem.Section.Get()->GetRowIndex());
							}
							AnimLayerItem.SectionItems.Add(NewSectionItem);
							BaseLayer->AnimLayerItems.Add(Pair.Key, AnimLayerItem);
							//since we moved the section over we reset it on the merged so we don't delete it when we remove the layer
							if (FAnimLayerItem* OldAnimLayeritem = AnimLayer->AnimLayerItems.Find(Pair.Key))
							{
								SectionItem.Section.Reset();
							}
						}
					}
				}
			}
		}
		if (AnimLayer->GetType() == EAnimLayerType::Override &&
			(BaseLayer->GetType() == EAnimLayerType::Additive))
		{
			BaseLayer->SetType(EAnimLayerType::Override);
		}
		int32 LayerIndex = GetAnimLayerIndex(AnimLayer);
		if (LayerIndex != INDEX_NONE)
		{
			DeleteAnimLayer(InSequencer, LayerIndex);
		}
	}
	UAnimLayer* BaseLayer = LayersToMerge[LayersToMerge.Num() - 1];
	if (BaseLayer != AnimLayers[0]) //if not base layer
	{
		FString Merged(TEXT("Merged"));
		FString ExistingName = BaseLayer->GetName().ToString();
		if (ExistingName.Contains(Merged) == false)
		{
			FString NewLayerName = FString::Printf(TEXT("%s_Merged"), *ExistingName);
			FText LayerText;
			LayerText = LayerText.FromString(NewLayerName);
			BaseLayer->SetName(LayerText); //need items/sections to be added so we can change their track row names
		}
	}
	else
	{
		SetUpBaseLayerSections(); //if it is the base reset it
	}
	InSequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
	return true;
}

void UAnimLayers::AnimLayerListChangedBroadcast()
{
	OnAnimLayerListChanged.Broadcast(this);
}

TArray<UMovieSceneSection*> UAnimLayers::GetSelectedLayerSections() const
{
	TArray<UMovieSceneSection*> Sections;
	for (const UAnimLayer* AnimLayer : AnimLayers)
	{
		if (AnimLayer && AnimLayer->bIsSelectedInList)
		{
			for (const TPair<TWeakObjectPtr<UObject>, FAnimLayerItem>& Pair : AnimLayer->AnimLayerItems)
			{
				if (Pair.Key != nullptr)
				{
					for (const FAnimLayerSectionItem& SectionItem : Pair.Value.SectionItems)
					{
						if (SectionItem.Section.IsValid() )
						{
							Sections.Add(SectionItem.Section.Get());
						}
					}
				}
			}
		}
	}
	return Sections;
}

bool UAnimLayers::IsTrackOnSelectedLayer(const UMovieSceneTrack* InTrack)const
{
	TSet<UMovieSceneTrack*> CurrentTracks;
	for (const UAnimLayer* AnimLayer : AnimLayers)
	{
		if (AnimLayer && AnimLayer->bIsSelectedInList)
		{
			for (const TPair<TWeakObjectPtr<UObject>,FAnimLayerItem>& Pair : AnimLayer->AnimLayerItems)
			{
				if (Pair.Key != nullptr)
				{
					for (const FAnimLayerSectionItem& SectionItem : Pair.Value.SectionItems)
					{
						if (SectionItem.Section.IsValid())
						{
							UMovieSceneTrack* OwnerTrack = SectionItem.Section->GetTypedOuter<UMovieSceneTrack>();
							if (OwnerTrack == InTrack)
							{
								return true;
							}
						}
					}
				}
			}
		}
	}
	return false;
}

int32 UAnimLayers::AddAnimLayerFromSelection(ISequencer* SequencerPtr)
{
	int32 NewIndex = INDEX_NONE;
	//wrap scoped transaction since it can deselect control rigs
	TArray<FControlRigAndControlsAndTrack> SelectedCRs;
	TArray<FObjectAndTrack> SelectedBoundObjects; 
	{
		const FScopedTransaction Transaction(LOCTEXT("AddAnimLayer_Transaction", "Add Anim Layer"), !GIsTransacting);
		Modify();
		if (AnimLayers.Num() == 0)
		{
			AddBaseLayer();
		}
		UAnimLayer* AnimLayer = NewObject<UAnimLayer>(this, NAME_None, RF_Transactional);

		GetSelectedControlRigsAndBoundObjects(SequencerPtr, SelectedCRs, SelectedBoundObjects);

		if (SelectedCRs.Num() <= 0 && SelectedBoundObjects.Num() <= 0)
		{
			FString LayerName = FString::Printf(TEXT("Empty Layer %d"), AnimLayers.Num());
			FText LayerText;
			LayerText = LayerText.FromString(LayerName);
			AnimLayer->SetName(LayerText); //need items/sections to be added so we can change their track row names
			AnimLayer->State.Weight = 1.0;
			AnimLayer->State.Type = (EAnimLayerType::Additive);
			int32 Index = AnimLayers.Add(AnimLayer);
			AnimLayerListChangedBroadcast();

			return Index;
		}

		bool bItemAdded = false;

		for (FControlRigAndControlsAndTrack& CRControls : SelectedCRs)
		{
			Modify();
			FAnimLayerItem AnimLayerItem;
			FAnimLayerSectionItem SectionItem;
			SectionItem.AnimLayerSet.BoundObject = CRControls.ControlRig;
			for (FName& ControlName : CRControls.Controls)
			{
				FAnimLayerPropertyAndChannels Channels;
				Channels.Name = ControlName;
				Channels.Channels = (uint32)EControlRigContextChannelToKey::AllTransform;
				SectionItem.AnimLayerSet.Names.Add(ControlName, Channels);

			}
			CRControls.Track->Modify();
			// Add a new section that starts and ends at the same time
			TGuardValue<bool> GuardSetSection(CRControls.Track->bSetSectionToKeyPerControl, false);
			if (UMovieSceneControlRigParameterSection* NewSection = Cast<UMovieSceneControlRigParameterSection>(CRControls.Track->CreateNewSection()))
			{
				if (bItemAdded == false)
				{
					AnimLayer->State.Weight = 1.0;
					AnimLayer->State.Type = (EAnimLayerType::Additive);
					bItemAdded = true;
				}

				ensureAlwaysMsgf(NewSection->HasAnyFlags(RF_Transactional), TEXT("CreateNewSection must return an instance with RF_Transactional set! (pass RF_Transactional to NewObject)"));
				NewSection->SetFlags(RF_Transactional);
				NewSection->SetTransformMask(FMovieSceneTransformMask{ EMovieSceneTransformChannel::All });
				FMovieSceneFloatChannel* FloatChannel = &NewSection->Weight;
				SectionItem.Section = NewSection;
				AnimLayerItem.SectionItems.Add(SectionItem);
				AnimLayer->AnimLayerItems.Add(CRControls.ControlRig, AnimLayerItem);
				SetUpSectionDefaults(SequencerPtr, AnimLayer, CRControls.Track, NewSection, FloatChannel);
				SetUpControlRigSection(NewSection, CRControls.Controls);

			}
		}
		for (FObjectAndTrack& ObjectAndTrack : SelectedBoundObjects)
		{
			Modify();

			FAnimLayerItem& AnimLayerItem = AnimLayer->AnimLayerItems.FindOrAdd(ObjectAndTrack.BoundObject);
			FAnimLayerSectionItem SectionItem;
			SectionItem.AnimLayerSet.BoundObject = ObjectAndTrack.BoundObject;
			/*
			for (FName& ControlName : SelectedControls)
			{
				FAnimLayerPropertyAndChannels Channels;
				Channels.Name = ControlName;
				Channels.Channels = (uint32)EControlRigContextChannelToKey::AllTransform;
				AnimLayerItem.AnimLayerSet.Names.Add(ControlName, Channels);

			}*/

			// Add a new section that starts and ends at the same time
			ObjectAndTrack.Track->Modify();
			if (UMovieSceneSection* NewSection = ObjectAndTrack.Track->CreateNewSection())
			{
				if (bItemAdded == false)
				{
					AnimLayer->State.Weight = 1.0;
					AnimLayer->State.Type = (EAnimLayerType::Additive);
					bItemAdded = true;
				}

				ensureAlwaysMsgf(NewSection->HasAnyFlags(RF_Transactional), TEXT("CreateNewSection must return an instance with RF_Transactional set! (pass RF_Transactional to NewObject)"));
				NewSection->SetFlags(RF_Transactional);
				SectionItem.Section = NewSection;
				FMovieSceneFloatChannel* FloatChannel = nullptr;
				if (UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(NewSection))
				{
					TransformSection->SetMask(FMovieSceneTransformMask{ EMovieSceneTransformChannel::All });
					FloatChannel = TransformSection->GetWeightChannel();
				}
				AnimLayerItem.SectionItems.Add(SectionItem);
				SetUpSectionDefaults(SequencerPtr, AnimLayer, ObjectAndTrack.Track, NewSection, FloatChannel);
			}
		}

		if (bItemAdded)
		{
			FString LayerName = FString::Printf(TEXT("Anim Layer %d"), AnimLayers.Num());
			FText LayerText;
			LayerText = LayerText.FromString(LayerName);
			AnimLayer->SetName(LayerText); //need items/sections to be added so we can change their track row names
			int32 Index = AnimLayers.Add(AnimLayer);
			SetUpBaseLayerSections();
			AnimLayer->SetKeyed();
			SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);

			NewIndex = Index;;
			AnimLayerListChangedBroadcast();
		}
	}
	//may need to reselect controls here
	for (FControlRigAndControlsAndTrack& CRControls : SelectedCRs)
	{
		for (const FName& Control : CRControls.Controls)
		{
			if (CRControls.ControlRig->IsControlSelected(Control) == false)
			{
				CRControls.ControlRig->SelectControl(Control, true);
			}
		}
	}
	return NewIndex;
}

static void AddSectionToAnimLayerItem(const FAnimLayerSelectionSet& CurrentAnimLayerSet, FAnimLayerItem* AnimLayerItem, UObject* BoundObject, UMovieSceneSection* InSection)
{
	FAnimLayerSectionItem NewSectionItem;
	NewSectionItem.AnimLayerSet.BoundObject = BoundObject;
	NewSectionItem.Section = InSection;
	if (UMovieSceneControlRigParameterSection* CRSection = Cast<UMovieSceneControlRigParameterSection>(NewSectionItem.Section))
	{
		FMovieSceneFloatChannel* FloatChannel = &CRSection->Weight;
		FloatChannel->SetDefault(1.0f);
		CRSection->SetTransformMask(CRSection->GetTransformMask().GetChannels() | EMovieSceneTransformChannel::Weight);
	}
	else if (UMovieScene3DTransformSection* Section = Cast<UMovieScene3DTransformSection>(NewSectionItem.Section))
	{
		FMovieSceneFloatChannel* FloatChannel = Section->GetWeightChannel();
		FloatChannel->SetDefault(1.0f);
		Section->SetMask(Section->GetMask().GetChannels() | EMovieSceneTransformChannel::Weight);
	}
	for (const TPair<FName, FAnimLayerPropertyAndChannels>& Set : CurrentAnimLayerSet.Names)
	{
		FAnimLayerPropertyAndChannels Channels;
		Channels.Name = Set.Value.Name;
		Channels.Channels = Set.Value.Channels;
		NewSectionItem.AnimLayerSet.Names.Add(Set.Key, Channels);
	}
	AnimLayerItem->SectionItems.Add(NewSectionItem);
}

void UAnimLayers::SetUpBaseLayerSections()
{
	if (AnimLayers.Num() > 0)
	{
		if (UAnimLayer* BaseAnimLayer = AnimLayers[0])
		{
			BaseAnimLayer->Modify();
			BaseAnimLayer->AnimLayerItems.Reset(); //clear it out
			for (int32 Index = 1; Index < AnimLayers.Num(); ++Index)
			{
				UAnimLayer* AnimLayer = AnimLayers[Index];
				AnimLayer->Modify();
				for (const TPair<TWeakObjectPtr<UObject>,FAnimLayerItem>& Pair : AnimLayer->AnimLayerItems)
				{
					if (Pair.Key != nullptr)
					{
						for (const FAnimLayerSectionItem& SectionItem : Pair.Value.SectionItems)
						{
							if (SectionItem.Section.IsValid())
							{
								if (UMovieSceneTrack* Track = SectionItem.Section->GetTypedOuter<UMovieSceneTrack>())
								{
									const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
									if (Sections.Num() > 1 && Sections[0]->GetBlendType().IsValid() && Sections[0]->GetBlendType() == EMovieSceneBlendType::Absolute)
									{
										if (FAnimLayerItem* Existing = BaseAnimLayer->AnimLayerItems.Find(Pair.Key))
										{
											if (Pair.Key->IsA<UControlRig>()) //if control rig just merge over control names
											{
												for (const TPair<FName, FAnimLayerPropertyAndChannels>& Set : SectionItem.AnimLayerSet.Names)
												{
													for (FAnimLayerSectionItem& ExistingSectionItem : Existing->SectionItems)
													{
														if (ExistingSectionItem.AnimLayerSet.Names.Find(Set.Key) == nullptr)
														{
															FAnimLayerPropertyAndChannels Channels;
															Channels.Name = Set.Value.Name;
															Channels.Channels = Set.Value.Channels;
															ExistingSectionItem.AnimLayerSet.Names.Add(Set.Key, Channels);
														}
													}
												}
											}
											else
											{
												AddSectionToAnimLayerItem(SectionItem.AnimLayerSet,Existing, Pair.Key.Get(), Sections[0]);
											}
										}
										else
										{
											FAnimLayerItem AnimLayerItem;
											AddSectionToAnimLayerItem(SectionItem.AnimLayerSet, &AnimLayerItem, Pair.Key.Get(), Sections[0]);
											BaseAnimLayer->AnimLayerItems.Add(Pair.Key, AnimLayerItem);
										}
									}
								}
							}
						}
					}
				}
			}
			//set the name will set it on all base sections
			FText LayerText = BaseAnimLayer->State.Name;
			BaseAnimLayer->SetName(LayerText);
		}
	}
	else
	{
		AddBaseLayer();
	}
}

void UAnimLayers::GetAnimLayerStates(TArray<FAnimLayerState>& OutStates)
{
	OutStates.Reset();
	for (UAnimLayer* AnimLayer : AnimLayers)
	{
		if (AnimLayer)
		{
			OutStates.Emplace(AnimLayer->State);
		}
	}
}

void UAnimLayers::SetUpSectionDefaults(ISequencer* SequencerPtr, UAnimLayer* Layer, UMovieSceneTrack* Track, UMovieSceneSection* NewSection, FMovieSceneFloatChannel* WeightChannel)
{
	int32 OverlapPriority = 0;
	TMap<int32, int32> NewToOldRowIndices;
	int32 RowIndex = Track->GetMaxRowIndex() + 1;
	for (UMovieSceneSection* Section : Track->GetAllSections())
	{
		OverlapPriority = FMath::Max(Section->GetOverlapPriority() + 1, OverlapPriority);

		// Move existing sections on the same row or beyond so that they don't overlap with the new section
		if (Section != NewSection && Section->GetRowIndex() >= RowIndex)
		{
			int32 OldRowIndex = Section->GetRowIndex();
			int32 NewRowIndex = Section->GetRowIndex() + 1;
			NewToOldRowIndices.FindOrAdd(NewRowIndex, OldRowIndex);
			Section->Modify();
			Section->SetRowIndex(NewRowIndex);
		}
	}

	Track->Modify();

	Track->OnRowIndicesChanged(NewToOldRowIndices);
	NewSection->SetRange(TRange<FFrameNumber>::All());
	
	NewSection->SetOverlapPriority(OverlapPriority);
	NewSection->SetRowIndex(RowIndex);

	Track->AddSection(*NewSection);
	Track->UpdateEasing();

	if (UMovieSceneNameableTrack* NameableTrack = Cast<UMovieSceneNameableTrack>(Track))
	{
		NameableTrack->SetTrackRowDisplayName(Layer->GetName(), RowIndex);
	}

	switch (Layer->State.Type)
	{
	case EAnimLayerType::Additive:
		NewSection->SetBlendType(EMovieSceneBlendType::Additive);
		break;
	case EAnimLayerType::Override:
		NewSection->SetBlendType(EMovieSceneBlendType::Override);
		SetDefaultsForOverride(NewSection);
		break;
	case EAnimLayerType::Base:
		NewSection->SetBlendType(EMovieSceneBlendType::Absolute);
		break;
	}
	if (WeightChannel)
	{
		WeightChannel->SetDefault(1.0f);
	}
}

void UAnimLayers::SetUpControlRigSection(UMovieSceneControlRigParameterSection* ParameterSection, TArray<FName>& ControlNames)
{
	if (ParameterSection)
	{
		UControlRig* ControlRig = ParameterSection ? ParameterSection->GetControlRig() : nullptr;

		if (ParameterSection && ControlRig)
		{
			ParameterSection->Modify();
			ParameterSection->FillControlNameMask(false);

			TArray<FRigControlElement*> Controls;
			ControlRig->GetControlsInOrder(Controls);
			for (const FName& RigName : ControlNames)
			{
				ParameterSection->SetControlNameMask(RigName, true);
			}
		}
	}
}

struct FKeyInterval
{
	FFrameNumber StartFrame;
	double StartValue;
	FFrameNumber EndFrame;
	double EndValue;
};

template<typename ChannelType>
void GetPairs(ChannelType* Channel, TArray<FKeyInterval>& OutKeyIntervals)
{
	using ChannelValueType = typename ChannelType::ChannelValueType;
	TMovieSceneChannelData<ChannelValueType> ChannelData = Channel->GetData();

	TArrayView<FFrameNumber> Times = ChannelData.GetTimes();
	TArrayView <ChannelValueType> Values = ChannelData.GetValues();
	OutKeyIntervals.SetNum(0);
	for (int32 Index = 0; Index < Times.Num() -1; ++Index)
	{
		FKeyInterval KeyInterval;
		KeyInterval.StartFrame = Times[Index];
		KeyInterval.StartValue = Values[Index].Value;
		KeyInterval.EndFrame = Times[Index + 1];
		KeyInterval.EndValue = Values[Index + 1].Value;
		OutKeyIntervals.Add(KeyInterval);
	}
}

template<typename ChannelType>
void  EvaluateCurveOverRange(ChannelType* Channel, const FFrameNumber& StartTime, const FFrameNumber& EndTime,
	const FFrameNumber& Interval, TArray<TPair<FFrameNumber, double>>& OutKeys)
{
	using CurveValueType = typename ChannelType::CurveValueType;
	CurveValueType Value;
	FFrameNumber CurrentTime = StartTime;
	OutKeys.SetNum(0);
	while (CurrentTime < EndTime)
	{
		Channel->Evaluate(CurrentTime, Value);
		CurrentTime += Interval;
		TPair<FFrameNumber, double> OutValue;
		OutValue.Key = CurrentTime;
		OutValue.Value = (double) Value;
		OutKeys.Add(OutValue);
	}
}

void GetPercentageOfChange(const TArray<TPair<FFrameNumber, double>>& InKeys, TArray<double>& ValueDifferences,
TArray<TPair<FFrameNumber, double>>& PercentageDifferences)
{
	ValueDifferences.SetNum(InKeys.Num());
	for (int32 Index = 0; Index < InKeys.Num() -1 ; ++Index)
	{
		const double ValueDifference = InKeys[Index + 1].Value - InKeys[Index].Value;
		ValueDifferences[Index] = ValueDifference;
	}
	const double TotalChange = Algo::Accumulate(ValueDifferences, 0.);
	if (FMath::IsNearlyZero(TotalChange) == false)
	{
		const double TotalChangePercentage = (100.0 / TotalChange);
		PercentageDifferences.SetNum(InKeys.Num());
		for (int32 Index = 0; Index < InKeys.Num(); ++Index)
		{
			PercentageDifferences[Index].Key = InKeys[Index].Key;
			PercentageDifferences[Index].Value = TotalChangePercentage * ValueDifferences[Index];
		}
	}
	else
	{
		PercentageDifferences.SetNum(0);
	}
}

template<typename ChannelType>
void AdjustmentBlend(UMovieSceneSection* Section, TArrayView<ChannelType*>& BaseChannels, 
	TArrayView<ChannelType*>& LayerChannels,ISequencer* InSequencer)
{
	if (BaseChannels.Num() != LayerChannels.Num())
	{
		return;
	}
	const FFrameRate& FrameRate = InSequencer->GetFocusedDisplayRate();
	const FFrameRate& TickResolution = InSequencer->GetFocusedTickResolution();
	const FFrameNumber Interval = TickResolution.AsFrameNumber(FrameRate.AsInterval());
	EMovieSceneKeyInterpolation DefaultInterpolation = InSequencer->GetKeyInterpolation();

	TArray<FKeyInterval> KeyIntervals;
	TArray <TPair<FFrameNumber, double>> Keys;
	TArray<double> ValueDifferences;
	TArray<TPair<FFrameNumber, double>> PercentageDifferences;
	for (int32 Index = 0; Index < BaseChannels.Num(); ++Index)
	{
		ChannelType* BaseChannel = BaseChannels[Index];
		ChannelType* LayerChannel = LayerChannels[Index];
		KeyIntervals.SetNum(0);
		Keys.SetNum(0);
		GetPairs(LayerChannel, KeyIntervals);
		for (FKeyInterval& KeyInterval : KeyIntervals)
		{
			EvaluateCurveOverRange(BaseChannel, KeyInterval.StartFrame, KeyInterval.EndFrame,
				Interval, Keys);
			GetPercentageOfChange(Keys, ValueDifferences, PercentageDifferences);
			const double TotalPoseLayerChange = FMath::Abs(KeyInterval.EndValue - KeyInterval.StartValue);
			double PreviousValue = KeyInterval.StartValue;
			for (TPair<FFrameNumber, double>& TimeValue : PercentageDifferences)
			{
				const double ValueDelta = (TotalPoseLayerChange / 100.0) * TimeValue.Value;
				const double CurrentValue = (KeyInterval.EndValue > KeyInterval.StartValue) ?
					PreviousValue + ValueDelta : PreviousValue - ValueDelta;
				AddKeyToChannel(LayerChannel, DefaultInterpolation, TimeValue.Key, CurrentValue);
			}
		}
	}
}

bool UAnimLayers::AdjustmentBlendLayers(ISequencer* InSequencer, int32 LayerIndex)
{
	if (InSequencer == nullptr)
	{
		return false;
	}
	if (LayerIndex < 1 || LayerIndex >= AnimLayers.Num())
	{
		return false;
	}
	UAnimLayer* BaseLayer = AnimLayers[0];
	UAnimLayer* AnimLayer = AnimLayers[LayerIndex];
	const FFrameNumber Min =  TNumericLimits<FFrameNumber>::Lowest();
	const FFrameNumber Max =  TNumericLimits<FFrameNumber>::Max();
	TRange<FFrameNumber> Range(Min, Max);
	FScopedTransaction Transaction(LOCTEXT("AdjustmentBlendLayer", "Adjustment Blend layer"), !GIsTransacting);
	
	Modify();
	AnimLayer->Modify();


	for (TPair<TWeakObjectPtr<UObject>, FAnimLayerItem>& Pair : AnimLayer->AnimLayerItems)
	{
		if (Pair.Key != nullptr)
		{
			for (FAnimLayerSectionItem& SectionItem : Pair.Value.SectionItems)
			{
				if (SectionItem.Section.IsValid())
				{
					FAnimLayerSectionItem* BaseSectionItem = nullptr;
					if (FAnimLayerItem* Owner = BaseLayer->AnimLayerItems.Find(Pair.Key))
					{
						BaseSectionItem = Owner->FindMatchingSectionItem(SectionItem.Section.Get());

					}
					if (BaseSectionItem && BaseSectionItem->Section.IsValid())
					{
						if (SectionItem.Section->IsActive())//active sections merge them
						{
							TArrayView<FMovieSceneFloatChannel*> BaseFloatChannels = BaseSectionItem->Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
							TArrayView<FMovieSceneDoubleChannel*> BaseDoubleChannels = BaseSectionItem->Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
							TArrayView<FMovieSceneFloatChannel*> LayerFloatChannels = SectionItem.Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
							TArrayView<FMovieSceneDoubleChannel*> LayerDoubleChannels = SectionItem.Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();

							
							if (BaseDoubleChannels.Num() > 0)
							{
								AdjustmentBlend<FMovieSceneDoubleChannel>(SectionItem.Section.Get(), BaseDoubleChannels,LayerDoubleChannels,  InSequencer);
							}
							else if (BaseFloatChannels.Num() > 0)
							{
								AdjustmentBlend<FMovieSceneFloatChannel>(SectionItem.Section.Get(), BaseFloatChannels,LayerFloatChannels, InSequencer);
							}
						}	
					}
				}
			}
		}
	}

	InSequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
	return true;
}

#undef LOCTEXT_NAMESPACE
