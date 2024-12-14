// Copyright Epic Games, Inc. All Rights Reserved.

#include "StaticCameraTests/AutomatedStaticCameraPerfTestBase.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "AutomatedPerfTesting.h"
#include "Camera/CameraActor.h"
#include "Misc/CommandLine.h"

UAutomatedStaticCameraPerfTestProjectSettings::UAutomatedStaticCameraPerfTestProjectSettings(const FObjectInitializer& Initializer)
	: Super(Initializer)
	, bCaptureScreenshots(true)
	, WarmUpTime(5.0)
	, SoakTime(5.0)
	, CooldownTime(1.0)
	, CSVOutputMode(EAutomatedPerfTestCSVOutputMode::Separate)
{

}

bool UAutomatedStaticCameraPerfTestProjectSettings::GetMapFromAssetName(FString AssetName, FSoftObjectPath& OutSoftObjectPath) const
{
	for (FSoftObjectPath MapPath : MapsToTest)
	{
		if(MapPath.GetAssetName() == AssetName)
		{
			OutSoftObjectPath = MapPath;
			return true;
		}
	}
	return false;
}

void UAutomatedStaticCameraPerfTestBase::SetupTest()
{
	// load up into the map defined in project settings
	if(!CurrentMapPath.IsNull())
	{
		if(GetCurrentMap() == CurrentMapPath.GetAssetName())
		{
			Super::SetupTest();
			
			UE_LOG(LogAutomatedPerfTest, Log, TEXT("UAutomatedStaticCameraPerfTestBase::SetupTest"))
			// make sure the world exists, then create a sequence player
			if(UWorld* const World = GetWorld())
			{
				CamerasToTest = GetMapCameraActors();

				if(CamerasToTest.Num() <= 0)
				{
					UE_LOG(LogAutomatedPerfTest, Warning, TEXT("No cameras found in the map %s, skipping to next map"), *CurrentMapPath.GetAssetName());
					NextMap();
					return;
				}

				UE_LOG(LogAutomatedPerfTest, Log, TEXT("Found %hhd cameras to test in map %s"), CamerasToTest.Num(), *CurrentMapPath.GetAssetName());

				GetFirstPlayerController()->SetCinematicMode(true, true, true);
				
				// delay for WarmUpDelay, and call RunTest
				FTimerHandle UnusedHandle;
				GetWorld()->GetTimerManager().SetTimer(UnusedHandle, this, &UAutomatedStaticCameraPerfTestBase::RunTest, 1.0, false, Settings->WarmUpTime);
			}
			// if we have an invalid world, we can't run the test, so we should bail out
			else
			{
				UE_LOG(LogAutomatedPerfTest, Error, TEXT("Invalid World when starting UAutomatedStaticCameraPerfTest, exiting..."));
				EndAutomatedPerfTest(1);
			}
		}
		else
		{
			UE_LOG(LogAutomatedPerfTest, Log, TEXT("Current Map Name %s is not expected %s, calling NextMap."), *GetCurrentMap(), *CurrentMapPath.GetAssetName())
			NextMap();
		}
	}
	else
	{
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("Current Map Path has not been set, calling NextMap to trigger."))
		NextMap();
	}
}

void UAutomatedStaticCameraPerfTestBase::RunTest()
{
	Super::RunTest();

	UE_LOG(LogAutomatedPerfTest, Log, TEXT("UAutomatedStaticCameraPerfTestBase::RunTest"))
	
	FTimerHandle UnusedHandle;
	GetWorld()->GetTimerManager().SetTimer(UnusedHandle, this, &UAutomatedStaticCameraPerfTestBase::SetUpNextCamera, 1.0, false, Settings->WarmUpTime);
}

FString UAutomatedStaticCameraPerfTestBase::GetTestID()
{
	return Super::GetTestID() + "_StaticCamera_" + GetCurrentMap();
}

