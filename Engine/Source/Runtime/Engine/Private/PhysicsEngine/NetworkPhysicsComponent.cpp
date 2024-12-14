// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/NetworkPhysicsComponent.h"
#include "Components/PrimitiveComponent.h"
#include "EngineLogs.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "PBDRigidsSolver.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "Chaos/PhysicsObjectInternalInterface.h"
#include "Net/Core/PushModel/PushModel.h"

#if UE_WITH_IRIS
#include "Iris/ReplicationState/PropertyNetSerializerInfoRegistry.h"
#endif // UE_WITH_IRIS

#include UE_INLINE_GENERATED_CPP_BY_NAME(NetworkPhysicsComponent)

namespace PhysicsReplicationCVars
{
	namespace ResimulationCVars
	{
		int32 RedundantInputs = 2;
		static FAutoConsoleVariableRef CVarResimRedundantInputs(TEXT("np2.Resim.RedundantInputs"), RedundantInputs, TEXT("How many extra inputs to send with each unreliable network message, to account for packetloss."));
		int32 RedundantStates = 0;
		static FAutoConsoleVariableRef CVarResimRedundantStates(TEXT("np2.Resim.RedundantStates"), RedundantStates, TEXT("How many extra states to send with each unreliable network message, to account for packetloss."));
		bool bAllowRewindToClosestState = true;
		static FAutoConsoleVariableRef CVarResimAllowRewindToClosestState(TEXT("np2.Resim.AllowRewindToClosestState"), bAllowRewindToClosestState, TEXT("When rewinding to a specific frame, if the client doens't have state data for that frame, use closest data available. Only affects the first rewind frame, when FPBDRigidsEvolution is set to Reset."));
		bool bCompareStateToTriggerRewind = false;
		static FAutoConsoleVariableRef CVarResimCompareStateToTriggerRewind(TEXT("np2.Resim.CompareStateToTriggerRewind"), bCompareStateToTriggerRewind, TEXT("When true, cache local players custom state struct in rewind history and compare the predicted state with incoming server state to trigger resimulations if they differ, comparison done through FNetworkPhysicsData::CompareData"));
		bool bCompareInputToTriggerRewind = false;
		static FAutoConsoleVariableRef CVarResimCompareInputToTriggerRewind(TEXT("np2.Resim.CompareInputToTriggerRewind"), bCompareInputToTriggerRewind, TEXT("When true, compare local players predicted inputs with incoming server inputs to trigger resimulations if they differ, comparison done through FNetworkPhysicsData::CompareData."));
		bool bEnableUnreliableFlow = true;
		static FAutoConsoleVariableRef CVarResimEnableUnreliableFlow(TEXT("np2.Resim.EnableUnreliableFlow"), bEnableUnreliableFlow, TEXT("When true, allow data to be sent unreliably. Also sends FNetworkPhysicsData not marked with FNetworkPhysicsData::bimportant unreliably over the network."));
		bool bEnableReliableFlow = false;
		static FAutoConsoleVariableRef CVarResimEnableReliableFlow(TEXT("np2.Resim.EnableReliableFlow"), bEnableReliableFlow, TEXT("EXPERIMENTAL -- When true, allow data to be sent reliably. Also send FNetworkPhysicsData marked with FNetworkPhysicsData::bimportant reliably over the network."));
		bool bApplyDataInsteadOfMergeData = false;
		static FAutoConsoleVariableRef CVarResimApplyDataInsteadOfMergeData(TEXT("np2.Resim.ApplyDataInsteadOfMergeData"), bApplyDataInsteadOfMergeData, TEXT("When true, call ApplyData for each data instead of MergeData when having to use multiple data entries in one frame."));
		bool bAllowInputExtrapolation = true;
		static FAutoConsoleVariableRef CVarResimAllowInputExtrapolation(TEXT("np2.Resim.AllowInputExtrapolation"), bAllowInputExtrapolation, TEXT("When true and not locally controlled, allow inputs to be extrapolated from last known and if there is a gap allow interpolation between two known inputs."));
		bool bValidateDataOnGameThread = false;
		static FAutoConsoleVariableRef CVarResimValidateDataOnGameThread(TEXT("np2.Resim.ValidateDataOnGameThread"), bValidateDataOnGameThread, TEXT("When true, perform server-side input validation through FNetworkPhysicsData::ValidateData on the Game Thread. If false, perform the call on the Physics Thread."));
	}
}

/** These CVars are deprecated from UE 5.4, physics frame offset for networked physics prediction is now handled via PlayerController with automatic time dilation
* p.net.CmdOffsetEnabled = 0 is recommended to disable the deprecated flow */
namespace InputCmdCVars
{
	static bool bCmdOffsetEnabled = true;
	static FAutoConsoleVariableRef CVarCmdOffsetEnabled(TEXT("p.net.CmdOffsetEnabled"), bCmdOffsetEnabled, TEXT("Enables deprecated (5.4) logic for legacy that handles physics frame offset. Recommended: Set this to 0 to stop the deprecated physics frame offset flow. "));

	static int32 ForceFault = 0;
	static FAutoConsoleVariableRef CVarForceFault(TEXT("p.net.ForceFault"), ForceFault, TEXT("Forces server side input fault"));
	static int32 MaxBufferedCmds = 16;
	static FAutoConsoleVariableRef CVarMaxBufferedCmds(TEXT("p.net.MaxBufferedCmds"), MaxBufferedCmds, TEXT("MaxNumber of buffered server side commands"));
	static int32 TimeDilationEnabled = 0;
	static FAutoConsoleVariableRef CVarTimeDilationEnabled(TEXT("p.net.TimeDilationEnabled"), TimeDilationEnabled, TEXT("Enable clientside TimeDilation"));
	static float MaxTargetNumBufferedCmds = 5.0;
	static FAutoConsoleVariableRef CVarMaxTargetNumBufferedCmds(TEXT("p.net.MaxTargetNumBufferedCmds"), MaxTargetNumBufferedCmds, TEXT("Maximum number of buffered inputs the server will target per client."));
	static float MaxTimeDilationMag = 0.01f;
	static FAutoConsoleVariableRef CVarMaxTimeDilationMag(TEXT("p.net.MaxTimeDilationMag"), MaxTimeDilationMag, TEXT("Maximum time dilation that client will use to slow down / catch up with server"));
	static float TimeDilationAlpha = 0.1f;
	static FAutoConsoleVariableRef CVarTimeDilationAlpha(TEXT("p.net.TimeDilationAlpha"), TimeDilationAlpha, TEXT("Lerp strength for sliding client time dilation"));
	static float TargetNumBufferedCmdsDeltaOnFault = 1.0f;
	static FAutoConsoleVariableRef CVarTargetNumBufferedCmdsDeltaOnFault(TEXT("p.net.TargetNumBufferedCmdsDeltaOnFault"), TargetNumBufferedCmdsDeltaOnFault, TEXT("How much to increase TargetNumBufferedCmds when an input fault occurs"));
	static float TargetNumBufferedCmds = 1.9f;
	static FAutoConsoleVariableRef CVarTargetNumBufferedCmds(TEXT("p.net.TargetNumBufferedCmds"), TargetNumBufferedCmds, TEXT("How much to increase TargetNumBufferedCmds when an input fault occurs"));
	static float TargetNumBufferedCmdsAlpha = 0.005f;
	static FAutoConsoleVariableRef CVarTargetNumBufferedCmdsAlpha(TEXT("p.net.TargetNumBufferedCmdsAlpha"), TargetNumBufferedCmdsAlpha, TEXT("Lerp strength for TargetNumBufferedCmds"));
	static int32 LerpTargetNumBufferedCmdsAggresively = 0;
	static FAutoConsoleVariableRef CVarLerpTargetNumBufferedCmdsAggresively(TEXT("p.net.LerpTargetNumBufferedCmdsAggresively"), LerpTargetNumBufferedCmdsAggresively, TEXT("Aggresively lerp towards TargetNumBufferedCmds. Reduces server side buffering but can cause more artifacts."));
}
// --------------------------------------------------------------------------------------------------------------------------------------------------
//	Client InputCmd Stream stuff
// --------------------------------------------------------------------------------------------------------------------------------------------------
namespace
{
	int8 QuantizeTimeDilation(float F)
	{
		if (F == 1.f)
		{
			return 0;
		}
		float Normalized = FMath::Clamp<float>((F - 1.f) / InputCmdCVars::MaxTimeDilationMag, -1.f, 1.f);
		return (int8)(Normalized * 128.f);
	}

	float DeQuantizeTimeDilation(int8 i)
	{
		if (i == 0)
		{
			return 1.f;
		}
		float Normalized = (float)i / 128.f;
		float Uncompressed = 1.f + (Normalized * InputCmdCVars::MaxTimeDilationMag);
		return Uncompressed;
	}
}

bool FNetworkPhysicsRewindDataProxy::NetSerializeBase(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess, TUniqueFunction<TUniquePtr<Chaos::FBaseRewindHistory>()> CreateHistoryFunction)
{
	Ar << Owner;

	bool bHasData = History.IsValid();
	Ar.SerializeBits(&bHasData, 1);

	if (bHasData)
	{
		if (Ar.IsLoading() && !History.IsValid())
		{
			if(ensureMsgf(Owner, TEXT("FNetRewindDataBase::NetSerialize: owner is null")))
			{
				History = CreateHistoryFunction();
				if (!ensureMsgf(History.IsValid(), TEXT("FNetRewindDataBase::NetSerialize: failed to create history. Owner: %s"), *GetFullNameSafe(Owner)))
				{
					Ar.SetError();
					bOutSuccess = false;
					return true;
				}
			}
			else
			{
				Ar.SetError();
				bOutSuccess = false;
				return true;
			}
		}

		History->NetSerialize(Ar, Map);
	}

	return true;
}

FNetworkPhysicsRewindDataProxy& FNetworkPhysicsRewindDataProxy::operator=(const FNetworkPhysicsRewindDataProxy& Other)
{
	if (&Other != this)
	{
		Owner = Other.Owner;
		History = Other.History ? Other.History->Clone() : nullptr;
	}

	return *this;
}

