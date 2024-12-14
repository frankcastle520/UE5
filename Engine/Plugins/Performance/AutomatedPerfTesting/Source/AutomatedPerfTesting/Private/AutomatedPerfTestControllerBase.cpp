// Copyright Epic Games, Inc. All Rights Reserved.

#include "AutomatedPerfTestControllerBase.h"

#include "AutomatedPerfTesting.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "GameFramework/GameModeBase.h"
#include "AutomatedPerfTestInterface.h"
#include "AutomatedPerfTestProjectSettings.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "TimerManager.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Async/TaskGraphInterfaces.h"
#include "VideoRecordingSystem.h"
#include "PlatformFeatures.h"
#include "Engine/Engine.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/TraceScreenshot.h"
#include "UnrealClient.h"


DEFINE_LOG_CATEGORY(LogAutomatedPerfTest)
CSV_DEFINE_CATEGORY(AutomatedPerfTest, true);


void UAutomatedPerfTestControllerBase::OnPreWorldInitializeInternal(UWorld* World, const UWorld::InitializationValues IVS)
{
	TryEarlyExec(World);
	OnPreWorldInitialize(World);
}

void UAutomatedPerfTestControllerBase::OnPreWorldInitialize(UWorld* World)
{
	check(World);
	World->GameStateSetEvent.AddUObject(this, &ThisClass::OnGameStateSet);
	World->OnWorldBeginPlay.AddUObject(this, &ThisClass::OnWorldBeginPlay);
}

void UAutomatedPerfTestControllerBase::TryEarlyExec(UWorld* const World)
{
	check(World);
	
	if (GEngine)
	{
		// Search the list of deferred commands
		const TArray<FString>& DeferredCmds = GEngine->DeferredCommands;
		TArray<int32> ExecutedIndices;
		for (int32 DeferredCmdIndex = 0; DeferredCmdIndex < DeferredCmds.Num(); ++DeferredCmdIndex)
		{
			// If the deferred command is one that should be executed early
			const FString& DeferredCmd = DeferredCmds[DeferredCmdIndex];
			if (CmdsToExecEarly.ContainsByPredicate([&DeferredCmd](const FString& CmdToFind) { return DeferredCmd.StartsWith(CmdToFind); }))
			{
				UE_LOG(LogAutomatedPerfTest, Log, TEXT("EarlyExec: executing '%s' early."), *DeferredCmd);
				GEngine->Exec(World, *DeferredCmd);
				ExecutedIndices.Push(DeferredCmdIndex);
			}
		}

		// Remove the executed commands from the list of deferred commands
		// Note: This is done in reverse order to ensure the cached indices remain valid
		while (!ExecutedIndices.IsEmpty())
		{
			GEngine->DeferredCommands.RemoveAt(ExecutedIndices.Pop());
		}
	}
	else
	{
		UE_LOG(LogAutomatedPerfTest, Warning, TEXT("Attempted EarlyExec without GEngine being ready"))
	}
}

void UAutomatedPerfTestControllerBase::OnWorldBeginPlay()
{
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("OnWorldBeginPlay"));
	SetupTest();
}

void UAutomatedPerfTestControllerBase::OnGameStateSet(AGameStateBase* const GameStateBase)
{
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("Game State Set"));
	if (UWorld* const World = GetWorld())
	{
		World->GameStateSetEvent.RemoveAll(this);
	}
}

UAutomatedPerfTestControllerBase::UAutomatedPerfTestControllerBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, TraceChannels("default,screenshot,stats")
	, bRequestsFPSChart(false)
	, bRequestsInsightsTrace(false)
	, bRequestsCSVProfiler(false)
	, bRequestsVideoCapture(false)
    , CSVOutputMode(EAutomatedPerfTestCSVOutputMode::Single)
{
	// cache this off once, so that it's consistent throughout a session
	TestDatetime = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")); 
}

FString UAutomatedPerfTestControllerBase::GetTestName()
{
	return TestName;
}

FString UAutomatedPerfTestControllerBase::GetDeviceProfile()
{
	return DeviceProfileOverride.IsEmpty() ? UDeviceProfileManager::Get().GetActiveDeviceProfileName() : DeviceProfileOverride;  
}

