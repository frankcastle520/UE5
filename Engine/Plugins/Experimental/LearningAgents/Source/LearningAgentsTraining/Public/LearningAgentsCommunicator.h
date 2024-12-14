// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LearningAgentsTrainer.h"

#include "Engine/EngineTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Templates/SharedPointer.h"
#include "UObject/Object.h"
#include "UObject/ObjectPtr.h"

#include "LearningAgentsCommunicator.generated.h"

namespace UE::Learning
{
	struct IExternalTrainer;
	struct ITrainerProcess;
}

class ULearningAgentsInteractor;

/** Settings specific to shared memory communicators. */
USTRUCT(BlueprintType, Category = "LearningAgents")
struct LEARNINGAGENTSTRAINING_API FLearningAgentsSharedMemoryCommunicatorSettings
{
	GENERATED_BODY()

public:

	/** Training task name. Used to avoid filename collisions with other training processes running on the same machine. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	FString TaskName = TEXT("Training");

	/** Time in seconds to wait for the training process before timing out. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float Timeout = 10.0f;
};

/** Settings specific to socket communicators. */
USTRUCT(BlueprintType, Category = "LearningAgents")
struct LEARNINGAGENTSTRAINING_API FLearningAgentsSocketCommunicatorSettings
{
	GENERATED_BODY()

public:

	/** IP Address for the socket. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents")
	FString IpAddress = TEXT("127.0.0.1");

	/** Port for the socket. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0", UIMin = "0", ClampMax = "65535", UIMax = "65535"))
	uint32 Port = 48491;

	/** Time in seconds to wait for the training process before timing out. */
	UPROPERTY(EditAnywhere, Category = "LearningAgents", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float Timeout = 10.0f;
};

/** Blueprint-compatible wrapper struct for ITrainerProcess. */
USTRUCT(BlueprintType)
struct LEARNINGAGENTSTRAINING_API FLearningAgentsTrainerProcess
{
	GENERATED_BODY()

	TSharedPtr<UE::Learning::ITrainerProcess> TrainerProcess;
};

/** Blueprint-compatible wrapper struct for IExternalTrainer. */
USTRUCT(BlueprintType)
struct LEARNINGAGENTSTRAINING_API FLearningAgentsCommunicator
{
	GENERATED_BODY()

	TSharedPtr<UE::Learning::IExternalTrainer> Trainer;
};

/** Contains functions for starting external trainers and communicating with them. */
UCLASS()
class LEARNINGAGENTSTRAINING_API ULearningAgentsCommunicatorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Start a local python training sub-process which will communicate via shared memory. Shared memory has the least
	 * communication overhead so prefer this for local development.
	 * 
	 * This must be called on game thread!
	 * 
	 * @param TrainerProcessSettings							Settings universal to all trainer processes.
	 * @param FLearningAgentsSharedMemoryCommunicatorSettings	Settings specific to shared memory communicators.
	 */ 
	UFUNCTION(BlueprintCallable, Category = "LearningAgents", meta = (AutoCreateRefTerm = "TrainerProcessSettings,SharedMemorySettings"))
	static FLearningAgentsTrainerProcess SpawnSharedMemoryTrainingProcess(
		const FLearningAgentsTrainerProcessSettings& TrainerProcessSettings = FLearningAgentsTrainerProcessSettings(),
		const FLearningAgentsSharedMemoryCommunicatorSettings& SharedMemorySettings = FLearningAgentsSharedMemoryCommunicatorSettings());

	/**
	 * Create a communicator which can be used to interact with a previously started shared memory trainer process.
	 *
	 * @param TrainerProcess									The shared memory trainer process to communicate with.
	 * @param TrainerProcessSettings							Settings universal to all trainer processes.
	 * @param FLearningAgentsSharedMemoryCommunicatorSettings	Settings specific to shared memory communicators.
	 */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents", meta = (AutoCreateRefTerm = "SharedMemorySettings"))
	static FLearningAgentsCommunicator MakeSharedMemoryCommunicator(
		const FLearningAgentsTrainerProcess& TrainerProcess,
		const FLearningAgentsSharedMemoryCommunicatorSettings& SharedMemorySettings = FLearningAgentsSharedMemoryCommunicatorSettings());

	/**
	 * Start a local python training sub-process which will communicate via sockets. Sockets have some overhead
	 * compared to shared memory but can work over networked connects. This provides no encryption so do not use on
	 * public internet if privacy is a concern.
	 *
	 * This must be called on game thread!
	 *
	 * @param TrainerProcessSettings					Settings universal to all trainer processes.
	 * @param FLearningAgentsSocketCommunicatorSettings	Settings specific to socket communicators.
	 */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents", meta = (AutoCreateRefTerm = "TrainerProcessSettings,SocketSettings"))
	static FLearningAgentsTrainerProcess SpawnSocketTrainingProcess(
		const FLearningAgentsTrainerProcessSettings& TrainerProcessSettings = FLearningAgentsTrainerProcessSettings(),
		const FLearningAgentsSocketCommunicatorSettings& SocketSettings = FLearningAgentsSocketCommunicatorSettings());

	/**
	 * Create a communicator which can be used to interact with a previously started socket trainer process.
	 *
	 * @param TrainerProcess							The socket trainer process to communicate with (optional).
	 * @param TrainerProcessSettings					Settings universal to all trainer processes.
	 * @param FLearningAgentsSocketCommunicatorSettings	Settings specific to socket communicators.
	 */
	UFUNCTION(BlueprintCallable, Category = "LearningAgents", meta = (AutoCreateRefTerm = "SocketSettings"))
	static FLearningAgentsCommunicator MakeSocketCommunicator(
		FLearningAgentsTrainerProcess TrainerProcess,
		const FLearningAgentsSocketCommunicatorSettings& SocketSettings = FLearningAgentsSocketCommunicatorSettings());
};
