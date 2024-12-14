// Copyright Epic Games, Inc. All Rights Reserved.

#include "DeprecatedWindowsStylusInputInterface.h"

#if PLATFORM_WINDOWS

#include <Algo/Find.h>
#include <Framework/Application/SlateApplication.h>

#include "StylusInputTabletContext.h"

FDeprecatedWindowsStylusInputInterface::FDeprecatedWindowsStylusInputInterface()
	: TabletContexts(MakeUnique<TArray<FDeprecatedStylusInputDevice>>())
{
}

FDeprecatedWindowsStylusInputInterface::~FDeprecatedWindowsStylusInputInterface() = default;

void FDeprecatedWindowsStylusInputInterface::Tick()
{
	for (const FDeprecatedStylusInputDevice& TabletContext : *TabletContexts)
	{
		// don't change focus if any stylus is down
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if (TabletContext.GetCurrentState().IsStylusDown())
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		{
			return;
		}
	}

	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	FSlateApplication& Application = FSlateApplication::Get();

	FWidgetPath WidgetPath = Application.LocateWindowUnderMouse(Application.GetCursorPos(), Application.GetInteractiveTopLevelWindows());
	if (WidgetPath.IsValid())
	{
		const TSharedPtr<SWindow> Window = WidgetPath.GetWindow();
		if (Window.IsValid() && Window->IsRegularWindow())
		{
			TUniquePtr<FStylusInputInstanceWrapper> *const StylusInputInstance = StylusInputInstances.Find(Window.Get());
			if (StylusInputInstance == nullptr)
			{
				CreateStylusInputInstance(Window.Get());
				Window->GetOnWindowClosedEvent().AddSP(this, &FDeprecatedWindowsStylusInputInterface::RemoveStylusInputInstance);
			}
		}
	}
}