FString UAutomatedPerfTestControllerBase::GetTestID()
{
	const TArray<FString> TestCaseIDElements = {FApp::GetBuildVersion(),
										  FPlatformProperties::PlatformName(),
										  TestDatetime,
								          *GetDeviceProfile(),
								          *GetTestName()};

	// TODO make this format definable in project settings, similar to how MovieRenderQueue does it for renders
	// construct a unique ID of the form BuildVersion_PlatformName_YYYYMMDD-HHMMSS_DeviceProfile_TestName
	FString TestCaseID = FString::Join(TestCaseIDElements, TEXT("_"));

	return TestCaseID;
}

FString UAutomatedPerfTestControllerBase::GetOverallRegionName()
{
	return GetTestID() + "_" + "Overall";
}

FString UAutomatedPerfTestControllerBase::GetTraceChannels()
{
	return TraceChannels;
}

bool UAutomatedPerfTestControllerBase::RequestsInsightsTrace() const
{
	return bRequestsInsightsTrace;
}

bool UAutomatedPerfTestControllerBase::RequestsCSVProfiler() const
{
	return bRequestsCSVProfiler;
}

bool UAutomatedPerfTestControllerBase::RequestsFPSChart() const
{
	return bRequestsFPSChart;
}

bool UAutomatedPerfTestControllerBase::RequestsVideoCapture() const
{
	return bRequestsVideoCapture;
}

bool UAutomatedPerfTestControllerBase::TryStartInsightsTrace()
{
	const FString TraceFileName = GetTestID() + ".utrace";
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("Attempting to start insights trace to file %s with channels %s"), *TraceFileName, *GetTraceChannels());
	return FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::File, *TraceFileName, *GetTraceChannels());
}

bool UAutomatedPerfTestControllerBase::TryStopInsightsTrace()
{
	if(FTraceAuxiliary::IsConnected())
	{
		return FTraceAuxiliary::Stop();
	}
	return false;
}

bool UAutomatedPerfTestControllerBase::TryStartCSVProfiler()
{
	return TryStartCSVProfiler(GetTestID());
}

bool UAutomatedPerfTestControllerBase::TryStartCSVProfiler(FString CSVFileName)
{
#if CSV_PROFILER
	if(FCsvProfiler* const CsvProfiler = FCsvProfiler::Get())
	{
		if(!CSVFileName.EndsWith(".csv"))
		{
			CSVFileName += TEXT(".csv");
		}

		UE_LOG(LogAutomatedPerfTest, Log, TEXT("Attempting to start CSV Profile to file %s"), *CSVFileName);
		
		CsvProfiler->BeginCapture(-1, FString(), CSVFileName);
		CsvProfiler->SetDeviceProfileName(GetDeviceProfile());
		
		return CsvProfiler->IsCapturing();
	}
#endif
	UE_LOG(LogAutomatedPerfTest, Warning, TEXT("CSVProfiler Start requested, but not available."))
	return false;
}

bool UAutomatedPerfTestControllerBase::TryStopCSVProfiler()
{
#if CSV_PROFILER
	if(FCsvProfiler* const CsvProfiler = FCsvProfiler::Get())
	{
		const FGraphEventRef AutomatedPerfTestEndEvent = FGraphEvent::CreateGraphEvent();
		CsvProfiler->EndCapture(AutomatedPerfTestEndEvent);
		return true;
	}
#endif
	UE_LOG(LogAutomatedPerfTest, Warning, TEXT("CSVProfiler Stop requested, but not available."))
	return false;
}

bool UAutomatedPerfTestControllerBase::TryStartFPSChart()
{
	// don't open the folder the FPS chart gets sent to on exit, as it can cause issues when running unattended
	GEngine->Exec(GetWorld(), TEXT("t.FPSChart.OpenFolderOnDump 0"));
	GEngine->StartFPSChart(*GetOverallRegionName(), false);

	return true;
}

bool UAutomatedPerfTestControllerBase::TryStopFPSChart()
{
	GEngine->StopFPSChart(*GetOverallRegionName());
	return true;
}

