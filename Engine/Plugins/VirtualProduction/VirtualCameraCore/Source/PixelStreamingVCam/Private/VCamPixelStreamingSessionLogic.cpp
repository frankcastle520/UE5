// Copyright Epic Games, Inc. All Rights Reserved.

#include "VCamPixelStreamingSessionLogic.h"

#include "BuiltinProviders/VCamPixelStreamingSession.h"
#include "IDecoupledOutputProviderModule.h"
#include "Media/PixelStreamingMediaOutput.h"
#include "Output/VCamOutputComposure.h"
#include "VCamComponent.h"
#include "VCamPixelStreamingSubsystem.h"

#include "Async/Async.h"
#include "Containers/UnrealString.h"
#include "Editor/EditorPerformanceSettings.h"
#include "GameFramework/Actor.h"
#include "IPixelStreamingStats.h"
#include "IPixelStreamingModule.h"
#include "IPixelStreamingInputModule.h"
#include "Math/Matrix.h"
#include "Misc/CoreDelegates.h"
#include "PixelStreamingDelegates.h"
#include "PixelStreamingInputEnums.h"
#include "PixelStreamingInputMessage.h"
#include "PixelStreamingInputProtocol.h"
#include "PixelStreamingVCamLog.h"
#include "PixelStreamingVCamModule.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/MemoryReader.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SVirtualWindow.h"
#include "Widgets/VPFullScreenUserWidget.h"

#if WITH_EDITOR
#include "Framework/Application/SlateApplication.h"
#endif

#define LOCTEXT_NAMESPACE "FVCamPixelStreamingSessionLogic"


namespace UE::PixelStreamingVCam::Private
{
	static FString GenerateDefaultStreamerName(const UVCamPixelStreamingSession& Session)
	{
		using namespace VCamCore;
		const bool bContainsOtherPixelStreamingOutput = Session.GetVCamComponent()->GetOutputProviders().ContainsByPredicate([&Session](const TObjectPtr<UVCamOutputProviderBase>& OtherOutputProvider)
		{
			return OtherOutputProvider != &Session && OtherOutputProvider->GetClass()->IsChildOf(Session.GetClass());
		});
		return GenerateUniqueOutputProviderName(Session, bContainsOtherPixelStreamingOutput ? ENameGenerationFlags::None : ENameGenerationFlags::SkipAppendingIndex);
	}
	
	/** Sets the owning VCam's live link subject to this the subject created by this session, if this behaviour is enabled. */
	static void ConditionallySetLiveLinkSubjectToThis(const UVCamPixelStreamingSession& Session)
	{
		UVCamComponent* VCamComponent = Session.GetTypedOuter<UVCamComponent>();
		if (Session.bAutoSetLiveLinkSubject && IsValid(VCamComponent) && Session.IsActive())
		{
			VCamComponent->SetLiveLinkSubobject(FName(Session.StreamerId));
		}
	}

	/** Makes sure that all systems relying on the subject name have the latest name. */
	static void UpdateLiveLinkSubject(const UVCamPixelStreamingSession& Session)
	{
		if (UVCamPixelStreamingSubsystem* PixelStreamingSubsystem = UVCamPixelStreamingSubsystem::Get())
		{
			PixelStreamingSubsystem->UpdateLiveLinkSource(Session);
		}

		// Also need to make sure that the VCam uses the new subject name
		ConditionallySetLiveLinkSubjectToThis(Session);
	}
}

namespace UE::PixelStreamingVCam
{
	FVCamPixelStreamingSessionLogic::FVCamPixelStreamingSessionLogic(const DecoupledOutputProvider::FOutputProviderLogicCreationArgs& Args)
		: ManagedOutputProvider(Cast<UVCamPixelStreamingSession>(Args.Provider))
	{
#if WITH_EDITOR
		FCoreDelegates::OnActorLabelChanged.AddRaw(this, &FVCamPixelStreamingSessionLogic::OnActorLabelChanged);
#endif
	}
	
	FVCamPixelStreamingSessionLogic::~FVCamPixelStreamingSessionLogic()
	{
		UnregisterPixelStreamingDelegates();
#if WITH_EDITOR
		FCoreDelegates::OnActorLabelChanged.RemoveAll(this);
#endif
	}

