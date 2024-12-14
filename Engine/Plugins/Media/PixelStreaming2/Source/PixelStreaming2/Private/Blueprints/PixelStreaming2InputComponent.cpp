// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreaming2InputComponent.h"
#include "IPixelStreaming2Module.h"
#include "PixelStreaming2Module.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "GameFramework/GameUserSettings.h"
#include "Logging.h"
#include "IPixelStreaming2Streamer.h"
#include "PixelStreaming2Utils.h"

UPixelStreaming2Input::UPixelStreaming2Input(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, PixelStreaming2Module((UE::PixelStreaming2::FPixelStreaming2Module*)UE::PixelStreaming2::FPixelStreaming2Module::GetModule())
{
	bAutoActivate = true;
	PrimaryComponentTick.bCanEverTick = true;
	SetComponentTickEnabled(true);
}

void UPixelStreaming2Input::BeginPlay()
{
	Super::BeginPlay();

	if (PixelStreaming2Module)
	{
		// When this component is initializing it registers itself with the Pixel Streaming module.
		PixelStreaming2Module->AddInputComponent(this);
	}
	else
	{
		UE_LOG(LogPixelStreaming2, Warning, TEXT("Pixel Streaming input component not added because Pixel Streaming module is not loaded. This is expected on dedicated servers."));
	}
}

void UPixelStreaming2Input::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (PixelStreaming2Module)
	{
		// When this component is destructing it unregisters itself with the Pixel Streaming module.
		PixelStreaming2Module->RemoveInputComponent(this);
	}
	else
	{
		UE_LOG(LogPixelStreaming2, Warning, TEXT("Pixel Streaming input component not removed because Pixel Streaming module is not loaded. This is expected on dedicated servers."));
	}
}

void UPixelStreaming2Input::SendPixelStreaming2Response(const FString& Descriptor)
{
	if (PixelStreaming2Module)
	{
		PixelStreaming2Module->ForEachStreamer([&Descriptor, this](TSharedPtr<IPixelStreaming2Streamer> Streamer) {
			TSharedPtr<IPixelStreaming2InputHandler> Handler = Streamer->GetInputHandler().Pin();
			{
				UE_LOG(LogPixelStreaming2, Error, TEXT("Pixel Streaming input handler was null when sending response message."));
				return;
			}
			Streamer->SendAllPlayersMessage(EPixelStreaming2FromStreamerMessage::Response, Descriptor);
		});
	}
	else
	{
		UE_LOG(LogPixelStreaming2, Warning, TEXT("Pixel Streaming input component skipped sending response. This is expected on dedicated servers."));
	}
}

void UPixelStreaming2Input::GetJsonStringValue(FString Descriptor, FString FieldName, FString& StringValue, bool& Success)
{
	UE::PixelStreaming2::ExtractJsonFromDescriptor(Descriptor, FieldName, StringValue, Success);
}

void UPixelStreaming2Input::AddJsonStringValue(const FString& Descriptor, FString FieldName, FString StringValue, FString& NewDescriptor, bool& Success)
{
	UE::PixelStreaming2::ExtendJsonWithField(Descriptor, FieldName, StringValue, NewDescriptor, Success);
}