#if UE_WITH_IRIS
UE_NET_IMPLEMENT_NAMED_STRUCT_LASTRESORT_NETSERIALIZER_AND_REGISTRY_DELEGATES(NetworkPhysicsRewindDataInputProxy);
UE_NET_IMPLEMENT_NAMED_STRUCT_LASTRESORT_NETSERIALIZER_AND_REGISTRY_DELEGATES(NetworkPhysicsRewindDataStateProxy);
UE_NET_IMPLEMENT_NAMED_STRUCT_LASTRESORT_NETSERIALIZER_AND_REGISTRY_DELEGATES(NetworkPhysicsRewindDataImportantInputProxy);
UE_NET_IMPLEMENT_NAMED_STRUCT_LASTRESORT_NETSERIALIZER_AND_REGISTRY_DELEGATES(NetworkPhysicsRewindDataImportantStateProxy);
#endif // UE_WITH_IRIS

bool FNetworkPhysicsRewindDataInputProxy::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	return NetSerializeBase(Ar, Map, bOutSuccess, [this]() { return Owner->ReplicatedInputs.History->CreateNew(); });
}

bool FNetworkPhysicsRewindDataStateProxy::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	return NetSerializeBase(Ar, Map, bOutSuccess, [this]() { return Owner->ReplicatedStates.History->CreateNew(); });
}

bool FNetworkPhysicsRewindDataImportantInputProxy::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	return NetSerializeBase(Ar, Map, bOutSuccess, [this]() { return Owner->ReplicatedImportantInput.History->CreateNew(); });
}

bool FNetworkPhysicsRewindDataImportantStateProxy::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	return NetSerializeBase(Ar, Map, bOutSuccess, [this]() { return Owner->ReplicatedImportantState.History->CreateNew(); });
}


// --------------------------- Network Physics Callback ---------------------------

// Before PreSimulate_Internal
void FNetworkPhysicsCallback::ProcessInputs_Internal(int32 PhysicsStep, const TArray<Chaos::FSimCallbackInputAndObject>& SimCallbacks)
{
	PreProcessInputsInternal.Broadcast(PhysicsStep);
	for (Chaos::ISimCallbackObject* SimCallbackObject : RewindableCallbackObjects)
	{
		SimCallbackObject->ProcessInputs_Internal(PhysicsStep);
	}
	PostProcessInputsInternal.Broadcast(PhysicsStep);
}

void FNetworkPhysicsCallback::PreResimStep_Internal(int32 PhysicsStep, bool bFirst)
{
	if (bFirst)
	{
		for (Chaos::ISimCallbackObject* SimCallbackObject : RewindableCallbackObjects)
		{
			SimCallbackObject->FirstPreResimStep_Internal(PhysicsStep);
		}
	}
}

void FNetworkPhysicsCallback::PostResimStep_Internal(int32 PhysicsStep)
{

}

int32 FNetworkPhysicsCallback::TriggerRewindIfNeeded_Internal(int32 LatestStepCompleted)
{
	int32 ResimFrame = INDEX_NONE;
	for (Chaos::ISimCallbackObject* SimCallbackObject : RewindableCallbackObjects)
	{
		const int32 CallbackFrame = SimCallbackObject->TriggerRewindIfNeeded_Internal(LatestStepCompleted);
		ResimFrame = (ResimFrame == INDEX_NONE) ? CallbackFrame : FMath::Min(CallbackFrame, ResimFrame);
	}

	if (RewindData)
	{
		const int32 TargetStateComparisonFrame = RewindData->CompareTargetsToLastFrame();
		ResimFrame = (ResimFrame == INDEX_NONE) ? TargetStateComparisonFrame : (TargetStateComparisonFrame == INDEX_NONE) ? ResimFrame : FMath::Min(TargetStateComparisonFrame, ResimFrame);

		const int32 ReplicationFrame = RewindData->GetResimFrame();
		ResimFrame = (ResimFrame == INDEX_NONE) ? ReplicationFrame : (ReplicationFrame == INDEX_NONE) ? ResimFrame : FMath::Min(ReplicationFrame, ResimFrame);

		if (ResimFrame != INDEX_NONE)
		{
			const int32 ValidFrame = RewindData->FindValidResimFrame(ResimFrame);
#if DEBUG_NETWORK_PHYSICS || DEBUG_REWIND_DATA
			UE_LOG(LogChaos, Log, TEXT("CLIENT | PT | TriggerRewindIfNeeded_Internal | Requested Resim Frame = %d (%d / %d) | Valid Resim Frame = %d"), ResimFrame, TargetStateComparisonFrame, ReplicationFrame, ValidFrame);
#endif
			ResimFrame = ValidFrame;
		}
	}
	
	return ResimFrame;
}

/* Deprecated 5.4 */
void FNetworkPhysicsCallback::UpdateClientPlayer_External(int32 PhysicsStep)
{
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		// ------------------------------------------------
		// Send RPC to server telling them what (client/local) physics step we are running
		//	* Note that SendData is empty because of the existing API, should change this
		// ------------------------------------------------	
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		TArray<uint8> SendData;
		PC->PushClientInput(PhysicsStep, SendData);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

		if (InputCmdCVars::TimeDilationEnabled > 0)
		{
			UE_LOG(LogChaos, Warning, TEXT("p.net.TimeDilationEnabled is set to true, this CVar is deprecated in UE5.4 and does not affect Time Dilation. Time Dilation is automatically used via the PlayerController if Physics Prediction is enabled in Project Settings. It's also recommended to disable the legacy flow that handled physics frame offset and this time dilation by setting: p.net.CmdOffsetEnabled = 0"));
		}
	}
}

/* Deprecated 5.4 */
void FNetworkPhysicsCallback::UpdateServerPlayer_External(int32 PhysicsStep)
{
	// -----------------------------------------------
	// Server: "consume" an InputCmd from each Player Controller
	// All this means in this context is updating FServerFrameInfo:: LastProcessedInputFrame, LastLocalFrame
	// (E.g, telling each client what "Input" of theirs we were processing and our local physics frame number.
	// In cases where the buffer has a fault, we calculate a suggested time dilation to temporarily make client speed up 
	// or slow down their input cmd production.
	// -----------------------------------------------
	const bool bForceFault = InputCmdCVars::ForceFault > 0;
	InputCmdCVars::ForceFault = FMath::Max(0, InputCmdCVars::ForceFault - 1);
	for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if (APlayerController* PC = Iterator->Get())
		{
			PC->UpdateServerTimestampToCorrect();

			APlayerController::FServerFrameInfo& FrameInfo = PC->GetServerFrameInfo();
			APlayerController::FInputCmdBuffer& InputBuffer = PC->GetInputBuffer();

			const int32 NumBufferedInputCmds = bForceFault ? 0 : (InputBuffer.HeadFrame() - FrameInfo.LastProcessedInputFrame);
			// Check Overflow
			if (NumBufferedInputCmds > InputCmdCVars::MaxBufferedCmds)
			{
				UE_LOG(LogChaos, Warning, TEXT("[Remote.Input] overflow %d %d -> %d"), InputBuffer.HeadFrame(), FrameInfo.LastProcessedInputFrame, NumBufferedInputCmds);
				FrameInfo.LastProcessedInputFrame = InputBuffer.HeadFrame() - InputCmdCVars::MaxBufferedCmds + 1;
			}
			// Check fault - we are waiting for Cmds to reach TargetNumBufferedCmds before continuing
			if (FrameInfo.bFault)
			{
				if (NumBufferedInputCmds < (int32)FrameInfo.TargetNumBufferedCmds)
				{
					// Skip this because it is in fault. We will use the prev input for this frame.
					UE_CLOG(FrameInfo.LastProcessedInputFrame != INDEX_NONE, LogPhysics, Warning, TEXT("[Remote.Input] in fault. Reusing Inputcmd. (Client) Input: %d. (Server) Local Frame: %d"), FrameInfo.LastProcessedInputFrame, FrameInfo.LastLocalFrame);
					continue;
				}
				FrameInfo.bFault = false;
			}
			else if (NumBufferedInputCmds <= 0)
			{
				// No Cmds to process, enter fault state. Increment TargetNumBufferedCmds each time this happens.
				// TODO: We should have something to bring this back down (which means skipping frames) we don't want temporary poor conditions to cause permanent high input buffering
				FrameInfo.bFault = true;
				FrameInfo.TargetNumBufferedCmds = FMath::Min(FrameInfo.TargetNumBufferedCmds + InputCmdCVars::TargetNumBufferedCmdsDeltaOnFault, InputCmdCVars::MaxTargetNumBufferedCmds);
				UE_CLOG(FrameInfo.LastProcessedInputFrame != INDEX_NONE, LogPhysics, Warning, TEXT("[Remote.Input] ENTERING fault. New Target: %.2f. (Client) Input: %d. (Server) Local Frame: %d"), FrameInfo.TargetNumBufferedCmds, FrameInfo.LastProcessedInputFrame, FrameInfo.LastLocalFrame);
				continue;
			}
			float TargetTimeDilation = 1.f;
			if (NumBufferedInputCmds < (int32)FrameInfo.TargetNumBufferedCmds)
			{
				TargetTimeDilation += InputCmdCVars::MaxTimeDilationMag; // Tell client to speed up, we are starved on cmds
			}
			FrameInfo.TargetTimeDilation = FMath::Lerp(FrameInfo.TargetTimeDilation, TargetTimeDilation, InputCmdCVars::TimeDilationAlpha);
				
			FrameInfo.QuantizedTimeDilation = QuantizeTimeDilation(TargetTimeDilation);
				
			if (InputCmdCVars::LerpTargetNumBufferedCmdsAggresively != 0)
			{
				// When aggressive, always lerp towards target
				FrameInfo.TargetNumBufferedCmds = FMath::Lerp(FrameInfo.TargetNumBufferedCmds, InputCmdCVars::TargetNumBufferedCmds, InputCmdCVars::TargetNumBufferedCmdsAlpha);
			}
			FrameInfo.LastProcessedInputFrame++;
			FrameInfo.LastLocalFrame = PhysicsStep;
		}
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
}

void FNetworkPhysicsCallback::InjectInputs_External(int32 PhysicsStep, int32 NumSteps)
{
	InjectInputsExternal.Broadcast(PhysicsStep, NumSteps);
}

