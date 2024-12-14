// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IStylusState.h"

#include "EditorSubsystem.h"
#include "TickableEditorObject.h"
#include "Modules/ModuleManager.h"

#include "IStylusInputModule.generated.h"

class FSpawnTabArgs;
class IStylusInputDevice;
class IStylusMessageHandler;

DEFINE_LOG_CATEGORY_STATIC(LogStylusInput, Log, All);

/**
 * Module to handle Wacom-style tablet input using styluses.
 */
class UE_DEPRECATED(5.5, "Please use the new API in StylusInput.h instead.") STYLUSINPUT_API IStylusInputModule : public IModuleInterface
{
public:

	/**
	 * Retrieve the module instance.
	 */
	static inline IStylusInputModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IStylusInputModule>("StylusInput");
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("StylusInput");
	}
};

// This is the interface that all platform-specific implementations must implement.
class UE_DEPRECATED(5.5, "Please use the new API in StylusInput.h instead.") IStylusInputInterfaceInternal
{
public:
	virtual void Tick() = 0;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	virtual IStylusInputDevice* GetInputDevice(int32 Index) const = 0;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	virtual int32 NumInputDevices() const = 0;
};


UCLASS()
class UE_DEPRECATED(5.5, "Please use the new API's window-specific IStylusInputInstance in StylusInput.h instead. If you do need to use this subsystem, please also set the CVar 'stylusinput.EnableLegacySubsystem' to true, otherwise tablet input will not automatically be set up for each window.")
STYLUSINPUT_API UStylusInputSubsystem : 
	public UEditorSubsystem, 
	public FTickableEditorObject
{
	GENERATED_BODY()
public:
	// UEditorSubsystem implementation
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Retrieve the input device that is at the given index, or nullptr if not found. Corresponds to the StylusIndex in IStylusMessageHandler. */
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const IStylusInputDevice* GetInputDevice(int32 Index) const;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	/** Return the number of active input devices. */
	int32 NumInputDevices() const; 

	/** Add a message handler to receive messages from the stylus. */
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	void AddMessageHandler(IStylusMessageHandler& MessageHandler);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	/** Remove a previously registered message handler. */
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	void RemoveMessageHandler(IStylusMessageHandler& MessageHandler);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	// FTickableEditorObject implementation
	virtual bool IsTickable() const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UStylusInputSubsystem, STATGROUP_Tickables); }

private:
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	TSharedPtr<IStylusInputInterfaceInternal> InputInterface;
	TArray<IStylusMessageHandler*> MessageHandlers;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
};
