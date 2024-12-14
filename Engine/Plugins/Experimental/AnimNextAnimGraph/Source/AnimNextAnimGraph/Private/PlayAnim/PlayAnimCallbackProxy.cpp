// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayAnim/PlayAnimCallbackProxy.h"
#include "Animation/AnimSequence.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PlayAnimCallbackProxy)

//////////////////////////////////////////////////////////////////////////
// UPlayAnimCallbackProxy

UPlayAnimCallbackProxy::UPlayAnimCallbackProxy(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UPlayAnimCallbackProxy* UPlayAnimCallbackProxy::CreateProxyObjectForPlayAnim(
	UAnimNextComponent* AnimNextComponent,
	FName SlotName,
	UAnimSequence* AnimationObject,
	float PlayRate,
	float StartPosition,
	UE::AnimNext::FPlayAnimBlendSettings BlendInSettings,
	UE::AnimNext::FPlayAnimBlendSettings BlendOutSettings)
{
	UPlayAnimCallbackProxy* Proxy = NewObject<UPlayAnimCallbackProxy>();
	Proxy->SetFlags(RF_StrongRefOnFrame);
	Proxy->Play(AnimNextComponent, SlotName, AnimationObject, PlayRate, StartPosition, BlendInSettings, BlendOutSettings);
	return Proxy;
}

UPlayAnimCallbackProxy* UPlayAnimCallbackProxy::CreateProxyObjectForPlayAsset(
	UAnimNextComponent* AnimNextComponent,
	FName SlotName,
	UObject* Asset,
	const FInstancedStruct& Payload,
	UE::AnimNext::FPlayAnimBlendSettings BlendInSettings,
	UE::AnimNext::FPlayAnimBlendSettings BlendOutSettings)
{
	UPlayAnimCallbackProxy* Proxy = NewObject<UPlayAnimCallbackProxy>();
	Proxy->SetFlags(RF_StrongRefOnFrame);
	FInstancedStruct PayloadCopy(Payload);
	Proxy->Play(AnimNextComponent, SlotName, Asset, MoveTemp(PayloadCopy), BlendInSettings, BlendOutSettings);
	return Proxy;
}

bool UPlayAnimCallbackProxy::Play(
	UAnimNextComponent* AnimNextComponent,
	FName SlotName,
	UAnimSequence* AnimationObject,
	float PlayRate,
	float StartPosition,
	const UE::AnimNext::FPlayAnimBlendSettings& BlendInSettings,
	const UE::AnimNext::FPlayAnimBlendSettings& BlendOutSettings)
{
	FInstancedStruct Payload;
	Payload.InitializeAs<FAnimNextPlayAnimPayload>();
	FAnimNextPlayAnimPayload& PlayAnimPayload = Payload.GetMutable<FAnimNextPlayAnimPayload>();
	PlayAnimPayload.AnimationObject = AnimationObject;
	PlayAnimPayload.PlayRate = PlayRate;
	PlayAnimPayload.StartPosition = StartPosition;
	return Play(AnimNextComponent, SlotName, AnimationObject, MoveTemp(Payload), BlendInSettings, BlendOutSettings);
}

bool UPlayAnimCallbackProxy::Play(
	UAnimNextComponent* AnimNextComponent,
	FName SlotName,
	UObject* Object,
	FInstancedStruct&& Payload,
	const UE::AnimNext::FPlayAnimBlendSettings& BlendInSettings,
	const UE::AnimNext::FPlayAnimBlendSettings& BlendOutSettings)
{
	bool bPlayedSuccessfully = false;
	if (AnimNextComponent != nullptr)
	{
		UE::AnimNext::FPlayAnimRequestArgs RequestArgs;
		RequestArgs.SlotName = SlotName;
		RequestArgs.Object = Object;
		RequestArgs.BlendInSettings = BlendInSettings;
		RequestArgs.BlendOutSettings = BlendOutSettings;
		RequestArgs.Payload = MoveTemp(Payload);

		auto Request = UE::AnimNext::MakePlayAnimRequest();
		Request->OnCompleted.BindUObject(this, &UPlayAnimCallbackProxy::OnPlayAnimCompleted);
		Request->OnInterrupted.BindUObject(this, &UPlayAnimCallbackProxy::OnPlayAnimInterrupted);
		Request->OnBlendingOut.BindUObject(this, &UPlayAnimCallbackProxy::OnPlayAnimBlendingOut);

		bPlayedSuccessfully = Request->Play(MoveTemp(RequestArgs), AnimNextComponent);

		PlayingRequest = Request;
		bWasInterrupted = false;
	}

	if (!bPlayedSuccessfully)
	{
		OnInterrupted.Broadcast();
		Reset();
	}

	return bPlayedSuccessfully;
}

void UPlayAnimCallbackProxy::OnPlayAnimCompleted(const UE::AnimNext::FPlayAnimRequest& Request)
{
	if (!bWasInterrupted)
	{
		const UE::AnimNext::EPlayAnimStatus Status = Request.GetStatus();
		check(!EnumHasAnyFlags(Status, UE::AnimNext::EPlayAnimStatus::Interrupted));

		if (EnumHasAnyFlags(Status, UE::AnimNext::EPlayAnimStatus::Expired))
		{
			OnInterrupted.Broadcast();
		}
		else
		{
			OnCompleted.Broadcast();
		}
	}

	Reset();
}

void UPlayAnimCallbackProxy::OnPlayAnimInterrupted(const UE::AnimNext::FPlayAnimRequest& Request)
{
	bWasInterrupted = true;

	OnInterrupted.Broadcast();
}

void UPlayAnimCallbackProxy::OnPlayAnimBlendingOut(const UE::AnimNext::FPlayAnimRequest& Request)
{
	if (!bWasInterrupted)
	{
		OnBlendOut.Broadcast();
	}
}

void UPlayAnimCallbackProxy::Reset()
{
	PlayingRequest = nullptr;
	bWasInterrupted = false;
}

void UPlayAnimCallbackProxy::BeginDestroy()
{
	Reset();

	Super::BeginDestroy();
}
