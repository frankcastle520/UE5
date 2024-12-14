// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <CoreMinimal.h>
#include <Misc/Variant.h>
#include "HTTP/HTTPManager.h"
#include "Utils/MPEG/ElectraUtilsMP4.h"
#include "MediaStreamMetadata.h"

namespace Electra
{

class UtilsMP4
{
public:
	static constexpr uint32 Make4CC(const uint8 A, const uint8 B, const uint8 C, const uint8 D)
	{
		return (static_cast<uint32>(A) << 24) | (static_cast<uint32>(B) << 16) | (static_cast<uint32>(C) << 8) | static_cast<uint32>(D);
	}


	/**
	 * This class parses metadata embedded in an mp4 / ISO14496-12 file.
	 * Presently only the structure as used and defined by Apple iTunes is supported.
	 */
	class FMetadataParser
	{
	public:
		FMetadataParser();
		enum class EResult
		{
			Success,
			NotSupported,
			MissingBox
		};
		struct FBoxInfo
		{
			FBoxInfo(uint32 InType, const void* InData, uint32 InSize) : Type(InType), Data(InData), Size(InSize)
			{}
			uint32 Type = 0;
			const void* Data = nullptr;
			uint32 Size = 0;
		};
		EResult Parse(uint32 InHandler, uint32 InHandlerReserved0, const TArray<FBoxInfo>& InBoxes);
		bool IsDifferentFrom(const FMetadataParser& Other);
		FString GetAsJSON() const;
		TSharedPtr<TMap<FString, TArray<TSharedPtr<IMediaStreamMetadata::IItem, ESPMode::ThreadSafe>>>, ESPMode::ThreadSafe> GetMediaStreamMetadata() const;

		void AddItem(const FString& InType, const FString& InValue);
		void AddItem(const FString& InType, const FString& InMimeType, const TArray<uint8>& InValue);
	private:
		FString PrintableBoxAtom(const uint32 InAtom);
		void Parse(const FBoxInfo& InBox);
		void ParseBoxDataList(const FString& AsCategory, const uint8* InBoxData, uint32 InBoxSize);
		void ParseBoxDataiTunes(const uint8* InBoxData, uint32 InBoxSize);

		class FItem : public IMediaStreamMetadata::IItem
		{
		public:
			~FItem() {}
			const FString& GetLanguageCode() const override
			{ return Language; }
			const FString& GetMimeType() const override
			{ return MimeType; }
			const FVariant& GetValue() const override
			{ return Value; }

			FString Language;				// ISO 639-2; if not set (all zero) the default entry for all languages
			FString MimeType;
			int32 Type = 0;					// Well-known data type (see Quicktime reference)
			FVariant Value;
			FString ToJSONValue() const;
			static TArray<TCHAR> CharsToEscapeInJSON;
			bool operator != (const FItem& Other) const
			{
				return Type != Other.Type || Language != Other.Language || Value != Other.Value;
			}
			bool operator == (const FItem& Other) const
			{
				return !(*this != Other);
			}
		};

		TMap<uint32, FString> WellKnownItems;
		TMap<FString, TArray<TSharedPtr<FItem, ESPMode::ThreadSafe>>> Items;
		uint32 NumTotalItems = 0;
	};


	class FMP4RootBoxLocator
	{
	public:
		struct FBoxInfo
		{
			int64 Size = 0;
			int64 Offset = 0;
			uint32 Type = 0;
			uint8 UUID[16] {};
			TSharedPtrTS<FWaitableBuffer> DataBuffer;
		};
		FMP4RootBoxLocator() = default;
		~FMP4RootBoxLocator();
		DECLARE_DELEGATE_RetVal(bool, FCancellationCheckDelegate);
		bool LocateRootBoxes(TArray<FBoxInfo>& OutBoxInfos, const TSharedPtrTS<IElectraHttpManager>& InHTTPManager, const FString& InURL, const TArray<uint32>& InFirstBoxes, const TArray<uint32>& InStopAfterBoxes, const TArray<uint32>& InReadDataOfBoxes, FCancellationCheckDelegate InCheckCancellationDelegate);

		int64 GetFileSize() const
		{ return FileSize; }
		bool DidDownloadFail() const
		{ return bHasErrored; }
		const HTTP::FConnectionInfo& GetConnectionInfo() const
		{ return ConnectionInfo; }

		const FString& GetErrorMessage() const
		{ return ErrorMsg; }
	private:
		HTTP::FConnectionInfo ConnectionInfo;
		FString ErrorMsg;
		int64 FileSize = -1;
		volatile bool bHasErrored = false;
	};

	class FMP4ChunkLoader
	{
	public:		
		FMP4ChunkLoader() = default;
		~FMP4ChunkLoader() = default;
		
		DECLARE_DELEGATE_RetVal(bool, FCancellationCheckDelegate);
		TSharedPtrTS<FWaitableBuffer> LoadChunk(const int64 InOffset, const int64 InSize, const TSharedPtrTS<IElectraHttpManager>& InHTTPManager, const TSharedPtrTS<IHTTPResponseCache>& InHttpResponseCache, const FString& InURL, FCancellationCheckDelegate InCheckCancellationDelegate);

		int64 GetFileSize() const
		{ return FileSize; }
		bool DidDownloadFail() const
		{ return bHasErrored; }
		const HTTP::FConnectionInfo& GetConnectionInfo() const
		{ return ConnectionInfo; }

		const FString& GetErrorMessage() const
		{ return ErrorMsg; }
	private:
		HTTP::FConnectionInfo ConnectionInfo;
		FString ErrorMsg;
		int64 FileSize = -1;
		volatile bool bHasErrored = false;
	};
};

} // namespace Electra
