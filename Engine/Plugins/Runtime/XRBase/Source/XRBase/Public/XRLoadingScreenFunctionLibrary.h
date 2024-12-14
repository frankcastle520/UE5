// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "IXRLoadingScreen.h"
#include "XRLoadingScreenFunctionLibrary.generated.h"



/**
 * XR Loading Screen Function Library 
 */
UCLASS()
class XRBASE_API UXRLoadingScreenFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

	/**
	 * Sets the loading screen texture for VR displays.
	 *
	 * @param Texture			(in) A texture asset to be used for the loading screen.
	 * @param Scale				(in) Scale of the loading screen texture quad.
	 * @param Offset			(in) Offset of the loading screen texture quad relative to the tracking space origin, in meters.
	 * @param bShowLoadingMovie	(in) If true, support animated texture assets, such as media textures.
	 * @param bShowOnSet		(in) If true, immediately show the loading screen after it's set.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input|HeadMountedDisplay|LoadingScreen")
	static void SetLoadingScreen(class UTexture* Texture, FVector2D Scale = FVector2D(1.0f, 1.0f), FVector Offset = FVector::ZeroVector, bool bShowLoadingMovie = false, bool bShowOnSet = false);
	
	UFUNCTION(BlueprintCallable, Category = "Input|HeadMountedDisplay|LoadingScreen")
	static void ClearLoadingScreenSplashes();

	/**
     * Adds a splash element to the loading screen.
     *
	 * @param Texture			(in) A texture asset to be used for the splash.
	 * @param Translation       (in) Initial translation of the center of the splash.
	 * @param Rotation			(in) Initial rotation of the splash screen, with the origin at the center of the splash.
	 * @param Size      		(in) Size, of the quad with the splash screen.
	 * @param DeltaRotation		(in) Incremental rotation, that is added each 2nd frame to the quad transform. The quad is rotated around the center of the quad.
	 * @param bClearBeforeAdd	(in) If true, clears splashes before adding a new one.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input|HeadMountedDisplay|LoadingScreen")
	static void AddLoadingScreenSplash(class UTexture* Texture, FVector Translation, FRotator Rotation, FVector2D Size = FVector2D(1.0f, 1.0f), FRotator DeltaRotation = FRotator::ZeroRotator, bool bClearBeforeAdd = false);
	
	/**
	 * Show the loading screen and override the VR display
	 */	
	UFUNCTION(BlueprintCallable, Category = "Input|HeadMountedDisplay|LoadingScreen")
	static void ShowLoadingScreen();

	/**
	 * Hide the splash screen and return to normal display.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input|HeadMountedDisplay|LoadingScreen")
	static void HideLoadingScreen();
};