void FNetworkPhysicsCallback::ProcessInputs_External(int32 PhysicsStep, const TArray<Chaos::FSimCallbackInputAndObject>& SimCallbackInputs)
{
	for(const Chaos::FSimCallbackInputAndObject& SimCallbackObject : SimCallbackInputs)
	{
		if (SimCallbackObject.CallbackObject && SimCallbackObject.CallbackObject->HasOption(Chaos::ESimCallbackOptions::Rewind))
		{
			SimCallbackObject.CallbackObject->ProcessInputs_External(PhysicsStep);
		}
	}
	
	/* Deprecated 5.4 */
	if (InputCmdCVars::bCmdOffsetEnabled)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if (World && World->GetNetMode() == NM_Client)
		{
			UpdateClientPlayer_External(PhysicsStep);
		}
		else
		{
			UpdateServerPlayer_External(PhysicsStep);
		}
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
}


// --------------------------- Network Physics System ---------------------------

UNetworkPhysicsSystem::UNetworkPhysicsSystem()
{}

void UNetworkPhysicsSystem::Initialize(FSubsystemCollectionBase& Collection)
{
	UWorld* World = GetWorld();
	check(World);

	if (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game)
	{
		FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &UNetworkPhysicsSystem::OnWorldPostInit);
	}
}

void UNetworkPhysicsSystem::Deinitialize()
{}

void UNetworkPhysicsSystem::OnWorldPostInit(UWorld* World, const UWorld::InitializationValues)
{
	if (World != GetWorld())
	{
		return;
	}

	if (UPhysicsSettings::Get()->PhysicsPrediction.bEnablePhysicsPrediction || UPhysicsSettings::Get()->PhysicsPrediction.bEnablePhysicsHistoryCapture)
	{
		if (FPhysScene* PhysScene = World->GetPhysicsScene())
		{
			if (Chaos::FPhysicsSolver* Solver = PhysScene->GetSolver())
			{ 
				if (Solver->GetRewindCallback() == nullptr)
				{
					Solver->SetRewindCallback(MakeUnique<FNetworkPhysicsCallback>(World));
				}

				if (UPhysicsSettings::Get()->PhysicsPrediction.bEnablePhysicsHistoryCapture)
				{
					if (Solver->GetRewindData() == nullptr)
					{
						Solver->EnableRewindCapture();
					}
				}
			}
		}
	}
}


// --------------------------- GameThread Network Physics Component ---------------------------

UNetworkPhysicsComponent::UNetworkPhysicsComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitPhysics();
}

UNetworkPhysicsComponent::UNetworkPhysicsComponent() : Super()
{
	InitPhysics();
}

void UNetworkPhysicsComponent::InitPhysics()
{
	if (const IConsoleVariable* CVarRedundantInputs = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.Resim.RedundantInputs")))
	{
		SetNumberOfInputsToNetwork(CVarRedundantInputs->GetInt());
	}
	if (const IConsoleVariable* CVarRedundantStates = IConsoleManager::Get().FindConsoleVariable(TEXT("np2.Resim.RedundantStates")))
	{
		SetNumberOfStatesToNetwork(CVarRedundantStates->GetInt());
	}

	if (AActor* Owner = GetOwner())
	{
		if (UPrimitiveComponent* RootPrimComp = Cast<UPrimitiveComponent>(Owner->GetRootComponent()))
		{
			RootPhysicsObject = RootPrimComp->GetPhysicsObjectByName(NAME_None);
		}
	}

	/** NOTE:
	* If the NetworkPhysicsComponent is added as a SubObject after the actor has processed bAutoActivate and bWantsInitializeComponent
	* SetActive(true) and InitializeComponent() needs to be called manually for the component to function properly. */
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	bAutoActivate = true;
	bWantsInitializeComponent = true;
	SetIsReplicatedByDefault(true);
}

void UNetworkPhysicsComponent::BeginPlay()
{
	Super::BeginPlay();

	// Update async component with current component properties
	UpdateAsyncComponent(true);
}

void UNetworkPhysicsComponent::InitializeComponent()
{
	Super::InitializeComponent();

	// Cache CVar values
	bEnableUnreliableFlow = PhysicsReplicationCVars::ResimulationCVars::bEnableUnreliableFlow;
	bEnableReliableFlow = PhysicsReplicationCVars::ResimulationCVars::bEnableReliableFlow;
	bValidateDataOnGameThread = PhysicsReplicationCVars::ResimulationCVars::bValidateDataOnGameThread;

	UNetworkPhysicsSettingsComponent* SettingsComponent = nullptr;
	if (AActor* Owner = GetOwner())
	{
		// Get settings from NetworkPhysicsSettingsComponent, if there is one
		SettingsComponent = Owner->FindComponentByClass<UNetworkPhysicsSettingsComponent>();
		if (SettingsComponent)
		{
			SetNumberOfInputsToNetwork(SettingsComponent->NetworkPhysicsComponentSettings.GetRedundantInputs());
			SetNumberOfStatesToNetwork(SettingsComponent->NetworkPhysicsComponentSettings.GetRedundantStates());
			bEnableUnreliableFlow = SettingsComponent->NetworkPhysicsComponentSettings.GetEnableUnreliableFlow();
			bEnableReliableFlow = SettingsComponent->NetworkPhysicsComponentSettings.GetEnableReliableFlow();
			bValidateDataOnGameThread = SettingsComponent->NetworkPhysicsComponentSettings.GetValidateDataOnGameThread();

			if (ReplicatedInputs.History)
			{
				ReplicatedInputs.History->ResizeDataHistory(InputsToNetwork);
			}
			if (ReplicatedStates.History)
			{
				ReplicatedStates.History->ResizeDataHistory(StatesToNetwork);
			}
		}
	}

	if (UWorld* World = GetWorld())
	{
		if (FPhysScene* PhysScene = World->GetPhysicsScene())
		{
			if (Chaos::FPhysicsSolver* Solver = PhysScene->GetSolver())
			{
				// Create async component to run on Physics Thread
				NetworkPhysicsComponent_Internal = Solver->CreateAndRegisterSimCallbackObject_External<FAsyncNetworkPhysicsComponent>();
				NetworkPhysicsComponent_Internal->SettingsComponent = SettingsComponent ? SettingsComponent->GetNetworkPhysicsSettings_Internal() : nullptr;
				NetworkPhysicsComponent_Internal->RootPhysicsObject = RootPhysicsObject;
				NetworkPhysicsComponent_Internal->InputsToNetwork = InputsToNetwork;
				NetworkPhysicsComponent_Internal->StatesToNetwork = StatesToNetwork;
				CreateAsyncDataHistory();
			}
		}
	}
}

void UNetworkPhysicsComponent::UninitializeComponent()
{
	Super::UninitializeComponent();

	if (NetworkPhysicsComponent_Internal)
	{
		if (FAsyncNetworkPhysicsComponentInput* AsyncInput = NetworkPhysicsComponent_Internal->GetProducerInputData_External())
		{
			AsyncInput->ActorComponent = nullptr;
		}
	}

	if (UWorld* World = GetWorld())
	{
		if (FPhysScene* PhysScene = World->GetPhysicsScene())
		{
			if (Chaos::FPhysicsSolver* Solver = PhysScene->GetSolver())
			{
				// Clear async component from Physics Thread and memory
				Solver->UnregisterAndFreeSimCallbackObject_External(NetworkPhysicsComponent_Internal);
			}
		}
	}
}

void UNetworkPhysicsComponent::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams ReplicatedParams;
	ReplicatedParams.Condition = COND_None;
	ReplicatedParams.RepNotifyCondition = REPNOTIFY_Always; //REPNOTIFY_OnChanged;
	ReplicatedParams.bIsPushBased = true;

	DOREPLIFETIME_WITH_PARAMS_FAST(UNetworkPhysicsComponent, ReplicatedInputs, ReplicatedParams);
	DOREPLIFETIME_WITH_PARAMS_FAST(UNetworkPhysicsComponent, ReplicatedStates, ReplicatedParams);
}

// Called every Game Thread frame
void UNetworkPhysicsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateAsyncComponent(false);
	NetworkMarshaledData();
}

void UNetworkPhysicsComponent::NetworkMarshaledData()
{
	if (NetworkPhysicsComponent_Internal)
	{
		const bool bIsServer = HasServerWorld();
		while (Chaos::TSimCallbackOutputHandle<FAsyncNetworkPhysicsComponentOutput> AsyncOutput = NetworkPhysicsComponent_Internal->PopFutureOutputData_External())
		{
			// Unimportant / Unreliable
			if (bEnableUnreliableFlow
				&& AsyncOutput->InputData
				&& AsyncOutput->InputData->HasDataInHistory()
				&& AsyncOutput->InputData->CopyAllData(*ReplicatedInputs.History, /*bIncludeUnimportant*/ true, /*bIncludeImportant*/ bEnableReliableFlow == false))
			{
				if (bIsServer)
				{
					// Server sends inputs through property replication
					MARK_PROPERTY_DIRTY_FROM_NAME(UNetworkPhysicsComponent, ReplicatedInputs, this);
				}
				else if (IsLocallyControlled())
				{
					// Clients send inputs through an RPC to the server
					ServerReceiveInputData(ReplicatedInputs);
				}
			}

			// Important / Reliable
			if (bEnableReliableFlow)
			{
				for (const TUniquePtr<Chaos::FBaseRewindHistory>& InputImportant : AsyncOutput->InputDataImportant)
				{
					if (!InputImportant || !InputImportant->HasDataInHistory())
					{
						continue;
					}

					ReplicatedImportantInput.History->ResizeDataHistory(InputImportant->GetHistorySize());
					if (InputImportant->CopyAllData(*ReplicatedImportantInput.History, /*bIncludeUnimportant*/ false, /*bIncludeImportant*/ true))
					{
						if (bIsServer)
						{
							MulticastReceiveImportantInputData(ReplicatedImportantInput);
						}
						else if (IsLocallyControlled())
						{
							ServerReceiveImportantInputData(ReplicatedImportantInput);
						}
					}
				}
			}

			if (bIsServer)
			{
				// Unimportant / Unreliable
				if (bEnableUnreliableFlow
					&& AsyncOutput->StateData
					&& AsyncOutput->StateData->HasDataInHistory()
					&& AsyncOutput->StateData->CopyAllData(*ReplicatedStates.History, /*bIncludeUnimportant*/ true, /*bIncludeImportant*/ bEnableReliableFlow == false))
				{
					// If on server we should send the states onto all the clients through repnotify
					MARK_PROPERTY_DIRTY_FROM_NAME(UNetworkPhysicsComponent, ReplicatedStates, this);
				}

				// Important / Reliable
				if (bEnableReliableFlow)
				{
					for (const TUniquePtr<Chaos::FBaseRewindHistory>& StateImportant : AsyncOutput->StateDataImportant)
					{
						if (!StateImportant || !StateImportant->HasDataInHistory())
						{
							continue;
						}

						ReplicatedImportantState.History->ResizeDataHistory(StateImportant->GetHistorySize());
						if (StateImportant->CopyAllData(*ReplicatedImportantState.History, /*bIncludeUnimportant*/ false, /*bIncludeImportant*/ true))
						{
							MulticastReceiveImportantStateData(ReplicatedImportantState);
						}
					}
				}
			}

			if (bStopRelayingLocalInputsDeferred)
			{
				bIsRelayingLocalInputs = false;
				bStopRelayingLocalInputsDeferred = false;
			}
		}
	}
}