bool UAutomatedPerfTestControllerBase::TryStartVideoCapture()
{
	if (IVideoRecordingSystem* const VideoRecordingSystem = IPlatformFeaturesModule::Get().GetVideoRecordingSystem())
	{
		const EVideoRecordingState RecordingState = VideoRecordingSystem->GetRecordingState();

		if (RecordingState == EVideoRecordingState::None)
		{
			VideoRecordingSystem->EnableRecording(true);
			
			VideoRecordingTitle = FText::FromString(FPaths::Combine(FPaths::ProjectSavedDir(), GetTestID()));
			const FVideoRecordingParameters VideoRecordingParameters(VideoRecordingSystem->GetMaximumRecordingSeconds(), true, false, false, FPlatformMisc::GetPlatformUserForUserIndex(0));
			VideoRecordingSystem->NewRecording(*GetTestID(), VideoRecordingParameters);

			if (VideoRecordingSystem->IsEnabled())
			{
				if (VideoRecordingSystem->GetRecordingState() == EVideoRecordingState::Starting || VideoRecordingSystem->GetRecordingState() == EVideoRecordingState::Recording)
				{
					UE_LOG(LogAutomatedPerfTest, Log, TEXT("Starting video recording %s..."), *GetTestID());
					return true;
				}
				UE_LOG(LogAutomatedPerfTest, Warning, TEXT("Failed to start video recording %s. Current state is %i"), *GetTestID(), VideoRecordingSystem->GetRecordingState());
			}
			else
			{
				UE_LOG(LogAutomatedPerfTest, Warning, TEXT("Video recording could not be enabled."));
			}
		}
		else
		{
			UE_LOG(LogAutomatedPerfTest, Warning, TEXT("Could not start a new recording, may be already recording."));
		}
	}
	else
	{
		UE_LOG(LogAutomatedPerfTest, Warning, TEXT("Video recording system is null."));
	}

	return false;	
}

bool UAutomatedPerfTestControllerBase::TryFinalizingVideoCapture(const bool bStopAutoContinue/*=false*/)
{
	if (IVideoRecordingSystem* const VideoRecordingSystem = IPlatformFeaturesModule::Get().GetVideoRecordingSystem())
	{
		if (VideoRecordingSystem->GetRecordingState() != EVideoRecordingState::None)
		{
			VideoRecordingSystem->FinalizeRecording(true, VideoRecordingTitle, FText::GetEmpty(), bStopAutoContinue);

			if (VideoRecordingSystem->GetRecordingState() == EVideoRecordingState::Finalizing)
			{
				UE_LOG(LogAutomatedPerfTest, Log, TEXT("Finalizing recording..."));
				VideoRecordingSystem->GetOnVideoRecordingFinalizedDelegate().AddUObject(this, &ThisClass::OnVideoRecordingFinalized);
				return true;
			}
			else
			{
				UE_LOG(LogAutomatedPerfTest, Warning, TEXT("Attempted to finalize video recording, but current state %i is not %i"), VideoRecordingSystem->GetRecordingState(), EVideoRecordingState::Finalizing)
			}
		}
		else
		{
			UE_LOG(LogAutomatedPerfTest, Warning, TEXT("Attempted to finalize video recording, but state is %i"), VideoRecordingSystem->GetRecordingState())
		}
	}
	else
	{
		UE_LOG(LogAutomatedPerfTest, Warning, TEXT("Video recording system is null."));
	}

	return false;	
}

void UAutomatedPerfTestControllerBase::SetupTest()
{
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("Base:: SetupTest"));
	if(RequestsInsightsTrace())
	{
		TryStartInsightsTrace();
	}

	if(RequestsCSVProfiler() && CSVOutputMode ==  EAutomatedPerfTestCSVOutputMode::Single)
	{
		TryStartCSVProfiler();
	}

	if(RequestsFPSChart())
	{
		TryStartFPSChart();
	}

	if(RequestsVideoCapture())
	{
		TryStartVideoCapture();
	}
	
	GameMode = GetWorld() ? GetWorld()->GetAuthGameMode() : NULL;

	if(GameMode && GameMode->GetClass()->ImplementsInterface(UAutomatedPerfTestInterface::StaticClass()))
	{
		IAutomatedPerfTestInterface::Execute_SetupTest(GameMode);
	}

	// Subclasses should implement their own transitions from SetupTest to RunTest depending on their needs

	UE_LOG(LogAutomatedPerfTest, Log, TEXT("CSV Output Mode: %s"), *UEnum::GetValueAsString(CSVOutputMode))
}

void UAutomatedPerfTestControllerBase::RunTest()
{
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("Base:: RunTest"));
	if(RequestsInsightsTrace())
	{
		TRACE_BEGIN_REGION(*GetOverallRegionName());
	}

	if(RequestsCSVProfiler())
	{
		if(CSVOutputMode == EAutomatedPerfTestCSVOutputMode::Separate)
		{
			TryStartCSVProfiler();
		}
		CSV_EVENT(AutomatedPerfTest, TEXT("START"), *GetOverallRegionName())
	}
	
	if(GameMode && GameMode->GetClass()->ImplementsInterface(UAutomatedPerfTestInterface::StaticClass()))
	{
		IAutomatedPerfTestInterface::Execute_RunTest(GameMode);
	}
}

