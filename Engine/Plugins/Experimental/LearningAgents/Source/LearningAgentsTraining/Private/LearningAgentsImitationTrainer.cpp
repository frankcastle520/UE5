// Copyright Epic Games, Inc. All Rights Reserved.

#include "LearningAgentsImitationTrainer.h"

#include "LearningAgentsManager.h"
#include "LearningAgentsInteractor.h"
#include "LearningExperience.h"
#include "LearningLog.h"
#include "LearningNeuralNetwork.h"
#include "LearningExternalTrainer.h"
#include "LearningAgentsCommunicator.h"
#include "LearningAgentsRecording.h"
#include "LearningAgentsPolicy.h"

#include "Dom/JsonObject.h"
#include "Misc/Paths.h"

TSharedRef<FJsonObject> FLearningAgentsImitationTrainerTrainingSettings::AsJsonConfig() const
{
	TSharedRef<FJsonObject> ConfigObject = MakeShared<FJsonObject>();

	ConfigObject->SetNumberField(TEXT("IterationNum"), NumberOfIterations);
	ConfigObject->SetNumberField(TEXT("LearningRate"), LearningRate);
	ConfigObject->SetNumberField(TEXT("LearningRateDecay"), LearningRateDecay);
	ConfigObject->SetNumberField(TEXT("WeightDecay"), WeightDecay);
	ConfigObject->SetNumberField(TEXT("BatchSize"), BatchSize);
	ConfigObject->SetNumberField(TEXT("Window"), Window);
	ConfigObject->SetNumberField(TEXT("ActionRegularizationWeight"), ActionRegularizationWeight);
	ConfigObject->SetNumberField(TEXT("ActionEntropyWeight"), ActionEntropyWeight);
	ConfigObject->SetNumberField(TEXT("Seed"), RandomSeed);
	ConfigObject->SetStringField(TEXT("Device"), UE::Learning::Trainer::GetDeviceString(UE::Learning::Agents::GetTrainingDevice(Device)));
	ConfigObject->SetBoolField(TEXT("UseTensorBoard"), bUseTensorboard);
	ConfigObject->SetBoolField(TEXT("SaveSnapshots"), bSaveSnapshots);

	return ConfigObject;
}

ULearningAgentsImitationTrainer::ULearningAgentsImitationTrainer() : Super(FObjectInitializer::Get()) {}
ULearningAgentsImitationTrainer::ULearningAgentsImitationTrainer(FVTableHelper& Helper) : Super(Helper) {}
ULearningAgentsImitationTrainer::~ULearningAgentsImitationTrainer() = default;

void ULearningAgentsImitationTrainer::BeginDestroy()
{
	if (IsTraining())
	{
		EndTraining();
	}

	Super::BeginDestroy();
}

ULearningAgentsImitationTrainer* ULearningAgentsImitationTrainer::MakeImitationTrainer(
	ULearningAgentsManager*& InManager,
	ULearningAgentsInteractor*& InInteractor,
	ULearningAgentsPolicy*& InPolicy,
	const FLearningAgentsCommunicator& Communicator,
	TSubclassOf<ULearningAgentsImitationTrainer> Class,
	const FName Name)
{
	if (!InManager)
	{
		UE_LOG(LogLearning, Error, TEXT("MakeImitationTrainer: InManager is nullptr."));
		return nullptr;
	}

	if (!Class)
	{
		UE_LOG(LogLearning, Error, TEXT("MakeImitationTrainer: Class is nullptr."));
		return nullptr;
	}

	const FName UniqueName = MakeUniqueObjectName(InManager, Class, Name, EUniqueObjectNameOptions::GloballyUnique);

	ULearningAgentsImitationTrainer* ImitationTrainer = NewObject<ULearningAgentsImitationTrainer>(InManager, Class, UniqueName);
	if (!ImitationTrainer) { return nullptr; }

	ImitationTrainer->SetupImitationTrainer(InManager, InInteractor, InPolicy, Communicator);

	return ImitationTrainer->IsSetup() ? ImitationTrainer : nullptr;
}

