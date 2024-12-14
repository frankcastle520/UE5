// Copyright Epic Games, Inc. All Rights Reserved.


#include "AutomatedSequencePerfTest.h"

#include "TimerManager.h"
#include "AutomatedPerfTesting.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "MovieSceneSequencePlayer.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"

/*****
 * UAutomatedSequencePerfTestProjectSettings
 *****/

UAutomatedSequencePerfTestProjectSettings::UAutomatedSequencePerfTestProjectSettings(const FObjectInitializer& Initializer)
	: Super(Initializer)
	, CSVOutputMode(EAutomatedPerfTestCSVOutputMode::Separate)
{
}

bool UAutomatedSequencePerfTestProjectSettings::GetComboFromTestName(FName TestName, FAutomatedPerfTestMapSequenceCombo& FoundSequence) const
{
	for (FAutomatedPerfTestMapSequenceCombo TestCombo : MapsAndSequencesToTest)
	{
		if(TestCombo.ComboName == TestName)
		{
			FoundSequence = TestCombo;
			return true;
		}
	}
	return false;
}


/*****
 * UAutomatedSequencePerfTest
 *****/
void UAutomatedSequencePerfTest::SetupTest()
{
	if(CurrentMapSequenceCombo.IsSet())
	{
		if(GetCurrentMap() == CurrentMapSequenceCombo->Map.GetAssetName())
		{
			// don't even try to set up the test if we're not in the correct map
			Super::SetupTest();
			
			// make sure the world exists, then create a sequence player
			if(UWorld* const World = GetWorld())
			{
				// reset the camera cut number
				NumCameraCuts = -1;
				
				// load the sequence specified by the user
				UE_LOG(LogAutomatedPerfTest, Log, TEXT("Loading sequence %s"), *CurrentMapSequenceCombo->Sequence.ToString());
				ULevelSequence* TargetSequence = LoadObject<ULevelSequence>(NULL, *CurrentMapSequenceCombo->Sequence.ToString(), NULL, LOAD_None, NULL);
				check(TargetSequence);
		
				UE_LOG(LogAutomatedPerfTest, Log, TEXT("World is valid, creating sequence player"));
				SequencePlayer = ULevelSequencePlayer::CreateLevelSequencePlayer(World, TargetSequence, FMovieSceneSequencePlaybackSettings(), SequenceActor);

				if (SequencePlayer == nullptr)
				{
					UE_LOG(LogAutomatedPerfTest, Error, TEXT("Unable to create sequence player when starting AutomatedSequencePerfTest, exiting..."));
					EndAutomatedPerfTest(1);
				}

				// set the sequence up at the beginning
				FMovieSceneSequencePlaybackParams PlaybackParams = FMovieSceneSequencePlaybackParams();
				FMovieSceneSequencePlayToParams PlayToParams = FMovieSceneSequencePlayToParams();

				PlaybackParams.Time = 0.0;
				PlaybackParams.UpdateMethod = EUpdatePositionMethod::Scrub;

				UE_LOG(LogAutomatedPerfTest, Log, TEXT("SetupMapTest:: Scrubbing to start"));
				SequencePlayer->PlayTo(PlaybackParams, PlayToParams);

				UE_LOG(LogAutomatedPerfTest, Verbose, TEXT("SetupMapTest:: Waiting for %f seconds before playing seqeuence"), Settings->SequenceStartDelay);
				FTimerHandle UnusedHandle;
				GetWorld()->GetTimerManager().SetTimer(UnusedHandle, this, &UAutomatedSequencePerfTest::RunTest, 1.0, false, Settings->SequenceStartDelay);
			}
			// if we have an invalid world, we can't run the test, so we should bail out
			else
			{
				UE_LOG(LogAutomatedPerfTest, Error, TEXT("Invalid World when starting AutomatedSequencePerfTest, exiting..."));
				EndAutomatedPerfTest(1);
			}
		}
		else
		{
			UE_LOG(LogAutomatedPerfTest, Log, TEXT("Current Map Name %s is not expected %s, calling NextMap."), *GetCurrentMap(), *CurrentMapSequenceCombo->Map.GetAssetName())
			NextMap();
		}
	}
	else
	{
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("Current Map Sequence Combo has not been set, calling NextMap to trigger."))
		NextMap();
	}
}

