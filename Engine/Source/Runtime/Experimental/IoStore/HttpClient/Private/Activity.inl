// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace UE::IoStore::HTTP
{

// {{{1 activity ...............................................................

#if IAS_HTTP_WITH_PERF

////////////////////////////////////////////////////////////////////////////////
class FStopwatch
{
public:
	uint64	GetInterval(uint32 i) const;
	void	SendStart()		{ Impl(0); }
	void	SendEnd()		{ Impl(1); }
	void	RecvStart()		{ Impl(2); }
	void	RecvEnd()		{ Impl(3); }

private:
	void	Impl(uint32 Index);
	uint64	Samples[4] = {};
	uint32	Counts[2] = {};
};

////////////////////////////////////////////////////////////////////////////////
uint64 FStopwatch::GetInterval(uint32 i) const
{
	if (i >= UE_ARRAY_COUNT(Samples) - 1)
	{
		return 0;
	}
	return Samples[i + 1] - Samples[i];
}

////////////////////////////////////////////////////////////////////////////////
void FStopwatch::Impl(uint32 Index)
{
	if (uint64& Out = Samples[Index]; Out == 0)
	{
		Out = FPlatformTime::Cycles64();
	}
	Counts[Index >> 1] += !(Index & 1);
}

#endif // IAS_HTTP_WITH_PERF

////////////////////////////////////////////////////////////////////////////////
struct FResponseInternal
{
	FMessageOffsets Offsets;
	int32			ContentLength = 0;
	uint16			MessageLength;
	mutable int16	Code;
};

////////////////////////////////////////////////////////////////////////////////
struct alignas(16) FActivity
{
	enum class EState : uint8
	{
		None,
		Build,
		Send,
		RecvMessage,
		RecvStream,
		RecvContent,
		RecvDone,
		Completed,
		Cancelled,
		Failed,
		_Num,
	};

	FActivity*			Next = nullptr;
	int8				Slot = -1;
	EState				State = EState::None;
	uint8				IsKeepAlive : 1;
	uint8				NoContent : 1;
	uint8				bFollow30x : 1;
	uint8				bAllowChunked : 1;
	uint8				_Unused0 : 4;
	uint32				StateParam = 0;
#if IAS_HTTP_WITH_PERF
	FStopwatch			Stopwatch;
#endif
	union {
		FHost*			Host;
		FIoBuffer*		Dest;
		const char*		ErrorReason;
	};
	UPTRINT				SinkParam;
	FTicketSink			Sink;
	FResponseInternal	Response;
	FBuffer				Buffer;
};

////////////////////////////////////////////////////////////////////////////////
static void Activity_TraceStateNames()
{
	FAnsiStringView StateNames[] = {
		"None", "Build", "Send", "RecvMessage", "RecvStream", "RecvContent",
		"RecvDone", "Completed", "Cancelled", "Failed", "$",
	};
	static_assert(UE_ARRAY_COUNT(StateNames) == int32(FActivity::EState::_Num) + 1);

	TraceEnum(StateNames);
}

////////////////////////////////////////////////////////////////////////////////
static void Activity_ChangeState(FActivity* Activity, FActivity::EState InState, uint32 Param=0)
{
	Trace(Activity, ETrace::StateChange, InState);

	check(Activity->State != InState);
	Activity->State = InState;
	Activity->StateParam = Param;
}

////////////////////////////////////////////////////////////////////////////////
static int32 Activity_Rewind(FActivity* Activity)
{
	using EState = FActivity::EState;

	if (Activity->State == EState::Send)
	{
		Activity->StateParam = 0;
		return 0;
	}

	if (Activity->State == EState::RecvMessage)
	{
		Activity->Buffer.Resize(Activity->StateParam);
		Activity_ChangeState(Activity, EState::Send);
		return 1;
	}

	return -1;
}

////////////////////////////////////////////////////////////////////////////////
static uint32 Activity_RemainingKiB(FActivity* Activity)
{
	if (Activity->State <= FActivity::EState::RecvStream)  return MAX_uint32;
	if (Activity->State >  FActivity::EState::RecvContent) return 0;

	uint32 ContentLength = uint32(Activity->Response.ContentLength);
	check(Activity->StateParam <= ContentLength);
	return (ContentLength - Activity->StateParam) >> 10;
}

////////////////////////////////////////////////////////////////////////////////
static FActivity* Activity_Alloc(uint32 BufferSize)
{
	BufferSize = (BufferSize + 15) & ~15;

	uint32 Size = BufferSize + sizeof(FActivity);
	auto* Activity = (FActivity*)FMemory::Malloc(Size, alignof(FActivity));

	new (Activity) FActivity();

	auto* Scratch = (char*)(Activity + 1);
	uint32 ScratchSize = BufferSize;
	Activity->Buffer = FBuffer(Scratch, ScratchSize);

	Activity_ChangeState(Activity, FActivity::EState::Build);

	return Activity;
}

////////////////////////////////////////////////////////////////////////////////
static void Activity_Free(FActivity* Activity)
{
	Trace(Activity, ETrace::ActivityDestroy);

	Activity->~FActivity();
	FMemory::Free(Activity);
}

////////////////////////////////////////////////////////////////////////////////
static void Activity_SetError(FActivity* Activity, const char* Reason)
{
	Activity->IsKeepAlive = 0;
	Activity->ErrorReason = Reason;

	Activity_ChangeState(Activity, FActivity::EState::Failed, LastSocketResult());
}

// }}}

} // namespace UE::IoStore::HTTP