/** Deprecated 5.4 */
void UNetworkPhysicsComponent::CorrectServerToLocalOffset(const int32 LocalToServerOffset)
{
	if (IsLocallyControlled() && !HasServerWorld() && StateHistory)
	{
		TArray<int32> LocalFrames, ServerFrames, InputFrames;
		PRAGMA_DISABLE_DEPRECATION_WARNINGS // TODO: Change to DebugData() in UE 5.6 and remove deprecation pragma
		StateHistory->DebugDatas(*ReplicatedStates.History, LocalFrames, ServerFrames, InputFrames);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

		int32 ServerToLocalOffset = LocalToServerOffset;
		for (int32 FrameIndex = 0; FrameIndex < LocalFrames.Num(); ++FrameIndex)
		{
#if DEBUG_NETWORK_PHYSICS || DEBUG_REWIND_DATA
			UE_LOG(LogChaos, Log, TEXT("CLIENT | GT | CorrectServerToLocalOffset | Server frame = %d | Client Frame = %d | [NOTE: DEPRECATED logic, deactivate by setting CVar: p.net.CmdOffsetEnabled 0]"), ServerFrames[FrameIndex], InputFrames[FrameIndex]);
#endif
			ServerToLocalOffset = FMath::Min(ServerToLocalOffset, ServerFrames[FrameIndex] - InputFrames[FrameIndex]);
		}
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		GetPlayerController()->SetServerToLocalAsyncPhysicsTickOffset(ServerToLocalOffset);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

#if DEBUG_NETWORK_PHYSICS || DEBUG_REWIND_DATA
		UE_LOG(LogChaos, Log, TEXT("CLIENT | GT | CorrectServerToLocalOffset | Server to local offset = %d | Local to server offset = %d | [NOTE: DEPRECATED logic, deactivate by setting CVar: p.net.CmdOffsetEnabled 0]"), ServerToLocalOffset, LocalToServerOffset);
#endif
	}
}

void UNetworkPhysicsComponent::OnRep_SetReplicatedStates()
{
	if (!NetworkPhysicsComponent_Internal || !StateHelper || !ReplicatedStates.History)
	{
		return;
	}

	if (FAsyncNetworkPhysicsComponentInput* AsyncInput = NetworkPhysicsComponent_Internal->GetProducerInputData_External())
	{
		if (!AsyncInput->StateData)
		{
			AsyncInput->StateData = StateHelper->CreateUniqueRewindHistory(StatesToNetwork);
		}

		AsyncInput->StateData->ResetFast();
		ReplicatedStates.History->CopyAllData(*AsyncInput->StateData.Get(), /*bIncludeUnimportant*/ true, /*bIncludeImportant*/ true);
	}

	/** Deprecated 5.4 */
	if (InputCmdCVars::bCmdOffsetEnabled)
	{
		APlayerController* PlayerController = GetPlayerController();
		if (!PlayerController)
		{
			PlayerController = GetWorld()->GetFirstPlayerController();
		}

		if (PlayerController && !HasServerWorld())
		{
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			const int32 LocalToServerOffset = PlayerController->GetLocalToServerAsyncPhysicsTickOffset();
			CorrectServerToLocalOffset(LocalToServerOffset);
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}
	}
}

void UNetworkPhysicsComponent::OnRep_SetReplicatedInputs()
{
	if (!NetworkPhysicsComponent_Internal || !InputHelper || !ReplicatedInputs.History)
	{
		return ;
	}

	if (FAsyncNetworkPhysicsComponentInput* AsyncInput = NetworkPhysicsComponent_Internal->GetProducerInputData_External())
	{
		if (!AsyncInput->InputData)
		{
			AsyncInput->InputData = InputHelper->CreateUniqueRewindHistory(InputsToNetwork);
		}

		AsyncInput->InputData->ResetFast();
		ReplicatedInputs.History->CopyAllData(*AsyncInput->InputData.Get(), /*bIncludeUnimportant*/ true, /*bIncludeImportant*/ true);
	}
}

/** DEPRECATED UE 5.4*/
void UNetworkPhysicsComponent::ServerReceiveInputsDatas_Implementation(const FNetworkPhysicsRewindDataInputProxy& ClientInputs)
{
	ServerReceiveInputData_Implementation(ClientInputs);
}

void UNetworkPhysicsComponent::ServerReceiveInputData_Implementation(const FNetworkPhysicsRewindDataInputProxy& ClientInputs)
{
	if (!NetworkPhysicsComponent_Internal || !InputHelper || !ClientInputs.History)
	{
		return;
	}

	if (FAsyncNetworkPhysicsComponentInput* AsyncInput = NetworkPhysicsComponent_Internal->GetProducerInputData_External())
	{
		if (!AsyncInput->InputData)
		{
			AsyncInput->InputData = InputHelper->CreateUniqueRewindHistory(InputsToNetwork);
		}

		// Validate data in the received inputs
		if (bValidateDataOnGameThread && ActorComponent)
		{
			ClientInputs.History->ValidateDataInHistory(ActorComponent);
		}

		AsyncInput->InputData->ResetFast();
		ClientInputs.History->CopyAllData(*AsyncInput->InputData.Get(), /*bIncludeUnimportant*/ true, /*bIncludeImportant*/ true);
	}
}

void UNetworkPhysicsComponent::ServerReceiveImportantInputData_Implementation(const FNetworkPhysicsRewindDataImportantInputProxy& ClientInputs)
{
	if (!NetworkPhysicsComponent_Internal || !ClientInputs.History)
	{
		return;
	}

	if (FAsyncNetworkPhysicsComponentInput* AsyncInput = NetworkPhysicsComponent_Internal->GetProducerInputData_External())
	{
		// Initialize received data since not all data is networked and when we clone this we expect to have fully initialized data
		ClientInputs.History->Initialize();

		// Validate data in the received inputs
		if (bValidateDataOnGameThread && ActorComponent)
		{
			ClientInputs.History->ValidateDataInHistory(ActorComponent);
		}

		// Create new data collection for marshaling
		AsyncInput->InputDataImportant.Add(ClientInputs.History->Clone());
	}
}

void UNetworkPhysicsComponent::MulticastReceiveImportantInputData_Implementation(const FNetworkPhysicsRewindDataImportantInputProxy& ServerInputs)
{
	// Ignore Multicast on server
	if (HasServerWorld())
	{
		return;
	}

	if (!NetworkPhysicsComponent_Internal || !ServerInputs.History)
	{
		return;
	}

	if (FAsyncNetworkPhysicsComponentInput* AsyncInput = NetworkPhysicsComponent_Internal->GetProducerInputData_External())
	{
		// Initialize received data since not all data is networked and when we clone this we expect to have fully initialized data
		ServerInputs.History->Initialize();

		// Create new data collection for marshaling
		AsyncInput->InputDataImportant.Add(ServerInputs.History->Clone());
	}
}

void UNetworkPhysicsComponent::MulticastReceiveImportantStateData_Implementation(const FNetworkPhysicsRewindDataImportantStateProxy& ServerStates)
{
	// Ignore Multicast on server
	if (HasServerWorld())
	{
		return;
	}

	if (!NetworkPhysicsComponent_Internal || !ServerStates.History)
	{
		return;
	}

	if (FAsyncNetworkPhysicsComponentInput* AsyncInput = NetworkPhysicsComponent_Internal->GetProducerInputData_External())
	{
		// Initialize received data since not all data is networked and when we clone this we expect to have fully initialized data
		ServerStates.History->Initialize();

		// Create new data collection for marshaling
		AsyncInput->StateDataImportant.Add(ServerStates.History->Clone());
	}
}

/** DEPRECATED UE 5.5 */
const float UNetworkPhysicsComponent::GetCurrentInputDecay(FNetworkPhysicsData* PhysicsData)
{
	if (!PhysicsData)
	{
		return 0.0f;
	}

	FPhysScene* PhysScene = GetWorld()->GetPhysicsScene();
	if (!PhysScene)
	{
		return 0.0f;
	}

	Chaos::FPhysicsSolver* Solver = PhysScene->GetSolver();
	if (!Solver)
	{
		return 0.0f;
	}

	Chaos::FRewindData* RewindData = Solver->GetRewindData();
	if (!RewindData)
	{
		return 0.0f;
	}
	
	const float NumPredictedInputs = RewindData->CurrentFrame() - PhysicsData->LocalFrame; // Number of frames we have used the same PhysicsData for during resim
	const float MaxPredictedInputs = RewindData->GetLatestFrame() - 1 - PhysicsData->LocalFrame; // Max number of frames PhysicsData registered frame until end of resim

	// Linear decay
	const float PredictionAlpha = MaxPredictedInputs > 0 ? (NumPredictedInputs / MaxPredictedInputs) : 0.0f;

	return PredictionAlpha;
}

bool UNetworkPhysicsComponent::HasServerWorld() const
{
	return GetWorld()->IsNetMode(NM_DedicatedServer) || GetWorld()->IsNetMode(NM_ListenServer);
}

