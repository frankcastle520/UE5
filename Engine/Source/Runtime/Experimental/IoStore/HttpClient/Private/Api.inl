// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace UE::IoStore::HTTP
{

// {{{1 request ................................................................

////////////////////////////////////////////////////////////////////////////////
FRequest::~FRequest()
{
	if (Ptr == nullptr)
	{
		return;
	}

	Activity_Free(Ptr);
}

////////////////////////////////////////////////////////////////////////////////
FRequest::FRequest(FRequest&& Rhs)
{
	this->~FRequest();
	Swap(Ptr, Rhs.Ptr);
}

////////////////////////////////////////////////////////////////////////////////
FRequest&& FRequest::Accept(EMimeType MimeType)
{
	switch (MimeType)
	{
	case EMimeType::Text:				return Header("Accept", "text/html");
	case EMimeType::Binary:				return Header("Accept", "application/octet-stream");
	case EMimeType::Json:				return Header("Accept", "application/json");
	case EMimeType::Xml:				return Header("Accept", "application/xml");
	case EMimeType::CbObject:			return Header("Accept", "application/x-ue-cb");
	case EMimeType::CbPackage:			return Header("Accept", "application/x-ue-pkg");
	case EMimeType::CompressedBuffer:	return Header("Accept", "application/x-ue-comp");
	}

	return MoveTemp(*this);
}

////////////////////////////////////////////////////////////////////////////////
FRequest&& FRequest::Accept(FAnsiStringView MimeType)
{
	return Header("Accept", MimeType);
}

////////////////////////////////////////////////////////////////////////////////
FRequest&& FRequest::Header(FAnsiStringView Key, FAnsiStringView Value)
{
	checkSlow(Ptr->State == FActivity::EState::Build);
	FMessageBuilder(Ptr->Buffer) << Key << ": " << Value << "\r\n";
	return MoveTemp(*this);
}



// {{{1 response ...............................................................

////////////////////////////////////////////////////////////////////////////////
EStatusCodeClass FResponse::GetStatus() const
{
	uint32 Code = GetStatusCode();
	if (Code <= 199) return EStatusCodeClass::Informational;
	if (Code <= 299) return EStatusCodeClass::Successful;
	if (Code <= 399) return EStatusCodeClass::Redirection;
	if (Code <= 499) return EStatusCodeClass::ClientError;
	if (Code <= 599) return EStatusCodeClass::ServerError;
	return EStatusCodeClass::Unknown;
}

////////////////////////////////////////////////////////////////////////////////
uint32 FResponse::GetStatusCode() const
{
	const auto* Activity = (const FActivity*)this;
	const FResponseInternal& Internal = Activity->Response;

	const char* MessageData = Activity->Buffer.GetData() + Activity->StateParam;
	if (Internal.Code < 0)
	{
		const char* CodePtr = MessageData + Internal.Offsets.StatusCode;
		Internal.Code = uint16(CrudeToInt(FAnsiStringView(CodePtr, 3)));
	}

	return Internal.Code;
}

////////////////////////////////////////////////////////////////////////////////
FAnsiStringView FResponse::GetStatusMessage() const
{
	const auto* Activity = (const FActivity*)this;
	const FResponseInternal& Internal = Activity->Response;

	const char* MessageData = Activity->Buffer.GetData() + Activity->StateParam;
	return FAnsiStringView(
		MessageData + Internal.Offsets.Message,
		Internal.Offsets.Headers - Internal.Offsets.Message
	);
}

////////////////////////////////////////////////////////////////////////////////
int64 FResponse::GetContentLength() const
{
	const auto* Activity = (const FActivity*)this;
	const FResponseInternal& Internal = Activity->Response;
	return Internal.ContentLength;
}

////////////////////////////////////////////////////////////////////////////////
EMimeType FResponse::GetContentType() const
{
	FAnsiStringView Value;
	GetContentType(Value);

	if (Value == "text/html")					return EMimeType::Text;
	if (Value == "application/octet-stream")	return EMimeType::Binary;
	if (Value == "application/json")			return EMimeType::Json;
	if (Value == "application/xml")				return EMimeType::Xml;
	if (Value == "application/x-ue-cb")			return EMimeType::CbObject;
	if (Value == "application/x-ue-pkg")		return EMimeType::CbPackage;
	if (Value == "application/x-ue-comp")		return EMimeType::CompressedBuffer;

	return EMimeType::Unknown;
}

////////////////////////////////////////////////////////////////////////////////
void FResponse::GetContentType(FAnsiStringView& Out) const
{
	Out = GetHeader("Content-Type");

	int32 SemiColon;
	if (Out.FindChar(';', SemiColon))
	{
		Out = Out.Mid(SemiColon).TrimEnd();
	}
}

////////////////////////////////////////////////////////////////////////////////
FAnsiStringView FResponse::GetHeader(FAnsiStringView Name) const
{
	FAnsiStringView Result;
	ReadHeaders([&Result, Name] (FAnsiStringView Candidate, FAnsiStringView Value)
	{
		if (Candidate != Name)
		{
			return true;
		}

		Result = Value;
		return false;
	});
	return Result;
}

////////////////////////////////////////////////////////////////////////////////
void FResponse::ReadHeaders(FHeaderSink Sink) const
{
	const auto* Activity = (const FActivity*)this;
	const FResponseInternal& Internal = Activity->Response;

	const char* MessageData = Activity->Buffer.GetData() + Activity->StateParam;
	FAnsiStringView Result, Headers(
		MessageData + Internal.Offsets.Headers,
		Internal.MessageLength - Internal.Offsets.Headers
	);

	EnumerateHeaders(Headers, Sink);
}

////////////////////////////////////////////////////////////////////////////////
void FResponse::SetDestination(FIoBuffer* Buffer)
{
	auto* Activity = (FActivity*)this;
	Activity->Dest = Buffer;
}



// {{{1 ticket-status ..........................................................

////////////////////////////////////////////////////////////////////////////////
FTicketStatus::EId FTicketStatus::GetId() const
{
	const auto* Activity = (FActivity*)this;
	switch (Activity->State)
	{
	case FActivity::EState::RecvMessage:	return EId::Response;
	case FActivity::EState::RecvStream:
	case FActivity::EState::RecvDone:		return EId::Content;
	case FActivity::EState::Cancelled:		return EId::Cancelled;
	case FActivity::EState::Failed:			return EId::Error;
	default:								check(false);
	}
	return EId::Error;
}

////////////////////////////////////////////////////////////////////////////////
UPTRINT FTicketStatus::GetParam() const
{
	const auto* Activity = (FActivity*)this;
	return Activity->SinkParam;
}

////////////////////////////////////////////////////////////////////////////////
FTicket FTicketStatus::GetTicket() const
{
	const auto* Activity = (FActivity*)this;
	return 1ull << Activity->Slot;
}

////////////////////////////////////////////////////////////////////////////////
uint32 FTicketStatus::GetIndex() const
{
	const auto* Activity = (FActivity*)this;
	return Activity->Slot;
}

////////////////////////////////////////////////////////////////////////////////
FResponse& FTicketStatus::GetResponse() const
{
	check(GetId() < EId::Content);
	const auto* Activity = (FActivity*)this;
	return *(FResponse*)Activity;
}

////////////////////////////////////////////////////////////////////////////////
uint32 FTicketStatus::GetContentLength() const
{
	check(GetId() <= EId::Content);
	const auto* Activity = (FActivity*)this;
	return Activity->Response.ContentLength;
}

////////////////////////////////////////////////////////////////////////////////
const FTicketPerf& FTicketStatus::GetPerf() const
{
	check(GetId() == EId::Content);
	const auto* Activity = (FActivity*)this;
	return *(FTicketPerf*)Activity;
}

////////////////////////////////////////////////////////////////////////////////
const FIoBuffer& FTicketStatus::GetContent() const
{
	check(GetId() == EId::Content);
	const auto* Activity = (FActivity*)this;
	return *(Activity->Dest);
}

////////////////////////////////////////////////////////////////////////////////
const char* FTicketStatus::GetErrorReason() const
{
	check(GetId() == EId::Error);
	const auto* Activity = (FActivity*)this;
	return Activity->ErrorReason;
}



// {{{1 perf ...................................................................

#if IAS_HTTP_WITH_PERF

////////////////////////////////////////////////////////////////////////////////
FTicketPerf::FSample FTicketPerf::GetSample() const
{
	const auto* Activity = (FActivity*)this;

	static uint64 Freq;
	if (Freq == 0)
	{
		Freq = uint64(1.0 / FPlatformTime::GetSecondsPerCycle());
	}

	auto ToMs = [] (uint64 Value) { return uint32((Value * 1000ull) / Freq); };

	const FStopwatch& Stopwatch = Activity->Stopwatch;
	return {
		ToMs(Stopwatch.GetInterval(0)),
		ToMs(Stopwatch.GetInterval(1)),
		ToMs(Stopwatch.GetInterval(2)),
	};
}

#endif // IAS_HTTP_WITH_PERF



// }}}

} // namespace UE::IoStore::HTTP