void ULearningAgentsImitationTrainer::SetupImitationTrainer(
	ULearningAgentsManager* InManager,
	ULearningAgentsInteractor* InInteractor,
	ULearningAgentsPolicy* InPolicy,
	const FLearningAgentsCommunicator& Communicator)
{
	if (IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup already performed!"), *GetName());
		return;
	}

	if (!InManager)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: InManager is nullptr."), *GetName());
		return;
	}

	if (!InInteractor)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: InInteractor is nullptr."), *GetName());
		return;
	}

	if (!InInteractor->IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: %s's Setup must be run before it can be used."), *GetName(), *InInteractor->GetName());
		return;
	}

	if (!InPolicy)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: InPolicy is nullptr."), *GetName());
		return;
	}

	if (!InPolicy->IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: %s's Setup must be run before it can be used."), *GetName(), *InPolicy->GetName());
		return;
	}

	if (!Communicator.Trainer)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Communicator's Trainer is nullptr."), *GetName());
		return;
	}

	Manager = InManager;
	Interactor = InInteractor;
	Policy = InPolicy;
	Trainer = Communicator.Trainer;

	bIsSetup = true;

	Manager->AddListener(this);
}

void ULearningAgentsImitationTrainer::BeginTraining(
	const ULearningAgentsRecording* Recording,
	const FLearningAgentsImitationTrainerSettings& ImitationTrainerSettings,
	const FLearningAgentsImitationTrainerTrainingSettings& ImitationTrainerTrainingSettings,
	const FLearningAgentsTrainerProcessSettings& ImitationTrainerPathSettings)
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	if (IsTraining())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Cannot begin training as we are already training!"), *GetName());
		return;
	}

	if (!Recording)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Recording is nullptr."), *GetName());
		return;
	}

	if (Recording->Records.IsEmpty())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Recording is empty!"), *GetName());
		return;
	}

	// Check Paths

	const FString PythonExecutablePath = UE::Learning::Trainer::GetPythonExecutablePath(ImitationTrainerPathSettings.GetIntermediatePath());

	if (!FPaths::FileExists(PythonExecutablePath))
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Can't find Python executable \"%s\"."), *GetName(), *PythonExecutablePath);
		return;
	}
	const FString PythonContentPath = UE::Learning::Trainer::GetPythonContentPath(ImitationTrainerPathSettings.GetEditorEnginePath());

	if (!FPaths::DirectoryExists(PythonContentPath))
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Can't find LearningAgents plugin Content \"%s\"."), *GetName(), *PythonContentPath);
		return;
	}

	const FString IntermediatePath = UE::Learning::Trainer::GetIntermediatePath(ImitationTrainerPathSettings.GetIntermediatePath());

	// Sizes

	const int32 ObservationNum = Interactor->GetObservationVectorSize();
	const int32 ActionNum = Interactor->GetActionVectorSize();

	// Get Number of Steps

	int32 TotalEpisodeNum = 0;
	int32 TotalStepNum = 0;
	for (const FLearningAgentsRecord& Record : Recording->Records)
	{
		if (Record.ObservationDimNum != ObservationNum)
		{
			UE_LOG(LogLearning, Warning, TEXT("%s: Record has wrong dimensionality for observations, got %i, policy expected %i."), *GetName(), Record.ObservationDimNum, ObservationNum);
			continue;
		}

		if (Record.ActionDimNum != ActionNum)
		{
			UE_LOG(LogLearning, Warning, TEXT("%s: Record has wrong dimensionality for actions, got %i, policy expected %i."), *GetName(), Record.ActionDimNum, ActionNum);
			continue;
		}

		TotalEpisodeNum++;
		TotalStepNum += Record.StepNum;
	}

	if (TotalStepNum == 0)
	{
		UE_LOG(LogLearning, Warning, TEXT("%s: Recording contains no valid training data."), *GetName());
		return;
	}

	// Copy into Flat Arrays

	TLearningArray<1, int32> RecordedEpisodeStarts;
	TLearningArray<1, int32> RecordedEpisodeLengths;
	TLearningArray<2, float> RecordedObservations;
	TLearningArray<2, float> RecordedActions;

	RecordedEpisodeStarts.SetNumUninitialized({ TotalEpisodeNum });
	RecordedEpisodeLengths.SetNumUninitialized({ TotalEpisodeNum });
	RecordedObservations.SetNumUninitialized({ TotalStepNum, ObservationNum });
	RecordedActions.SetNumUninitialized({ TotalStepNum, ActionNum });

	int32 EpisodeIdx = 0;
	int32 StepIdx = 0;
	for (const FLearningAgentsRecord& Record : Recording->Records)
	{
		if (Record.ObservationDimNum != ObservationNum) { continue; }
		if (Record.ActionDimNum != ActionNum) { continue; }

		TLearningArrayView<2, const float> ObservationsView = TLearningArrayView<2, const float>(Record.ObservationData.GetData(), { Record.StepNum, Record.ObservationDimNum });
		TLearningArrayView<2, const float> ActionsView = TLearningArrayView<2, const float>(Record.ActionData.GetData(), { Record.StepNum, Record.ActionDimNum });

		RecordedEpisodeStarts[EpisodeIdx] = StepIdx;
		RecordedEpisodeLengths[EpisodeIdx] = Record.StepNum;
		UE::Learning::Array::Copy(RecordedObservations.Slice(StepIdx, Record.StepNum), ObservationsView);
		UE::Learning::Array::Copy(RecordedActions.Slice(StepIdx, Record.StepNum), ActionsView);
		EpisodeIdx++;
		StepIdx += Record.StepNum;
	}

	UE_LEARNING_CHECK(EpisodeIdx == TotalEpisodeNum);
	UE_LEARNING_CHECK(StepIdx == TotalStepNum);

	const int32 ObservationSchemaId = 0;
	const int32 ActionSchemaId = 0;

	// Create Replay Buffer from Records
	ReplayBuffer = MakeUnique<UE::Learning::FReplayBuffer>();
	ReplayBuffer->AddRecords(
		TotalEpisodeNum,
		TotalStepNum,
		ObservationSchemaId,
		ObservationNum,
		ActionSchemaId,
		ActionNum,
		RecordedEpisodeStarts,
		RecordedEpisodeLengths,
		RecordedObservations,
		RecordedActions);

	// We need to setup the trainer prior to sending the config
	PolicyNetworkId = Trainer->AddNetwork(*Policy->GetPolicyNetworkAsset()->NeuralNetworkData);
	EncoderNetworkId = Trainer->AddNetwork(*Policy->GetEncoderNetworkAsset()->NeuralNetworkData);
	DecoderNetworkId = Trainer->AddNetwork(*Policy->GetDecoderNetworkAsset()->NeuralNetworkData);
	ReplayBufferId = Trainer->AddReplayBuffer(*ReplayBuffer);

	TSharedRef<FJsonObject> ConfigObject = CreateConfig(ImitationTrainerTrainingSettings);

	UE_LOG(LogLearning, Display, TEXT("%s: Sending config..."), *GetName());
	SendConfig(ConfigObject);

	UE_LOG(LogLearning, Display, TEXT("%s: Imitation Training Started"), *GetName());
	UE::Learning::ETrainerResponse Response = UE::Learning::ETrainerResponse::Success;

	UE_LOG(LogLearning, Display, TEXT("%s: Sending / Receiving initial policy..."), *GetName());
	Response = Trainer->SendNetwork(PolicyNetworkId, *Policy->GetPolicyNetworkAsset()->NeuralNetworkData);
	if (Response != UE::Learning::ETrainerResponse::Success)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Error sending policy to trainer: %s. Check log for additional errors."), *GetName(), UE::Learning::Trainer::GetResponseString(Response));
		bHasTrainingFailed = true;
		Trainer->Terminate();
		return;
	}

	Response = Trainer->SendNetwork(EncoderNetworkId, *Policy->GetEncoderNetworkAsset()->NeuralNetworkData);
	if (Response != UE::Learning::ETrainerResponse::Success)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Error sending encoder to trainer: %s. Check log for additional errors."), *GetName(), UE::Learning::Trainer::GetResponseString(Response));
		bHasTrainingFailed = true;
		Trainer->Terminate();
		return;
	}

	Response = Trainer->SendNetwork(DecoderNetworkId, *Policy->GetDecoderNetworkAsset()->NeuralNetworkData);
	if (Response != UE::Learning::ETrainerResponse::Success)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Error sending decoder to trainer: %s. Check log for additional errors."), *GetName(), UE::Learning::Trainer::GetResponseString(Response));
		bHasTrainingFailed = true;
		Trainer->Terminate();
		return;
	}

	UE_LOG(LogLearning, Display, TEXT("%s: Sending Experience..."), *GetName());
	Response = Trainer->SendReplayBuffer(ReplayBufferId, *ReplayBuffer);
	if (Response != UE::Learning::ETrainerResponse::Success)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Error sending experience to trainer: %s. Check log for additional errors."), *GetName(), UE::Learning::Trainer::GetResponseString(Response));
		bHasTrainingFailed = true;
		Trainer->Terminate();
		return;
	}

	bIsTraining = true;
}