bool UNetworkPhysicsComponent::HasLocalController() const
{
	if (APlayerController* PlayerController = GetPlayerController())
	{
		return PlayerController->IsLocalController();
	}
	return false;
}

bool UNetworkPhysicsComponent::IsLocallyControlled() const
{
	if (bIsRelayingLocalInputs)
	{
		return true;
	}

	if (APlayerController* PlayerController = GetPlayerController())
	{
		return PlayerController->IsLocalController();
	}
	return false;
}

void UNetworkPhysicsComponent::SetIsRelayingLocalInputs(bool bInRelayingLocalInputs)
{
	bIsRelayingLocalInputs = bInRelayingLocalInputs;
}

APlayerController* UNetworkPhysicsComponent::GetPlayerController() const
{
	if (APlayerController* PC = Cast<APlayerController>(GetOwner()))
	{
		return PC;
	}

	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (APlayerController * PC = Pawn->GetController<APlayerController>())
		{
			return PC;
		}

		// In this case the APlayerController can be found as the owner of the pawn
		if (APlayerController* PC = Cast<APlayerController>(Pawn->GetOwner()))
		{
			return PC;
		}
	}

	return nullptr;
}

void UNetworkPhysicsComponent::UpdateAsyncComponent(const bool bFullUpdate)
{
	// Marshal data from Game Thread to Physics Thread
	if (NetworkPhysicsComponent_Internal)
	{
		if (FAsyncNetworkPhysicsComponentInput* AsyncInput = NetworkPhysicsComponent_Internal->GetProducerInputData_External())
		{
			if (!HasServerWorld())
			{
				if (UWorld* World = GetWorld())
				{
					if (APlayerController* PC = World->GetFirstPlayerController())
					{
						AsyncInput->NetworkPhysicsTickOffset = PC->GetNetworkPhysicsTickOffset();
					}
				}
			}

			// bIsLocallyControlled is marshaled outside of the bFullUpdate because it's not always set at BeginPlay when last bFullUpdate is called.
			AsyncInput->bIsLocallyControlled = IsLocallyControlled();

			if (bFullUpdate)
			{
				if (UWorld* World = GetWorld())
				{ 
					AsyncInput->NetMode = World->GetNetMode();
				}

				if (AActor* Owner = GetOwner())
				{ 
					AsyncInput->NetRole = Owner->GetLocalRole();
					AsyncInput->PhysicsReplicationMode = Owner->GetPhysicsReplicationMode();
					AsyncInput->ActorName = AActor::GetDebugName(Owner);
				}
				
				if (ActorComponent)
				{
					AsyncInput->ActorComponent = ActorComponent;
				}
			}
		}
	}
}

void UNetworkPhysicsComponent::CreateAsyncDataHistory()
{
	if (NetworkPhysicsComponent_Internal)
	{
		if (FAsyncNetworkPhysicsComponentInput* AsyncInput = NetworkPhysicsComponent_Internal->GetProducerInputData_External())
		{
			AsyncInput->ActorComponent = ActorComponent;

			if (InputHelper)
			{
				// Marshal the input helper to create both input data and input history on the physics thread
				AsyncInput->InputHelper = InputHelper->Clone();
			}

			if (StateHelper)
			{
				// Marshal the state helper to create both state data and state history on the physics thread
				AsyncInput->StateHelper = StateHelper->Clone();
			}
		}
	}
}

void UNetworkPhysicsComponent::RemoveDataHistory()
{
	// Tell the async network physics component to unregister from RewindData
	if (NetworkPhysicsComponent_Internal)
	{
		if (FAsyncNetworkPhysicsComponentInput* AsyncInput = NetworkPhysicsComponent_Internal->GetProducerInputData_External())
		{
			AsyncInput->bUnregisterDataHistoryFromRewindData = true;
		}
	}
}

void UNetworkPhysicsComponent::AddDataHistory()
{
	// Tell the async network physics component to register in RewindData
	if (NetworkPhysicsComponent_Internal)
	{
		if (FAsyncNetworkPhysicsComponentInput* AsyncInput = NetworkPhysicsComponent_Internal->GetProducerInputData_External())
		{
			AsyncInput->bRegisterDataHistoryInRewindData = true;
		}
	}
}

TSharedPtr<Chaos::FBaseRewindHistory>& UNetworkPhysicsComponent::GetStateHistory_Internal()
{
	if (NetworkPhysicsComponent_Internal)
	{
		return NetworkPhysicsComponent_Internal->StateHistory;
	}
	return StateHistory;
}

TSharedPtr<Chaos::FBaseRewindHistory>& UNetworkPhysicsComponent::GetInputHistory_Internal()
{
	if (NetworkPhysicsComponent_Internal)
	{
		return NetworkPhysicsComponent_Internal->InputHistory;
	}
	return InputHistory;
}


// --------------------------- Async Network Physics Component ---------------------------

// Initialize static
const FNetworkPhysicsSettingsNetworkPhysicsComponent FAsyncNetworkPhysicsComponent::SettingsNetworkPhysicsComponent_Default = FNetworkPhysicsSettingsNetworkPhysicsComponent();

FAsyncNetworkPhysicsComponent::FAsyncNetworkPhysicsComponent() : TSimCallbackObject()
	, bIsLocallyControlled(true)
	, NetMode(ENetMode::NM_Standalone)
	, NetRole(ENetRole::ROLE_Authority)
	, NetworkPhysicsTickOffset(0)
	, PhysicsReplicationMode(EPhysicsReplicationMode::Default)
{
}

FAsyncNetworkPhysicsComponent::~FAsyncNetworkPhysicsComponent()
{
	if (Chaos::FPhysicsSolverBase* BaseSolver = GetSolver())
	{
		// Unregister for Pre- and Post- ProcessInputs_Internal callbacks
		if (FNetworkPhysicsCallback* SolverCallback = static_cast<FNetworkPhysicsCallback*>(BaseSolver->GetRewindCallback()))
		{
			SolverCallback->PreProcessInputsInternal.Remove(DelegateOnPreProcessInputs_Internal);
			DelegateOnPreProcessInputs_Internal.Reset();

			SolverCallback->PostProcessInputsInternal.Remove(DelegateOnPostProcessInputs_Internal);
			DelegateOnPostProcessInputs_Internal.Reset();
		}
	}

	UnregisterDataHistoryFromRewindData();
}

void FAsyncNetworkPhysicsComponent::OnPostInitialize_Internal()
{
	if (Chaos::FPhysicsSolverBase* BaseSolver = GetSolver())
	{
		if (BaseSolver->IsNetworkPhysicsPredictionEnabled())
		{
			// Register for Pre- and Post- ProcessInputs_Internal callbacks
			if (FNetworkPhysicsCallback* SolverCallback = static_cast<FNetworkPhysicsCallback*>(BaseSolver->GetRewindCallback()))
			{
				DelegateOnPreProcessInputs_Internal = SolverCallback->PreProcessInputsInternal.AddRaw(this, &FAsyncNetworkPhysicsComponent::OnPreProcessInputs_Internal);
				DelegateOnPostProcessInputs_Internal = SolverCallback->PostProcessInputsInternal.AddRaw(this, &FAsyncNetworkPhysicsComponent::OnPostProcessInputs_Internal);
			}
		}
		else
		{
			UE_LOG(LogChaos, Warning, TEXT("A NetworkPhysicsComponent is trying to set up but 'Project Settings -> Physics -> Physics Prediction' is not enabled. The component might not work as intended."));
		}
	}
}

const FNetworkPhysicsSettingsNetworkPhysicsComponent& FAsyncNetworkPhysicsComponent::GetComponentSettings()
{
	return SettingsComponent ? SettingsComponent->Settings.NetworkPhysicsComponentSettings : SettingsNetworkPhysicsComponent_Default;
};

