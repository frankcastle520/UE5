// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreaming2DataProtocol.h"
#include "Logging.h"
#include "Templates/SharedPointer.h"

#include "epic_rtc/core/data_track.h"

namespace UE::PixelStreaming2
{
	template <typename T>
	inline size_t ValueSize(T&& Value)
	{
		return sizeof(Value);
	}

	template <typename T>
	inline const void* ValueLoc(T&& Value)
	{
		return &Value;
	}

	inline size_t ValueSize(FString&& Value)
	{
		return Value.Len() * sizeof(TCHAR);
	}

	inline const void* ValueLoc(FString&& Value)
	{
		return *Value;
	}

	struct FBufferBuilder
	{
		TArray<uint8> Buffer;
		size_t		  Pos;

		FBufferBuilder(size_t size)
			: Pos(0)
		{
			Buffer.SetNum(size);
		}

		size_t Serialize(const void* Data, size_t DataSize)
		{
			check(Pos + DataSize <= Buffer.Num());
			FMemory::Memcpy(Buffer.GetData() + Pos, Data, DataSize);
			return Pos + DataSize;
		}

		template <typename T>
		void Insert(T&& Value)
		{
			const size_t VSize = ValueSize(Forward<T>(Value));
			const void*	 VLoc = ValueLoc(Forward<T>(Value));
			check(Pos + VSize <= Buffer.Num());
			FMemory::Memcpy(Buffer.GetData() + Pos, VLoc, VSize);
			Pos += VSize;
		}
	};

	class FEpicRtcDataTrack : public TSharedFromThis<FEpicRtcDataTrack>
	{
	public:
		static TSharedPtr<FEpicRtcDataTrack> Create(TRefCountPtr<EpicRtcDataTrackInterface> InTrack, TWeakPtr<IPixelStreaming2DataProtocol> InDataProtocol);

		virtual ~FEpicRtcDataTrack() = default;

		/**
		 * Sends a series of arguments to the data channel with the given type.
		 * @param MessageType The name of the message you want to send. This message name must be registered IPixelStreaming2InputHandler::GetFromStreamerProtocol().
		 * @returns True of the message was successfully sent.
		 */
		template <typename... Args>
		bool SendMessage(FString MessageType, Args... VarArgs) const
		{
			if (!IsActive())
			{
				return false;
			}

			uint8 MessageId;
			if (!GetMessageId(MessageType, MessageId))
			{
				return false;
			}

			FBufferBuilder Builder = EncodeMessage(MessageId, Forward<Args>(VarArgs)...);

			return Send(Builder.Buffer);
		}

		/**
		 * Sends a large buffer of data to the data track, will chunk into multiple data frames if frames greater than 16KB.
		 * @param MessageType The name of the message, it must be registered in IPixelStreaming2InputHandler::GetTo/FromStreamerProtocol()
		 * @param DataBytes The raw byte buffer to send.
		 * @returns True of the message was successfully sent.
		 */
		bool SendArbitraryData(const FString& MessageType, const TArray64<uint8>& DataBytes) const;

		/**
		 * @return The id of the underlying EpicRtc data track.
		 */
		EpicRtcStringView GetId() const;

		/**
		 * @return The state of the underlying EpicRtc data track.
		 */
		EpicRtcTrackState GetState() const { return Track->GetState(); }

		void SetSendTrack(TRefCountPtr<EpicRtcDataTrackInterface> InSendTrack) { SendTrack = InSendTrack; }

	protected:
		FEpicRtcDataTrack(TRefCountPtr<EpicRtcDataTrackInterface> InTrack, TWeakPtr<IPixelStreaming2DataProtocol> InDataProtocol);
		FEpicRtcDataTrack(TSharedPtr<FEpicRtcDataTrack> InTrack, TWeakPtr<IPixelStreaming2DataProtocol> InDataProtocol);

		virtual void PrependData(FBufferBuilder& Builder) const {};

		bool IsActive() const;

		bool GetMessageId(const FString& MessageType, uint8& OutMessageId) const;

	private:
		TRefCountPtr<EpicRtcDataTrackInterface> Track;

		/**
		 * Track that is used for sending data with Consumer/Producer architecture.
		 */
		TRefCountPtr<EpicRtcDataTrackInterface> SendTrack;

		TWeakPtr<IPixelStreaming2DataProtocol> WeakDataProtocol;

		bool Send(TArray<uint8>& Buffer) const;

		template <typename... Args>
		FBufferBuilder EncodeMessage(uint8 Type, Args... VarArgs) const
		{

			FBufferBuilder Builder(sizeof(Type) + (0 + ... + ValueSize(Forward<Args>(VarArgs))));
			PrependData(Builder);
			Builder.Insert(Type);
			(Builder.Insert(Forward<Args>(VarArgs)), ...);

			return MoveTemp(Builder);
		}
	};

	class FEpicRtcMutliplexDataTrack : public FEpicRtcDataTrack
	{
	public:
		static TSharedPtr<FEpicRtcMutliplexDataTrack> Create(TSharedPtr<FEpicRtcDataTrack> InTrack, TWeakPtr<IPixelStreaming2DataProtocol> InDataProtocol, const FString& InPlayerId);

	protected:
		FEpicRtcMutliplexDataTrack(TSharedPtr<FEpicRtcDataTrack> InTrack, TWeakPtr<IPixelStreaming2DataProtocol> InDataProtocol, const FString& InPlayerId);

		virtual void PrependData(FBufferBuilder& Builder) const override;

	private:
		const FString PlayerId;
	};
} // namespace UE::PixelStreaming2