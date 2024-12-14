// Copyright Epic Games, Inc. All Rights Reserved.

#include "DaySequenceFactoryNew.h"
#include "DaySequence.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieSceneTrack.h"
#include "MovieScenePossessable.h"
#include "MovieSceneToolsProjectSettings.h"
#include "DaySequenceActor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DaySequenceFactoryNew)

#define LOCTEXT_NAMESPACE "DaySequenceFactory"

UDaySequenceFactoryNew::UDaySequenceFactoryNew()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UDaySequence::StaticClass();
}


UObject* UDaySequenceFactoryNew::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UDaySequence* NewDaySequence = NewObject<UDaySequence>(InParent, Name, Flags|RF_Transactional);
	NewDaySequence->Initialize();

	// TODO: Set default playback range from DaySequence project settings
	const UMovieSceneToolsProjectSettings* ProjectSettings = GetDefault<UMovieSceneToolsProjectSettings>();
	const FFrameRate TickResolution = NewDaySequence->GetMovieScene()->GetTickResolution();
	NewDaySequence->GetMovieScene()->SetPlaybackRange((ProjectSettings->DefaultStartTime*TickResolution).FloorToFrame(), (ProjectSettings->DefaultDuration*TickResolution).FloorToFrame().Value);

	AddDefaultBindings(NewDaySequence);
	return NewDaySequence;
}


bool UDaySequenceFactoryNew::ShouldShowInNewMenu() const
{
	return true;
}

void UDaySequenceFactoryNew::AddDefaultBindings(UDaySequence* NewDaySequence)
{
	static const FString DefaultBindingName = LOCTEXT("DefaultBindingName", "Root Day Sequence Actor").ToString();

	UMovieScene* MovieScene = NewDaySequence->GetMovieScene();

	// Add a default binding
	FMovieScenePossessable Possessable(DefaultBindingName, ADaySequenceActor::StaticClass());
	FMovieSceneBinding     Binding(Possessable.GetGuid(), DefaultBindingName);

	NewDaySequence->AddDefaultBinding(Possessable.GetGuid());
	MovieScene->AddPossessable(Possessable, Binding);
}


#undef LOCTEXT_NAMESPACE

