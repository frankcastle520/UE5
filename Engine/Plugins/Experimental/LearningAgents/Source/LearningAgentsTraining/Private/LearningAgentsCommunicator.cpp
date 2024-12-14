// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsCommunicator.h"

#include "LearningAgentsInteractor.h"
#include "LearningExternalTrainer.h"

#include "Misc/Paths.h"

FLearningAgentsTrainerProcess ULearningAgentsCommunicatorLibrary::SpawnSharedMemoryTrainingProcess(
	const FLearningAgentsTrainerProcessSettings& TrainerProcessSettings,
	const FLearningAgentsSharedMemoryCommunicatorSettings& SharedMemorySettings)
{
	FLearningAgentsTrainerProcess TrainerProcess;

	if (PLATFORM_MAC)
	{
		UE_LOG(LogLearning, Error, TEXT("SpawnSharedMemoryTrainingProcess: Shared Memory not supported on Mac. Switch to Socket Communicator instead."));
		return TrainerProcess;
	}

	const FString PythonExecutablePath = UE::Learning::Trainer::GetPythonExecutablePath(TrainerProcessSettings.GetIntermediatePath());
	if (!FPaths::FileExists(PythonExecutablePath))
	{
		UE_LOG(LogLearning, Error, TEXT("SpawnSharedMemoryTrainingProcess: Can't find Python executable \"%s\"."), *PythonExecutablePath);
		return TrainerProcess;
	}

	const FString PythonContentPath = UE::Learning::Trainer::GetPythonContentPath(TrainerProcessSettings.GetEditorEnginePath());
	if (!FPaths::DirectoryExists(PythonContentPath))
	{
		UE_LOG(LogLearning, Error, TEXT("SpawnSharedMemoryTrainingProcess: Can't find LearningAgents plugin Content \"%s\"."), *PythonContentPath);
		return TrainerProcess;
	}

	const FString IntermediatePath = UE::Learning::Trainer::GetIntermediatePath(TrainerProcessSettings.GetIntermediatePath());

	const FString CustomTrainerModulePath = TrainerProcessSettings.GetCustomTrainerModulePath();
	if (!CustomTrainerModulePath.IsEmpty() and !FPaths::DirectoryExists(CustomTrainerModulePath))
	{
		UE_LOG(LogLearning, Error, TEXT("SpawnSharedMemoryTrainingProcess: Can't find custom trainer module \"%s\"."), *CustomTrainerModulePath);
		return TrainerProcess;
	}

	TrainerProcess.TrainerProcess = MakeShared<UE::Learning::FSharedMemoryTrainerServerProcess>(
		SharedMemorySettings.TaskName,
		CustomTrainerModulePath,
		TrainerProcessSettings.TrainerFileName,
		PythonExecutablePath,
		PythonContentPath,
		IntermediatePath,
		1, // ProcessNum hard-coded to 1 for now
		SharedMemorySettings.Timeout);

	return TrainerProcess;
}

FLearningAgentsCommunicator ULearningAgentsCommunicatorLibrary::MakeSharedMemoryCommunicator(
	const FLearningAgentsTrainerProcess& TrainerProcess,
	const FLearningAgentsSharedMemoryCommunicatorSettings& SharedMemorySettings)
{
	FLearningAgentsCommunicator Communicator;

	if (PLATFORM_MAC)
	{
		UE_LOG(LogLearning, Error, TEXT("MakeSharedMemoryCommunicator: Shared Memory not supported on Mac. Switch to Socket Communicator instead."));
		return Communicator;
	}

	if (!TrainerProcess.TrainerProcess)
	{
		UE_LOG(LogLearning, Error, TEXT("MakeSharedMemoryCommunicator: TrainerProcess is nullptr"));
		return Communicator;
	}

	Communicator.Trainer = MakeShared<UE::Learning::FSharedMemoryTrainer>(
		SharedMemorySettings.TaskName,
		1, // ProcessNum hard-coded to 1 for now
		TrainerProcess.TrainerProcess,
		SharedMemorySettings.Timeout);

	return Communicator;
}

FLearningAgentsTrainerProcess ULearningAgentsCommunicatorLibrary::SpawnSocketTrainingProcess(
	const FLearningAgentsTrainerProcessSettings& TrainerProcessSettings,
	const FLearningAgentsSocketCommunicatorSettings& SocketSettings)
{
	FLearningAgentsTrainerProcess TrainerProcess;

	const FString PythonExecutablePath = UE::Learning::Trainer::GetPythonExecutablePath(TrainerProcessSettings.GetIntermediatePath());
	if (!FPaths::FileExists(PythonExecutablePath))
	{
		UE_LOG(LogLearning, Error, TEXT("SpawnSocketTrainingProcess: Can't find Python executable \"%s\"."), *PythonExecutablePath);
		return TrainerProcess;
	}

	const FString PythonContentPath = UE::Learning::Trainer::GetPythonContentPath(TrainerProcessSettings.GetEditorEnginePath());
	if (!FPaths::DirectoryExists(PythonContentPath))
	{
		UE_LOG(LogLearning, Error, TEXT("SpawnSocketTrainingProcess: Can't find LearningAgents plugin Content \"%s\"."), *PythonContentPath);
		return TrainerProcess;
	}

	const FString IntermediatePath = UE::Learning::Trainer::GetIntermediatePath(TrainerProcessSettings.GetIntermediatePath());

	const FString CustomTrainerModulePath = TrainerProcessSettings.GetCustomTrainerModulePath();
	if (!CustomTrainerModulePath.IsEmpty() and !FPaths::DirectoryExists(CustomTrainerModulePath))
	{
		UE_LOG(LogLearning, Error, TEXT("SpawnSocketTrainingProcess: Can't find custom trainer module \"%s\"."), *CustomTrainerModulePath);
		return TrainerProcess;
	}

	TrainerProcess.TrainerProcess = MakeShared<UE::Learning::FSocketTrainerServerProcess>(
		CustomTrainerModulePath,
		TrainerProcessSettings.TrainerFileName,
		PythonExecutablePath,
		PythonContentPath,
		IntermediatePath,
		*SocketSettings.IpAddress,
		SocketSettings.Port,
		SocketSettings.Timeout);

	return TrainerProcess;
}

FLearningAgentsCommunicator ULearningAgentsCommunicatorLibrary::MakeSocketCommunicator(
	FLearningAgentsTrainerProcess TrainerProcess,
	const FLearningAgentsSocketCommunicatorSettings& SocketSettings)
{
	UE::Learning::ETrainerResponse Response = UE::Learning::ETrainerResponse::Success;

	FLearningAgentsCommunicator Communicator;
	Communicator.Trainer = MakeShared<UE::Learning::FSocketTrainer>(
		Response,
		TrainerProcess.TrainerProcess,
		*SocketSettings.IpAddress,
		SocketSettings.Port,
		SocketSettings.Timeout);

	if (Response != UE::Learning::ETrainerResponse::Success)
	{
		UE_LOG(LogLearning, Error, TEXT("MakeSocketCommunicator: Failed to connect to training process: %s. Check log for additional errors."), UE::Learning::Trainer::GetResponseString(Response));
		Communicator.Trainer->Terminate();
	}

	return Communicator;
}