void FAsyncNetworkPhysicsComponent::ConsumeAsyncInput(const int32 PhysicsStep)
{
	if (const FAsyncNetworkPhysicsComponentInput* AsyncInput = GetConsumerInput_Internal())
	{
		const FNetworkPhysicsSettingsNetworkPhysicsComponent& ComponentSettings = GetComponentSettings();

		/** Onetime setup marshaled data */
		{
			if (AsyncInput->bIsLocallyControlled.IsSet())
			{
				bIsLocallyControlled = *AsyncInput->bIsLocallyControlled;
			}
			if (AsyncInput->NetMode.IsSet())
			{
				NetMode = *AsyncInput->NetMode;
			}
			if (AsyncInput->NetRole.IsSet())
			{
				NetRole = *AsyncInput->NetRole;
			}
			if (AsyncInput->NetworkPhysicsTickOffset.IsSet())
			{
				NetworkPhysicsTickOffset = *AsyncInput->NetworkPhysicsTickOffset;
			}
			if (AsyncInput->PhysicsReplicationMode.IsSet())
			{
				PhysicsReplicationMode = *AsyncInput->PhysicsReplicationMode;
			}
			if (AsyncInput->ActorComponent.IsSet())
			{
				ActorComponent = *AsyncInput->ActorComponent;
			}
			if (AsyncInput->ActorName.IsSet())
			{
				ActorName = *AsyncInput->ActorName;
			}
			if (AsyncInput->bRegisterDataHistoryInRewindData.IsSet())
			{
				RegisterDataHistoryInRewindData();
			}
			if (AsyncInput->bUnregisterDataHistoryFromRewindData.IsSet())
			{
				UnregisterDataHistoryFromRewindData();
			}
			if (AsyncInput->InputHelper.IsSet())
			{
				// Setup rewind data if not already done, and get history size
				const int32 NumFrames = SetupRewindData();

				// Create input history and local data properties
				InputData = (*AsyncInput->InputHelper)->CreateUniqueData();
				LatestInputReceiveData = (*AsyncInput->InputHelper)->CreateUniqueData();
				InputHistory = MakeShareable((*AsyncInput->InputHelper)->CreateUniqueRewindHistory(NumFrames).Release());
				RegisterDataHistoryInRewindData();
			}
			if (AsyncInput->StateHelper.IsSet())
			{
				// Setup rewind data if not already done, and get history size
				const int32 NumFrames = SetupRewindData();

				// Create state history and local property
				StateData = (*AsyncInput->StateHelper)->CreateUniqueData();
				StateHistory = MakeShareable((*AsyncInput->StateHelper)->CreateUniqueRewindHistory(NumFrames).Release());
				RegisterDataHistoryInRewindData();
			}
		}

		/** Continuously marshaled data */
		{
			/** Receive data helper */
			auto ReceiveHelper = [&](Chaos::FBaseRewindHistory* History, Chaos::FBaseRewindHistory* ReceiveData, const bool bImportant, const bool bCompareData)
			{
				const bool bCompareDataForRewind = bCompareData && IsLocallyControlled() && !IsServer();
				const int32 ResimFrame = History->ReceiveNewData(*ReceiveData, (IsServer() ? 0 : NetworkPhysicsTickOffset), bCompareDataForRewind, bImportant);
				if (bCompareDataForRewind)
				{
					TriggerResimulation(ResimFrame);
				}

#if DEBUG_NETWORK_PHYSICS
				{
					FString NetRoleString = IsServer() ? FString("SERVER") : (IsLocallyControlled() ? FString("AUTONO") : FString("PROXY "));
					ReceiveData->DebugData(FString::Printf(TEXT("%s | PT | RECEIVE DATA | LatestFrame: %d | bImportant: %d | Name: %s"), *NetRoleString, ReceiveData->GetLatestFrame(), bImportant, *GetActorName()));
				}
#endif

				// Reset the received data after having consumed it
				ReceiveData->ResetFast();
			};

			// Receive Inputs
			if (AsyncInput->InputData && AsyncInput->InputData->HasDataInHistory())
			{
				/* Extract latest received input from client on the server, to be used if input buffer runs empty
				* TODO, improve flow to not require this before ReceiveHelper */
				FNetworkPhysicsData* PhysicsData = nullptr;
				if (IsServer() && LatestInputReceiveData)
				{
					PhysicsData = LatestInputReceiveData.Get();
					if (AsyncInput->InputData->ExtractData(AsyncInput->InputData->GetLatestFrame(), false, PhysicsData, true) == false)
					{
						// Extraction failed
						ensureMsgf(false, TEXT("Failed to extract latest input data from received inputs"));
						PhysicsData = nullptr;

#if DEBUG_NETWORK_PHYSICS
						AsyncInput->InputData->DebugData(FString::Printf(TEXT("SERVER | PT | Failed to extract LatestInputReceiveData | LatestFrame: %d | Name: %s"), AsyncInput->InputData->GetLatestFrame(), *GetActorName()));
#endif
					}
				}

				// Validate data in the received inputs on the server
				if (!ComponentSettings.GetValidateDataOnGameThread() && IsServer() && ActorComponent.IsValid() && ActorComponent.Get()->IsBeingDestroyed() == false)
				{
					AsyncInput->InputData->ValidateDataInHistory(ActorComponent.Get());
				}
				ReceiveHelper(InputHistory.Get(), AsyncInput->InputData.Get(), /*bImportant*/false, ComponentSettings.GetCompareInputToTriggerRewind());

				/* If the server-side input history doesn't have any entries ahead of the current physics tick, the input buffer is empty, inject the latest received input as the input for the current tick.
				* This happens during a desync where the client is far behind the server */
				if (IsServer() && InputHistory->GetLatestFrame() < PhysicsStep && PhysicsData)
				{
#if DEBUG_NETWORK_PHYSICS
					UE_LOG(LogChaos, Log, TEXT("SERVER | PT | Input Buffer Empty, Injecting Received Input at frame %d || LocalFrame = %d || ServerFrame = %d || InputFrame = %d || Data: %s || Actor: %s")
						, PhysicsStep, PhysicsData->LocalFrame, PhysicsData->ServerFrame, PhysicsData->InputFrame, *PhysicsData->DebugData(), *GetActorName());
#endif
					
					// Record data in InputHistory
					PhysicsData->LocalFrame = PhysicsStep;
					PhysicsData->ServerFrame = PhysicsStep;
					InputHistory->RecordData(PhysicsStep, PhysicsData);
				}

			}

			// Receive States
			if (AsyncInput->StateData && AsyncInput->StateData->HasDataInHistory())
			{
				ReceiveHelper(StateHistory.Get(), AsyncInput->StateData.Get(), /*bImportant*/false, ComponentSettings.GetCompareStateToTriggerRewind());
			}

			// Receive Important Inputs
			for (const TUniquePtr<Chaos::FBaseRewindHistory>& InputImportant : AsyncInput->InputDataImportant)
			{
				if (!InputImportant || !InputImportant->HasDataInHistory())
				{
					continue;
				}
				ReceiveHelper(InputHistory.Get(), InputImportant.Get(), /*bImportant*/true, ComponentSettings.GetCompareInputToTriggerRewind());
			}

			// Receive Important States
			for (const TUniquePtr<Chaos::FBaseRewindHistory>& StateImportant : AsyncInput->StateDataImportant)
			{
				if (!StateImportant || !StateImportant->HasDataInHistory())
				{
					continue;
				}
				ReceiveHelper(StateHistory.Get(), StateImportant.Get(), /*bImportant*/true, ComponentSettings.GetCompareStateToTriggerRewind());
			}
		}
	}
}

FAsyncNetworkPhysicsComponentOutput& FAsyncNetworkPhysicsComponent::GetAsyncOutput_Internal()
{
	FAsyncNetworkPhysicsComponentOutput& AsyncOutput = GetProducerOutputData_Internal();

	// InputData marshal from PT to GT is needed for: LocallyControlled and Server
	if ((IsLocallyControlled() || IsServer()) && AsyncOutput.InputData == nullptr && InputHistory != nullptr)
	{
		AsyncOutput.InputData = InputHistory->CreateNew();
		AsyncOutput.InputData->ResizeDataHistory(InputsToNetwork);
	}

	// StateData marshal from PT to GT is needed for: Server
	if (IsServer() && AsyncOutput.StateData == nullptr && StateHistory != nullptr)
	{
		AsyncOutput.StateData = StateHistory->CreateNew();
		AsyncOutput.StateData->ResizeDataHistory(StatesToNetwork);
	}

	return AsyncOutput;
}

