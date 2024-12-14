// Copyright Epic Games, Inc. All Rights Reserved.

#include "AutomatedMaterialPerfTest.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "AutomatedPerfTesting.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/Player.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"

UAutomatedMaterialPerfTestProjectSettings::UAutomatedMaterialPerfTestProjectSettings(const FObjectInitializer& Initializer)
	: Super(Initializer)
	, bCaptureScreenshots(true)
	, WarmUpTime(5.0)
	, SoakTime(5.0)
	, CooldownTime(1.0)
	, MaterialPerformanceTestMap(FSoftObjectPath("/AutomatedPerfTesting/Tests/Materials/AutomatedMaterialPerfTestDefaultMap.AutomatedMaterialPerfTestDefaultMap"))
	, CameraProjectionMode(ECameraProjectionMode::Type::Orthographic)
	, PlateDistanceFromCamera(512.0)
	, MaterialPlate(FSoftObjectPath("/AutomatedPerfTesting/Tests/Materials/SM_AutomatedMaterialPerfTestDefaultPlate.SM_AutomatedMaterialPerfTestDefaultPlate"))
	, CSVOutputMode(EAutomatedPerfTestCSVOutputMode::Granular)
{
}

void UAutomatedMaterialPerfTest::SetupTest()
{
	// load up into the map defined in project settings
	if(GetCurrentMap() == Settings->MaterialPerformanceTestMap.GetAssetName())
	{
		Super::SetupTest();

		UE_LOG(LogAutomatedPerfTest, Log, TEXT("UAutomatedMaterialPerfTest::SetupTest"))
		// make sure the world exists, then create a sequence player
		if(UWorld* const World = GetWorld())
		{
			// Also load and verify the material plate mesh
			UE_LOG(LogAutomatedPerfTest, Log, TEXT("Loading material plate mesh: %s"), *Settings->MaterialPlate.ToString())
			UStaticMesh* LoadedMaterialPlateMesh = LoadObject<UStaticMesh>(NULL, *Settings->MaterialPlate.ToString(), NULL, LOAD_None, NULL);
			check(LoadedMaterialPlateMesh);

			// reset the material index;
			CurrentMaterialIndex = -1;
			
			// hide the pawn so it doesn't interfere with screenshots
			GetFirstPlayerController()->GetPawn()->SetHidden(true);
			
			// then spawn the camera into the world and set it up 
			Camera = GetWorld()->SpawnActor<ACameraActor>();
			Camera->GetCameraComponent()->SetProjectionMode(Settings->CameraProjectionMode);
			Camera->GetCameraComponent()->SetOrthoWidth(Settings->PlateDistanceFromCamera);

			// and spawn the material plate into the world, and move it PlateDistanceFromCamera away down the X axis
			MaterialPlate = GetWorld()->SpawnActor<AStaticMeshActor>();
			MaterialPlate->SetMobility(EComponentMobility::Type::Movable);
			MaterialPlate->GetStaticMeshComponent()->SetStaticMesh(LoadedMaterialPlateMesh);
			MaterialPlate->SetActorLocation(FVector(Settings->PlateDistanceFromCamera, 0.0, 0.0));
			float Scale = Settings->PlateDistanceFromCamera / (LoadedMaterialPlateMesh->GetBounds().BoxExtent.Y);

			UE_LOG(LogAutomatedPerfTest, Verbose, TEXT("SizeY = %f, Scale = %f"), LoadedMaterialPlateMesh->GetBounds().BoxExtent.Y, Scale);
			
			MaterialPlate->SetActorScale3D(FVector(1.0, Scale, Scale));
			
			GetFirstPlayerController()->SetViewTarget(Camera);
			
        	// delay for WarmUpDelay, and call RunTest
        	FTimerHandle UnusedHandle;
        	GetWorld()->GetTimerManager().SetTimer(UnusedHandle, this, &UAutomatedMaterialPerfTest::RunTest, 1.0, false, Settings->WarmUpTime);
		}
		// if we have an invalid world, we can't run the test, so we should bail out
		else
		{
			UE_LOG(LogAutomatedPerfTest, Error, TEXT("Invalid World when starting UAutomatedMaterialPerfTest, exiting..."));
			EndAutomatedPerfTest(1);
		}
	}
	else
	{
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("Current Map Name %s is not the expected %s, loading the material performance test map"), *GetCurrentMap(), *Settings->MaterialPerformanceTestMap.GetAssetName())
		OpenMaterialPerformanceTestMap();
	}
}

void UAutomatedMaterialPerfTest::RunTest()
{
	Super::RunTest();

	UE_LOG(LogAutomatedPerfTest, Log, TEXT("UAutomatedMaterialPerfTest::RunTest"))
	
	FTimerHandle UnusedHandle;
	GetWorld()->GetTimerManager().SetTimer(UnusedHandle, this, &UAutomatedMaterialPerfTest::SetUpNextMaterial, 1.0, false, Settings->WarmUpTime);
}

FString UAutomatedMaterialPerfTest::GetTestID()
{
	return Super::GetTestID() + "_Materials";
}

