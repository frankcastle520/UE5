// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IInputDeviceModule.h"

class URazerChromaAnimationAsset;

/**
 * Input Device module that will create the Razer Chroma input device module.
 */
class RAZERCHROMADEVICES_API FRazerChromaDeviceModule : public IInputDeviceModule
{
public:
	static FRazerChromaDeviceModule* Get();
	
	static FName GetModularFeatureName();

	/**
	* Returns a string representing the given Razer Error code
	* 
	* @see RzErrors.h
	*/
	static const FString RazerErrorToString(const int64 ErrorCode);

protected:
	
	//~ Begin IInputDeviceModule interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual TSharedPtr<IInputDevice> CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override;		
	//~ End IInputDeviceModule interface
	
#if RAZER_CHROMA_SUPPORT

	/**
     * Returns true if the Chroma DLL has been successfully loaded
     */
    bool IsChromaAvailable() const;

	/**
	* Cleans up the SDK and all currently playing animations.
	*/
	void CleanupSDK();

	/** Handle to the Razer Chroma dynamic DLL */
	void* RazerChromaEditorDLLHandle = nullptr;

	/** True if the dynamic API was successfully loaded from the DLL handle. */
	bool bLoadedDynamicAPISuccessfully = false;

	/**
	* A map of animation names (URazerChromaAnimationAsset::AnimationName) to their Animation ID
	* loaded in from Razer Chroma.
	*/
	TMap<FString, int32> LoadedAnimationIdMap;

public:

	/**
	* This will call the Unit and Init functions over again.
	* This can be useful if you need to completely reset the state of your razer devices
	* as if the application has been closed and re-opened again
	*/
	void ForceReinitalize();

	/**
	* Returns true if the Razer Chroma runtime is available (the DLL has been successfully loaded and all of the functions we request have been found)
	*/
	static bool IsChromaRuntimeAvailable();

	/**
	* Attempts to load the given animation property.
	*
	* Returns the int ID of the animation. -1 is invalid and means it failed to load.
	*/
	const int32 FindOrLoadAnimationData(const URazerChromaAnimationAsset* AnimAsset);

	/**
	* Attempts to load the given animation property.
	*
	* Returns the int ID of the animation. -1 is invalid and means it failed to load.
	*/
	const int32 FindOrLoadAnimationData(const FString& AnimName, const uint8* AnimByteBuffer);

#endif // #if RAZER_CHROMA_SUPPORT

};