void FAsyncNetworkPhysicsComponent::OnPreProcessInputs_Internal(const int32 PhysicsStep)
{
	ConsumeAsyncInput(PhysicsStep);

	const FNetworkPhysicsSettingsNetworkPhysicsComponent& ComponentSettings = GetComponentSettings();
	const bool bIsServer = IsServer();

	bool bIsSolverReset = false;
	bool bIsSolverResim = false;
	if (Chaos::FPBDRigidsEvolution* Evolution = GetEvolution())
	{
		bIsSolverResim = Evolution->IsResimming();
		bIsSolverReset = Evolution->IsResetting();
	}

#if DEBUG_NETWORK_PHYSICS
	{
		const int32 InputBufferSize = (IsServer() && InputHistory) ? (InputHistory->GetLatestFrame() - PhysicsStep) : 0;
		const FString NetRoleString = IsServer() ? FString("SERVER") : (IsLocallyControlled() ? FString("AUTONO") : FString("PROXY "));
		UE_LOG(LogChaos, Log, TEXT("%s | PT | OnPreProcessInputsInternal | At Frame %d | IsResim: %d | FirstResimFrame: %d | InputBuffer: %d | Name = %s"), *NetRoleString, PhysicsStep, bIsSolverResim, bIsSolverReset, InputBufferSize, *GetActorName());
	}
#endif

	if (ActorComponent.IsValid() && ActorComponent.Get()->IsBeingDestroyed() == false)
	{
		// Apply replicated state on clients if we are resimulating
		if (bIsSolverResim && StateHistory && StateData)
		{
			FNetworkPhysicsData* PhysicsData = StateData.Get();
			PhysicsData->LocalFrame = PhysicsStep;
			const bool bExactFrame = PhysicsReplicationCVars::ResimulationCVars::bAllowRewindToClosestState ? !bIsSolverReset : true;
			if (StateHistory->ExtractData(PhysicsStep, bIsSolverReset, PhysicsData, bExactFrame) && PhysicsData->bReceivedData)
			{
				PhysicsData->ApplyData(ActorComponent.Get());
#if DEBUG_NETWORK_PHYSICS
				UE_LOG(LogChaos, Log, TEXT("			Applying extracted state from history | bExactFrame = %d | LocalFrame = %d | ServerFrame = %d | InputFrame = %d | Data: %s")
					, bExactFrame, PhysicsData->LocalFrame, PhysicsData->ServerFrame, PhysicsData->InputFrame, *PhysicsData->DebugData());
#endif
			}
#if DEBUG_NETWORK_PHYSICS
			else if (PhysicsStep <= StateHistory->GetLatestFrame())
			{
				UE_LOG(LogChaos, Log, TEXT("		Non-Determinism: FAILED to extract and apply state from history | bExactFrame = %d | -- Printing history --"), bExactFrame);
				StateHistory->DebugData(FString::Printf(TEXT("StateHistory | Component = %s"), *GetActorName()));
			}
#endif
		}

		// Apply replicated inputs on server and simulated proxies (and on local player if we are resimulating)
		if (InputHistory && InputData && (!IsLocallyControlled() || bIsSolverResim))
		{
			FNetworkPhysicsData* PhysicsData = InputData.Get();
			int32 NextExpectedLocalFrame = PhysicsData->LocalFrame + 1;
			PhysicsData->LocalFrame = PhysicsStep;

			// There are important inputs earlier than upcoming input to apply
			if (NewImportantInputFrame < NextExpectedLocalFrame && !bIsSolverResim)
			{
				if (ComponentSettings.GetApplyDataInsteadOfMergeData())
				{
#if DEBUG_NETWORK_PHYSICS
					UE_LOG(LogChaos, Log, TEXT("		Non-Determinism: Reapplying multiple data due to receiving an important data that was previously missed. FromFrame: %d | ToFrame: %d | IsLocallyControlled = %d"), NewImportantInputFrame, (NextExpectedLocalFrame - 1), IsLocallyControlled());
#endif
					// Apply all inputs in range
					InputHistory->ApplyDataRange(NewImportantInputFrame, NextExpectedLocalFrame - 1, ActorComponent.Get(), /*bOnlyImportant*/false);
				}
				else
				{
					// Merge all inputs from earliest new important
					NextExpectedLocalFrame = NewImportantInputFrame;
#if DEBUG_NETWORK_PHYSICS
					UE_LOG(LogChaos, Log, TEXT("		Non-Determinism: Prepare to reapply multiple data through MergeData due to receiving an important data that was previously missed. FromFrame: %d | ToFrame: %d | IsLocallyControlled = %d"), NewImportantInputFrame, (NextExpectedLocalFrame - 1), IsLocallyControlled());
#endif
				}
			}

			if (InputHistory->ExtractData(PhysicsStep, bIsSolverReset, PhysicsData, /*bExactFrame*/(ComponentSettings.GetAllowInputExtrapolation() == false)))
			{
				// Calculate input decay if we are resimulating and we don't have up to date inputs
				if (bIsSolverResim)
				{
					if (PhysicsData->LocalFrame < PhysicsStep)
					{
						const float InputDecay = GetCurrentInputDecay(PhysicsData);
						PhysicsData->DecayData(InputDecay);
					}
				}
				// Check if we have a gap between last used input and current input
				else if (PhysicsData->LocalFrame > NextExpectedLocalFrame)
				{
					if (ComponentSettings.GetApplyDataInsteadOfMergeData())
					{
#if DEBUG_NETWORK_PHYSICS
						UE_LOG(LogChaos, Log, TEXT("		Non-Determinism: Applying multiple data instead of merging, from LocalFrame %d into LocalFrame %d | IsLocallyControlled = %d"), NextExpectedLocalFrame, PhysicsData->LocalFrame, IsLocallyControlled());
#endif
						// Iterate over each input and call ApplyData, except on the last, it will get handled by the normal ApplyData call further down
						const int32 LastFrame = PhysicsData->LocalFrame;
						for (; NextExpectedLocalFrame <= LastFrame; NextExpectedLocalFrame++)
						{
							if (InputHistory->ExtractData(NextExpectedLocalFrame, bIsSolverReset, PhysicsData, true) && NextExpectedLocalFrame < LastFrame)
							{
								PhysicsData->ApplyData(ActorComponent.Get());
							}
						}
					}
					else
					{
#if DEBUG_NETWORK_PHYSICS
						UE_LOG(LogChaos, Log, TEXT("		Non-Determinism: Merging inputs from LocalFrame %d into LocalFrame %d | IsLocallyControlled = %d"), NextExpectedLocalFrame, PhysicsData->LocalFrame, IsLocallyControlled());
#endif
						// Merge all inputs since last used input
						InputHistory->MergeData(NextExpectedLocalFrame, PhysicsData);
					}
				}

				// If the extracted input data was altered (extrapolated, interpolated) on the server, record it into the history for it to get replicated to clients
				if (bIsServer && PhysicsData->InputFrame == INDEX_NONE)
				{
#if DEBUG_NETWORK_PHYSICS
					if (PhysicsStep > InputHistory->GetLatestFrame())
					{
						UE_LOG(LogChaos, Log, TEXT("		Non-Determinism: Input buffer Empty, input for frame %d was extrapolated from frame: %d"), PhysicsStep, PhysicsData->LocalFrame);
					}
#endif
					PhysicsData->bReceivedData = true; // Mark the input data as received so that it doesn't get overwritten by incoming client inputs
					PhysicsData->bImportant = false;
					PhysicsData->LocalFrame = PhysicsStep;
					InputHistory->RecordData(PhysicsStep, PhysicsData);
				}

				PhysicsData->ApplyData(ActorComponent.Get());

#if DEBUG_NETWORK_PHYSICS
				{
					UE_LOG(LogChaos, Log, TEXT("			Applying extracted input from history | LocalFrame = %d | ServerFrame = %d | InputFrame = %d | IsResim = %d | IsLocallyControlled = %d | InputDecay = %f | Data: %s")
						, PhysicsData->LocalFrame, PhysicsData->ServerFrame, PhysicsData->InputFrame, bIsSolverResim, IsLocallyControlled(), GetCurrentInputDecay(PhysicsData), *PhysicsData->DebugData());
				}
#endif
			}
#if DEBUG_NETWORK_PHYSICS
			else if (PhysicsStep <= InputHistory->GetLatestFrame())
			{
				UE_LOG(LogChaos, Log, TEXT("		Non-Determinism: FAILED to extract and apply input from history | IsResim = %d | IsLocallyControlled = %d | -- Printing history --"), bIsSolverResim, IsLocallyControlled());
				InputHistory->DebugData(FString::Printf(TEXT("InputHistory | Name = %s"), *GetActorName()));
			}
#endif
		}
	}
	NewImportantInputFrame = INT_MAX;
}

void FAsyncNetworkPhysicsComponent::OnPostProcessInputs_Internal(const int32 PhysicsStep)
{
	const FNetworkPhysicsSettingsNetworkPhysicsComponent& ComponentSettings = GetComponentSettings();
	const bool bIsServer = IsServer();

	bool bIsSolverReset = false;
	bool bIsSolverResim = false;
	if (Chaos::FPBDRigidsEvolution* Evolution = GetEvolution())
	{
		bIsSolverResim = Evolution->IsResimming();
		bIsSolverReset = Evolution->IsResetting();
	}

#if DEBUG_NETWORK_PHYSICS
	{
		FString NetRoleString = IsServer() ? FString("SERVER") : (IsLocallyControlled() ? FString("AUTONO") : FString("PROXY "));
		UE_LOG(LogChaos, Log, TEXT("%s | PT | OnPostProcessInputsInternal | At Frame %d | IsResim: %d | FirstResimFrame: %d | Name = %s"), *NetRoleString, PhysicsStep, bIsSolverResim, bIsSolverReset, *GetActorName());
	}
#endif

	if (ActorComponent.IsValid() && ActorComponent.Get()->IsBeingDestroyed() == false)
	{
		// Cache current input if we are locally controlled
		const bool bShouldCacheInputHistory = IsLocallyControlled() && !bIsSolverResim;
		if (bShouldCacheInputHistory && (InputData != nullptr))
		{
			// Prepare to gather input data
			FNetworkPhysicsData* PhysicsData = InputData.Get();
			PhysicsData->PrepareFrame(PhysicsStep, bIsServer, GetNetworkPhysicsTickOffset());

			// Gather input data from implementation
			PhysicsData->BuildData(ActorComponent.Get());

			// Record input in history
			InputHistory->RecordData(PhysicsStep, PhysicsData);

#if DEBUG_NETWORK_PHYSICS
			{
				UE_LOG(LogChaos, Log, TEXT("		Recording input into history | LocalFrame = %d | ServerFrame = %d | InputFrame = %d | Input: %s ")
					, PhysicsData->LocalFrame, PhysicsData->ServerFrame, PhysicsData->InputFrame, *PhysicsData->DebugData());
			}
#endif
		}

		// Cache current state if this is the server of we are comparing predicted states on autonomous proxy
		const bool bShouldCacheStateHistory = bIsServer || (ComponentSettings.GetCompareStateToTriggerRewind() && bShouldCacheInputHistory);
		if (StateHistory && StateData && bShouldCacheStateHistory)
		{
			// Compute of the local frame coming from the client that was used to generate this state
			int32 InputFrame = INDEX_NONE;
			if (InputData)
			{
				FNetworkPhysicsData* PhysicsData = InputData.Get();
				if (InputHistory && InputHistory->ExtractData(PhysicsStep, false, PhysicsData, true))
				{
					InputFrame = PhysicsData->InputFrame;
				}
			}

			// Prepare to gather state data
			FNetworkPhysicsData* PhysicsData = StateData.Get();
			PhysicsData->PrepareFrame(PhysicsStep, bIsServer, GetNetworkPhysicsTickOffset());
			PhysicsData->InputFrame = InputFrame;

			// Gather input data from implementation
			PhysicsData->BuildData(ActorComponent.Get());

			// Record input in history
			StateHistory->RecordData(PhysicsStep, PhysicsData);

#if DEBUG_NETWORK_PHYSICS
			{
				UE_LOG(LogChaos, Log, TEXT("		Recording state into history | LocalFrame = %d | ServerFrame = %d | InputFrame = %d | State: %s ")
					, PhysicsData->LocalFrame, PhysicsData->ServerFrame, PhysicsData->InputFrame, *PhysicsData->DebugData());
			}
#endif
		}
	}

	// Marshal inputs and states from PT to GT for networking
	FAsyncNetworkPhysicsComponentOutput& AsyncOutput = GetAsyncOutput_Internal();
	SendInputData_Internal(AsyncOutput, PhysicsStep);
	SendStateData_Internal(AsyncOutput, PhysicsStep);
	FinalizeOutputData_Internal();
}