	void FVCamPixelStreamingSessionLogic::OnDeinitialize(DecoupledOutputProvider::IOutputProviderEvent& Args)
	{
		UnregisterPixelStreamingDelegates();
		CleanupMediaOutput();
	}

	void FVCamPixelStreamingSessionLogic::OnActivate(DecoupledOutputProvider::IOutputProviderEvent& Args)
	{
		UVCamPixelStreamingSession* This = ManagedOutputProvider.Get();
		AActor* OwningActor = This ? This->GetTypedOuter<AActor>() : nullptr;
		if (!ensure(This && OwningActor))
		{
			return;
		}
		
		const TWeakObjectPtr<UVCamPixelStreamingSession> WeakThisUObjectPtr = This;
		if (This->StreamerId.IsEmpty())
		{
			RefreshStreamerName(*This);
		}

		// Setup livelink source
		if (UVCamPixelStreamingSubsystem* PixelStreamingSubsystem = UVCamPixelStreamingSubsystem::Get())
		{
			PixelStreamingSubsystem->RegisterActiveOutputProvider(This);
			Private::ConditionallySetLiveLinkSubjectToThis(*This);
		}

		// If we don't have a UMG assigned, we still need to create an empty 'dummy' UMG in order to properly route the input back from the RemoteSession device
		if (!This->GetUMGClass())
		{
			bUsingDummyUMG = true;
			const FSoftClassPath EmptyUMGSoftClassPath(TEXT("/VCamCore/Assets/VCam_EmptyVisibleUMG.VCam_EmptyVisibleUMG_C"));
			This->SetUMGClass(EmptyUMGSoftClassPath.TryLoadClass<UUserWidget>());
		}

		// create a new media output if we dont already have one, or its not valid, or if the id has changed
		if (!MediaOutput || !MediaOutput->IsValid() || MediaOutput->GetStreamer()->GetId() != This->StreamerId)
		{
			// If there already is a MediaOutput, unregister from the below delegates.
			CleanupMediaOutput();
			
			MediaOutput = UPixelStreamingMediaOutput::Create(GetTransientPackage(), This->StreamerId);
			MediaOutput->OnRemoteResolutionChanged().AddSP(this, &FVCamPixelStreamingSessionLogic::OnRemoteResolutionChanged, WeakThisUObjectPtr);
			MediaOutput->GetStreamer()->OnPreConnection().AddSP(this, &FVCamPixelStreamingSessionLogic::OnPreStreaming, WeakThisUObjectPtr);
			MediaOutput->GetStreamer()->OnStreamingStarted().AddSP(this, &FVCamPixelStreamingSessionLogic::OnStreamingStarted, WeakThisUObjectPtr);
			MediaOutput->GetStreamer()->OnStreamingStopped().AddSP(this, &FVCamPixelStreamingSessionLogic::OnStreamingStopped);
		}

		UEditorPerformanceSettings* Settings = GetMutableDefault<UEditorPerformanceSettings>();
		bOldThrottleCPUWhenNotForeground = Settings->bThrottleCPUWhenNotForeground;
		if (This->PreventEditorIdle)
		{
			Settings->bThrottleCPUWhenNotForeground = false;
			Settings->PostEditChange();
		}

		// Super::Activate() creates our UMG which we need before setting up our custom input handling
		Args.ExecuteSuperFunction();

		// We setup custom handling of ARKit transforms coming from iOS devices here
		SetupCustomInputHandling(This);
		// We need signalling server to be up before we can start streaming
		SetupSignallingServer(*This);

		if (MediaOutput)
		{
			UE_LOG(LogPixelStreamingVCam, Log, TEXT("Activating PixelStreaming VCam Session. Endpoint: %s"), *MediaOutput->GetStreamer()->GetSignallingServerURL());

			// Start streaming here, this will trigger capturer to start
			MediaOutput->StartStreaming();
		}

		if (UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates())
		{
			Delegates->OnAllConnectionsClosedNative.AddRaw(this, &FVCamPixelStreamingSessionLogic::OnAllConnectionsClosed);
		}

		FPixelStreamingVCamModule::Get().AddActiveSession(WeakThisUObjectPtr);
	}