void UAutomatedStaticCameraPerfTestBase::SetUpNextCamera()
{
	if(CamerasToTest.Num() <= 0)
	{
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("No more cameras left to test, moving to next map."))
		NextMap();
		return;
	}

	CurrentCamera = CamerasToTest.Pop();

	UE_LOG(LogAutomatedPerfTest, Log, TEXT("Setting up %s to test"), *CurrentCamera->GetActorNameOrLabel());

	GetFirstPlayerController()->SetViewTarget(CurrentCamera);
	FVector ViewLocation = CurrentCamera->GetActorLocation();
	FRotator ViewRotation = CurrentCamera->GetActorRotation();

	FString GoString = FString::Printf(TEXT("BugItGo %f %f %f %f %f %f"), ViewLocation.X, ViewLocation.Y, ViewLocation.Z, ViewRotation.Pitch, ViewRotation.Yaw, ViewRotation.Roll);
	
	FTimerHandle UnusedHandle;
	GetWorld()->GetTimerManager().SetTimer(UnusedHandle, this, &UAutomatedStaticCameraPerfTestBase::EvaluateCamera, 1.0, false, Settings->WarmUpTime);
}

void UAutomatedStaticCameraPerfTestBase::EvaluateCamera()
{
	MarkCameraStart();

	FTimerHandle UnusedHandle;
	GetWorld()->GetTimerManager().SetTimer(UnusedHandle, this, &UAutomatedStaticCameraPerfTestBase::FinishCamera, 1.0, false, Settings->SoakTime);
}

void UAutomatedStaticCameraPerfTestBase::FinishCamera()
{
	MarkCameraEnd();
	
	if(Settings->bCaptureScreenshots)
	{
		FTimerHandle UnusedHandle;
		GetWorld()->GetTimerManager().SetTimer(UnusedHandle, this, &UAutomatedStaticCameraPerfTestBase::ScreenshotCamera, 1.0, false, Settings->CooldownTime);		
	}
	else
	{
		FTimerHandle UnusedHandle;
		GetWorld()->GetTimerManager().SetTimer(UnusedHandle, this, &UAutomatedStaticCameraPerfTestBase::SetUpNextCamera, 1.0, false, Settings->CooldownTime);
	}
}

void UAutomatedStaticCameraPerfTestBase::ScreenshotCamera()
{
	TakeScreenshot(GetCurrentCameraRegionName());

	FTimerHandle UnusedHandle;
	GetWorld()->GetTimerManager().SetTimer(UnusedHandle, this, &UAutomatedStaticCameraPerfTestBase::SetUpNextCamera, 1.0, false, Settings->CooldownTime);
}

void UAutomatedStaticCameraPerfTestBase::NextMap()
{
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("UAutomatedStaticCameraPerfTestBase::NextMap"))

	if(MapsToTest.Num() > 0)
	{
		CurrentMapPath = MapsToTest.Pop();
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("Setting up test for Map %s"), *CurrentMapPath.GetAssetName())

		// no need to prepend this with a ? since OpenLevel handles that part for us
		FString OptionsString;
		if(!Settings->GameModeOverride.IsEmpty())
		{
			UE_LOG(LogAutomatedPerfTest, Log, TEXT("Game Mode overridden to %s"), *Settings->GameModeOverride)
			OptionsString += "game=" + Settings->GameModeOverride;
		}
		
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("Opening map %s%s"), *CurrentMapPath.GetAssetName(), *OptionsString);
		UGameplayStatics::OpenLevel(AutomatedPerfTest::FindCurrentWorld(), *CurrentMapPath.GetAssetName(), true, OptionsString);
	}
	else
	{
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("UAutomatedStaticCameraPerfTestBase::NextMap, all maps complete, exiting after delay."))
		TriggerExitAfterDelay();
	}
}

TArray<ACameraActor*> UAutomatedStaticCameraPerfTestBase::GetMapCameraActors()
{
	UE_LOG(LogAutomatedPerfTest, Warning, TEXT("GetMapCameraActors called in base class UAutomatedStaticCameraPerfTestBase, please ensure you've overridden this function in the subclass, and you're not using the base class as your test controller"));
	return TArray<ACameraActor*>();
}