void UAutomatedSequencePerfTest::NextMap()
{
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("UAutomatedSequencePerfTest::NextMap"))

	if(MapSequenceCombos.Num() > 0)
	{
		CurrentMapSequenceCombo = MapSequenceCombos.Pop();
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("Setting up test for Map/Sequence combo %s"), *CurrentMapSequenceCombo->ComboName.ToString())

		// no need to prepend this with a ? since OpenLevel handles that part for us
		FString OptionsString;
		if(!CurrentMapSequenceCombo->GameModeOverride.IsEmpty())
		{
			UE_LOG(LogAutomatedPerfTest, Log, TEXT("Game Mode overridden to %s"), *CurrentMapSequenceCombo->GameModeOverride)
			OptionsString += "game=" + CurrentMapSequenceCombo->GameModeOverride;
		}
		
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("Opening map %s%s"), *CurrentMapSequenceCombo->Map.GetAssetName(), *OptionsString);
		UGameplayStatics::OpenLevel(AutomatedPerfTest::FindCurrentWorld(), *CurrentMapSequenceCombo->Map.GetAssetName(), true, OptionsString);
	}
	else
	{
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("UAutomatedSequencePerfTest::NextMap, all maps complete, exiting after delay."))
		TriggerExitAfterDelay();
	}
}

void UAutomatedSequencePerfTest::RunTest()
{
	Super::RunTest();

	UE_LOG(LogAutomatedPerfTest, Log, TEXT("RunTest"));
	
	// make sure we have a valid sequence player
	if(SequencePlayer)
	{
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("RunTest::Valid Sequence Player, proceeding"));

		// trigger a camera cut manually in order to start the region for the first camera cut
        SequencePlayer->Play();
		OnCameraCut(SequencePlayer->GetActiveCameraComponent());
		
		// When the sequence has finished, we'll tear down the test in this map via the OnSequenceFinished dispatch
		// because TeardownTest's signature doesn't match OnFinished
        SequencePlayer->OnFinished.AddDynamic(this, &UAutomatedSequencePerfTest::OnSequenceFinished);
		
		SequencePlayer->OnCameraCut.AddDynamic(this, &UAutomatedSequencePerfTest::OnCameraCut);
	}
	// otherwise bail out of the test
	else
	{
		UE_LOG(LogAutomatedPerfTest, Error, TEXT("Invalid SequencePlayer when starting AutomatedSequencePerfTest, exiting..."));
		EndAutomatedPerfTest(1);
	}
}

void UAutomatedSequencePerfTest::OnSequenceFinished()
{
	// trigger OnCameraCut again with a nullptr for the new camera so that
	// we can end the final camera cut's region
	OnCameraCut(nullptr);
	TeardownTest(false);
}

void UAutomatedSequencePerfTest::TeardownTest(bool bExitAfterTeardown)
{
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("AutomatedSequencePerfTest::TeardownTest"));
	
	Super::TeardownTest(bExitAfterTeardown);

	UnbindAllDelegates();

	// null out the references we have to our world objects
	CurrentCamera = nullptr;
	SequencePlayer = nullptr;
	SequenceActor = nullptr;
	
	NextMap();
}

void UAutomatedSequencePerfTest::Exit()
{
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("AutomatedSequencePerfTest::Exit"));
	Super::Exit();
}

