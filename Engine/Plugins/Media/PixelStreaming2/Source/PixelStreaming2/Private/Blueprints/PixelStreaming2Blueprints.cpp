// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreaming2Blueprints.h"

#include "IPixelStreaming2Streamer.h"
#include "Logging.h"
#include "Misc/FileHelper.h"
#include "PixelStreaming2AudioComponent.h"
#include "PixelStreaming2InputComponent.h"

TSharedPtr<IPixelStreaming2Streamer> GetStreamer(FString StreamerId)
{
	IPixelStreaming2Module* Module = UE::PixelStreaming2::FPixelStreaming2Module::GetModule();
	if (!Module)
	{
		UE_LOG(LogPixelStreaming2, Error, TEXT("Could not get Pixel Streaming module, so unable to get streamer by id."));
		return nullptr;
	}
	TSharedPtr<IPixelStreaming2Streamer> Streamer = Module->FindStreamer(StreamerId);
	if (Streamer)
	{
		return Streamer;
	}
	else
	{
		UE_LOG(LogPixelStreaming2, Error, TEXT("Unable to get streamer with id %s"), *StreamerId);
		return nullptr;
	}
}

void UPixelStreaming2Blueprints::SendFileAsByteArray(TArray<uint8> ByteArray, FString MimeType, FString FileExtension)
{
	StreamerSendFileAsByteArray(GetDefaultStreamerID(), ByteArray, MimeType, FileExtension);
}

void UPixelStreaming2Blueprints::StreamerSendFileAsByteArray(FString StreamerId, TArray<uint8> ByteArray, FString MimeType, FString FileExtension)
{
	if (TSharedPtr<IPixelStreaming2Streamer> Streamer = GetStreamer(StreamerId))
	{
		const TArray64<uint8> LargeByteArray(ByteArray);
		Streamer->SendFileData(LargeByteArray, MimeType, FileExtension);
	}
}

void UPixelStreaming2Blueprints::SendFile(FString FilePath, FString MimeType, FString FileExtension)
{
	StreamerSendFile(GetDefaultStreamerID(), FilePath, MimeType, FileExtension);
}

void UPixelStreaming2Blueprints::StreamerSendFile(FString StreamerId, FString FilePath, FString MimeType, FString FileExtension)
{
	if (TSharedPtr<IPixelStreaming2Streamer> Streamer = GetStreamer(StreamerId))
	{
		TArray64<uint8> ByteData;
		bool			bSuccess = FFileHelper::LoadFileToArray(ByteData, *FilePath);
		if (bSuccess)
		{
			Streamer->SendFileData(ByteData, MimeType, FileExtension);
		}
		else
		{
			UE_LOG(LogPixelStreaming2, Error, TEXT("FileHelper failed to load file data"));
		}
	}
}

void UPixelStreaming2Blueprints::ForceKeyFrame()
{
	StreamerForceKeyFrame(GetDefaultStreamerID());
}

void UPixelStreaming2Blueprints::StreamerForceKeyFrame(FString StreamerId)
{
	if (TSharedPtr<IPixelStreaming2Streamer> Streamer = GetStreamer(StreamerId))
	{
		Streamer->ForceKeyFrame();
	}
}

void UPixelStreaming2Blueprints::FreezeFrame(UTexture2D* Texture)
{
	StreamerFreezeStream(GetDefaultStreamerID(), Texture);
}

void UPixelStreaming2Blueprints::StreamerFreezeStream(FString StreamerId, UTexture2D* Texture)
{
	if (TSharedPtr<IPixelStreaming2Streamer> Streamer = GetStreamer(StreamerId))
	{
		Streamer->FreezeStream(Texture);
	}
}

void UPixelStreaming2Blueprints::UnfreezeFrame()
{
	StreamerUnfreezeStream(GetDefaultStreamerID());
}

void UPixelStreaming2Blueprints::StreamerUnfreezeStream(FString StreamerId)
{
	if (TSharedPtr<IPixelStreaming2Streamer> Streamer = GetStreamer(StreamerId))
	{
		Streamer->UnfreezeStream();
	}
}

void UPixelStreaming2Blueprints::KickPlayer(FString PlayerId)
{
	StreamerKickPlayer(GetDefaultStreamerID(), PlayerId);
}

void UPixelStreaming2Blueprints::StreamerKickPlayer(FString StreamerId, FString PlayerId)
{
	if (TSharedPtr<IPixelStreaming2Streamer> Streamer = GetStreamer(StreamerId))
	{
		Streamer->KickPlayer(PlayerId);
	}
}

TArray<FString> UPixelStreaming2Blueprints::GetConnectedPlayers()
{
	return StreamerGetConnectedPlayers(GetDefaultStreamerID());
}

TArray<FString> UPixelStreaming2Blueprints::StreamerGetConnectedPlayers(FString StreamerId)
{
	if (TSharedPtr<IPixelStreaming2Streamer> Streamer = GetStreamer(StreamerId))
	{
		return Streamer->GetConnectedPlayers();
	}
	else
	{
		UE_LOG(LogPixelStreaming2, Error, TEXT("No streamer found with specified id - %s. Returning no connected players."), *StreamerId);
		return {};
	}
}

FString UPixelStreaming2Blueprints::GetDefaultStreamerID()
{
	IPixelStreaming2Module* Module = UE::PixelStreaming2::FPixelStreaming2Module::GetModule();
	if (!Module)
	{
		UE_LOG(LogPixelStreaming2, Error, TEXT("Pixel Streaming module not initialized yet - cannot get default streamer id."));
		return FString(TEXT("ModuleNotInitializedYet"));
	}
	return Module->GetDefaultStreamerID();
}

UPixelStreaming2Delegates* UPixelStreaming2Blueprints::GetDelegates()
{
	return UPixelStreaming2Delegates::Get();
}