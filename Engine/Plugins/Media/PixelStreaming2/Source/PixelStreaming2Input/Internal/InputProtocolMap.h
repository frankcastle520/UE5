// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Misc/ScopeLock.h"
#include "IPixelStreaming2InputMessage.h"
#include "IPixelStreaming2DataProtocol.h"
#include "PixelStreaming2InputEnums.h"

namespace UE::PixelStreaming2Input
{

	/**
	 * @brief An map type that broadcasts the OnProtocolUpdated whenever
	 * it's inner map is updated
	 */
	class PIXELSTREAMING2INPUT_API FInputProtocolMap : public IPixelStreaming2DataProtocol
	{
	public:
		FInputProtocolMap(EPixelStreaming2MessageDirection InDirection)
			: Direction(InDirection) {}

		// Begin IPixelStreaming2DataProtocol interface
		TSharedPtr<IPixelStreaming2InputMessage> Add(FString Key) override;
		TSharedPtr<IPixelStreaming2InputMessage> Add(FString Key, TArray<EPixelStreaming2MessageTypes> InStructure) override;
		TSharedPtr<IPixelStreaming2InputMessage> Find(FString Key) override;
		FOnProtocolUpdated&						 OnProtocolUpdated() override { return OnProtocolUpdatedDelegate; };
		TSharedPtr<FJsonObject>					 ToJson() override;
		// End IPixelStreaming2DataProtocol interface

		bool										   AddInternal(FString Key, uint8 Id);
		bool										   AddInternal(FString Key, uint8 Id, TArray<EPixelStreaming2MessageTypes> InStructure);
		int											   Remove(FString Key);
		const TSharedPtr<IPixelStreaming2InputMessage> Find(FString Key) const;

		void Clear();
		bool IsEmpty() const;

		void Apply(const TFunction<void(FString, TSharedPtr<IPixelStreaming2InputMessage>)>& Visitor);

	private:
		TSharedPtr<IPixelStreaming2InputMessage> AddMessageInternal(FString Key, uint8 Id, TArray<EPixelStreaming2MessageTypes> InStructure);

	private:
		TSet<uint8>												Ids;
		TMap<FString, TSharedPtr<IPixelStreaming2InputMessage>> InnerMap;
		FOnProtocolUpdated										OnProtocolUpdatedDelegate;
		EPixelStreaming2MessageDirection						Direction;
		uint8													UserMessageId = 200;
	};

} // namespace UE::PixelStreaming2Input