void UAutomatedSequencePerfTest::OnCameraCut(UCameraComponent* CameraComponent)
{
	// null check the CurrentCamera so that we can use OnCameraCut to mark the starting camera cut region
	if(CurrentCamera)
	{
		if(RequestsInsightsTrace())
		{
			TRACE_END_REGION(*GetCameraCutID());
		}
#if CSV_PROFILER
		if(RequestsCSVProfiler())
		{
			CSV_EVENT(AutomatedPerfTest, TEXT("END_%s"), *GetCameraCutID())
			if(GetCSVOutputMode() == EAutomatedPerfTestCSVOutputMode::Granular)
			{
				TryStopCSVProfiler();
			}
		}
#endif
	}

	// then null check the new CameraComponent so that we can use OnCameraCut to mark the end of the final camera cut region
	if(CameraComponent)
	{
		NumCameraCuts += 1;
		
		// Then bring in the new camera component for this cut and mark the start of it
		CurrentCamera = CameraComponent;
		if(RequestsInsightsTrace())
		{
			TRACE_BEGIN_REGION(*GetCameraCutID());
		}
#if CSV_PROFILER
		if(RequestsCSVProfiler())
		{
			if(GetCSVOutputMode() == EAutomatedPerfTestCSVOutputMode::Granular)
			{
				TryStartCSVProfiler(GetCameraCutFullName());
			}
			CSV_EVENT(AutomatedPerfTest, TEXT("START_%s"), *GetCameraCutID())
			
		}
#endif
	}
}

FString UAutomatedSequencePerfTest::GetTestID()
{
	return CurrentMapSequenceCombo.IsSet() ? Super::GetTestID() + "_Sequence_" + CurrentMapSequenceCombo->ComboName.ToString() : Super::GetTestID();
}

FString UAutomatedSequencePerfTest::GetCameraCutID()
{
	if(CurrentCamera != nullptr)
	{
		// getting the label of a spawnable camera from Sequencer in a packaged build isn't possible
		// via this method. TODO: find a more reliable way to return camera cut names that are set in Sequencerj
		//const AActor* Owner = CurrentCamera->GetOwner();
		//FString CutName = Owner ? Owner->GetActorNameOrLabel() : CurrentCamera->GetName();
		//return CutName;
		FString NumCuts = FString::FromInt(NumCameraCuts);

		// Pad it out to at least four characters for now
		while(NumCuts.Len() < 4)
		{
			NumCuts = "0" + NumCuts;
		}
		
		return FString::Format(TEXT("CameraCut{0}"), {NumCuts});
	}
	return "";
}

FString UAutomatedSequencePerfTest::GetCameraCutFullName()
{
	// the full name for the camera cut should include the TestID so that the data can be 
	if(CurrentCamera != nullptr)
	{
		return GetTestID() + "_" + GetCameraCutID(); 
	}
	return GetTestID();
}

void UAutomatedSequencePerfTest::OnInit()
{
	Super::OnInit();
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("AutomatedSequencePerfTest::OnInit"));
	
	Settings = GetDefault<UAutomatedSequencePerfTestProjectSettings>();

	SetCSVOutputMode(Settings->CSVOutputMode);
	
	// if an explicit map/sequence name was set from commandline, use this to override the test
	if (FParse::Value(FCommandLine::Get(), TEXT("AutomatedPerfTest.SequencePerfTest.MapSequenceName="), SequenceTestName))
	{
		FAutomatedPerfTestMapSequenceCombo MapSequenceCombo;
		if(Settings->GetComboFromTestName(SequenceTestName, MapSequenceCombo))
		{
			MapSequenceCombos.Add(MapSequenceCombo);
		}
		else
		{
			UE_LOG(LogAutomatedPerfTest, Error, TEXT("Failed to find MapSequence combo name matching %s"), *SequenceTestName.ToString())
		}
	}
	// otherwise, use all the maps defined in project settings
	else
	{
		for(const FAutomatedPerfTestMapSequenceCombo& MapSequenceCombo : Settings->MapsAndSequencesToTest)
		{
			MapSequenceCombos.Add(MapSequenceCombo);
		}
	}
	
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("Initialized with %hhd MapSequence combos"), MapSequenceCombos.Num());
}

void UAutomatedSequencePerfTest::UnbindAllDelegates()
{
	Super::UnbindAllDelegates();

	// if we have a valid sequence player, make sure we unbind our events from it when we're wrapping up the test.
	if(SequencePlayer != nullptr)
	{
		SequencePlayer->OnCameraCut.RemoveAll(this);
		SequencePlayer->OnFinished.RemoveAll(this);
		GetWorld()->GetTimerManager().ClearAllTimersForObject(SequencePlayer);
	}

	// clear any stray timers that might be lying around
	GetWorld()->GetTimerManager().ClearAllTimersForObject(this);
}