void FAsyncNetworkPhysicsComponent::SendInputData_Internal(FAsyncNetworkPhysicsComponentOutput& AsyncOutput, const int32 PhysicsStep)
{
	const bool bIsServer = IsServer();

	// Inputs are sent from the server or locally controlled actors/pawns
	if (AsyncOutput.InputData && InputHistory && (IsLocallyControlled() || bIsServer))
	{
		const FNetworkPhysicsSettingsNetworkPhysicsComponent& ComponentSettings = GetComponentSettings();

		// Send latest N frames from history
		const int32 ToFrame = FMath::Max(0, PhysicsStep);

		// -- Default / Unreliable Flow --
		if (ComponentSettings.GetEnableUnreliableFlow())
		{
			const int32 FromFrame = FMath::Max(0, ToFrame - InputsToNetwork - 1); // Remove 1 since both ToFrame and FromFrame are inclusive

			// Resize marshaling history if needed
			AsyncOutput.InputData->ResizeDataHistory(InputsToNetwork);
			
			if (InputHistory->CopyData(*AsyncOutput.InputData, FromFrame, ToFrame, /*bIncludeUnimportant*/ true, /*bIncludeImportant*/ ComponentSettings.GetEnableReliableFlow() == false))
			{
#if DEBUG_NETWORK_PHYSICS
				{
					const int32 LocalFrame = GetRigidSolver()->GetCurrentFrame();
					const int32 ServerFrame = IsServer() ? LocalFrame : LocalFrame + GetNetworkPhysicsTickOffset();
					FString NetRoleString = IsServer() ? FString("SERVER") : (IsLocallyControlled() ? FString("AUTONO") : FString("PROXY "));
					AsyncOutput.InputData->DebugData(FString::Printf(TEXT("%s | PT | SendInputData_Internal | UNRELIABLE | CurrentLocalFrame = %d | CurrentServerFrame = %d | Name: %s"), *NetRoleString, LocalFrame, ServerFrame, *GetActorName()));
				}
#endif
			}
		}

		// -- Important / Reliable flow --
		if (ComponentSettings.GetEnableReliableFlow())
		{
			/* Get the latest valid frame that can hold new important data:
			* 1. Frame after last time we called SendInputData
			* 2. Earliest possible frame in history */
			const int32 FromFrame = FMath::Max(LastInputSendFrame + 1, ToFrame - InputHistory->GetHistorySize());

			// Check if we have important data to marshal
			const int32 Count = InputHistory->CountValidData(FromFrame, ToFrame, /*bIncludeUnimportant*/ false, /*bIncludeImportant*/ true);
			if (Count > 0)
			{
				// Create new data collection for marshaling
				const int32 Idx = AsyncOutput.InputDataImportant.Add(InputHistory->CreateNew());
				AsyncOutput.InputDataImportant[Idx]->ResizeDataHistory(Count);

				// Copy over data
				if (InputHistory->CopyData(*AsyncOutput.InputDataImportant[Idx], FromFrame, ToFrame, /*bIncludeUnimportant*/ false, /*bIncludeImportant*/ true))
				{
#if DEBUG_NETWORK_PHYSICS
					{
						const int32 LocalFrame = GetRigidSolver()->GetCurrentFrame();
						const int32 ServerFrame = IsServer() ? LocalFrame : LocalFrame + GetNetworkPhysicsTickOffset();
						FString NetRoleString = IsServer() ? FString("SERVER") : (IsLocallyControlled() ? FString("AUTONO") : FString("PROXY "));
						AsyncOutput.InputDataImportant[Idx]->DebugData(FString::Printf(TEXT("%s | PT | SendInputData_Internal | RELIABLE | CurrentLocalFrame = %d | CurrentServerFrame = %d | Name: %s"), *NetRoleString, LocalFrame, ServerFrame, *GetActorName()));
					}
#endif
				}
				
			}
		}
		LastInputSendFrame = InputHistory->GetLatestFrame();
	}
}

void FAsyncNetworkPhysicsComponent::SendStateData_Internal(FAsyncNetworkPhysicsComponentOutput& AsyncOutput, const int32 PhysicsStep)
{
	if (IsServer() && StateHistory && AsyncOutput.StateData)
	{
		const FNetworkPhysicsSettingsNetworkPhysicsComponent& ComponentSettings = GetComponentSettings();

		// Send latest N frames from history
		const int32 ToFrame = FMath::Max(0, PhysicsStep);

		// -- Default / Unreliable Flow --
		if (ComponentSettings.GetEnableUnreliableFlow())
		{
			const int32 FromFrame = FMath::Max(0, ToFrame - StatesToNetwork - 1); // Remove 1 since both ToFrame and FromFrame are inclusive

			// Resize marshaling history if needed
			AsyncOutput.StateData->ResizeDataHistory(StatesToNetwork);

			if (StateHistory->CopyData(*AsyncOutput.StateData, FromFrame, ToFrame, /*bIncludeUnimportant*/ true, /*bIncludeImportant*/ ComponentSettings.GetEnableReliableFlow() == false))
			{
#if DEBUG_NETWORK_PHYSICS
				{
					const int32 LocalFrame = GetRigidSolver()->GetCurrentFrame();
					const int32 ServerFrame = IsServer() ? LocalFrame : LocalFrame + GetNetworkPhysicsTickOffset();
					AsyncOutput.StateData->DebugData(FString::Printf(TEXT("SERVER | PT | SendStateData_Internal | UNRELIABLE | CurrentLocalFrame = %d | CurrentServerFrame = %d | Name: %s"), LocalFrame, ServerFrame, *GetActorName()));
				}
#endif
			}
		}

		// -- Important / Reliable flow --
		if (ComponentSettings.GetEnableReliableFlow())
		{
			/* Get the latest valid frame that can hold new important data:
			* 1. Frame after last time we called SendStateData
			* 2. Earliest possible frame in history */
			const int32 FromFrame = FMath::Max(LastStateSendFrame + 1, ToFrame - StateHistory->GetHistorySize());

			// Check if we have important data to marshal
			const int32 Count = StateHistory->CountValidData(FromFrame, ToFrame, /*bIncludeUnimportant*/ false, /*bIncludeImportant*/ true);
			if (Count > 0)
			{
				// Create new data collection for marshaling
				const int32 Idx = AsyncOutput.StateDataImportant.Add(StateHistory->CreateNew());
				AsyncOutput.StateDataImportant[Idx]->ResizeDataHistory(Count);

				// Copy over data
				if (StateHistory->CopyData(*AsyncOutput.StateDataImportant[Idx], FromFrame, ToFrame, /*bIncludeUnimportant*/ false, /*bIncludeImportant*/ true))
				{
#if DEBUG_NETWORK_PHYSICS
					{
						const int32 LocalFrame = GetRigidSolver()->GetCurrentFrame();
						const int32 ServerFrame = IsServer() ? LocalFrame : LocalFrame + GetNetworkPhysicsTickOffset();
						AsyncOutput.StateDataImportant[Idx]->DebugData(FString::Printf(TEXT("SERVER | PT | SendStateData_Internal | RELIABLE | CurrentLocalFrame = %d | CurrentServerFrame = %d | Name: %s"), LocalFrame, ServerFrame, *GetActorName()));
					}
#endif
				}
			}
		}
		LastStateSendFrame = StateHistory->GetLatestFrame();
	}
}

Chaos::FPBDRigidsSolver* FAsyncNetworkPhysicsComponent::GetRigidSolver()
{
	return static_cast<Chaos::FPBDRigidsSolver*>(GetSolver());
}

Chaos::FPBDRigidsEvolution* FAsyncNetworkPhysicsComponent::GetEvolution()
{
	if (Chaos::FPBDRigidsSolver* RigidSolver = GetRigidSolver())
	{
		return RigidSolver->GetEvolution();
	}
	return nullptr;
}

void FAsyncNetworkPhysicsComponent::TriggerResimulation(int32 ResimFrame)
{
	if (ResimFrame != INDEX_NONE)
	{
		if (Chaos::FPBDRigidsSolver* RigidSolver = GetRigidSolver())
		{
			if (Chaos::FRewindData* RewindData = RigidSolver->GetRewindData())
			{
				// Mark particle/island as resim
				Chaos::FReadPhysicsObjectInterface_Internal Interface = Chaos::FPhysicsObjectInternalInterface::GetRead();
				if (Chaos::FPBDRigidParticleHandle* POHandle = Interface.GetRigidParticle(RootPhysicsObject))
				{
					if (Chaos::FPBDRigidsEvolution* Evolution = GetEvolution())
					{
						Evolution->GetIslandManager().SetParticleResimFrame(POHandle, ResimFrame);
					}
				}

				// Set resim frame in rewind data
				ResimFrame = (RewindData->GetResimFrame() == INDEX_NONE) ? ResimFrame : FMath::Min(ResimFrame, RewindData->GetResimFrame());
				RewindData->SetResimFrame(ResimFrame);
			}
		}
	}
}

const float FAsyncNetworkPhysicsComponent::GetCurrentInputDecay(const FNetworkPhysicsData* PhysicsData)
{
	if (!PhysicsData)
	{
		return 0.0f;
	}
	
	Chaos::FPhysicsSolverBase* BaseSolver = GetSolver();
	if (!BaseSolver)
	{
		return 0.0f;
	}

	Chaos::FRewindData* RewindData = BaseSolver->GetRewindData();
	if (!RewindData)
	{
		return 0.0f;
	}

	const float NumPredictedInputs = RewindData->CurrentFrame() - PhysicsData->LocalFrame; // Number of frames we have used the same PhysicsData for during resim
	const float MaxPredictedInputs = RewindData->GetLatestFrame() - 1 - PhysicsData->LocalFrame; // Max number of frames PhysicsData registered frame until end of resim

	// Linear decay
	const float PredictionAlpha = MaxPredictedInputs > 0 ? (NumPredictedInputs / MaxPredictedInputs) : 0.0f;

	return PredictionAlpha;
}

void FAsyncNetworkPhysicsComponent::RegisterDataHistoryInRewindData()
{
	if (Chaos::FPhysicsSolverBase* BaseSolver = GetSolver())
	{
		if (Chaos::FRewindData* RewindData = BaseSolver->GetRewindData())
		{
			UnregisterDataHistoryFromRewindData();

			RewindData->AddInputHistory(InputHistory);
			if (StateHistory)
			{
				RewindData->AddStateHistory(StateHistory);
			}
		}
	}
}

void FAsyncNetworkPhysicsComponent::UnregisterDataHistoryFromRewindData()
{
	if (Chaos::FPhysicsSolverBase* BaseSolver = GetSolver())
	{
		if (Chaos::FRewindData* RewindData = BaseSolver->GetRewindData())
		{
			RewindData->RemoveInputHistory(InputHistory);
			RewindData->RemoveStateHistory(StateHistory);
		}
	}
}

const int32 FAsyncNetworkPhysicsComponent::SetupRewindData()
{
	int32 NumFrames = 0;

	if (Chaos::FPBDRigidsSolver* RigidSolver = GetRigidSolver())
	{
		NumFrames = FMath::Max<int32>(1, FMath::CeilToInt32((0.001f * Chaos::FPBDRigidsSolver::GetPhysicsHistoryTimeLength()) / RigidSolver->GetAsyncDeltaTime()));

		if (IsServer())
		{
			return NumFrames;
		}

		// Don't let this actor initialize RewindData if not using resimulation
		if (GetPhysicsReplicationMode() == EPhysicsReplicationMode::Resimulation)
		{
			if (RigidSolver->IsNetworkPhysicsPredictionEnabled() && RigidSolver->GetRewindData() == nullptr)
			{
				RigidSolver->EnableRewindCapture();
			}
		}

		if (Chaos::FRewindData* RewindData = RigidSolver->GetRewindData())
		{
			NumFrames = RewindData->Capacity();
		}
	}

	return NumFrames;
}