ACameraActor* UAutomatedStaticCameraPerfTestBase::GetCurrentCamera() const
{
	return CurrentCamera;
}

FString UAutomatedStaticCameraPerfTestBase::GetCurrentCameraRegionName()
{
	return GetCurrentCamera()->GetActorNameOrLabel();
}

FString UAutomatedStaticCameraPerfTestBase::GetCurrentCameraRegionFullName()
{
	return GetTestID() + "_" + GetCurrentCameraRegionName();
}

void UAutomatedStaticCameraPerfTestBase::MarkCameraStart()
{
	// safety check on the current material
	if(CurrentCamera)
	{
		if(RequestsInsightsTrace())
		{
			TRACE_BEGIN_REGION(*GetCurrentCameraRegionName());
		}
#if CSV_PROFILER
		if(RequestsCSVProfiler())
		{
			if(GetCSVOutputMode() == EAutomatedPerfTestCSVOutputMode::Granular)
			{
				TryStartCSVProfiler(GetCurrentCameraRegionFullName());
			}
			CSV_EVENT(AutomatedPerfTest, TEXT("START_%s"), *GetCurrentCameraRegionName())
		}
#endif
	}
}

void UAutomatedStaticCameraPerfTestBase::MarkCameraEnd()
{
	// safety check on the current material
	if(CurrentCamera)
	{
		if(RequestsInsightsTrace())
		{
			TRACE_END_REGION(*GetCurrentCameraRegionName());
		}
#if CSV_PROFILER
		if(RequestsCSVProfiler())
		{
			CSV_EVENT(AutomatedPerfTest, TEXT("END_%s"), *GetCurrentCameraRegionName())
			if(GetCSVOutputMode() == EAutomatedPerfTestCSVOutputMode::Granular)
			{
				TryStopCSVProfiler();
			}
		}
#endif
	}
}

void UAutomatedStaticCameraPerfTestBase::OnInit()
{
	Super::OnInit();
	
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("UAutomatedStaticCameraPerfTestBase::OnInit"));

	Settings = GetDefault<UAutomatedStaticCameraPerfTestProjectSettings>();

	SetCSVOutputMode(Settings->CSVOutputMode);
	
	// if an explicit map/sequence name was set from commandline, use this to override the test
	if (FParse::Value(FCommandLine::Get(), TEXT("AutomatedPerfTest.StaticCameraPerfTest.MapName="), CurrentMapName))
	{
		for (FSoftObjectPath MapPath : Settings->MapsToTest)
		{
			if(MapPath.GetAssetName() == CurrentMapName)
			{
				MapsToTest.Add(MapPath);
			}
		}
		if(MapsToTest.Num() == 0)
		{
			UE_LOG(LogAutomatedPerfTest, Error, TEXT("Couldn't find a map name matching %s in Static Camera Maps to Test setting. Exiting."), *CurrentMapName)
			EndAutomatedPerfTest(1);
		}
	}
	// otherwise, use all the maps defined in project settings
	else
	{
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("No map name specified, testing all maps."))
		for(FSoftObjectPath MapName : Settings->MapsToTest)
		{
			MapsToTest.Add(MapName);
		}
	}
	
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("Initialized with %hhd MapSequence combos"), MapsToTest.Num());
	
	// early out if there aren't actually any maps set in project settings
	if(Settings->MapsToTest.Num() < 0)
	{
		UE_LOG(LogAutomatedPerfTest, Error, TEXT("No maps defined in the project's Automated Perf Test | Static Camera settings. Exiting test early."));
		EndAutomatedPerfTest(1);
	}
}

void UAutomatedStaticCameraPerfTestBase::UnbindAllDelegates()
{
	Super::UnbindAllDelegates();
	
	GetWorld()->GetTimerManager().ClearAllTimersForObject(this);

	CurrentCamera = nullptr;
	CamerasToTest.Empty();
}