void UAutomatedPerfTestControllerBase::TeardownTest(bool bExitAfterTeardown)
{
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("Base:: TeardownTest"));
	
	if(RequestsInsightsTrace())
	{
		TRACE_END_REGION(*GetOverallRegionName());
	}
	
	if(RequestsCSVProfiler())
	{
		CSV_EVENT(AutomatedPerfTest, TEXT("END"), *GetOverallRegionName())
		if(CSVOutputMode == EAutomatedPerfTestCSVOutputMode::Separate)
		{
			TryStopCSVProfiler();
		}
	}

	if(RequestsFPSChart())
	{
		TryStopFPSChart();
	}

	if(RequestsVideoCapture())
	{
		if(!TryFinalizingVideoCapture())
		{
			UE_LOG(LogAutomatedPerfTest, Warning, TEXT("Attempted to finalize requested video capture, but failed."))
		}
	}

	if(GameMode && GameMode->GetClass()->ImplementsInterface(UAutomatedPerfTestInterface::StaticClass()))
	{
		IAutomatedPerfTestInterface::Execute_TeardownTest(GameMode);
	}

	if(bExitAfterTeardown)
	{
		TriggerExitAfterDelay();
	}
}

void UAutomatedPerfTestControllerBase::TriggerExitAfterDelay()
{
	FTimerHandle UnusedHandle;
	
	GetWorld()->GetTimerManager().SetTimer(UnusedHandle, this, &UAutomatedPerfTestControllerBase::Exit, 1.0, false, GetDefault<UAutomatedPerfTestProjectSettings>()->TeardownToExitDelay);	
}

void UAutomatedPerfTestControllerBase::Exit()
{
	if(RequestsInsightsTrace())
	{
		TryStopInsightsTrace();
	}

	if(RequestsCSVProfiler())
	{
		CSV_EVENT(AutomatedPerfTest, TEXT("END"), *GetOverallRegionName())
		if(CSVOutputMode == EAutomatedPerfTestCSVOutputMode::Single)
		{
			TryStopCSVProfiler();
		}
	}

	if(GameMode && GameMode->GetClass()->ImplementsInterface(UAutomatedPerfTestInterface::StaticClass()))
	{
		IAutomatedPerfTestInterface::Execute_Exit(GameMode);
	}

	if(RequestsCSVProfiler())
	{
#if CSV_PROFILER
		if(FCsvProfiler::Get()->IsWritingFile())
		{
			UE_LOG(LogAutomatedPerfTest, Log, TEXT("CSVProfile requested, and test is exiting, but CSV Profiler isn't done writing."))
			// if we requested a CSV Profile, and the CSV Profiler is still writing the file, add a lambda to call the EndAutomatedPerfTest function
			// so that we don't exit out of the application before the CSV Profiler is done
			
			// TODO investigate if this is necessary if we set csv.BlockOnCaptureEnd
			FCsvProfiler::Get()->OnCSVProfileFinished().AddLambda([this](const FString& Filename)
			{
				EndAutomatedPerfTest();
			});
			return;
		}
#endif
	}
	
	EndAutomatedPerfTest();
}

AGameModeBase* UAutomatedPerfTestControllerBase::GetGameMode() const
{
	return GameMode;
}

void UAutomatedPerfTestControllerBase::TakeScreenshot(FString ScreenshotName)
{
	if(RequestsInsightsTrace())
	{
		// trace screenshots are disabled in shipping by default
#if UE_SCREENSHOT_TRACE_ENABLED
		FTraceScreenshot::RequestScreenshot(ScreenshotName, false, LogAutomatedPerfTest);
#endif
	}
	else
	{
		FScreenshotRequest::RequestScreenshot(ScreenshotName, false, false);
	}
}

void UAutomatedPerfTestControllerBase::SetCSVOutputMode(EAutomatedPerfTestCSVOutputMode NewOutputMode)
{
	CSVOutputMode = NewOutputMode;	
}