TSharedRef<FJsonObject> ULearningAgentsImitationTrainer::CreateConfig(const FLearningAgentsImitationTrainerTrainingSettings& TrainingSettings) const
{
	TSharedRef<FJsonObject> ConfigObject = MakeShared<FJsonObject>();
	
	// Add Training Config Entries
	ConfigObject->SetStringField(TEXT("TaskName"), TEXT("Training"));
	ConfigObject->SetStringField(TEXT("TrainerMethod"), TEXT("BehaviorCloning"));
	ConfigObject->SetStringField(TEXT("TimeStamp"), *FDateTime::Now().ToFormattedString(TEXT("%Y-%m-%d_%H-%M-%S")));

	const int32 ObservationSchemaId = 0;
	const int32 ActionSchemaId = 0;

	// Add Neural Network Config Entries
	TArray<TSharedPtr<FJsonValue>> NetworkObjects;

	// Policy
	{
		TSharedPtr<FJsonObject> NetworkObject = MakeShared<FJsonObject>();
		NetworkObject->SetNumberField(TEXT("Id"), PolicyNetworkId);
		NetworkObject->SetStringField(TEXT("Name"), Policy->GetPolicyNetworkAsset()->GetFName().ToString());
		NetworkObject->SetNumberField(TEXT("MaxByteNum"), Policy->GetPolicyNetworkAsset()->NeuralNetworkData->GetSnapshotByteNum());

		TSharedRef<FJsonValueObject> JsonValue = MakeShared<FJsonValueObject>(NetworkObject);
		NetworkObjects.Add(JsonValue);
	}

	// Encoder
	{
		TSharedPtr<FJsonObject> NetworkObject = MakeShared<FJsonObject>();
		NetworkObject->SetNumberField(TEXT("Id"), EncoderNetworkId);
		NetworkObject->SetStringField(TEXT("Name"), Policy->GetEncoderNetworkAsset()->GetFName().ToString());
		NetworkObject->SetNumberField(TEXT("MaxByteNum"), Policy->GetEncoderNetworkAsset()->NeuralNetworkData->GetSnapshotByteNum());
		NetworkObject->SetNumberField(TEXT("InputSchemaId"), ObservationSchemaId);

		TSharedRef<FJsonValueObject> JsonValue = MakeShared<FJsonValueObject>(NetworkObject);
		NetworkObjects.Add(JsonValue);
	}

	// Decoder
	{
		TSharedPtr<FJsonObject> NetworkObject = MakeShared<FJsonObject>();
		NetworkObject->SetNumberField(TEXT("Id"), DecoderNetworkId);
		NetworkObject->SetStringField(TEXT("Name"), Policy->GetDecoderNetworkAsset()->GetFName().ToString());
		NetworkObject->SetNumberField(TEXT("MaxByteNum"), Policy->GetDecoderNetworkAsset()->NeuralNetworkData->GetSnapshotByteNum());
		NetworkObject->SetNumberField(TEXT("OutputSchemaId"), ActionSchemaId);

		TSharedRef<FJsonValueObject> JsonValue = MakeShared<FJsonValueObject>(NetworkObject);
		NetworkObjects.Add(JsonValue);
	}

	ConfigObject->SetArrayField(TEXT("Networks"), NetworkObjects);

	// Add Replay Buffers Config Entries
	TArray<TSharedPtr<FJsonValue>> ReplayBufferObjects;
	TSharedRef<FJsonValueObject> ReplayBufferJsonValue = MakeShared<FJsonValueObject>(ReplayBuffer->AsJsonConfig(ReplayBufferId));
	ReplayBufferObjects.Add(ReplayBufferJsonValue);
	ConfigObject->SetArrayField(TEXT("ReplayBuffers"), ReplayBufferObjects);

	// Schemas
	TSharedPtr<FJsonObject> SchemasObject = MakeShared<FJsonObject>();

	// Add the observation schemas
	TArray<TSharedPtr<FJsonValue>> ObservationSchemaObjects;
	{
		// For this trainer, add the one observation schema we have
		TSharedPtr<FJsonObject> ObservationSchemaObject = MakeShared<FJsonObject>();
		ObservationSchemaObject->SetNumberField(TEXT("Id"), ObservationSchemaId);
		ObservationSchemaObject->SetStringField(TEXT("Name"), "Default");
		ObservationSchemaObject->SetObjectField(TEXT("Schema"),
			UE::Learning::Trainer::ConvertObservationSchemaToJSON(Interactor->GetObservationSchema()->ObservationSchema,
				Interactor->GetObservationSchemaElement().SchemaElement));

		TSharedRef<FJsonValueObject> JsonValue = MakeShared<FJsonValueObject>(ObservationSchemaObject);
		ObservationSchemaObjects.Add(JsonValue);
	}
	SchemasObject->SetArrayField(TEXT("Observations"), ObservationSchemaObjects);

	// Add the action schemas
	TArray<TSharedPtr<FJsonValue>> ActionSchemaObjects;
	{
		// For this trainer, add the one action schema we have
		TSharedPtr<FJsonObject> ActionSchemaObject = MakeShared<FJsonObject>();
		ActionSchemaObject->SetNumberField(TEXT("Id"), ActionSchemaId);
		ActionSchemaObject->SetStringField(TEXT("Name"), "Default");
		ActionSchemaObject->SetObjectField(TEXT("Schema"),
			UE::Learning::Trainer::ConvertActionSchemaToJSON(Interactor->GetActionSchema()->ActionSchema,
				Interactor->GetActionSchemaElement().SchemaElement));

		TSharedRef<FJsonValueObject> JsonValue = MakeShared<FJsonValueObject>(ActionSchemaObject);
		ActionSchemaObjects.Add(JsonValue);
	}
	SchemasObject->SetArrayField(TEXT("Actions"), ActionSchemaObjects);

	ConfigObject->SetObjectField(TEXT("Schemas"), SchemasObject);

	// Add Imitation Specific Config Entries
	ConfigObject->SetObjectField(TEXT("BehaviorCloningSettings"), TrainingSettings.AsJsonConfig());
	ConfigObject->SetNumberField(TEXT("MemoryStateNum"), Policy->GetMemoryStateSize());

	return ConfigObject;
}

