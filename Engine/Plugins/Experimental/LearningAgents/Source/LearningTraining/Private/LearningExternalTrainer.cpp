// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningExternalTrainer.h"

#include "LearningExperience.h"
#include "LearningNeuralNetwork.h"
#include "LearningSharedMemoryTraining.h"
#include "LearningSocketTraining.h"

#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include "Sockets.h"
#include "Common/TcpSocketBuilder.h"
#include "SocketSubsystem.h"

namespace UE::Learning
{
	FSharedMemoryTrainerServerProcess::FSharedMemoryTrainerServerProcess(
		const FString& TaskName,
		const FString& CustomTrainerPath,
		const FString& TrainerFileName,
		const FString& PythonExecutablePath,
		const FString& PythonContentPath,
		const FString& InIntermediatePath,
		const int32 ProcessNum,
		const float InTimeout,
		const ESubprocessFlags TrainingProcessFlags)
	{
		check(ProcessNum > 0);

		int32 ProcessIdx = 0;
		FParse::Value(FCommandLine::Get(), TEXT("LearningProcessIdx"), ProcessIdx);
		
		Timeout = InTimeout;
		IntermediatePath = InIntermediatePath;

		if (ProcessIdx == 0)
		{
			// Allocate the control memory if we are the parent UE process
			Controls = SharedMemory::Allocate<2, volatile int32>({ ProcessNum, SharedMemoryTraining::GetControlNum()});
		}
		else
		{
			FGuid ControlsGuid; ensure(FParse::Value(FCommandLine::Get(), TEXT("LearningControlsGuid"), ControlsGuid));
			Controls = SharedMemory::Map<2, volatile int32>(ControlsGuid, { ProcessNum, SharedMemoryTraining::GetControlNum()});

			// We do not want to launch another training process if we are a child process
			return;
		}

		UE_LEARNING_CHECK(FPaths::FileExists(PythonExecutablePath));
		UE_LEARNING_CHECK(FPaths::DirectoryExists(PythonContentPath));

		// We need to zero the control memory before we start the training sub-process since it may contain
		// uninitialized values or those left over from previous runs.
		Array::Zero(Controls.View);

		// Set the ID columns to -1
		for (int32 Index = 0; Index < Controls.View.Num<0>(); Index++)
		{
			Controls.View[Index][(uint8)SharedMemoryTraining::EControls::NetworkId] = -1;
			Controls.View[Index][(uint8)SharedMemoryTraining::EControls::ReplayBufferId] = -1;
		}

		const FString TimeStamp = FDateTime::Now().ToFormattedString(TEXT("%Y-%m-%d_%H-%M-%S"));
		const FString TrainerType = TEXT("SharedMemory");
		ConfigPath = InIntermediatePath / TEXT("Configs") / FString::Printf(TEXT("%s_%s_%s_%s.json"), *TaskName, *TrainerFileName, *TrainerType, *TimeStamp);

		IFileManager& FileManager = IFileManager::Get();
		const FString CommandLineArguments = FString::Printf(TEXT("\"%s\" \"%s\" \"%s\" SharedMemory \"%s\" %i \"%s\""),
			*FileManager.ConvertToAbsolutePathForExternalAppForRead(*(PythonContentPath / TEXT("train.py"))),
			*FileManager.ConvertToAbsolutePathForExternalAppForRead(*CustomTrainerPath),
			*TrainerFileName,
			*Controls.Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces),
			ProcessNum,
			*FileManager.ConvertToAbsolutePathForExternalAppForRead(*ConfigPath));

