// Copyright Epic Games, Inc. All Rights Reserved.

#include "SignalingServerLifecycle.h"

#include "BuiltinProviders/VCamPixelStreamingSession.h"
#include "IPixelStreamingEditorModule.h"
#include "VCamPixelStreamingSubsystem.h"

#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

namespace UE::PixelStreamingVCam
{
	namespace Private
	{
		static bool IsLastSessionOrThereAreNoSessions(const UVCamPixelStreamingSubsystem& Subsystem, UVCamPixelStreamingSession* Session)
		{
			const TArray<TWeakObjectPtr<UVCamPixelStreamingSession>>& Sessions = Subsystem.GetRegisteredSessions();
			return Sessions.IsEmpty() || (Sessions.Num() == 1 && Sessions.Contains(Session));
		}
	}
	
	FSignalingServerLifecycle::FSignalingServerLifecycle(UVCamPixelStreamingSubsystem& Subsystem)
		: Subsystem(Subsystem)
	{}

	void FSignalingServerLifecycle::LaunchSignallingServerIfNeeded(UVCamPixelStreamingSession& Session)
	{
		const bool bIsFirstSession = Subsystem.GetRegisteredSessions().Num() == 1 && Subsystem.GetRegisteredSessions().Contains(&Session);
		if (!bIsFirstSession
			|| LifecycleState != ELifecycleState::NoClients)
		{
			return;
		}
		
		IPixelStreamingEditorModule& Module = IPixelStreamingEditorModule::Get();
		const bool bIsServerRunning = Module.GetSignallingServer().IsValid();
		if (bIsServerRunning || Module.UseExternalSignallingServer())
		{
			LifecycleState = ELifecycleState::KeepAliveOnLastSession;
		}
		else
		{
			LifecycleState = ELifecycleState::ShutdownOnLastSession;
			Module.StartSignalling();
		}
	}

	void FSignalingServerLifecycle::StopSignallingServerIfNeeded(UVCamPixelStreamingSession& Session)
	{
		if (!Private::IsLastSessionOrThereAreNoSessions(Subsystem, &Session)
			|| LifecycleState == ELifecycleState::NoClients)
		{
			return;
		}
		
		switch(LifecycleState)
		{
		case ELifecycleState::NoClients:
			break;
		case ELifecycleState::ShutdownOnLastSession:
			if (TSharedPtr<PixelStreamingServers::IServer> Server = IPixelStreamingEditorModule::Get().GetSignallingServer())
			{
				Server->GetNumStreamers([WeakSubsystemPtr = TWeakObjectPtr(&Subsystem), SessionPtr = &Session](int32 NumStreamers)
				{
					// GetNumStreamers is an async call. Validate that ...
					
					// ... there's no external systems streaming (e.g. user could have used toolbar to stream).
					if (NumStreamers == 1
						// ... there's no still no VCam sessions
						&& (!WeakSubsystemPtr.IsValid() || Private::IsLastSessionOrThereAreNoSessions(*WeakSubsystemPtr.Get(), SessionPtr)))
					{
						IPixelStreamingEditorModule::Get().StopSignalling();
					}
				});
			}
			break;
		case ELifecycleState::KeepAliveOnLastSession:
			
			break;
		default: checkNoEntry();
		}

		LifecycleState = ELifecycleState::NoClients;
	}
}