void ULearningAgentsImitationTrainer::SendConfig(const TSharedRef<FJsonObject>& ConfigObject)
{
	UE::Learning::ETrainerResponse Response = UE::Learning::ETrainerResponse::Success;
	Response = Trainer->SendConfig(ConfigObject, UE::Learning::ELogSetting::Normal); // todo pipe the log settings up to the blueprint graph

	if (Response != UE::Learning::ETrainerResponse::Success)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Error sending config to trainer: %s. Check log for additional errors."), *GetName(), UE::Learning::Trainer::GetResponseString(Response));
		bHasTrainingFailed = true;
		Trainer->Terminate();
		return;
	}
}

void ULearningAgentsImitationTrainer::DoneTraining()
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	if (IsTraining())
	{
		// Wait for Trainer to finish
		Trainer->Wait();

		// If not finished in time, terminate
		Trainer->Terminate();

		bIsTraining = false;
	}
}

void ULearningAgentsImitationTrainer::EndTraining()
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	if (IsTraining())
	{
		UE_LOG(LogLearning, Display, TEXT("%s: Stopping training..."), *GetName());
		Trainer->SendStop();
		DoneTraining();
	}
}

void ULearningAgentsImitationTrainer::IterateTraining()
{
	UE_LEARNING_TRACE_CPUPROFILER_EVENT_SCOPE(ULearningAgentsImitationTrainer::IterateTraining);

	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	if (!IsTraining())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Training not running."), *GetName());
		return;
	}

	if (Trainer->HasNetworkOrCompleted())
	{
		UE_LOG(LogLearning, Display, TEXT("Receiving trained networks..."));

		UE::Learning::ETrainerResponse Response = Trainer->ReceiveNetwork(PolicyNetworkId, *Policy->GetPolicyNetworkAsset()->NeuralNetworkData);
		if (Response == UE::Learning::ETrainerResponse::Completed)
		{
			UE_LOG(LogLearning, Display, TEXT("%s: Trainer completed training."), *GetName());
			DoneTraining();
			return;
		}
		else if (Response != UE::Learning::ETrainerResponse::Success)
		{
			UE_LOG(LogLearning, Error, TEXT("%s: Error receiving policy from trainer: %s. Check log for additional errors."), *GetName(), UE::Learning::Trainer::GetResponseString(Response));
			bHasTrainingFailed = true;
			EndTraining();
			return;
		}
		Policy->GetPolicyNetworkAsset()->ForceMarkDirty();

		Response = Trainer->ReceiveNetwork(EncoderNetworkId, *Policy->GetEncoderNetworkAsset()->NeuralNetworkData);
		if (Response != UE::Learning::ETrainerResponse::Success)
		{
			UE_LOG(LogLearning, Error, TEXT("%s: Error receiving encoder from trainer: %s. Check log for additional errors."), *GetName(), UE::Learning::Trainer::GetResponseString(Response));
			bHasTrainingFailed = true;
			EndTraining();
			return;
		}
		Policy->GetEncoderNetworkAsset()->ForceMarkDirty();

		Response = Trainer->ReceiveNetwork(DecoderNetworkId, *Policy->GetDecoderNetworkAsset()->NeuralNetworkData);
		if (Response != UE::Learning::ETrainerResponse::Success)
		{
			UE_LOG(LogLearning, Error, TEXT("%s: Error receiving decoder from trainer: %s. Check log for additional errors."), *GetName(), UE::Learning::Trainer::GetResponseString(Response));
			bHasTrainingFailed = true;
			EndTraining();
			return;
		}
		Policy->GetDecoderNetworkAsset()->ForceMarkDirty();
	}
}

