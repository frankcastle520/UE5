// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "IO/IoBuffer.h"
#include "IO/IoStatus.h"
#include "Misc/Optional.h"

#if !UE_BUILD_SHIPPING

enum class EStorageServerContentType : uint8
{
	Unknown = 0,
	CbObject,
	Binary,
	CompressedBinary,
};

class IStorageServerHttpClient 
{
public:
	using FResult = TTuple<TIoStatusOr<FIoBuffer>, EStorageServerContentType>;
	using FResultCallback = TFunction<void(FResult)>;

	virtual ~IStorageServerHttpClient() = default;

	virtual FResult RequestSync(
		FAnsiStringView Url,
		EStorageServerContentType Accept = EStorageServerContentType::Unknown,
		FAnsiStringView Verb = "GET",
		TOptional<FIoBuffer> OptPayload = TOptional<FIoBuffer>(),
		TOptional<FIoBuffer> OptDestination = TOptional<FIoBuffer>(),
		float TimeoutSeconds = -1.f,
		const bool bReportErrors = true
	) = 0;

	virtual void RequestAsync(
		FResultCallback&& Callback,
		FAnsiStringView Url,
		EStorageServerContentType Accept = EStorageServerContentType::Unknown,
		FAnsiStringView Verb = "GET",
		TOptional<FIoBuffer> OptPayload = TOptional<FIoBuffer>(),
		TOptional<FIoBuffer> OptDestination = TOptional<FIoBuffer>(),
		float TimeoutSeconds = -1.f,
		const bool bReportErrors = true
	) = 0;
};

#endif
