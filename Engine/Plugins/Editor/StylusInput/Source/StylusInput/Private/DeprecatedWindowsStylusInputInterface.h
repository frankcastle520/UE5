// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <HAL/Platform.h>

#if PLATFORM_WINDOWS

#include "IStylusInputModule.h"
#include "StylusInput.h"
#include "StylusInputPacket.h"

class SWindow;

/**
 * An implementation of the deprecated @see UStylusInputSubsystem interface by means of using the new interface and Windows implementation.
 */
class  FDeprecatedWindowsStylusInputInterface
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	: public IStylusInputInterfaceInternal
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	, public TSharedFromThis<FDeprecatedWindowsStylusInputInterface>
{
public:
	FDeprecatedWindowsStylusInputInterface();
	virtual ~FDeprecatedWindowsStylusInputInterface();

	virtual void Tick() override;
	virtual int32 NumInputDevices() const override;
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	virtual IStylusInputDevice* GetInputDevice(int32 Index) const override;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

private:
	void CreateStylusInputInstance(SWindow* Window);
	void RemoveStylusInputInstance(const TSharedRef<SWindow>& Window);

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	struct FDeprecatedStylusInputDevice : IStylusInputDevice
	{
		explicit FDeprecatedStylusInputDevice(uint32 TabletContextId, const TSharedPtr<UE::StylusInput::IStylusInputTabletContext>& TabletContext);

		void SetDirty() { Dirty = true; }
		virtual void Tick() override;

		uint32 TabletContextId;

		UE::StylusInput::FStylusInputPacket LastPacket;
	};
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	class FStylusInputEventHandler : public UE::StylusInput::IStylusInputEventHandler
	{
	public:
		explicit FStylusInputEventHandler(TArray<FDeprecatedStylusInputDevice>& TabletContexts);
		virtual ~FStylusInputEventHandler() override = default;
		virtual FString GetName() override { return "DeprecatedWindowsStylusInputInterfaceEventHandler"; }
		virtual void OnPacket(const UE::StylusInput::FStylusInputPacket& Packet, UE::StylusInput::IStylusInputInstance* Instance) override;
		virtual void OnDebugEvent(const FString& Message, UE::StylusInput::IStylusInputInstance* Instance) override;

	private:
		FDeprecatedStylusInputDevice* GetTabletContext(uint32 TabletContextId, UE::StylusInput::IStylusInputInstance* Instance);

		TArray<FDeprecatedStylusInputDevice>& TabletContexts;
	};

	class FStylusInputInstanceWrapper
	{
	public:
		explicit FStylusInputInstanceWrapper(SWindow* Window, TArray<FDeprecatedStylusInputDevice>& TabletContexts);
		~FStylusInputInstanceWrapper();

		UE::StylusInput::IStylusInputInstance* Instance;
		FStylusInputEventHandler EventHandler;
	};

	TMap<SWindow*, TUniquePtr<FStylusInputInstanceWrapper>> StylusInputInstances;
	TUniquePtr<TArray<FDeprecatedStylusInputDevice>> TabletContexts;
};

#endif