void ULearningAgentsImitationTrainer::RunTraining(
	const ULearningAgentsRecording* Recording,
	const FLearningAgentsImitationTrainerSettings& ImitationTrainerSettings,
	const FLearningAgentsImitationTrainerTrainingSettings& ImitationTrainerTrainingSettings,
	const FLearningAgentsTrainerProcessSettings& ImitationTrainerPathSettings)
{
	if (!IsSetup())
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Setup not complete."), *GetName());
		return;
	}

	if (bHasTrainingFailed)
	{
		UE_LOG(LogLearning, Error, TEXT("%s: Training has failed. Check log for errors."), *GetName());
		return;
	}

	// If we aren't training yet, then start training and do the first inference step.
	if (!IsTraining())
	{
		BeginTraining(
			Recording,
			ImitationTrainerSettings,
			ImitationTrainerTrainingSettings,
			ImitationTrainerPathSettings);

		if (!IsTraining())
		{
			// If IsTraining is false, then BeginTraining must have failed and we can't continue.
			return;
		}
	}

	// Otherwise, do the regular training process.
	IterateTraining();
}

bool ULearningAgentsImitationTrainer::IsTraining() const
{
	return bIsTraining;
}

bool ULearningAgentsImitationTrainer::HasTrainingFailed() const
{
	return bHasTrainingFailed;
}
