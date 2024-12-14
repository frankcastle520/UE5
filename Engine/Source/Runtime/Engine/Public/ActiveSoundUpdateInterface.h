// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"
#include "ActiveSoundUpdateInterface.generated.h"

// Forward Declarations 
struct FActiveSound;
struct FSoundParseParameters;

/** Interface for modifying active sounds during their update */
UINTERFACE(meta = (CannotImplementInterfaceInBlueprint))
class UActiveSoundUpdateInterface : public UInterface
{
	GENERATED_BODY()
};

class IActiveSoundUpdateInterface
{
	GENERATED_BODY()

public:

	/**
	 * Gathers interior data that can affect the active sound.  Non-const as this step can be used to track state about the sound on the implementing object
	 * NOTE! Called on the AudioThread
	 *
	 * @param ActiveSound	The active sound affected
	 * @param ParseParams	The parameters to apply to the wave instances
	 */
	virtual void GatherInteriorData(const FActiveSound& ActiveSound, FSoundParseParameters& ParseParams) {}

	/**
	 * Applies interior data previously collected to the active sound and parse parameters.
	 * NOTE! Called on the AudioThread
	 *
	 * @param ActiveSound	The active sound affected
	 * @param ParseParams	The parameters to apply to the wave instances
	 */
	virtual void ApplyInteriorSettings(const FActiveSound& ActiveSound, FSoundParseParameters& ParseParams) {}

	/**
	 * Called when an active sound has just been added to the audio engine,
	 * both for brand new sounds and for virtualized sounds that have just become active.
	 * In the latter case, a corresponding NotifyVirtualizedSoundDeleting will be received.
	 * You can correlate the two objects by matching their GetPlayOrder() value.
	 *
	 * NOTE! Called on the AudioThread
	 *
	 * @param ActiveSound	The active sound being created
	 */
	virtual void NotifyActiveSoundCreated(FActiveSound& ActiveSound) {}

	/**
	 * Called when an active sound has just been removed from the audio engine, by being stopped or virtualized.
	 * In either case, the referenced ActiveSound object is about to be deleted; any pointers to it should be discarded.
	 * 
	 * NOTE! Called on the AudioThread
	 *
	 * @param ActiveSound	The active sound being deleted
	 */
	virtual void NotifyActiveSoundDeleting(const FActiveSound& ActiveSound) {}

	/**
	 * Called when a virtualized sound has just been added to the audio engine,
	 * both for brand new sounds and for active sounds that have just become virtualized.
	 * When virtualizing, the corresponding NotifyActiveSoundDeleting will arrive after any fade-out has finished.
	 * You can correlate the two objects by matching their GetPlayOrder() value.
	 * 
	 * NOTE! Called on the AudioThread
	 *
	 * @param ActiveSound	The virtualized sound being created
	 */
	virtual void NotifyVirtualizedSoundCreated(FActiveSound& ActiveSound) {}

	/**
	 * Called when a virtualized sound has just been removed from the audio engine, by being stopped or re-triggered.
	 * In either case, the referenced ActiveSound object is about to be deleted; any pointers to it should be discarded.
	 * 
	 * NOTE! Called on the AudioThread
	 *
	 * @param ActiveSound	The active sound being removed
	 */
	virtual void NotifyVirtualizedSoundDeleting(const FActiveSound& ActiveSound) {}


	UE_DEPRECATED(5.5, "OnNotifyAddActiveSound is deprecated. Use NotifyActiveSoundCreated instead.")
	virtual void OnNotifyAddActiveSound(FActiveSound& ActiveSound) {}

	UE_DEPRECATED(5.5, "OnNotifyPendingDelete is deprecated. Use NotifyActiveSoundDeleting and/or NotifyVirtualizedSoundDeleting instead.")
	virtual void OnNotifyPendingDelete(const FActiveSound& ActiveSound) {}
};