int32 FDeprecatedWindowsStylusInputInterface::NumInputDevices() const
{
	return TabletContexts->Num();
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
IStylusInputDevice* FDeprecatedWindowsStylusInputInterface::GetInputDevice(int32 Index) const
{
	return 0 <= Index && Index < TabletContexts->Num() ? &(*TabletContexts)[Index] : nullptr;
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void FDeprecatedWindowsStylusInputInterface::CreateStylusInputInstance(SWindow* Window)
{	
	StylusInputInstances.Emplace(Window, MakeUnique<FStylusInputInstanceWrapper>(Window, *TabletContexts));
}

void FDeprecatedWindowsStylusInputInterface::RemoveStylusInputInstance(const TSharedRef<SWindow>& Window)
{
	StylusInputInstances.Remove(&Window.Get());
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
FStylusState ToDeprecatedStylusState(const UE::StylusInput::FStylusInputPacket& Packet)
{
	return {
		{Packet.X, Packet.Y}, Packet.Z, {Packet.XTiltOrientation, Packet.YTiltOrientation}, Packet.TwistOrientation, Packet.NormalPressure,
		Packet.TangentPressure, {Packet.Width, Packet.Height},
		(Packet.PenStatus & UE::StylusInput::EPenStatus::CursorIsTouching) != UE::StylusInput::EPenStatus::None,
		(Packet.PenStatus & UE::StylusInput::EPenStatus::CursorIsInverted) != UE::StylusInput::EPenStatus::None
	};
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

PRAGMA_DISABLE_DEPRECATION_WARNINGS
FDeprecatedWindowsStylusInputInterface::FDeprecatedStylusInputDevice::FDeprecatedStylusInputDevice(uint32 TabletContextId, const TSharedPtr<UE::StylusInput::IStylusInputTabletContext>& TabletContext)
	: TabletContextId(TabletContextId)
{
	if (TabletContext)
	{
		UE::StylusInput::ETabletSupportedProperties SupportedProperties = TabletContext->GetSupportedProperties();

		auto IsSupported = [SupportedProperties](UE::StylusInput::ETabletSupportedProperties Property)
		{
			return (SupportedProperties & Property) != UE::StylusInput::ETabletSupportedProperties::None;
		};

		if (IsSupported(UE::StylusInput::ETabletSupportedProperties::X) && IsSupported(UE::StylusInput::ETabletSupportedProperties::Y))
		{
			SupportedInputs.Add(EStylusInputType::Position);
		}

		if (IsSupported(UE::StylusInput::ETabletSupportedProperties::Z))
		{
			SupportedInputs.Add(EStylusInputType::Z);
		}

		if (IsSupported(UE::StylusInput::ETabletSupportedProperties::NormalPressure))
		{
			SupportedInputs.Add(EStylusInputType::Pressure);
		}

		if (IsSupported(UE::StylusInput::ETabletSupportedProperties::XTiltOrientation) && IsSupported(UE::StylusInput::ETabletSupportedProperties::YTiltOrientation))
		{
			SupportedInputs.Add(EStylusInputType::Tilt);
		}

		if (IsSupported(UE::StylusInput::ETabletSupportedProperties::TangentPressure))
		{
			SupportedInputs.Add(EStylusInputType::TangentPressure);
		}

		if (IsSupported(UE::StylusInput::ETabletSupportedProperties::ButtonPressure))
		{
			SupportedInputs.Add(EStylusInputType::ButtonPressure);
		}

		if (IsSupported(UE::StylusInput::ETabletSupportedProperties::TwistOrientation))
		{
			SupportedInputs.Add(EStylusInputType::Tilt);
		}

		if (IsSupported(UE::StylusInput::ETabletSupportedProperties::Width) && IsSupported(UE::StylusInput::ETabletSupportedProperties::Height))
		{
			SupportedInputs.Add(EStylusInputType::Size);
		}
	}
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void FDeprecatedWindowsStylusInputInterface::FDeprecatedStylusInputDevice::Tick()
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	PreviousState = CurrentState;
	CurrentState = ToDeprecatedStylusState(LastPacket);
	Dirty = false;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

FDeprecatedWindowsStylusInputInterface::FStylusInputEventHandler::FStylusInputEventHandler(TArray<FDeprecatedStylusInputDevice>& TabletContexts)
	: TabletContexts(TabletContexts)
{
}

void FDeprecatedWindowsStylusInputInterface::FStylusInputEventHandler::OnPacket(const UE::StylusInput::FStylusInputPacket& Packet,
                                                                                UE::StylusInput::IStylusInputInstance* Instance)
{
	FDeprecatedStylusInputDevice* TabletContext = GetTabletContext(Packet.TabletContextID, Instance);

	TabletContext->LastPacket = Packet;
	TabletContext->SetDirty();
}

void FDeprecatedWindowsStylusInputInterface::FStylusInputEventHandler::OnDebugEvent(const FString& Message, UE::StylusInput::IStylusInputInstance* Instance)
{
}

FDeprecatedWindowsStylusInputInterface::FDeprecatedStylusInputDevice* FDeprecatedWindowsStylusInputInterface::FStylusInputEventHandler::GetTabletContext(
	uint32 TabletContextId, UE::StylusInput::IStylusInputInstance* Instance)
{
	FDeprecatedStylusInputDevice* TabletContext = Algo::FindByPredicate(
		TabletContexts, [TabletContextId](const FDeprecatedStylusInputDevice& Value)
		{
			return
				Value.TabletContextId == TabletContextId;
		});

	if (TabletContext)
	{
		return TabletContext;
	}

	TabletContext = &TabletContexts.Add_GetRef(FDeprecatedStylusInputDevice(TabletContextId, Instance->GetTabletContext(TabletContextId)));
	
	return TabletContext;
}

FDeprecatedWindowsStylusInputInterface::FStylusInputInstanceWrapper::FStylusInputInstanceWrapper(SWindow* Window,
                                                                                                 TArray<FDeprecatedStylusInputDevice>& TabletContexts)
	: Instance(UE::StylusInput::CreateInstance(*Window))
	, EventHandler(TabletContexts)
{
	if (Instance)
	{
		Instance->AddEventHandler(&EventHandler, UE::StylusInput::EEventHandlerThread::OnGameThread);
	}
}

FDeprecatedWindowsStylusInputInterface::FStylusInputInstanceWrapper::~FStylusInputInstanceWrapper()
{
	if (Instance)
	{
		Instance->RemoveEventHandler(&EventHandler);
		UE::StylusInput::ReleaseInstance(Instance);
	}
}


PRAGMA_DISABLE_DEPRECATION_WARNINGS
TSharedPtr<IStylusInputInterfaceInternal> CreateStylusInputInterface()
PRAGMA_ENABLE_DEPRECATION_WARNINGS
{
	return MakeShared<FDeprecatedWindowsStylusInputInterface>();
}

#endif