void UAutomatedPerfTestControllerBase::OnInit()
{
	Super::OnInit();

	UE_LOG(LogAutomatedPerfTest, Log, TEXT("Base:: OnInit"));

	// don't stop on separator because this will come in comma-separated
	FParse::Value(FCommandLine::Get(), TEXT("AutomatedPerfTest.TraceChannels="), TraceChannels, false);
	
	FParse::Value(FCommandLine::Get(), TEXT("AutomatedPerfTest.DeviceProfileOverride="), DeviceProfileOverride);
	FParse::Value(FCommandLine::Get(), TEXT("AutomatedPerfTest.TestName="), TestName);
	
	if(FParse::Param(FCommandLine::Get(), TEXT("AutomatedPerfTest.DoInsightsTrace")))
	{
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("Insights Trace Requested"))
		bRequestsInsightsTrace = true;
	}
	if(FParse::Param(FCommandLine::Get(), TEXT("AutomatedPerfTest.DoCSVProfiler")))
	{
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("CSV Profiler Requested"))
		bRequestsCSVProfiler = true;
	}
	if(FParse::Param(FCommandLine::Get(), TEXT("AutomatedPerfTest.DoFPSChart")))
	{
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("FPSCharts Requested"))
		bRequestsFPSChart = true;
	}
	if(FParse::Param(FCommandLine::Get(), TEXT("AutomatedPerfTest.DoVideoCapture")))
	{
		UE_LOG(LogAutomatedPerfTest, Log, TEXT("Video Capture Requested"))
		bRequestsVideoCapture = true;
	}
	
	FWorldDelegates::OnPreWorldInitialization.AddUObject(this, &ThisClass::OnPreWorldInitializeInternal);
}

void UAutomatedPerfTestControllerBase::OnTick(float TimeDelta)
{
	Super::OnTick(TimeDelta);
	
	MarkHeartbeatActive();
}

void UAutomatedPerfTestControllerBase::OnStateChange(FName OldState, FName NewState)
{
	Super::OnStateChange(OldState, NewState);
}

void UAutomatedPerfTestControllerBase::OnPreMapChange()
{
	Super::OnPreMapChange();
}

void UAutomatedPerfTestControllerBase::BeginDestroy()
{
	UnbindAllDelegates();
	
	Super::BeginDestroy();
}

void UAutomatedPerfTestControllerBase::EndAutomatedPerfTest(const int32 ExitCode)
{
	UnbindAllDelegates();

	UE_LOG(LogAutomatedPerfTest, Log, TEXT("Test ID %s completed, requesting exit..."), *GetTestID());
	
	EndTest(ExitCode);
}

void UAutomatedPerfTestControllerBase::OnVideoRecordingFinalized(bool Succeeded, const FString& FilePath)
{
	if(!Succeeded)
	{
		UE_LOG(LogAutomatedPerfTest, Warning, TEXT("Video Capture finalized, but did not succeed"))
	}
/* TODO moving this to automation layer due to file access restrictions
	FString SrcFilePath;
	FString FileName;
	FString Extension;
	FPaths::Split(FilePath, SrcFilePath, FileName, Extension);

	FString DestFileName = FileName + "." + Extension;
	
	const FString DestinationDir = FPaths::Combine(FPaths::ProjectSavedDir(), "Videos");
	const FString DestinationFilePath = FPaths::Combine(DestinationDir, DestFileName);
	
	UE_LOG(LogAutomatedPerfTest, Log, TEXT("Copying video file %s to Saved: %s"), *FilePath, *DestinationFilePath);
	
	if(IFileManager::Get().Copy(*DestinationFilePath, *FilePath, 1, 1) != COPY_OK)
	{
		UE_LOG(LogAutomatedPerfTest, Warning, TEXT("Failed to copy video file"));
	}
	*/
}

void UAutomatedPerfTestControllerBase::UnbindAllDelegates()
{
	if(UWorld* const World = GetWorld())
	{
		World->OnWorldBeginPlay.RemoveAll(this);
		World->GameStateSetEvent.RemoveAll(this);
	}

#if CSV_PROFILER
	if (FCsvProfiler* const CsvProfiler = FCsvProfiler::Get())
	{
		CsvProfiler->OnCSVProfileFinished().Remove(CsvProfilerDelegateHandle);
	}
#endif // CSV_PROFILER

	if(RequestsVideoCapture())
	{
		if (IVideoRecordingSystem* const VideoRecordingSystem = IPlatformFeaturesModule::Get().GetVideoRecordingSystem())
		{
			VideoRecordingSystem->GetOnVideoRecordingFinalizedDelegate().RemoveAll(this);
		}
	}
}

EAutomatedPerfTestCSVOutputMode UAutomatedPerfTestControllerBase::GetCSVOutputMode() const
{
	return CSVOutputMode;
}
