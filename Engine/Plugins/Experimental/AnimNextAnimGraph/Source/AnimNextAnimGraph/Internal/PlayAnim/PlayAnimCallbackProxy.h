// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "PlayAnim/PlayAnimRequest.h"

#include "PlayAnimCallbackProxy.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlayAnimPlayDelegate);

UCLASS(MinimalAPI)
class UPlayAnimCallbackProxy : public UObject
{
	GENERATED_UCLASS_BODY()

	// Called when the provided animation object finished playing and hasn't been interrupted
	UPROPERTY(BlueprintAssignable)
	FOnPlayAnimPlayDelegate OnCompleted;

	// Called when the provided animation object starts blending out and hasn't been interrupted
	UPROPERTY(BlueprintAssignable)
	FOnPlayAnimPlayDelegate OnBlendOut;

	// Called when the provided animation object has been interrupted (or failed to play)
	UPROPERTY(BlueprintAssignable)
	FOnPlayAnimPlayDelegate OnInterrupted;

	// Called to perform the query internally
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true"))
	static ANIMNEXTANIMGRAPH_API UPlayAnimCallbackProxy* CreateProxyObjectForPlayAnim(
		class UAnimNextComponent* AnimNextComponent,
		FName SlotName,
		class UAnimSequence* AnimationObject,
		float PlayRate = 1.0f,
		float StartPosition = 0.0f,
		FAnimNextPlayAnimBlendSettings BlendInSettings = FAnimNextPlayAnimBlendSettings(),
		FAnimNextPlayAnimBlendSettings BlendOutSettings = FAnimNextPlayAnimBlendSettings());

	// Called to perform the query internally
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true"))
	static ANIMNEXTANIMGRAPH_API UPlayAnimCallbackProxy* CreateProxyObjectForPlayAsset(
		class UAnimNextComponent* AnimNextComponent,
		FName SlotName,
		UObject* Asset,
		const FInstancedStruct& Payload,
		FAnimNextPlayAnimBlendSettings BlendInSettings = FAnimNextPlayAnimBlendSettings(),
		FAnimNextPlayAnimBlendSettings BlendOutSettings = FAnimNextPlayAnimBlendSettings());
	
public:
	//~ Begin UObject Interface
	ANIMNEXTANIMGRAPH_API virtual void BeginDestroy() override;
	//~ End UObject Interface

protected:
	ANIMNEXTANIMGRAPH_API void OnPlayAnimCompleted(const UE::AnimNext::FPlayAnimRequest& Request);
	ANIMNEXTANIMGRAPH_API void OnPlayAnimInterrupted(const UE::AnimNext::FPlayAnimRequest& Request);
	ANIMNEXTANIMGRAPH_API void OnPlayAnimBlendingOut(const UE::AnimNext::FPlayAnimRequest& Request);

	// Attempts to play an object with the specified payload. Returns whether it started or not.
	ANIMNEXTANIMGRAPH_API bool Play(
		class UAnimNextComponent* AnimNextComponent,
		FName SlotName,
		UObject* Object,
		FInstancedStruct&& Payload,
		const UE::AnimNext::FPlayAnimBlendSettings& BlendInSettings,
		const UE::AnimNext::FPlayAnimBlendSettings& BlendOutSettings);
	
	// Attempts to play an animation with the specified settings. Returns whether it started or not.
	ANIMNEXTANIMGRAPH_API bool Play(
		class UAnimNextComponent* AnimNextComponent,
		FName SlotName,
		class UAnimSequence* AnimationObject,
		float PlayRate,
		float StartPosition,
		const UE::AnimNext::FPlayAnimBlendSettings& BlendInSettings,
		const UE::AnimNext::FPlayAnimBlendSettings& BlendOutSettings);

private:
	void Reset();

	UE::AnimNext::FPlayAnimRequestPtr PlayingRequest;
	bool bWasInterrupted = false;
};