	void FVCamPixelStreamingSessionLogic::OnDeactivate(DecoupledOutputProvider::IOutputProviderEvent& Args)
	{
		UVCamPixelStreamingSession* This = Cast<UVCamPixelStreamingSession>(&Args.GetOutputProvider());
		if (UVCamPixelStreamingSubsystem* PixelStreamingSubsystem = UVCamPixelStreamingSubsystem::Get())
		{
			PixelStreamingSubsystem->UnregisterActiveOutputProvider(This);
		}

		StopEverything(*This);

		Args.ExecuteSuperFunction();
		if (bUsingDummyUMG)
		{
			This->SetUMGClass(nullptr);
			bUsingDummyUMG = false;
		}

		UEditorPerformanceSettings* Settings = GetMutableDefault<UEditorPerformanceSettings>();
		Settings->bThrottleCPUWhenNotForeground = bOldThrottleCPUWhenNotForeground;
		Settings->PostEditChange();

		UnregisterPixelStreamingDelegates();
		
		for (TPair<int32, TPromise<FVCamStringPromptResponse>>& PromisePair : StringPromptPromises)
		{
			PromisePair.Value.EmplaceValue(FVCamStringPromptResponse(EVCamStringPromptResult::Disconnected));
		}

		const TWeakObjectPtr<UVCamPixelStreamingSession> WeakThisPtr = This;
		FPixelStreamingVCamModule::Get().RemoveActiveSession(WeakThisPtr);
	}

	VCamCore::EViewportChangeReply FVCamPixelStreamingSessionLogic::PreReapplyViewport(DecoupledOutputProvider::IOutputProviderEvent& Args)
	{
		return VCamCore::EViewportChangeReply::ApplyViewportChange;
	}

	void FVCamPixelStreamingSessionLogic::PostReapplyViewport(DecoupledOutputProvider::IOutputProviderEvent& Args)
	{
		UVCamPixelStreamingSession* This = Cast<UVCamPixelStreamingSession>(&Args.GetOutputProvider());
		// We're called as part of UVCamOutputProviderBase::ReinitializeViewport, which has called FViewportManager::RequestResolutionRefresh.
		// RequestResolutionRefresh may update the viewport resolution at the end of the tick. If that happens, and we called SetupCapture now,
		// we'd get an EMediaCaptureState::Error in OnCaptureStateChanged (I don't know why).
		// This restarts the capture when the viewport is ready for it.
		This->GetWorld()->GetTimerManager().SetTimerForNextTick([this, WeakThis = TWeakObjectPtr(This)]
		{
			if (UVCamPixelStreamingSession* This = WeakThis.Get();
				This && This->IsOutputting())
			{
				StopCapture();

				SetupCapture(This);
				SetupCustomInputHandling(This);
			}
		});
	}

	void FVCamPixelStreamingSessionLogic::StopCapture()
	{
		if (MediaCapture)
		{
			MediaCapture->StopCapture(false);
			MediaCapture = nullptr;
		}
	}

	void FVCamPixelStreamingSessionLogic::OnPreStreaming(IPixelStreamingStreamer* PreConnectionStreamer, TWeakObjectPtr<UVCamPixelStreamingSession> WeakThisUObjectPtr)
	{
		SetupCapture(WeakThisUObjectPtr);
	}

	void FVCamPixelStreamingSessionLogic::StopStreaming()
	{
		if(!MediaOutput)
		{
			return;
		}

		MediaOutput->StopStreaming();
	}

	void FVCamPixelStreamingSessionLogic::OnStreamingStarted(IPixelStreamingStreamer* StartedStreamer, TWeakObjectPtr<UVCamPixelStreamingSession> WeakThisUObjectPtr)
	{
		SetupARKitResponseTimer(WeakThisUObjectPtr);
	}

	void FVCamPixelStreamingSessionLogic::OnStreamingStopped(IPixelStreamingStreamer* StartedStreamer)
	{
		StopARKitResponseTimer();
		StopCapture();
	}

	void FVCamPixelStreamingSessionLogic::StopEverything(UVCamPixelStreamingSession& Session)
	{
		CleanupMediaOutput();
		StopSignallingServer(Session);
		StopCapture();
	}