		TrainingProcess.Launch(
			FileManager.ConvertToAbsolutePathForExternalAppForRead(*PythonExecutablePath),
			CommandLineArguments, 
			TrainingProcessFlags);
	}

	bool FSharedMemoryTrainerServerProcess::IsRunning() const
	{
		return TrainingProcess.IsRunning();
	}

	bool FSharedMemoryTrainerServerProcess::Wait()
	{
		const float SleepTime = 0.001f;
		float WaitTime = 0.0f;

		while (TrainingProcess.Update())
		{
			FPlatformProcess::Sleep(SleepTime);
			WaitTime += SleepTime;

			if (WaitTime > Timeout)
			{
				return false;
			}
		}

		return true;
	}

	void FSharedMemoryTrainerServerProcess::Terminate()
	{
		TrainingProcess.Terminate();
	}

	TSharedMemoryArrayView<2, volatile int32> FSharedMemoryTrainerServerProcess::GetControlsSharedMemoryArrayView() const
	{
		return Controls;
	}

	const FString& FSharedMemoryTrainerServerProcess::GetIntermediatePath() const
	{
		return IntermediatePath;
	}

	const FString& FSharedMemoryTrainerServerProcess::GetConfigPath() const
	{
		return ConfigPath;
	}

	FSubprocess* FSharedMemoryTrainerServerProcess::GetTrainingSubprocess()
	{
		return &TrainingProcess;
	}

	void FSharedMemoryTrainerServerProcess::Deallocate()
	{
		if (Controls.Region != nullptr)
		{
			SharedMemory::Deallocate(Controls);
		}
	}

	void FSharedMemoryTrainer::FSharedMemoryExperienceContainer::Deallocate()
	{
		if (EpisodeStarts.Region != nullptr)
		{
			SharedMemory::Deallocate(EpisodeStarts);
			SharedMemory::Deallocate(EpisodeLengths);
			SharedMemory::Deallocate(EpisodeCompletionModes);

			for(TSharedMemoryArrayView<3, float>& SharedMemoryArrayView : EpisodeFinalObservations)
			{
				SharedMemory::Deallocate(SharedMemoryArrayView);
			}

			for (TSharedMemoryArrayView<3, float>& SharedMemoryArrayView : EpisodeFinalMemoryStates)
			{
				SharedMemory::Deallocate(SharedMemoryArrayView);
			}

			for (TSharedMemoryArrayView<3, float>& SharedMemoryArrayView : Observations)
			{
				SharedMemory::Deallocate(SharedMemoryArrayView);
			}

			for (TSharedMemoryArrayView<3, float>& SharedMemoryArrayView : Actions)
			{
				SharedMemory::Deallocate(SharedMemoryArrayView);
			}

			for (TSharedMemoryArrayView<3, float>& SharedMemoryArrayView : MemoryStates)
			{
				SharedMemory::Deallocate(SharedMemoryArrayView);
			}

			for (TSharedMemoryArrayView<3, float>& SharedMemoryArrayView : Rewards)
			{
				SharedMemory::Deallocate(SharedMemoryArrayView);
			}
		}
	}

	FSharedMemoryTrainer::FSharedMemoryTrainer(
		const FString& InTaskName,
		const int32 InProcessNum,
		const TSharedPtr<UE::Learning::ITrainerProcess>& ExternalTrainerProcess,
		const float InTimeout)
	{
		FSharedMemoryTrainerServerProcess* SharedMemoryTrainerProcess = (FSharedMemoryTrainerServerProcess*)ExternalTrainerProcess.Get();
		if (!SharedMemoryTrainerProcess)
		{
			UE_LOG(LogLearning, Error, TEXT("FSharedMemoryTrainer ctor: Trainer process is nullptr. Is it not a shared memory process?"));
			return;
		}

		check(InProcessNum > 0);

		TaskName = InTaskName;
		ConfigPath = SharedMemoryTrainerProcess->GetConfigPath();
		IntermediatePath = SharedMemoryTrainerProcess->GetIntermediatePath();
		TrainingProcess = SharedMemoryTrainerProcess->GetTrainingSubprocess();
		ProcessNum = InProcessNum;
		Controls = SharedMemoryTrainerProcess->GetControlsSharedMemoryArrayView();
		Timeout = InTimeout;

		ProcessIdx = 0;
		FParse::Value(FCommandLine::Get(), TEXT("LearningProcessIdx"), ProcessIdx);
	}

	FSharedMemoryTrainer::~FSharedMemoryTrainer()
	{
		Terminate();
	}

	ETrainerResponse FSharedMemoryTrainer::Wait()
	{
		return ETrainerResponse::Success;
	}

	bool FSharedMemoryTrainer::HasNetworkOrCompleted()
	{
		return SharedMemoryTraining::HasNetworkOrCompleted(Controls.View[ProcessIdx]);
	}

	void FSharedMemoryTrainer::Terminate()
	{
		Deallocate();
	}

	ETrainerResponse FSharedMemoryTrainer::SendStop()
	{
		check(ProcessIdx != INDEX_NONE);
		checkf(Controls.Region, TEXT("SendStop: Controls Shared Memory Region is nullptr"));

		return SharedMemoryTraining::SendStop(Controls.View[ProcessIdx]);
	}

	ETrainerResponse FSharedMemoryTrainer::SendConfig(const TSharedRef<FJsonObject>& ConfigObject, const ELogSetting LogSettings)
	{
		check(ProcessNum > 0);

		if (ProcessIdx != 0)
		{
			// Only the parent process will send the config
			return ETrainerResponse::Success;
		}

		IFileManager& FileManager = IFileManager::Get();
		ConfigObject->SetStringField(TEXT("IntermediatePath"), *FileManager.ConvertToAbsolutePathForExternalAppForRead(*IntermediatePath));
		ConfigObject->SetBoolField(TEXT("LoggingEnabled"), LogSettings == ELogSetting::Silent ? false : true);

		TSharedPtr<FJsonObject> SharedMemoryObject = MakeShared<FJsonObject>();

		SharedMemoryObject->SetNumberField(TEXT("ProcessNum"), ProcessNum);

		TArray<TSharedPtr<FJsonValue>> NetworkGuidsArray;
		for(int32 Index = 0; Index < NeuralNetworkSharedMemoryArrayViews.Num(); Index++)
		{
			TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
			JsonObject->SetNumberField(TEXT("NetworkId"), Index);
			JsonObject->SetStringField(TEXT("Guid"), *NeuralNetworkSharedMemoryArrayViews[Index].Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces));

			TSharedRef<FJsonValueObject> JsonValue = MakeShared<FJsonValueObject>(JsonObject);
			NetworkGuidsArray.Add(JsonValue);
		}
		SharedMemoryObject->SetArrayField(TEXT("NetworkGuids"), NetworkGuidsArray);

		TArray<TSharedPtr<FJsonValue>> ExperienceContainerObjectsArray;
		for (const FSharedMemoryExperienceContainer& SharedMemoryExperienceContainer : SharedMemoryExperienceContainers)
		{
			TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
			JsonObject->SetStringField(TEXT("EpisodeStartsGuid"), *SharedMemoryExperienceContainer.EpisodeStarts.Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces));
			JsonObject->SetStringField(TEXT("EpisodeLengthsGuid"), *SharedMemoryExperienceContainer.EpisodeLengths.Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces));
			JsonObject->SetStringField(TEXT("EpisodeCompletionModesGuid"), *SharedMemoryExperienceContainer.EpisodeCompletionModes.Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces));

			TArray<TSharedPtr<FJsonValue>> EpisodeFinalObservationsGuidsArray;
			for (const TSharedMemoryArrayView<3, float>& EpisodeFinalObservations : SharedMemoryExperienceContainer.EpisodeFinalObservations)
			{
				EpisodeFinalObservationsGuidsArray.Add(MakeShared<FJsonValueString>(*EpisodeFinalObservations.Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces)));
				
			}
			JsonObject->SetArrayField(TEXT("EpisodeFinalObservationsGuids"), EpisodeFinalObservationsGuidsArray);

			TArray<TSharedPtr<FJsonValue>> EpisodeFinalMemoryStatesGuidsArray;
			for (const TSharedMemoryArrayView<3, float>& EpisodeFinalMemoryStates : SharedMemoryExperienceContainer.EpisodeFinalMemoryStates)
			{
				EpisodeFinalMemoryStatesGuidsArray.Add(MakeShared<FJsonValueString>(*EpisodeFinalMemoryStates.Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces)));

			}
			JsonObject->SetArrayField(TEXT("EpisodeFinalMemoryStatesGuids"), EpisodeFinalMemoryStatesGuidsArray);

			TArray<TSharedPtr<FJsonValue>> ObservationsGuidsArray;
			for (const TSharedMemoryArrayView<3, float>& Observations : SharedMemoryExperienceContainer.Observations)
			{
				ObservationsGuidsArray.Add(MakeShared<FJsonValueString>(*Observations.Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces)));

			}
			JsonObject->SetArrayField(TEXT("ObservationsGuids"), ObservationsGuidsArray);

			TArray<TSharedPtr<FJsonValue>> ActionsGuidsArray;
			for (const TSharedMemoryArrayView<3, float>& Actions : SharedMemoryExperienceContainer.Actions)
			{
				ActionsGuidsArray.Add(MakeShared<FJsonValueString>(*Actions.Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces)));

			}
			JsonObject->SetArrayField(TEXT("ActionsGuids"), ActionsGuidsArray);

			TArray<TSharedPtr<FJsonValue>> MemoryStatesGuidsArray;
			for (const TSharedMemoryArrayView<3, float>& MemoryStates : SharedMemoryExperienceContainer.MemoryStates)
			{
				MemoryStatesGuidsArray.Add(MakeShared<FJsonValueString>(*MemoryStates.Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces)));

			}
			JsonObject->SetArrayField(TEXT("MemoryStatesGuids"), MemoryStatesGuidsArray);

			TArray<TSharedPtr<FJsonValue>> RewardsGuidsArray;
			for (const TSharedMemoryArrayView<3, float>& Rewards : SharedMemoryExperienceContainer.Rewards)
			{
				RewardsGuidsArray.Add(MakeShared<FJsonValueString>(*Rewards.Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces)));

			}
			JsonObject->SetArrayField(TEXT("RewardsGuids"), RewardsGuidsArray);
			
			TSharedRef<FJsonValueObject> JsonValue = MakeShared<FJsonValueObject>(JsonObject);
			ExperienceContainerObjectsArray.Add(JsonValue);
		}
		SharedMemoryObject->SetArrayField(TEXT("ReplayBuffers"), ExperienceContainerObjectsArray);

		ConfigObject->SetObjectField(TEXT("SharedMemory"), SharedMemoryObject);
		
		FString ConfigString;
		TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&ConfigString, 0);
		FJsonSerializer::Serialize(ConfigObject, JsonWriter, true);
		FFileHelper::SaveStringToFile(ConfigString, *ConfigPath);

		return SharedMemoryTraining::SendConfigSignal(Controls.View[ProcessIdx], LogSettings);
	}

	int32 FSharedMemoryTrainer::AddNetwork(const ULearningNeuralNetworkData& Network)
	{
		const int32 NetworkId = NeuralNetworkSharedMemoryArrayViews.Num();
		NeuralNetworkSharedMemoryArrayViews.Add(SharedMemory::Allocate<1, uint8>({ Network.GetSnapshotByteNum() }));
		return NetworkId;
	}

	ETrainerResponse FSharedMemoryTrainer::ReceiveNetwork(
		const int32 NetworkId,
		ULearningNeuralNetworkData& OutNetwork,
		FRWLock* NetworkLock,
		const ELogSetting LogSettings)
	{
		check(ProcessIdx != INDEX_NONE);
		checkf(Controls.Region, TEXT("ReceiveNetwork: Controls Shared Memory Region is nullptr"));
		if (!ensureMsgf(NeuralNetworkSharedMemoryArrayViews.Num() >= NetworkId, TEXT("Network %d has not been added. Call AddNetwork prior to ReceiveNetwork."), NetworkId))
		{
			return ETrainerResponse::Unexpected;
		}

		return SharedMemoryTraining::RecvNetwork(
			Controls.View[ProcessIdx],
			NetworkId,
			OutNetwork,
			*TrainingProcess,
			NeuralNetworkSharedMemoryArrayViews[NetworkId].View,
			Timeout,
			NetworkLock,
			LogSettings);
	}

	ETrainerResponse FSharedMemoryTrainer::SendNetwork(
		const int32 NetworkId,
		const ULearningNeuralNetworkData& Network,
		FRWLock* NetworkLock,
		const ELogSetting LogSettings)
	{
		check(ProcessIdx != INDEX_NONE);
		checkf(Controls.Region, TEXT("SendNetwork: Controls Shared Memory Region is nullptr"));
		if (!ensureMsgf(NeuralNetworkSharedMemoryArrayViews.Num() >= NetworkId, TEXT("Network %d has not been added. Call AddNetwork prior to SendNetwork."), NetworkId))
		{
			return ETrainerResponse::Unexpected;
		}

		return SharedMemoryTraining::SendNetwork(
			Controls.View[ProcessIdx],
			NetworkId,
			NeuralNetworkSharedMemoryArrayViews[NetworkId].View,
			*TrainingProcess,
			Network,
			Timeout,
			NetworkLock,
			LogSettings);
	}

	int32 FSharedMemoryTrainer::AddReplayBuffer(const FReplayBuffer& ReplayBuffer)
	{
		check(ProcessNum > 0);

		FSharedMemoryExperienceContainer ExperienceContainer;
		if (ProcessIdx == 0)
		{
			ExperienceContainer.EpisodeStarts = SharedMemory::Allocate<2, int32>({ ProcessNum, ReplayBuffer.GetMaxEpisodeNum() });
			ExperienceContainer.EpisodeLengths = SharedMemory::Allocate<2, int32>({ ProcessNum, ReplayBuffer.GetMaxEpisodeNum() });

			if (ReplayBuffer.HasCompletions())
			{
				ExperienceContainer.EpisodeCompletionModes = SharedMemory::Allocate<2, ECompletionMode>({ ProcessNum, ReplayBuffer.GetMaxEpisodeNum() });
			}

			if (ReplayBuffer.HasFinalObservations())
			{
				for (int32 Index = 0; Index < ReplayBuffer.GetObservationsNum(); Index++)
				{
					const int32 DimNum = ReplayBuffer.GetEpisodeFinalObservations(Index).Num<1>();
					ExperienceContainer.EpisodeFinalObservations.Add(SharedMemory::Allocate<3, float>({ ProcessNum, ReplayBuffer.GetMaxEpisodeNum(), DimNum }));
				}
			}

			if (ReplayBuffer.HasFinalMemoryStates())
			{
				for (int32 Index = 0; Index < ReplayBuffer.GetMemoryStatesNum(); Index++)
				{
					const int32 DimNum = ReplayBuffer.GetEpisodeFinalMemoryStates(Index).Num<1>();
					ExperienceContainer.EpisodeFinalMemoryStates.Add(SharedMemory::Allocate<3, float>({ ProcessNum, ReplayBuffer.GetMaxEpisodeNum(), DimNum }));
				}
			}

			for (int32 Index = 0; Index < ReplayBuffer.GetObservationsNum(); Index++)
			{
				const int32 DimNum = ReplayBuffer.GetObservations(Index).Num<1>();
				ExperienceContainer.Observations.Add(SharedMemory::Allocate<3, float>({ ProcessNum, ReplayBuffer.GetMaxStepNum(), DimNum }));
			}

			for (int32 Index = 0; Index < ReplayBuffer.GetActionsNum(); Index++)
			{
				const int32 DimNum = ReplayBuffer.GetActions(Index).Num<1>();
				ExperienceContainer.Actions.Add(SharedMemory::Allocate<3, float>({ ProcessNum, ReplayBuffer.GetMaxStepNum(), DimNum }));
			}

			for (int32 Index = 0; Index < ReplayBuffer.GetMemoryStatesNum(); Index++)
			{
				const int32 DimNum = ReplayBuffer.GetMemoryStates(Index).Num<1>();
				ExperienceContainer.MemoryStates.Add(SharedMemory::Allocate<3, float>({ ProcessNum, ReplayBuffer.GetMaxStepNum(), DimNum }));
			}

			for (int32 Index = 0; Index < ReplayBuffer.GetRewardsNum(); Index++)
			{
				const int32 DimNum = ReplayBuffer.GetRewards(Index).Num<1>();
				ExperienceContainer.Rewards.Add(SharedMemory::Allocate<3, float>({ ProcessNum, ReplayBuffer.GetMaxStepNum(), DimNum }));
			}
		}
		else
		{
			FGuid EpisodeStartsGuid; ensure(FParse::Value(FCommandLine::Get(), TEXT("LearningEpisodeStartsGuid"), EpisodeStartsGuid));
			ExperienceContainer.EpisodeStarts = SharedMemory::Map<2, int32>(EpisodeStartsGuid, { ProcessNum, ReplayBuffer.GetMaxEpisodeNum() });

			FGuid EpisodeLengthsGuid; ensure(FParse::Value(FCommandLine::Get(), TEXT("LearningEpisodeLengthsGuid"), EpisodeLengthsGuid));
			ExperienceContainer.EpisodeLengths = SharedMemory::Map<2, int32>(EpisodeLengthsGuid, { ProcessNum, ReplayBuffer.GetMaxEpisodeNum() });

			if (ReplayBuffer.HasCompletions())
			{
				FGuid EpisodeCompletionModesGuid; ensure(FParse::Value(FCommandLine::Get(), TEXT("LearningEpisodeCompletionModesGuid"), EpisodeCompletionModesGuid));
				ExperienceContainer.EpisodeCompletionModes = SharedMemory::Map<2, ECompletionMode>(EpisodeCompletionModesGuid, { ProcessNum, ReplayBuffer.GetMaxEpisodeNum() });
			}

			// Final Observations
			if (ReplayBuffer.HasFinalObservations())
			{
				FString StringOfGuids; ensure(FParse::Value(FCommandLine::Get(), TEXT("LearningEpisodeFinalObservationsGuids"), StringOfGuids));
				TArray<FString> StringGuids;
				StringOfGuids.ParseIntoArray(StringGuids, TEXT(","));
				TArray<FGuid> Guids;
				for (const FString& StringGuid : StringGuids)
				{
					FGuid Guid;
					FGuid::Parse(StringGuid, Guid);
					Guids.Add(Guid);
				}
				check(Guids.Num() == ReplayBuffer.GetObservationsNum());

				for (int32 Index = 0; Index < ReplayBuffer.GetObservationsNum(); Index++)
				{
					const int32 DimNum = ReplayBuffer.GetEpisodeFinalObservations(Index).Num<1>();
					ExperienceContainer.EpisodeFinalObservations.Add(SharedMemory::Map<3, float>(Guids[Index], { ProcessNum, ReplayBuffer.GetMaxEpisodeNum(), DimNum }));
				}
			}

			// Final Memory States
			if (ReplayBuffer.HasFinalMemoryStates())
			{
				FString StringOfGuids; ensure(FParse::Value(FCommandLine::Get(), TEXT("LearningEpisodeFinalMemoryStatesGuid"), StringOfGuids));
				TArray<FString> StringGuids;
				StringOfGuids.ParseIntoArray(StringGuids, TEXT(","));
				TArray<FGuid> Guids;
				for (const FString& StringGuid : StringGuids)
				{
					FGuid Guid;
					FGuid::Parse(StringGuid, Guid);
					Guids.Add(Guid);
				}
				check(Guids.Num() == ReplayBuffer.GetMemoryStatesNum());

				for (int32 Index = 0; Index < ReplayBuffer.GetMemoryStatesNum(); Index++)
				{
					const int32 DimNum = ReplayBuffer.GetEpisodeFinalMemoryStates(Index).Num<1>();
					ExperienceContainer.EpisodeFinalMemoryStates.Add(SharedMemory::Map<3, float>(Guids[Index], { ProcessNum, ReplayBuffer.GetMaxEpisodeNum(), DimNum }));
				}
			}

			// Observations
			{
				FString StringOfGuids; ensure(FParse::Value(FCommandLine::Get(), TEXT("LearningObservationsGuid"), StringOfGuids));
				TArray<FString> StringGuids;
				StringOfGuids.ParseIntoArray(StringGuids, TEXT(","));
				TArray<FGuid> Guids;
				for (const FString& StringGuid : StringGuids)
				{
					FGuid Guid;
					FGuid::Parse(StringGuid, Guid);
					Guids.Add(Guid);
				}
				check(Guids.Num() == ReplayBuffer.GetObservationsNum());

				for (int32 Index = 0; Index < ReplayBuffer.GetObservationsNum(); Index++)
				{
					const int32 DimNum = ReplayBuffer.GetObservations(Index).Num<1>();
					ExperienceContainer.Observations.Add(SharedMemory::Map<3, float>(Guids[Index], { ProcessNum, ReplayBuffer.GetMaxStepNum(), DimNum }));
				}
			}

			// Actions
			{
				FString StringOfGuids; ensure(FParse::Value(FCommandLine::Get(), TEXT("LearningActionsGuid"), StringOfGuids));
				TArray<FString> StringGuids;
				StringOfGuids.ParseIntoArray(StringGuids, TEXT(","));
				TArray<FGuid> Guids;
				for (const FString& StringGuid : StringGuids)
				{
					FGuid Guid;
					FGuid::Parse(StringGuid, Guid);
					Guids.Add(Guid);
				}
				check(Guids.Num() == ReplayBuffer.GetActionsNum());

				for (int32 Index = 0; Index < ReplayBuffer.GetActionsNum(); Index++)
				{
					const int32 DimNum = ReplayBuffer.GetActions(Index).Num<1>();
					ExperienceContainer.Actions.Add(SharedMemory::Map<3, float>(Guids[Index], { ProcessNum, ReplayBuffer.GetMaxStepNum(), DimNum }));
				}
			}

			// Memory States
			{
				FString StringOfGuids; ensure(FParse::Value(FCommandLine::Get(), TEXT("LearningMemoryStatesGuid"), StringOfGuids));
				TArray<FString> StringGuids;
				StringOfGuids.ParseIntoArray(StringGuids, TEXT(","));
				TArray<FGuid> Guids;
				for (const FString& StringGuid : StringGuids)
				{
					FGuid Guid;
					FGuid::Parse(StringGuid, Guid);
					Guids.Add(Guid);
				}
				check(Guids.Num() == ReplayBuffer.GetMemoryStatesNum());

				for (int32 Index = 0; Index < ReplayBuffer.GetMemoryStatesNum(); Index++)
				{
					const int32 DimNum = ReplayBuffer.GetMemoryStates(Index).Num<1>();
					ExperienceContainer.MemoryStates.Add(SharedMemory::Map<3, float>(Guids[Index], { ProcessNum, ReplayBuffer.GetMaxStepNum(), DimNum }));
				}
			}

			// Rewards
			{
				FString StringOfGuids; ensure(FParse::Value(FCommandLine::Get(), TEXT("LearningRewardsGuid"), StringOfGuids));
				TArray<FString> StringGuids;
				StringOfGuids.ParseIntoArray(StringGuids, TEXT(","));
				TArray<FGuid> Guids;
				for (const FString& StringGuid : StringGuids)
				{
					FGuid Guid;
					FGuid::Parse(StringGuid, Guid);
					Guids.Add(Guid);
				}
				check(Guids.Num() == ReplayBuffer.GetRewardsNum());

				for (int32 Index = 0; Index < ReplayBuffer.GetRewardsNum(); Index++)
				{
					const int32 DimNum = ReplayBuffer.GetRewards(Index).Num<1>();
					ExperienceContainer.Rewards.Add(SharedMemory::Map<3, float>(Guids[Index], { ProcessNum, ReplayBuffer.GetMaxStepNum(), DimNum }));
				}
			}
		}
		
		const int32 ReplayBufferId = SharedMemoryExperienceContainers.Num();
		SharedMemoryExperienceContainers.Add(ExperienceContainer);
		return ReplayBufferId;
	}

	ETrainerResponse FSharedMemoryTrainer::SendReplayBuffer(const int32 ReplayBufferId, const FReplayBuffer& ReplayBuffer, const ELogSetting LogSettings)
	{
		check(ProcessIdx != INDEX_NONE);
		checkf(Controls.Region, TEXT("SendReplayBuffer: Controls Shared Memory Region is nullptr"));
		if (!ensureMsgf(SharedMemoryExperienceContainers.Num() >= ReplayBufferId, TEXT("ReplayBuffer %d has not been added. Call AddReplayBuffer prior to SendReplayBuffer."), ReplayBufferId))
		{
			return ETrainerResponse::Unexpected;
		}

		TArray<TLearningArrayView<2, float>> EpisodeFinalObservations;
		for (TSharedMemoryArrayView<3, float>& EpisodeFinalObs : SharedMemoryExperienceContainers[ReplayBufferId].EpisodeFinalObservations)
		{
			EpisodeFinalObservations.Add(EpisodeFinalObs.View[ProcessIdx]);
		}

		TArray<TLearningArrayView<2, float>> EpisodeFinalMemoryStates;
		for (TSharedMemoryArrayView<3, float>& EpisodeFinalMems : SharedMemoryExperienceContainers[ReplayBufferId].EpisodeFinalMemoryStates)
		{
			EpisodeFinalMemoryStates.Add(EpisodeFinalMems.View[ProcessIdx]);
		}

		TArray<TLearningArrayView<2, float>> Observations;
		for (TSharedMemoryArrayView<3, float>& Obs : SharedMemoryExperienceContainers[ReplayBufferId].Observations)
		{
			Observations.Add(Obs.View[ProcessIdx]);
		}

		TArray<TLearningArrayView<2, float>> Actions;
		for (TSharedMemoryArrayView<3, float>& Acts : SharedMemoryExperienceContainers[ReplayBufferId].Actions)
		{
			Actions.Add(Acts.View[ProcessIdx]);
		}

		TArray<TLearningArrayView<2, float>> MemoryStates;
		for (TSharedMemoryArrayView<3, float>& Mems : SharedMemoryExperienceContainers[ReplayBufferId].MemoryStates)
		{
			MemoryStates.Add(Mems.View[ProcessIdx]);
		}

		TArray<TLearningArrayView<2, float>> Rewards;
		for (TSharedMemoryArrayView<3, float>& Rews : SharedMemoryExperienceContainers[ReplayBufferId].Rewards)
		{
			Rewards.Add(Rews.View[ProcessIdx]);
		}

		TLearningArrayView<1, ECompletionMode> EmptyCompletionsArray;
		return SharedMemoryTraining::SendExperience(
			SharedMemoryExperienceContainers[ReplayBufferId].EpisodeStarts.View[ProcessIdx],
			SharedMemoryExperienceContainers[ReplayBufferId].EpisodeLengths.View[ProcessIdx],
			ReplayBuffer.HasCompletions() ? SharedMemoryExperienceContainers[ReplayBufferId].EpisodeCompletionModes.View[ProcessIdx] : EmptyCompletionsArray,
			EpisodeFinalObservations,
			EpisodeFinalMemoryStates,
			Observations,
			Actions,
			MemoryStates,
			Rewards,
			Controls.View[ProcessIdx],
			*TrainingProcess,
			ReplayBufferId,
			ReplayBuffer,
			Timeout,
			LogSettings);
	}

	void FSharedMemoryTrainer::Deallocate()
	{
		for (TSharedMemoryArrayView<1, uint8>& SharedMemoryArrayView : NeuralNetworkSharedMemoryArrayViews)
		{
			if (SharedMemoryArrayView.Region != nullptr)
			{
				SharedMemory::Deallocate(SharedMemoryArrayView);
			}
		}
		NeuralNetworkSharedMemoryArrayViews.Empty();

		for (FSharedMemoryExperienceContainer& SharedMemoryExperienceContainer : SharedMemoryExperienceContainers)
		{
			SharedMemoryExperienceContainer.Deallocate();
		}
		SharedMemoryExperienceContainers.Empty();
	}

	FSocketTrainerServerProcess::FSocketTrainerServerProcess(
		const FString& CustomTrainerPath,
		const FString& TrainerFileName,
		const FString& PythonExecutablePath,
		const FString& PythonContentPath,
		const FString& IntermediatePath,
		const TCHAR* IpAddress,
		const uint32 Port,
		const float InTimeout,
		const ESubprocessFlags TrainingProcessFlags,
		const ELogSetting LogSettings)
	{
		Timeout = InTimeout;

		UE_LEARNING_CHECK(FPaths::FileExists(PythonExecutablePath));
		UE_LEARNING_CHECK(FPaths::DirectoryExists(PythonContentPath));

		IFileManager& FileManager = IFileManager::Get();
		const FString CommandLineArguments = FString::Printf(TEXT("\"%s\" \"%s\" \"%s\" Socket \"%s:%i\" \"%s\" %i"),
			*FileManager.ConvertToAbsolutePathForExternalAppForRead(*(PythonContentPath / TEXT("train.py"))),
			*FileManager.ConvertToAbsolutePathForExternalAppForRead(*CustomTrainerPath),
			*TrainerFileName,
			IpAddress,
			Port,
			*FileManager.ConvertToAbsolutePathForExternalAppForRead(*IntermediatePath),
			LogSettings == ELogSetting::Normal ? 1 : 0);

		TrainingProcess.Launch(
			FileManager.ConvertToAbsolutePathForExternalAppForRead(*PythonExecutablePath),
			CommandLineArguments, 
			TrainingProcessFlags);

		if (PLATFORM_MAC)
		{
			// TODO we seem to have to sleep on Mac so the trainer can start listening before we try to connect
			FPlatformProcess::Sleep(1.0f);
		}
	}

	bool FSocketTrainerServerProcess::IsRunning() const
	{
		return TrainingProcess.IsRunning();
	}

	bool FSocketTrainerServerProcess::Wait()
	{
		const float SleepTime = 0.001f;
		float WaitTime = 0.0f;

		while (TrainingProcess.Update())
		{
			FPlatformProcess::Sleep(SleepTime);
			WaitTime += SleepTime;

			if (WaitTime > Timeout)
			{
				return false;
			}
		}

		return true;
	}

	void FSocketTrainerServerProcess::Terminate()
	{
		TrainingProcess.Terminate();
	}

	FSubprocess* FSocketTrainerServerProcess::GetTrainingSubprocess()
	{
		return &TrainingProcess;
	}

	FSocketTrainer::FSocketTrainer(
		ETrainerResponse& OutResponse,
		const TSharedPtr<UE::Learning::ITrainerProcess>& ExternalTrainerProcess,
		const TCHAR* IpAddress,
		const uint32 Port,
		const float InTimeout)
	{
		Timeout = InTimeout;

		FSocketTrainerServerProcess* SocketTrainerProcess = (FSocketTrainerServerProcess*)ExternalTrainerProcess.Get();
		if (SocketTrainerProcess)
		{
			TrainingProcess = SocketTrainerProcess->GetTrainingSubprocess();
		}

		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		checkf(SocketSubsystem, TEXT("Could not get socket subsystem"));

		bool bIsValid = false;
		TSharedRef<FInternetAddr> Address = SocketSubsystem->CreateInternetAddr();
		Address->SetIp(IpAddress, bIsValid);
		Address->SetPort(Port);

		if (!bIsValid)
		{
			UE_LOG(LogLearning, Error, TEXT("Invalid Ip Address \"%s\"..."), IpAddress);
			OutResponse = ETrainerResponse::Unexpected;
			return;
		}

		Socket = FTcpSocketBuilder(TEXT("LearningTrainerSocket")).AsBlocking().Build();

		OutResponse = SocketTraining::WaitForConnection(*Socket, TrainingProcess, *Address, Timeout);
	}

	FSocketTrainer::~FSocketTrainer()
	{
		Terminate();
	}

	ETrainerResponse FSocketTrainer::Wait()
	{
		return ETrainerResponse::Success;
	}

	bool FSocketTrainer::HasNetworkOrCompleted()
	{
		return SocketTraining::HasNetworkOrCompleted(*Socket, TrainingProcess);
	}

	void FSocketTrainer::Terminate()
	{
		if (Socket)
		{
			Socket->Close();
			Socket = nullptr;
		}
	}

	ETrainerResponse FSocketTrainer::SendStop()
	{
		if (!Socket)
		{
			UE_LOG(LogLearning, Error, TEXT("Training socket is nullptr"));
			return ETrainerResponse::Unexpected;
		}

		return SocketTraining::SendStop(*Socket, TrainingProcess, Timeout);
	}

	ETrainerResponse FSocketTrainer::SendConfig(
		const TSharedRef<FJsonObject>& ConfigObject,
		const ELogSetting LogSettings)
	{
		if (!Socket)
		{
			UE_LOG(LogLearning, Error, TEXT("Training socket is nullptr"));
			return ETrainerResponse::Unexpected;
		}

		FString ConfigString;
		TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&ConfigString, 0);
		FJsonSerializer::Serialize(ConfigObject, JsonWriter, true);

		return SocketTraining::SendConfig(*Socket, ConfigString, TrainingProcess, Timeout, LogSettings);
	}

	int32 FSocketTrainer::AddNetwork(const ULearningNeuralNetworkData& Network)
	{
		const int32 NetworkId = NetworkBuffers.Num();
		NetworkBuffers.Add(TLearningArray<1, uint8>());
		NetworkBuffers[NetworkId].SetNumUninitialized({Network.GetSnapshotByteNum()});
		return NetworkId;
	}

	ETrainerResponse FSocketTrainer::ReceiveNetwork(
		const int32 NetworkId,
		ULearningNeuralNetworkData& OutNetwork,
		FRWLock* NetworkLock,
		const ELogSetting LogSettings)
	{
		if (!Socket)
		{
			UE_LOG(LogLearning, Error, TEXT("Training socket is nullptr"));
			return ETrainerResponse::Unexpected;
		}

		if (!ensureMsgf(NetworkBuffers.Num() >= NetworkId, TEXT("Network %d has not been added. Call AddNetwork prior to ReceiveNetwork."), NetworkId))
		{
			return ETrainerResponse::Unexpected;
		}

		return SocketTraining::RecvNetwork(*Socket, NetworkId, OutNetwork, TrainingProcess, NetworkBuffers[NetworkId], Timeout, NetworkLock, LogSettings);
	}

	ETrainerResponse FSocketTrainer::SendNetwork(
		const int32 NetworkId,
		const ULearningNeuralNetworkData& Network,
		FRWLock* NetworkLock,
		const ELogSetting LogSettings)
	{
		if (!Socket)
		{
			UE_LOG(LogLearning, Error, TEXT("Training socket is nullptr"));
			return ETrainerResponse::Unexpected;
		}

		if (!ensureMsgf(NetworkBuffers.Num() >= NetworkId, TEXT("Network %d has not been added. Call AddNetwork prior to SendNetwork."), NetworkId))
		{
			return ETrainerResponse::Unexpected;
		}

		return SocketTraining::SendNetwork(*Socket, NetworkBuffers[NetworkId], TrainingProcess, NetworkId, Network, Timeout, NetworkLock, LogSettings);
	}

	int32 FSocketTrainer::AddReplayBuffer(const FReplayBuffer& ReplayBuffer)
	{
		LastReplayBufferId++;
		return LastReplayBufferId;
	}

	ETrainerResponse FSocketTrainer::SendReplayBuffer(const int32 ReplayBufferId, const FReplayBuffer& ReplayBuffer, const ELogSetting LogSettings)
	{
		if (!Socket)
		{
			UE_LOG(LogLearning, Error, TEXT("Training socket is nullptr"));
			return ETrainerResponse::Unexpected;
		}

		if (!ensureMsgf(ReplayBufferId <= LastReplayBufferId, TEXT("ReplayBuffer %d has not been added. Call AddReplayBuffer prior to SendReplayBuffer."), ReplayBufferId))
		{
			return ETrainerResponse::Unexpected;
		}

		return SocketTraining::SendExperience(*Socket, ReplayBufferId, ReplayBuffer, TrainingProcess, Timeout, LogSettings);
	}
}