void UAutomatedMaterialPerfTest::SetUpNextMaterial()
{
	CurrentMaterialIndex += 1;
	
	if(CurrentMaterialIndex >= Settings->MaterialsToTest.Num())
	{
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("No more materials left to test, moving to teardown."))
		TeardownTest();
		return;
	}
	
	// load the next material
	FSoftObjectPath MaterialSoftObjectPath = Settings->MaterialsToTest[CurrentMaterialIndex];
	
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("Loading material: %s"), *MaterialSoftObjectPath.ToString())
	CurrentMaterial = LoadObject<UMaterialInterface>(NULL, *MaterialSoftObjectPath.ToString(), NULL, LOAD_None, NULL);
	check(CurrentMaterial);

	UE_LOG(LogAutomatedPerfTest, Log, TEXT("Applying material: %s"), *CurrentMaterial->GetName());
	
	MaterialPlate->GetStaticMeshComponent()->SetMaterial(0, CurrentMaterial);
	
	FTimerHandle UnusedHandle;
	GetWorld()->GetTimerManager().SetTimer(UnusedHandle, this, &UAutomatedMaterialPerfTest::EvaluateMaterial, 1.0, false, Settings->WarmUpTime);
}

void UAutomatedMaterialPerfTest::EvaluateMaterial()
{
	MarkMaterialStart();

	FTimerHandle UnusedHandle;
	GetWorld()->GetTimerManager().SetTimer(UnusedHandle, this, &UAutomatedMaterialPerfTest::FinishMaterialEvaluation, 1.0, false, Settings->SoakTime);
}

void UAutomatedMaterialPerfTest::FinishMaterialEvaluation()
{
	MarkMaterialEnd();
	
	if(Settings->bCaptureScreenshots)
	{
		FTimerHandle UnusedHandle;
		GetWorld()->GetTimerManager().SetTimer(UnusedHandle, this, &UAutomatedMaterialPerfTest::ScreenshotMaterial, 1.0, false, Settings->CooldownTime);		
	}
	else
	{
		FTimerHandle UnusedHandle;
		GetWorld()->GetTimerManager().SetTimer(UnusedHandle, this, &UAutomatedMaterialPerfTest::SetUpNextMaterial, 1.0, false, Settings->CooldownTime);
	}
}

void UAutomatedMaterialPerfTest::ScreenshotMaterial()
{
	TakeScreenshot(GetCurrentMaterialRegionName());

	// start a timer to trigger the disk screenshot, since trace screenshots and disk screenshots can't happen in the same frame
	FTimerHandle UnusedHandle;
	GetWorld()->GetTimerManager().SetTimer(UnusedHandle, this, &UAutomatedMaterialPerfTest::SetUpNextMaterial, 1.0, false, .1);
}

UMaterialInterface* UAutomatedMaterialPerfTest::GetCurrentMaterial() const
{
	return CurrentMaterial;
}

FString UAutomatedMaterialPerfTest::GetCurrentMaterialRegionName()
{
	return GetCurrentMaterial()->GetName();
}

FString UAutomatedMaterialPerfTest::GetCurrentMaterialRegionFullName()
{
	return GetTestID() + "_" + GetCurrentMaterialRegionName();
}

void UAutomatedMaterialPerfTest::MarkMaterialStart()
{
	// safety check on the current material
	if(CurrentMaterial)
	{
		if(RequestsInsightsTrace())
		{
			TRACE_BEGIN_REGION(*GetCurrentMaterialRegionName());
		}
#if CSV_PROFILER
		if(RequestsCSVProfiler())
		{
			if(GetCSVOutputMode() == EAutomatedPerfTestCSVOutputMode::Granular)
			{
				TryStartCSVProfiler(GetCurrentMaterialRegionFullName());
			}
			CSV_EVENT(AutomatedPerfTest, TEXT("START_%s"), *GetCurrentMaterialRegionName())
		}
#endif
	}
}

void UAutomatedMaterialPerfTest::MarkMaterialEnd()
{
	// safety check on the current material
	if(CurrentMaterial)
	{
		if(RequestsInsightsTrace())
		{
			TRACE_END_REGION(*GetCurrentMaterialRegionName());
		}
#if CSV_PROFILER
		if(RequestsCSVProfiler())
		{
			CSV_EVENT(AutomatedPerfTest, TEXT("END_%s"), *GetCurrentMaterialRegionName())
			if(GetCSVOutputMode() == EAutomatedPerfTestCSVOutputMode::Granular)
			{
				TryStopCSVProfiler();
			}
		}
#endif
	}
}

void UAutomatedMaterialPerfTest::OnInit()
{
	Super::OnInit();
	
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("UAutomatedMaterialPerfTest::OnInit"));

	Settings = GetDefault<UAutomatedMaterialPerfTestProjectSettings>();

	SetCSVOutputMode(Settings->CSVOutputMode);
	
	// early out if there aren't actually any materials
	if(Settings->MaterialsToTest.Num() < 0)
	{
		UE_LOG(LogAutomatedPerfTest, Error, TEXT("No materials defined in the project's Automated Perf Test | Materials settings. Exiting test early."));
		EndAutomatedPerfTest(1);
	}
}

void UAutomatedMaterialPerfTest::UnbindAllDelegates()
{
	Super::UnbindAllDelegates();
	
	GetWorld()->GetTimerManager().ClearAllTimersForObject(this);
}

void UAutomatedMaterialPerfTest::OpenMaterialPerformanceTestMap() const
{
	// no need to prepend this with a ? since OpenLevel handles that part for us
	FString OptionsString;
	if(!Settings->GameModeOverride.IsEmpty())
	{
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("Game Mode overridden to %s"), *Settings->GameModeOverride)
		OptionsString += "game=" + Settings->GameModeOverride;
	}
	
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("Opening map %s%s"), *Settings->MaterialPerformanceTestMap.GetAssetName(), *OptionsString);
	UGameplayStatics::OpenLevel(AutomatedPerfTest::FindCurrentWorld(), *Settings->MaterialPerformanceTestMap.GetAssetName(), true, OptionsString);
}