	void FVCamPixelStreamingSessionLogic::OnAddReferencedObjects(DecoupledOutputProvider::IOutputProviderEvent& Args, FReferenceCollector& Collector)
	{
		Collector.AddReferencedObject(MediaOutput, &Args.GetOutputProvider());
		Collector.AddReferencedObject(MediaCapture, &Args.GetOutputProvider());
	}

	TFuture<FVCamStringPromptResponse> FVCamPixelStreamingSessionLogic::PromptClientForString(DecoupledOutputProvider::IOutputProviderEvent& Args, const FVCamStringPromptRequest& Request)
	{
		UVCamPixelStreamingSession* This = Cast<UVCamPixelStreamingSession>(&Args.GetOutputProvider());

		TSharedPtr<IPixelStreamingStreamer> Streamer = IPixelStreamingModule::Get().FindStreamer(This->StreamerId);
		if (!Streamer)
		{
			return MakeFulfilledPromise<FVCamStringPromptResponse>(EVCamStringPromptResult::Unavailable).GetFuture();
		}

		TPromise<FVCamStringPromptResponse>& ResponsePromise = StringPromptPromises.Emplace(NextStringRequestId);

		TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
		JsonObject->SetStringField(TEXT("command"), TEXT("stringPrompt"));
		JsonObject->SetStringField(TEXT("defaultValue"), Request.DefaultValue);
		JsonObject->SetStringField(TEXT("promptTitle"), Request.PromptTitle);
		JsonObject->SetNumberField(TEXT("requestId"), NextStringRequestId);

		++NextStringRequestId;

		FString Descriptor;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Descriptor);
		FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter);

		Streamer->SendPlayerMessage(FPixelStreamingInputProtocol::FromStreamerProtocol.Find("Command")->GetID(), Descriptor);

		return ResponsePromise.GetFuture();
	}

#if WITH_EDITOR

	void FVCamPixelStreamingSessionLogic::OnPostEditChangeProperty(DecoupledOutputProvider::IOutputProviderEvent& Args, FPropertyChangedEvent& PropertyChangedEvent)
	{
		UVCamPixelStreamingSession* This = Cast<UVCamPixelStreamingSession>(&Args.GetOutputProvider());
		if (!This)
		{
			return;
		}

		FProperty* Property = PropertyChangedEvent.MemberProperty;
		if (Property && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
		{
			const FName PropertyName = Property->GetFName();
			if (PropertyName == GET_MEMBER_NAME_CHECKED(UVCamPixelStreamingSession, FromComposureOutputProviderIndex))
			{
				This->SetActive(false);
			}
			else if (PropertyName == GET_MEMBER_NAME_CHECKED(UVCamPixelStreamingSession, bAutoSetLiveLinkSubject))
			{
				Private::ConditionallySetLiveLinkSubjectToThis(*This);
			}
		}
		
		OnEditStreamId(*This);
	}
	
	void FVCamPixelStreamingSessionLogic::OnEditStreamId(UVCamPixelStreamingSession& This) const
	{
		const TSharedPtr<IPixelStreamingStreamer> Streamer = MediaOutput && MediaOutput->GetStreamer() ? MediaOutput->GetStreamer() : nullptr;
		if (!Streamer || !This.IsOutputting())
		{
			Private::UpdateLiveLinkSubject(This);
			return;
		}

		if (!This.bOverrideStreamerName)
		{
			RefreshStreamerName(This);
		}
		Private::UpdateLiveLinkSubject(This);
		
		const FString OldStreamerId = Streamer->GetId();
		if (OldStreamerId != This.StreamerId
			&& This.IsActive())
		{
			This.SetActive(false);
			This.SetActive(true);
		}
	}

	void FVCamPixelStreamingSessionLogic::OnActorLabelChanged(AActor* Actor) const
	{
		UVCamPixelStreamingSession* Session = ManagedOutputProvider.Get();
		const bool bIsApplicable = Session
			// User wants the StreamerId to be the actor label?
			&& !Session->bOverrideStreamerName
			// Did our owning actor's name change?
			&& Session->GetTypedOuter<AActor>() == Actor;
		if (!bIsApplicable)
		{
			return;
		}
		
		const FString OldStreamId = Session->StreamerId;
		const FString NewStreamerName = Private::GenerateDefaultStreamerName(*Session);
		if (OldStreamId == NewStreamerName)
		{
			return;
		}

		// Avoid marking the map dirty for innocent GetActorLabel(bCreateIfNone=true) calls, which can happen during map load.
		// If this function is called in response to a user edit operation, then GUndo will be set and the change will be recorded as well.
		if (GUndo)
		{
			Session->Modify();
		}
		Session->StreamerId = NewStreamerName;
		OnEditStreamId(*ManagedOutputProvider);
	}
#endif

	void FVCamPixelStreamingSessionLogic::RefreshStreamerName(UVCamPixelStreamingSession& Session) const
	{
		Session.StreamerId = Private::GenerateDefaultStreamerName(Session);
	}

	void FVCamPixelStreamingSessionLogic::SetupSignallingServer(UVCamPixelStreamingSession& Session)
	{
		if (UVCamPixelStreamingSubsystem* PixelStreamingSubsystem = UVCamPixelStreamingSubsystem::Get())
		{
			PixelStreamingSubsystem->LaunchSignallingServerIfNeeded(Session);
		}
	}

	void FVCamPixelStreamingSessionLogic::StopSignallingServer(UVCamPixelStreamingSession& Session)
	{
		if (UVCamPixelStreamingSubsystem* PixelStreamingSubsystem = UVCamPixelStreamingSubsystem::Get())
		{
			PixelStreamingSubsystem->StopSignallingServerIfNeeded(Session);
		}
	}

	void FVCamPixelStreamingSessionLogic::SetupCapture(TWeakObjectPtr<UVCamPixelStreamingSession> WeakThisUObjectPtr)
	{
		UE_LOG(LogPixelStreamingVCam, Log, TEXT("Create new media capture for Pixel Streaming VCam."));

		if (MediaCapture)
		{
			MediaCapture->OnStateChangedNative.RemoveAll(this);
		}

		// Create a capturer that will capture frames from viewport and send them to streamer
		MediaCapture = Cast<UPixelStreamingMediaIOCapture>(MediaOutput->CreateMediaCapture());
		MediaCapture->OnStateChangedNative.AddSP(this, &FVCamPixelStreamingSessionLogic::OnCaptureStateChanged, WeakThisUObjectPtr);
		StartCapture(WeakThisUObjectPtr);
	}

	void FVCamPixelStreamingSessionLogic::StartCapture(TWeakObjectPtr<UVCamPixelStreamingSession> WeakThisUObjectPtr)
	{
		if (!WeakThisUObjectPtr.IsValid() || !MediaCapture)
		{
			return;
		}

		FMediaCaptureOptions Options;
		Options.bSkipFrameWhenRunningExpensiveTasks = false;
		Options.OverrunAction = EMediaCaptureOverrunAction::Skip;
		Options.ResizeMethod = EMediaCaptureResizeMethod::ResizeSource;

		// If we are rendering from a ComposureOutputProvider, get the requested render target and use that instead of the viewport
		if (UVCamOutputComposure* ComposureProvider = Cast<UVCamOutputComposure>(WeakThisUObjectPtr->GetOtherOutputProviderByIndex(WeakThisUObjectPtr->FromComposureOutputProviderIndex)))
		{
			if (ComposureProvider->FinalOutputRenderTarget)
			{
				MediaCapture->CaptureTextureRenderTarget2D(ComposureProvider->FinalOutputRenderTarget, Options);
				UE_LOG(LogPixelStreamingVCam, Log, TEXT("PixelStreaming set with ComposureRenderTarget"));
			}
			else
			{
				UE_LOG(LogPixelStreamingVCam, Warning, TEXT("PixelStreaming Composure usage was requested, but the specified ComposureOutputProvider has no FinalOutputRenderTarget set"));
			}
		}
		else
		{
			TWeakPtr<FSceneViewport> SceneViewport = WeakThisUObjectPtr->GetTargetSceneViewport();
			if (TSharedPtr<FSceneViewport> PinnedSceneViewport = SceneViewport.Pin())
			{
				MediaCapture->CaptureSceneViewport(PinnedSceneViewport, Options);
				UE_LOG(LogPixelStreamingVCam, Log, TEXT("PixelStreaming set to capture scene viewport."));
			}
		}
	}

	void FVCamPixelStreamingSessionLogic::SetupCustomInputHandling(UVCamPixelStreamingSession* This)
	{
		if (This->GetUMGWidget())
		{
			TSharedPtr<SVirtualWindow> InputWindow;
			// If we are rendering from a ComposureOutputProvider, we need to get the InputWindow from that UMG, not the one in the PixelStreamingOutputProvider
			if (UVCamOutputComposure* ComposureProvider = Cast<UVCamOutputComposure>(This->GetOtherOutputProviderByIndex(This->FromComposureOutputProviderIndex)))
			{
				if (UVPFullScreenUserWidget* ComposureUMGWidget = ComposureProvider->GetUMGWidget())
				{
					const EVPWidgetDisplayType WidgetDisplayType = ComposureUMGWidget->GetDisplayType(This->GetWorld());
					if (ensure(UVPFullScreenUserWidget::DoesDisplayTypeUsePostProcessSettings(WidgetDisplayType)))
					{
						InputWindow = ComposureUMGWidget->GetPostProcessDisplayTypeSettingsFor(WidgetDisplayType)->GetSlateWindow();
						UE_LOG(LogPixelStreamingVCam, Log, TEXT("InputChannel callback - Routing input to active viewport with Composure UMG"));
					}
				}
				else
				{
					UE_LOG(LogPixelStreamingVCam, Warning, TEXT("InputChannel callback - Composure usage was requested, but the specified ComposureOutputProvider has no UMG set"));
				}
			}
			else
			{
				checkf(UVPFullScreenUserWidget::DoesDisplayTypeUsePostProcessSettings(EVPWidgetDisplayType::PostProcessSceneViewExtension), TEXT("DisplayType not set up correctly in constructor!"));
				InputWindow = This->GetUMGWidget()->GetPostProcessDisplayTypeSettingsFor(EVPWidgetDisplayType::PostProcessSceneViewExtension)->GetSlateWindow();
				UE_LOG(LogPixelStreamingVCam, Log, TEXT("InputChannel callback - Routing input to active viewport with UMG"));
			}

			MediaOutput->GetStreamer()->SetTargetWindow(InputWindow);
			MediaOutput->GetStreamer()->SetInputHandlerType(EPixelStreamingInputType::RouteToWidget);
		}
		else
		{
			MediaOutput->GetStreamer()->SetTargetWindow(This->GetTargetInputWindow());
			MediaOutput->GetStreamer()->SetInputHandlerType(EPixelStreamingInputType::RouteToWidget);
			UE_LOG(LogPixelStreamingVCam, Log, TEXT("InputChannel callback - Routing input to active viewport"));
		}

		if (MediaOutput)
		{
			typedef EPixelStreamingMessageTypes EType;
			
			/*
			 * ====================
			 * ARKit Transform
			 * ====================
			 */
			const FPixelStreamingInputMessage ARKitMessage = FPixelStreamingInputMessage(100,
				{
					// 4x4 Transform
					EType::Float, EType::Float, EType::Float, EType::Float,
					EType::Float, EType::Float, EType::Float, EType::Float,
					EType::Float, EType::Float, EType::Float, EType::Float,
					EType::Float, EType::Float, EType::Float, EType::Float,
					// Timestamp
					EType::Double
				});
			const TWeakObjectPtr<UVCamPixelStreamingSession> WeakThisUObjectPtr = This;
			const IPixelStreamingInputHandler::MessageHandlerFn ARKitHandler = [this, WeakThisUObjectPtr](FString PlayerId, FMemoryReader Ar)
			{
				NumARKitEvents++;

				if (!WeakThisUObjectPtr.IsValid() || !WeakThisUObjectPtr->EnableARKitTracking)
				{
					return;
				}

				// The buffer contains the transform matrix stored as 16 floats
				FMatrix ARKitMatrix;
				for (int32 Row = 0; Row < 4; ++Row)
				{
					float Col0, Col1, Col2, Col3;
					Ar << Col0 << Col1 << Col2 << Col3;
					ARKitMatrix.M[Row][0] = Col0;
					ARKitMatrix.M[Row][1] = Col1;
					ARKitMatrix.M[Row][2] = Col2;
					ARKitMatrix.M[Row][3] = Col3;
				}
				ARKitMatrix.DiagnosticCheckNaN();

				// Extract timestamp
				double Timestamp;
				Ar << Timestamp;

				UVCamPixelStreamingSubsystem::Get()->PushTransformForSubject(*WeakThisUObjectPtr.Get(), FTransform(ARKitMatrix), Timestamp);
			};

			/*
			 * ====================
			 * String Prompt
			 * ====================
			 */
			FPixelStreamingInputMessage StringPromptMessage = FPixelStreamingInputMessage(101,
				{ // Request ID
					EType::Int16,
					// Cancelled (bool)
					EType::Uint8,
					// User-provided string
					EType::String
				});

			const IPixelStreamingInputHandler::MessageHandlerFn StringPromptHandler = [this, WeakThisUObjectPtr](FString PlayerId, FMemoryReader Ar)
			{
				if (!WeakThisUObjectPtr.IsValid())
				{
					return;
				}

				int16 RequestId;
				Ar << RequestId;

				uint8 CancelledUint;
				Ar << CancelledUint;

				uint16 EntryLength;
				Ar << EntryLength;

				FString Entry;
				Entry.GetCharArray().SetNumUninitialized(EntryLength / 2 + 1); // wchar uses 2 bytes per char (plus null terminator)
				Ar.Serialize(Entry.GetCharArray().GetData(), EntryLength);

				if (TPromise<FVCamStringPromptResponse>* ResponsePromise = StringPromptPromises.Find(RequestId))
				{
					FVCamStringPromptResponse Response;
					Response.Result = CancelledUint == 0 ? EVCamStringPromptResult::Submitted : EVCamStringPromptResult::Cancelled;
					Response.Entry = Entry;

					ResponsePromise->EmplaceValue(Response);

					StringPromptPromises.Remove(RequestId);
				}
			};

			// Register custom message protocols + handlers
			FPixelStreamingInputProtocol::ToStreamerProtocol.Add("ARKitTransform", ARKitMessage);
			FPixelStreamingInputProtocol::ToStreamerProtocol.Add("VCamStringPromptResponse", StringPromptMessage);
			
			if (TSharedPtr<IPixelStreamingInputHandler> InputHandler = MediaOutput->GetStreamer()->GetInputHandler().Pin())
			{
				InputHandler->RegisterMessageHandler("ARKitTransform", ARKitHandler);
				InputHandler->RegisterMessageHandler("VCamStringPromptResponse", StringPromptHandler);
			}
		}
		else
		{
			UE_LOG(LogPixelStreamingVCam, Error, TEXT("Failed to setup custom input handling."));
		}
	}

	void FVCamPixelStreamingSessionLogic::OnCaptureStateChanged(TWeakObjectPtr<UVCamPixelStreamingSession> WeakThisUObjectPtr)
	{
		if (!MediaCapture)
		{
			return;
		}

		switch (MediaCapture->GetState())
		{
		case EMediaCaptureState::Capturing:
			UE_LOG(LogPixelStreamingVCam, Log, TEXT("Starting media capture for Pixel Streaming VCam."));
			break;
		case EMediaCaptureState::Stopped:
			if (MediaCapture->WasViewportResized())
			{
				UE_LOG(LogPixelStreamingVCam, Log, TEXT("Pixel Streaming VCam capture was stopped due to resize, going to restart capture."));
				// If it was stopped and viewport resized we assume resize caused the stop, so try a restart of capture here.
				SetupCapture(WeakThisUObjectPtr);
			}
			else
			{
				UE_LOG(LogPixelStreamingVCam, Log, TEXT("Stopping media capture for Pixel Streaming VCam."));
			}
			break;
		case EMediaCaptureState::Error:
			UE_LOG(LogPixelStreamingVCam, Log, TEXT("Pixel Streaming VCam capture hit an error, capturing will stop."));
			break;
		default:
			break;
		}
	}

	void FVCamPixelStreamingSessionLogic::OnRemoteResolutionChanged(const FIntPoint& RemoteResolution, TWeakObjectPtr<UVCamPixelStreamingSession> WeakThisUObjectPtr)
	{
		// Early out if match remote resolution is not enabled.
		if (!ensure(WeakThisUObjectPtr.IsValid()) || !WeakThisUObjectPtr->bMatchRemoteResolution)
		{
			return;
		}

		// No need to apply override resolution if resolutions are the same (i.e. there was no actual resolution change).
		if(WeakThisUObjectPtr->OverrideResolution == RemoteResolution)
		{
			return;
		}

		// Ensure override resolution is being used
		if (!WeakThisUObjectPtr->bUseOverrideResolution)
		{
			WeakThisUObjectPtr->bUseOverrideResolution = true;
		}

		// Set the override resolution on the output provider base, this will trigger a resize
		WeakThisUObjectPtr->OverrideResolution = RemoteResolution;
		WeakThisUObjectPtr->RequestResolutionRefresh();
	}

	void FVCamPixelStreamingSessionLogic::SetupARKitResponseTimer(TWeakObjectPtr<UVCamPixelStreamingSession> WeakThisUObjectPtr)
	{
		if (GWorld && !GWorld->GetTimerManager().IsTimerActive(ARKitResponseTimer))
		{
			const auto SendARKitResponseFunction = [this, WeakThisUObjectPtr]() {
				if(!MediaOutput || !WeakThisUObjectPtr.IsValid())
				{
					return;
				}

				MediaOutput->GetStreamer()->SendPlayerMessage(FPixelStreamingInputProtocol::FromStreamerProtocol.Find("Response")->GetID(), FString::FromInt((int)NumARKitEvents));

				FName GraphName = FName(*(FString(TEXT("NTransformsSentSec_")) + WeakThisUObjectPtr->GetFName().ToString()));
				IPixelStreamingStats::Get().GraphValue(GraphName, NumARKitEvents, 60, 0, 300);
				NumARKitEvents = 0;
			};

			GWorld->GetTimerManager().SetTimer(ARKitResponseTimer, SendARKitResponseFunction, 1.0f, true);
		}
	}

	void FVCamPixelStreamingSessionLogic::StopARKitResponseTimer()
	{
		if (GWorld)
		{
			GWorld->GetTimerManager().ClearTimer(ARKitResponseTimer);
		}
	}

	void FVCamPixelStreamingSessionLogic::OnAllConnectionsClosed(FString StreamerId)
	{
		for (TPair<int32, TPromise<FVCamStringPromptResponse>>& PromisePair : StringPromptPromises)
		{
			PromisePair.Value.EmplaceValue(FVCamStringPromptResponse(EVCamStringPromptResult::Disconnected));
		}

		StringPromptPromises.Empty();
	}

	void FVCamPixelStreamingSessionLogic::UnregisterPixelStreamingDelegates()
	{
		if (UPixelStreamingDelegates* Delegates = UPixelStreamingDelegates::GetPixelStreamingDelegates())
		{
			Delegates->OnAllConnectionsClosedNative.RemoveAll(this);
		}
	}

	void FVCamPixelStreamingSessionLogic::CleanupMediaOutput()
	{
		if (MediaOutput)
		{
			// BeginDestroy will
			// 1. call UPixelStreamingMediaOutput::StopStreaming, and
			// 2. set its Streamer to nullptr - so get it beforehand
			const TSharedPtr<IPixelStreamingStreamer> Streamer = MediaOutput->GetStreamer();
			MediaOutput->ConditionalBeginDestroy();
			MediaOutput->OnRemoteResolutionChanged().RemoveAll(this);

			// We should clean this up because of good RAII practices, however, there is one more reason:
			// Our MediaOutput is usually the only to have registered Streamer, so it should be nullptr by now.
			// However, Streamer is a shared system resource, and technically some other system may be referencing it, e.g. because they called
			// IPixelStreamingModule::CreateStreamer with the same streamer ID as us. 
			if (Streamer)
			{
				Streamer->OnPreConnection().RemoveAll(this);
				Streamer->OnStreamingStarted().RemoveAll(this);
				Streamer->OnStreamingStopped().RemoveAll(this);
			}

			MediaOutput = nullptr;
		}
	}
}

#undef LOCTEXT_NAMESPACE