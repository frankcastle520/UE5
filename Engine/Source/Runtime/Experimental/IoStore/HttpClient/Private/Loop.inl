// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace UE::IoStore::HTTP
{

// {{{1 event-loop-int .........................................................

using FPeerType = FHttpPeer;

////////////////////////////////////////////////////////////////////////////////
static FOutcome DoSend(FActivity* Activity, FPeerType& Peer)
{
	Trace(Activity, ETrace::StateChange, Activity->State);

#if IAS_HTTP_WITH_PERF
	Activity->Stopwatch.SendStart();
#endif

	TRACE_CPUPROFILER_EVENT_SCOPE(IasHttp::DoSend);

	FBuffer& Buffer = Activity->Buffer;
	const char* SendData = Buffer.GetData();
	int32 SendSize = Buffer.GetSize();

	uint32 AlreadySent = Activity->StateParam;
	SendData += AlreadySent;
	SendSize -= AlreadySent;
	check(SendSize > 0);

	FOutcome Outcome = Peer.Send(SendData, SendSize);

	if (Outcome.IsError())
	{
		Activity_SetError(Activity, Outcome.GetMessage().GetData());
		return Outcome;
	}

	if (Outcome.IsWaiting())
	{
		return Outcome;
	}

	check(Outcome.IsOk());

	int32 Result = Outcome.GetResult();
	Activity->StateParam += Result;
	if (Activity->StateParam < Buffer.GetSize())
	{
		return DoSend(Activity, Peer);
	}

#if IAS_HTTP_WITH_PERF
	Activity->Stopwatch.SendEnd();
#endif

	Activity_ChangeState(Activity, FActivity::EState::RecvMessage, Buffer.GetSize());
	return FOutcome::Ok(Result);
}

////////////////////////////////////////////////////////////////////////////////
static FOutcome DoRecvPeer(FActivity* Activity, FPeerType& Peer, int32& MaxRecvSize, int32 Size)
{
	Size = FMath::Min(Size, MaxRecvSize);
	check(Size >= 0);
	if (Size == 0)
	{
		return FOutcome::Waiting();
	}

	Trace(Activity, ETrace::StateChange, Activity->State);

	FMutableMemoryView DestView = Activity->Dest->GetMutableView();
	char* Cursor = (char*)(DestView.GetData()) + Activity->StateParam;
	check(Size + Activity->StateParam <= DestView.GetSize());

	FOutcome Outcome = Peer.Recv(Cursor, Size);

	if (Outcome.IsWaiting())
	{
		return Outcome;
	}

	if (Outcome.IsError())
	{
		Activity_SetError(Activity, Outcome.GetMessage().GetData());
		return Outcome;
	}

	check(Outcome.IsOk());

	int32 Result = Outcome.GetResult();
	check(Result <= MaxRecvSize);
	Activity->StateParam += Result;
	MaxRecvSize -= Result;

	return Outcome;
}

////////////////////////////////////////////////////////////////////////////////
static FOutcome DoRecvMessage(FActivity* Activity, FPeerType& Peer)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IasHttp::DoRecvMessage);

	static const uint32 PageSize = 256;

	FBuffer& Buffer = Activity->Buffer;

	const char* MessageRight;
	while (true)
	{
		Trace(Activity, ETrace::StateChange, Activity->State);

#if IAS_HTTP_WITH_PERF
		Activity->Stopwatch.RecvStart();
#endif

		auto [Dest, DestSize] = Buffer.GetMutableFree(0, PageSize);
		FOutcome Outcome = Peer.Recv(Dest, DestSize);

		if (Outcome.IsError())
		{
			Activity_SetError(Activity, Outcome.GetMessage().GetData());
			return Outcome;
		}

		if (Outcome.IsWaiting())
		{
			return Outcome;
		}

		check(Outcome.IsOk());

		int32 Result = Outcome.GetResult();
		Buffer.AdvanceUsed(Result);

		// Rewind a little to cover cases where the terminal is fragmented across
		// recv() calls
		uint32 DestBias = 0;
		if (Dest - 3 >= Buffer.GetData() + Activity->StateParam)
		{
			Dest -= (DestBias = 3);
		}

		int32 MessageEnd = FindMessageTerminal(Dest, Result + DestBias);
		if (MessageEnd < 0)
		{
			if (Buffer.GetSize() > (8 << 10))
			{
				Activity_SetError(Activity, "Headers have grown larger than expected");
				return FOutcome::Error(Activity->ErrorReason);
			}

			continue;
		}

		MessageRight = Dest + MessageEnd;
		break;
	}

	// Fill out the internal response object
	FResponseInternal& Internal = Activity->Response;
	const char* MessageData = Buffer.GetData() + Activity->StateParam;
	Internal.MessageLength = uint16(ptrdiff_t(MessageRight - MessageData));

	FAnsiStringView ResponseView(MessageData, Internal.MessageLength);
	if (ParseMessage(ResponseView, Internal.Offsets) < 0)
	{
		Activity_SetError(Activity, "Failed to parse message status");
		return FOutcome::Error(Activity->ErrorReason);
	}

	// Parse headers
	FAnsiStringView Headers = ResponseView.Mid(
		Internal.Offsets.Headers,
		Internal.MessageLength - Internal.Offsets.Headers - 2 // "-2" trims off '\r\n' that signals end of headers
	);

	int32 Count = 3;
	bool bChunked = false;
	bool IsKeepAlive = true;
	int32 ContentLength = -1;
	EnumerateHeaders(
		Headers,
		[&ContentLength, &bChunked, &IsKeepAlive, &Count] (FAnsiStringView Name, FAnsiStringView Value)
		{
			// todo; may need smarter value handling; ;/, separated options & key-value pairs (ex. in rfc2068)

			if (Name.Equals("Content-Length", ESearchCase::IgnoreCase))
			{
				ContentLength = int32(CrudeToInt(Value));
				Count--;
			}

			else if (Name.Equals("Transfer-Encoding", ESearchCase::IgnoreCase))
			{
				bChunked = Value.Equals("chunked", ESearchCase::IgnoreCase);
				Count--;
			}

			else if (Name.Equals("Connection", ESearchCase::IgnoreCase))
			{
				IsKeepAlive = !Value.Equals("close");
				Count--;
			}

			return Count > 0;
		}
	);

	Activity->IsKeepAlive &= IsKeepAlive;

	// Validate that the server's told us how and how much it will transmit
	if (bChunked)
	{
		if (Activity->bAllowChunked == 0)
		{
			Activity_SetError(Activity, "Chunked transfer encoding disabled (ERRNOCHUNK)");
			return FOutcome::Error(Activity->ErrorReason);
		}

		ContentLength = -1;
	}
	else if (ContentLength < 0)
	{
		Activity_SetError(Activity, "Missing/invalid Content-Length header");
		return FOutcome::Error(Activity->ErrorReason);
	}

	// Call out to the sink to get a content destination
	FIoBuffer* PriorDest = Activity->Dest; // to retain unioned Host ptr (redirect uses it in sink)
	Internal.Code = -1;
	Internal.ContentLength = ContentLength;
	{
		FTicketStatus& SinkArg = *(FTicketStatus*)Activity;
		Activity->Sink(SinkArg);
	}
	Activity->NoContent |= (ContentLength == 0);

	// Check the user gave us a destination for content
	FIoBuffer& Dest = *(Activity->Dest);
	if (Activity->NoContent == 0)
	{
		if (&Dest == PriorDest)
		{
			Activity_SetError(Activity, "User did not provide a destination buffer");
			return FOutcome::Error(Activity->ErrorReason);
		}

		// The user seems to have forgotten something. Let's help them along
		if (int32 DestSize = int32(Dest.GetSize()); DestSize == 0)
		{
			static const uint32 DefaultChunkSize = 4 << 10;
			uint32 Size = bChunked ? DefaultChunkSize : ContentLength;
			Dest = FIoBuffer(Size);
		}
		else if (!bChunked && DestSize < ContentLength)
		{
			// todo: support piece-wise transfer of content (a la chunked).
			Activity_SetError(Activity, "Destination buffer too small");
			return FOutcome::Error(Activity->ErrorReason);
		}
		else if (enum { MinStreamBuf = 256 }; bChunked && DestSize < MinStreamBuf)
		{
			Dest = FIoBuffer(MinStreamBuf);
		}
	}

	// Perhaps we have some of the content already?
	const char* BufferRight = Buffer.GetData() + Buffer.GetSize();
	uint32 AlreadyReceived = uint32(ptrdiff_t(BufferRight - MessageRight));
	if (AlreadyReceived > uint32(ContentLength))
	{
		Activity_SetError(Activity, "More data received that expected");
		return FOutcome::Error(Activity->ErrorReason);
	}

	// HEAD methods
	if (Activity->NoContent == 1)
	{
		if (AlreadyReceived)
		{
			Activity_SetError(Activity, "Received content when none was expected");
			return FOutcome::Error(Activity->ErrorReason);
		}
		Activity_ChangeState(Activity, FActivity::EState::RecvDone);
		return FOutcome::Ok();
	}

	// We're all set to go and get content
	check(Activity->Dest != nullptr);

	auto NextState = bChunked ? FActivity::EState::RecvStream : FActivity::EState::RecvContent;
	Activity_ChangeState(Activity, NextState, AlreadyReceived);

	// Copy any of the content we may have already received.
	if (AlreadyReceived == 0)
	{
		return FOutcome::Ok();
	}

	// This ordinarily doesn't happen due to the way higher levels pipeline
	// requests. It can however occur with chunked transfers.
	if (AlreadyReceived > Dest.GetSize())
	{
		Dest = FIoBuffer(AlreadyReceived);
	}

	FMutableMemoryView DestView = Dest.GetMutableView();
	const char* Cursor = BufferRight - AlreadyReceived;
	::memcpy(DestView.GetData(), Cursor, AlreadyReceived);

	return FOutcome::Ok();
}

////////////////////////////////////////////////////////////////////////////////
static FOutcome DoRecvContent(FActivity* Activity, FPeerType& Peer, int32& MaxRecvSize)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IasHttp::DoRecvContent);

	while (true)
	{
		const FResponseInternal& Response = Activity->Response;
		int32 Size = (Response.ContentLength - Activity->StateParam);
		if (Size == 0)
		{
			break;
		}

		FOutcome Outcome = DoRecvPeer(Activity, Peer, MaxRecvSize, Size);
		if (!Outcome.IsOk())
		{
			return Outcome;
		}
	}

#if IAS_HTTP_WITH_PERF
	Activity->Stopwatch.RecvEnd();
#endif

	Activity_ChangeState(Activity, FActivity::EState::RecvDone);
	return FOutcome::Ok();
}

////////////////////////////////////////////////////////////////////////////////
static FOutcome DoRecvStream(FActivity* Activity, FPeerType& Peer, int32& MaxRecvSize)
{
	auto RaiseCrLfError = [Activity] ()
	{
		if (Activity->NoContent)
		{
			Activity_SetError(Activity, "Trailing headers are not supported (ERRTRAIL)");
			return FOutcome::Error(Activity->ErrorReason);
		}

		Activity_SetError(Activity, "Expected CRLF chunk terminal");
		return FOutcome::Error(Activity->ErrorReason);
	};

	auto SinkData = [Activity] (FMemoryView View)
	{
		if (View.GetSize() == 0)
		{
			return;
		}

		// Temporarily clamp IoBuffer so if the sink does GetView/GetSize() it
		// represents actual content and not the underlying working buffer.
		FIoBuffer& Dest = *(Activity->Dest);
		FIoBuffer Slice(View, Dest);
		Swap(Dest, Slice);
		
		FTicketStatus& SinkArg = *(FTicketStatus*)Activity;
		Activity->Sink(SinkArg);

		Swap(Dest, Slice);
	};

	auto Done = [Activity] ()
	{
#if IAS_HTTP_WITH_PERF
			Activity->Stopwatch.RecvEnd();
#endif
			*(Activity->Dest) = FIoBuffer();
			Activity_ChangeState(Activity, FActivity::EState::RecvDone);
			return FOutcome::Ok();
	};

	enum { CrLfLength = 2 };

	int32 Size = int32(Activity->StateParam);

	// Trailing chunk data.
	while (Size < 0)
	{
		Activity->StateParam = 0;
		Size = -Size;

		int32 RefillSize = FMath::Min<int32>(Size, int32(Activity->Dest->GetSize()));
		FOutcome Outcome = DoRecvPeer(Activity, Peer, MaxRecvSize, RefillSize);
		if (!Outcome.IsOk())
		{
			Activity->StateParam = 0 - Size;
			return Outcome;
		}

		int32 Result = Outcome.GetResult();
		check(Result > 0);

		FMemoryView View = Activity->Dest->GetView();
		if (Size > CrLfLength)
		{
			int32 SinkSize = Size - CrLfLength;
			SinkSize = FMath::Min(Result, SinkSize);
			SinkData(View.Left(SinkSize));
			View = View.Mid(SinkSize);
			Size -= SinkSize;
			Result -= SinkSize;
		}

		const char* Cursor = (char*)View.GetData();
		int32 CrLfError = 0;
		if (int32 n = CrLfLength; Size == n && Result >= n)
		{
			CrLfError |= (Cursor[0] != '\r');
			--Size; --Result;
			++Cursor;
		}
		if (int32 n = CrLfLength - 1; Size == n && Result >= n)
		{
			CrLfError |= (Cursor[0] != '\n');
			--Size; --Result;
		}
		if (CrLfError)
		{
			return RaiseCrLfError();
		}

		Size = Result - Size;
		Activity->StateParam = Size;
		check(Size <= 0);

		// Have we found the trailer-section that follows last-chunk?
		if (Size == 0 && Activity->NoContent)
		{
			return Done();
		}
	}

	// Peel off chunks
	for (FMemoryView View = Activity->Dest->GetView(); Size > 0;)
	{
		const char* Cursor = (char*)(View.GetData());

		// Isolate chunk size
 		int32 ChunkSize = -1;
		uint32 HeaderLength = 0;
		for (; HeaderLength < uint32(Size - 1); ++HeaderLength)
		{
			// Detect CRLF.
			if (Cursor[HeaderLength + 1] != '\n')
			{
				continue;
			}

			++HeaderLength;
			if (Cursor[HeaderLength - 1] != '\r')
			{
				continue;
			}
			++HeaderLength;

			ChunkSize = int32(CrudeToInt<16>(Cursor));
			if (ChunkSize < 0)
			{
				Activity_SetError(Activity, "Unparsable chunk size");
				return FOutcome::Error(Activity->ErrorReason);
			}

			break;
		}

		// Maybe we were not able to find a CRLF terminator and need more data
		if (ChunkSize < 0)
		{
			FMutableMemoryView WriteView = Activity->Dest->GetMutableView();
			std::memmove(WriteView.GetData(), Cursor, Size);
			Activity->StateParam = Size;
			break;
		}

		check(ChunkSize >= 0);
		Size -= HeaderLength;

		// Dispatch as much data as we can.
		uint32 SinkSize = FMath::Min<uint32>(ChunkSize, uint32(Size));
		SinkData(View.Mid(HeaderLength, SinkSize));
		View = View.Mid(HeaderLength + SinkSize);

		Activity->StateParam = (Size -= ChunkSize);
		Activity->NoContent = (ChunkSize == 0);

		// A CRLF follows a chunk's data
		Cursor = (char*)View.GetData();
		int32 CrLfError = 0;
		CrLfError |= (Size >= (CrLfLength - 1)) && Cursor[0] != '\r';
		CrLfError |= (Size >= (CrLfLength - 0)) && Cursor[1] != '\n';
		if (CrLfError != 0)
		{
			return RaiseCrLfError();
		}

		// Can we do CRLF now?
		if (Size >= CrLfLength)
		{
			// Have we found the trailer-section that follows last-chunk?
			if (Activity->NoContent)
			{
				return Done();
			}

			Activity->StateParam = (Size -= CrLfLength);
			View = View.Mid(CrLfLength);
			continue;
		}

		Activity->StateParam -= CrLfLength;
		check(int32(Activity->StateParam) < 0);
		break;
	}

	// Refill
	if (int32(Activity->StateParam) >= 0)
	{
		uint32 RefillSize = uint32(Activity->Dest->GetSize()) - Activity->StateParam;
		FOutcome Outcome = DoRecvPeer(Activity, Peer, MaxRecvSize, RefillSize);
		if (!Outcome.IsOk())
		{
			return Outcome;
		}
	}

	return DoRecvStream(Activity, Peer, MaxRecvSize);
}

////////////////////////////////////////////////////////////////////////////////
static FOutcome DoRecv(FActivity* Activity, FPeerType& Peer, int32& MaxRecvSize)
{
	using EState = FActivity::EState;

	EState State = Activity->State; 
	check(State >= EState::RecvMessage && State < EState::RecvDone);

	if (State == EState::RecvMessage)	return DoRecvMessage(Activity, Peer);
	if (State == EState::RecvContent)	return DoRecvContent(Activity, Peer, MaxRecvSize);
	if (State == EState::RecvStream)	return DoRecvStream(Activity, Peer, MaxRecvSize); //-V547
	
	check(false); // it is not expected that we'll get here
	return FOutcome::Error("unreachable");
}

////////////////////////////////////////////////////////////////////////////////
static void DoRecvDone(FActivity* Activity)
{
	// Notify the user we've received everything
	FTicketStatus& SinkArg = *(FTicketStatus*)Activity;
	Activity->Sink(SinkArg);

	Activity_ChangeState(Activity, FActivity::EState::Completed);
}

////////////////////////////////////////////////////////////////////////////////
static void DoCancel(FActivity* Activity)
{
	if (Activity->State >= FActivity::EState::Completed)
	{
		return;
	}

	Activity_ChangeState(Activity, FActivity::EState::Cancelled);

	FTicketStatus& SinkArg = *(FTicketStatus*)Activity;
	Activity->Sink(SinkArg);
}

////////////////////////////////////////////////////////////////////////////////
static void DoFail(FActivity* Activity)
{
	check(Activity->State == FActivity::EState::Failed);

	// Notify the user we've received everything
	FTicketStatus& SinkArg = *(FTicketStatus*)Activity;
	Activity->Sink(SinkArg);
}



// {{{1 work-queue .............................................................

/*
 * - Activities (requests send with a loop) are managed in singly-linked lists
 * - Each activity has an associated host it is talking to.
 * - Hosts are ephemeral, or represented externally via a FConnectionPool object
 * - Loop has a group for each host, and each host-group has a bunch of socket-groups
 * - Host-group has a list of work; pending activities waiting to start
 * - Socket-groups own up to two activities; one sending, one receiving
 * - As it recvs, a socket-group will, if possible, fetch more work from the host
 *
 *  Loop:
 *    FHostGroup[HostPtr]:
 *	    Work: Act0 -> Act1 -> Act2 -> Act3 -> ...
 *      FPeerGroup[0...HostMaxConnections]:
 *			Act.Send
 *			Act.Recv
 */

////////////////////////////////////////////////////////////////////////////////
struct FTickState
{
	FActivity*					DoneList;
	uint64						Cancels;
	int32&						RecvAllowance;
	int32						PollTimeoutMs;
	int32						FailTimeoutMs;
	uint32						NowMs;
	class FWorkQueue*			Work;
};



////////////////////////////////////////////////////////////////////////////////
class FWorkQueue
{
public:
						FWorkQueue() = default;
						~FWorkQueue();
	bool				HasWork() const { return List != nullptr; }
	void				AddActivity(FActivity* Activity);
	FActivity*			PopActivity();
	void				TickCancels(FTickState& State);

private:
	FActivity*			List = nullptr;
	FActivity*			ListTail = nullptr;
	uint64				ActiveSlots = 0;

	UE_NONCOPYABLE(FWorkQueue);
};

////////////////////////////////////////////////////////////////////////////////
FWorkQueue::~FWorkQueue()
{
	check(List == nullptr);
	check(ListTail == nullptr);
}

////////////////////////////////////////////////////////////////////////////////
void FWorkQueue::AddActivity(FActivity* Activity)
{
	// We use a tail pointer here to maintain order that requests were made

	check(Activity->Next == nullptr);

	if (ListTail != nullptr)
	{
		ListTail->Next = Activity;
	}
	List = (List == nullptr) ? Activity : List;
	ListTail = Activity;

	ActiveSlots |= (1ull << Activity->Slot);
}

////////////////////////////////////////////////////////////////////////////////
FActivity* FWorkQueue::PopActivity()
{
	if (List == nullptr)
	{
		return nullptr;
	}

	FActivity* Activity = List;
	if ((List = List->Next) == nullptr)
	{
		ListTail = nullptr;
	}

	check(ActiveSlots & (1ull << Activity->Slot));
	ActiveSlots ^= (1ull << Activity->Slot);

	Activity->Next = nullptr;
	return Activity;
}

////////////////////////////////////////////////////////////////////////////////
void FWorkQueue::TickCancels(FTickState& State)
{
	if (State.Cancels == 0 || (State.Cancels & ActiveSlots) == 0)
	{
		return;
	}

	// We are going to rebuild the list of activities to maintain order as the
	// activity list is singular.

	check(List != nullptr);
	FActivity* Activity = List;
	List = ListTail = nullptr;
	ActiveSlots = 0;

	for (FActivity* Next; Activity != nullptr; Activity = Next)
	{
		Next = Activity->Next;

		if (uint64 Slot = (1ull << Activity->Slot); (State.Cancels & Slot) == 0)
		{
			Activity->Next = nullptr;
			AddActivity(Activity);
			continue;
		}

		DoCancel(Activity);

		Activity->Next = State.DoneList;
		State.DoneList = Activity;
	}
}



// {{{1 peer-group .............................................................

////////////////////////////////////////////////////////////////////////////////
class FPeerGroup
{
public:
						FPeerGroup() = default;
						~FPeerGroup();
	bool				operator == (FSocket* Rhs) const { return (const FSocket*)Peer == Rhs; }
	void				Unwait()			{ check(bWaiting); bWaiting = false; }
	FSocket::FWaiter	GetWaiter() const;
	bool				Tick(FTickState& State);
	void				TickSend(FTickState& State, FHost& Host);
	void				Fail(FTickState& State, const char* Reason);

private:
	void				Negotiate(FTickState& State);
	void				RecvInternal(FTickState& State);
	void				SendInternal(FTickState& State);
	FActivity*			Send = nullptr;
	FActivity*			Recv = nullptr;
	FPeerType			Peer;
	uint32				LastUseMs = 0;
	uint8				IsKeepAlive = 0;
	bool				bNegotiating = false;
	bool				bWaiting = false;

	UE_NONCOPYABLE(FPeerGroup);
};

////////////////////////////////////////////////////////////////////////////////
FPeerGroup::~FPeerGroup()
{
	check(Send == nullptr);
	check(Recv == nullptr);
}

////////////////////////////////////////////////////////////////////////////////
FSocket::FWaiter FPeerGroup::GetWaiter() const
{
	if (!bWaiting)
	{
		return FSocket::FWaiter();
	}

	const auto& S = (const FSocket&)Peer;

	using EWhat = FSocket::FWaiter::EWhat;
	EWhat What = (Recv != nullptr) ? EWhat::Recv : EWhat::Send;
	return FSocket::FWaiter(S, What);
}

////////////////////////////////////////////////////////////////////////////////
void FPeerGroup::Fail(FTickState& State, const char* Reason)
{
	// Any send left at this point is unrecoverable
	if (Send != nullptr)
	{
		Send->Next = Recv;
		Recv = Send;
	}

	// Failure is quite terminal and we need to abort everything
	for (FActivity* Activity = Recv; Activity != nullptr;)
	{
		if (Activity->State != FActivity::EState::Failed)
		{
			Activity_SetError(Activity, Reason);
		}

		DoFail(Activity);

		FActivity* Next = Activity->Next;
		Activity->Next = State.DoneList;
		State.DoneList = Activity;
		Activity = Next;
	}

	Peer = FPeerType();
	Send = Recv = nullptr;
	bWaiting = false;
	IsKeepAlive = 0;
	bNegotiating = false;
}

////////////////////////////////////////////////////////////////////////////////
void FPeerGroup::Negotiate(FTickState& State)
{
	check(bNegotiating);
	check(Send != nullptr);
	check(Peer.IsValid());

	FOutcome Outcome = Peer.Handshake();
	if (Outcome.IsError())
	{
		Fail(State, Outcome.GetMessage().GetData());
		return;
	}

	if (Outcome.IsWaiting())
	{
		bWaiting = true;
		return;
	}

	bNegotiating = false;
	return SendInternal(State);
}

////////////////////////////////////////////////////////////////////////////////
void FPeerGroup::RecvInternal(FTickState& State)
{
	check(bNegotiating == false);
	check(Recv != nullptr);

	// Another helper lambda
	auto IsReceiving = [] (const FActivity* Act)
	{
		using EState = FActivity::EState;
		return (Act->State >= EState::RecvMessage) & (Act->State < EState::RecvDone);
	};

	FActivity* Activity = Recv;
	check(IsReceiving(Activity));

	FOutcome Outcome = DoRecv(Activity, Peer, State.RecvAllowance);

	// Any sort of error here is unrecoverable
	if (Outcome.IsError())
	{
		Fail(State, Outcome.GetMessage().GetData());
		return;
	}

	IsKeepAlive &= Activity->IsKeepAlive;
	LastUseMs = State.NowMs;

	// If we've only a small amount left to receive we can start more work
	if (IsKeepAlive & (Recv->Next == nullptr))
	{
		uint32 Remaining = Activity_RemainingKiB(Activity);
		if (Remaining < uint32(GRecvWorkThresholdKiB))
		{
			if (FActivity* Next = State.Work->PopActivity(); Next != nullptr)
			{
				Trace(Activity, ETrace::StartWork);
	
				check(Send == nullptr);
				Send = Next;
				SendInternal(State);

				if (!Peer.IsValid())
				{
					return;
				}
			}
		}
	}

	// If there was no data available this is far as receiving can go
	if (bWaiting = Outcome.IsWaiting(); bWaiting)
	{
		return;
	}

	// If we're still in a receiving state we will just try again otherwise it
	// is finished and we will let DoneList recipient finish it off.
	if (IsReceiving(Activity))
	{
		return;
	}

	DoRecvDone(Activity);

	Recv = Activity->Next;
	Activity->Next = State.DoneList;
	State.DoneList = Activity;

	// If the server wants to close the socket we need to rewind the send
	if (IsKeepAlive != 0)
	{
		return;
	}

	if (Send != nullptr && Activity_Rewind(Send) < 0)
	{
		Fail(State, "Unable to rewind on keep-alive close");
		return;
	}

	Peer = FPeerType();
}

////////////////////////////////////////////////////////////////////////////////
void FPeerGroup::SendInternal(FTickState& State)
{
	check(bNegotiating == false);
	check(IsKeepAlive == 1);
	check(Send != nullptr);

	FActivity* Activity = Send;

	FOutcome Outcome = DoSend(Activity, Peer);

	if (Outcome.IsWaiting())
	{
		// For now we'll not add the socket as a waiter. It is unlikely that we
		// send enough to need to wait currently.
		return;
	}

	if (Outcome.IsError())
	{
		Fail(State, Outcome.GetMessage().GetData());
		return;
	}

	Send = nullptr;

	// Pass along this send to be received
	if (Recv == nullptr)
	{
		Recv = Activity;
		return;
	}

	check(Recv->Next == nullptr);
	Recv->Next = Activity;
}

////////////////////////////////////////////////////////////////////////////////
bool FPeerGroup::Tick(FTickState& State)
{
	if (bNegotiating)
	{
		Negotiate(State);
	}

	else if (Send != nullptr)
	{
		SendInternal(State);
	}

	if (Recv != nullptr && State.RecvAllowance)
	{
		RecvInternal(State);
	}

	return !!IsKeepAlive | !!(UPTRINT(Send) | UPTRINT(Recv));
}

////////////////////////////////////////////////////////////////////////////////
void FPeerGroup::TickSend(FTickState& State, FHost& Host)
{
	// This path is only for those that are idle and have nothing to do
	if (Send != nullptr || Recv != nullptr)
	{
		return;
	}

	// Failing will try and recover work which we don't want to happen yet
	FActivity* Pending = State.Work->PopActivity();
	check(Pending != nullptr);

	// Close idle sockets
	if (Peer.IsValid() && LastUseMs + GIdleMs < State.NowMs)
	{
		LastUseMs = State.NowMs;
		Peer = FPeerType();
	}

	// We don't have a connected socket on first use, or if a keep-alive:close
	// was received from the server. So we connect here.
	bool bWillBlock = false;
	if (!Peer.IsValid())
	{
		IsKeepAlive = 1;
		bNegotiating = true;
		FOutcome Outcome = Host.Connect(Peer);

		// We failed to connect, let's bail.
		if (Outcome.IsError())
		{
			Pending->Next = Recv;
			Recv = Pending;
			Fail(State, Outcome.GetMessage().GetData());
			return;
		}

		bWillBlock = Outcome.IsWaiting();
	}

	Send = Pending;

	if (!bWillBlock)
	{
		if (bNegotiating)
		{
			return Negotiate(State);
		}
		return SendInternal(State);
	}

	// Non-blocking connect
	bWaiting = true;
}



// {{{1 host-group .............................................................

////////////////////////////////////////////////////////////////////////////////
class FHostGroup
{
public:
								FHostGroup(FHost& InHost);
	bool						IsBusy() const	{ return BusyCount != 0; }
	const FHost&				GetHost() const	{ return Host; }
	void						Tick(FTickState& State);
	void						AddActivity(FActivity* Activity);

private:
	int32						Wait(const FTickState& State);
	TArray<FPeerGroup>			PeerGroups;
	FWorkQueue					Work;
	FHost&						Host;
	uint32						BusyCount = 0;
	int32						WaitTimeAccum = 0;

	UE_NONCOPYABLE(FHostGroup);
};

////////////////////////////////////////////////////////////////////////////////
FHostGroup::FHostGroup(FHost& InHost)
: Host(InHost)
{
	uint32 Num = InHost.GetMaxConnections();
	PeerGroups.SetNum(Num);
}

////////////////////////////////////////////////////////////////////////////////
int32 FHostGroup::Wait(const FTickState& State)
{
	// Collect groups that are waiting on something
	TArray<FSocket::FWaiter, TFixedAllocator<64>> Waiters;
	for (FPeerGroup& Group : PeerGroups)
	{
		FSocket::FWaiter Waiter = Group.GetWaiter();
		if (Waiter.IsValid())
		{
			Waiters.Add(Waiter);
		}
	}

	if (Waiters.IsEmpty())
	{
		return 0;
	}

	Trace(0, ETrace::Wait);
	ON_SCOPE_EXIT { Trace(0, ETrace::Unwait); };

	// If the poll timeout is negative then treat that as a fatal timeout
	check(State.FailTimeoutMs);
	int32 PollTimeoutMs = State.PollTimeoutMs;
	if (PollTimeoutMs < 0)
	{
		PollTimeoutMs = State.FailTimeoutMs;
	}

	// Actually do the wait
	int32 Result = FSocket::Wait(Waiters, PollTimeoutMs);
	if (Result <= 0)
	{
		// If the user opts to not block then we don't accumulate wait time and
		// leave it to them to manage time a fail timoue
		WaitTimeAccum += PollTimeoutMs;

		if (State.PollTimeoutMs < 0 || WaitTimeAccum >= State.FailTimeoutMs)
		{
			return MIN_int32;
		}

		return Result;
	}

	WaitTimeAccum = 0;

	// For each waiter that's ready, find the associated group "unwait" them.
	int32 Count = 0;
	for (int32 i = 0, n = Waiters.Num(); i < n; ++i)
	{
		if (Waiters[i].Ready == 0)
		{
			continue;
		}

		auto* Candidate = (FSocket*)(Waiters[i].Candidate);
		auto Pred = [Candidate] (auto& Lhs) { return Lhs == Candidate; };
		FPeerGroup* Group = PeerGroups.FindByPredicate(Pred);
		check(Group != nullptr);
		Group->Unwait();

		Waiters.RemoveAtSwap(i, EAllowShrinking::No);
		--n, --i, ++Count;
	}
	check(Count == Result);

	return Result;
}

////////////////////////////////////////////////////////////////////////////////
void FHostGroup::Tick(FTickState& State)
{
	State.Work = &Work;

	if (BusyCount = Work.HasWork(); BusyCount)
	{
		Work.TickCancels(State);

		// Get available work out on idle sockets as soon as possible
		for (FPeerGroup& Group : PeerGroups)
		{
			if (!Work.HasWork())
			{
				break;
			}

			Group.TickSend(State, Host);
		}
	}

	// Wait on the groups that are
	if (int32 Result = Wait(State); Result < 0)
	{
		const char* Reason = (Result == MIN_int32)
			? "FailTimeout hit"
			: "poll() returned an unexpected error";

		for (FPeerGroup& Group : PeerGroups)
		{
			Group.Fail(State, Reason);
		}

		return;
	}

	// Tick everything, starting with groups that are maybe closest to finishing
	for (FPeerGroup& Group : PeerGroups)
	{
		BusyCount += (Group.Tick(State) == true);
	}
}

////////////////////////////////////////////////////////////////////////////////
void FHostGroup::AddActivity(FActivity* Activity)
{
	Work.AddActivity(Activity);
}



// {{{1 event-loop .............................................................

////////////////////////////////////////////////////////////////////////////////
static const FEventLoop::FRequestParams GDefaultParams;

////////////////////////////////////////////////////////////////////////////////
class FEventLoop::FImpl
{
public:
							~FImpl();
	uint32					Tick(int32 PollTimeoutMs=0);
	bool					IsIdle() const;
	void					Throttle(uint32 KiBPerSec);
	void					SetFailTimeout(int32 TimeoutMs);
	void					Cancel(FTicket Ticket);
	FRequest				Request(FAnsiStringView Method, FAnsiStringView Path, FActivity* Activity);
	FTicket					Send(FActivity* Activity);

private:
	void					ReceiveWork();
	FCriticalSection		Lock;
	std::atomic<uint64>		FreeSlots		= ~0ull;
	std::atomic<uint64>		Cancels			= 0;
	uint64					PrevFreeSlots	= ~0ull;
	FActivity*				Pending			= nullptr;
	FThrottler				Throttler;
	TArray<FHostGroup>		Groups;
	int32					FailTimeoutMs	= GIdleMs;
	uint32					BusyCount		= 0;
};

////////////////////////////////////////////////////////////////////////////////
FEventLoop::FImpl::~FImpl()
{
	check(BusyCount == 0);
}

////////////////////////////////////////////////////////////////////////////////
FRequest FEventLoop::FImpl::Request(
	FAnsiStringView Method,
	FAnsiStringView Path,
	FActivity* Activity)
{
	Trace(Activity, ETrace::ActivityCreate, this);

	if (Path.Len() == 0)
	{
		Path = "/";
	}

	Activity->NoContent = (Method == "HEAD");

	FMessageBuilder Builder(Activity->Buffer);

	Builder << Method << " " << Path << " HTTP/1.1" "\r\n"
		"Host: " << Activity->Host->GetHostName() << "\r\n";

	// HTTP/1.1 is persistent by default thus "Connection" header isn't required
	if (!Activity->IsKeepAlive)
	{
		Builder << "Connection: close\r\n";
	}

	FRequest Ret;
	Ret.Ptr = Activity;
	return Ret;
}

////////////////////////////////////////////////////////////////////////////////
FTicket FEventLoop::FImpl::Send(FActivity* Activity)
{
	Trace(Activity, ETrace::RequestBegin);

	FMessageBuilder(Activity->Buffer) << "\r\n";
	Activity_ChangeState(Activity, FActivity::EState::Send);

	uint64 Slot;
	{
		FScopeLock _(&Lock);

		for (;; FPlatformProcess::SleepNoStats(0.0f))
		{
			uint64 FreeSlotsLoad = FreeSlots.load(std::memory_order_relaxed);
			if (!FreeSlotsLoad)
			{
				// we don't handle oversubscription at the moment. Could return
				// activity to Reqeust and return a 0 ticket.
				check(false);
			}
			Slot = -int64(FreeSlotsLoad) & FreeSlotsLoad;
			if (FreeSlots.compare_exchange_weak(FreeSlotsLoad, FreeSlotsLoad - Slot, std::memory_order_relaxed))
			{
				break;
			}
		}
		Activity->Slot = int8(63 - FMath::CountLeadingZeros64(Slot));

		// This puts pending requests in reverse order of when they were made
		// but this will be undone when ReceiveWork() traverses the list
		Activity->Next = Pending;
		Pending = Activity;
	}

	return Slot;
}

////////////////////////////////////////////////////////////////////////////////
bool FEventLoop::FImpl::IsIdle() const
{
	return FreeSlots.load(std::memory_order_relaxed) == ~0ull;
}

////////////////////////////////////////////////////////////////////////////////
void FEventLoop::FImpl::Throttle(uint32 KiBPerSec)
{
	Throttler.SetLimit(KiBPerSec);
}

////////////////////////////////////////////////////////////////////////////////
void FEventLoop::FImpl::SetFailTimeout(int32 TimeoutMs)
{
	// While TimeoutMs must be >=0, it is signed so that MAX_uint32 can't be used
	check(TimeoutMs > 0);
	FailTimeoutMs = TimeoutMs;
}

////////////////////////////////////////////////////////////////////////////////
void FEventLoop::FImpl::Cancel(FTicket Ticket)
{
	Cancels.fetch_or(Ticket, std::memory_order_relaxed);
}

////////////////////////////////////////////////////////////////////////////////
void FEventLoop::FImpl::ReceiveWork()
{
	uint64 FreeSlotsLoad = FreeSlots.load(std::memory_order_relaxed);
	if (FreeSlots == PrevFreeSlots)
	{
		return;
	}
	PrevFreeSlots = FreeSlotsLoad;

	// Fetch the pending activities from out in the wild
	FActivity* Activity = nullptr;
	{
		FScopeLock _(&Lock);
		Swap(Activity, Pending);
	}

	// Pending is in the reverse of the order that requests were made
	FActivity* Reverse = nullptr;
	for (FActivity* Next; Activity != nullptr; Activity = Next)
	{
		Next = Activity->Next;
		Activity->Next = Reverse;
		Reverse = Activity;
	}
	Activity = Reverse;

	// Group activities by their host.
	for (FActivity* Next; Activity != nullptr; Activity = Next)
	{
		Next = Activity->Next;
		Activity->Next = nullptr;

		FHost& Host = *(Activity->Host);
		auto Pred = [&Host] (const FHostGroup& Lhs) { return &Lhs.GetHost() == &Host; };
		FHostGroup* Group = Groups.FindByPredicate(Pred);
		if (Group == nullptr)
		{
			Group = &(Groups.Emplace_GetRef(Host));
		}

		Group->AddActivity(Activity);
		++BusyCount;
	}
}

////////////////////////////////////////////////////////////////////////////////
uint32 FEventLoop::FImpl::Tick(int32 PollTimeoutMs)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(IasHttp::Tick);

	ReceiveWork();

	// We limit recv sizes as a way to control bandwidth use.
	int32 RecvAllowance = Throttler.GetAllowance();
	if (RecvAllowance <= 0)
	{
		if (PollTimeoutMs == 0)
		{
			return BusyCount;
		}

		int32 ThrottleWaitMs = -RecvAllowance;
		if (PollTimeoutMs > 0)
		{
			ThrottleWaitMs = FMath::Min(ThrottleWaitMs, PollTimeoutMs);
		}
		FPlatformProcess::SleepNoStats(float(ThrottleWaitMs) / 1000.0f);

		RecvAllowance = Throttler.GetAllowance();
		if (RecvAllowance <= 0)
		{
			return BusyCount;
		}
	}

	uint64 CancelsLoad = Cancels.load(std::memory_order_relaxed);

	uint32 NowMs;
	{
		// 4.2MM seconds will give us 50 days of uptime.
		static uint64 Freq = 0;
		static uint64 Base = 0;
		if (Freq == 0)
		{
			Freq = uint64(1.0 / FPlatformTime::GetSecondsPerCycle());
			Base = FPlatformTime::Cycles64();
		}
		uint64 NowBig = ((FPlatformTime::Cycles64() - Base) * 1000) / Freq;
		NowMs = uint32(NowBig);
		check(NowMs == NowBig);
	}

	// Tick groups and then remove ones that are idle
	FTickState TickState = {
		.DoneList = nullptr,
		.Cancels = CancelsLoad,
		.RecvAllowance = RecvAllowance,
		.PollTimeoutMs = PollTimeoutMs,
		.FailTimeoutMs = FailTimeoutMs,
		.NowMs = NowMs,
	};
	for (FHostGroup& Group : Groups)
	{
		Group.Tick(TickState);
	}

	for (uint32 i = 0, n = Groups.Num(); i < n; ++i)
	{
		FHostGroup& Group = Groups[i];
		if (Group.IsBusy())
		{
			continue;
		}

		Groups.RemoveAtSwap(i, EAllowShrinking::No);
		--n, --i;
	}

	Throttler.ReturnUnused(RecvAllowance);

	uint64 ReturnedSlots = 0;
	for (FActivity* Activity = TickState.DoneList; Activity != nullptr;)
	{
		FActivity* Next = Activity->Next;
		ReturnedSlots |= (1ull << Activity->Slot);
		Activity_Free(Activity);
		--BusyCount;
		Activity = Next;
	}

	uint32 BusyBias = 0;
	if (ReturnedSlots)
	{
		uint64 LatestFree = FreeSlots.fetch_add(ReturnedSlots, std::memory_order_relaxed);
		BusyBias += (LatestFree != PrevFreeSlots);
		PrevFreeSlots += ReturnedSlots;
	}

	if (CancelsLoad)
	{
		Cancels.fetch_and(~CancelsLoad, std::memory_order_relaxed);
	}

	return BusyCount + BusyBias;
}



////////////////////////////////////////////////////////////////////////////////
FEventLoop::FEventLoop()						{ Impl = new FEventLoop::FImpl(); Trace(Impl, ETrace::LoopCreate); }
FEventLoop::~FEventLoop()						{ Trace(Impl, ETrace::LoopDestroy); delete Impl; }
uint32 FEventLoop::Tick(int32 PollTimeoutMs)	{ return Impl->Tick(PollTimeoutMs); }
bool FEventLoop::IsIdle() const					{ return Impl->IsIdle(); }
void FEventLoop::Cancel(FTicket Ticket)			{ return Impl->Cancel(Ticket); }
void FEventLoop::Throttle(uint32 KiBPerSec)		{ return Impl->Throttle(KiBPerSec); }
void FEventLoop::SetFailTimeout(int32 Ms)		{ return Impl->SetFailTimeout(Ms); }

////////////////////////////////////////////////////////////////////////////////
FRequest FEventLoop::Request(
	FAnsiStringView Method,
	FAnsiStringView Url,
	const FRequestParams* Params)
{
	// Parse the URL into its components
	FUrlOffsets UrlOffsets;
	if (ParseUrl(Url, UrlOffsets) < 0)
	{
		return FRequest();
	}

	FAnsiStringView HostName = UrlOffsets.HostName.Get(Url);

	uint32 Port = 0;
	if (UrlOffsets.Port)
	{
		FAnsiStringView PortView = UrlOffsets.Port.Get(Url);
		Port = uint32(CrudeToInt(PortView));
	}

	FAnsiStringView Path;
	if (UrlOffsets.Path > 0)
	{
		Path = Url.Mid(UrlOffsets.Path);
	}

	// Create an activity and an emphemeral host
	Params = (Params != nullptr) ? Params : &GDefaultParams;

	FCertRootsRef VerifyCert = FCertRoots::NoTls();
	if (UrlOffsets.SchemeLength == 5)
	{
		if (VerifyCert = Params->VerifyCert; VerifyCert == ECertRootsRefType::None)
		{
			VerifyCert = FCertRoots::Default();
		}
		check(VerifyCert != ECertRootsRefType::None);
	}

	uint32 BufferSize = Params->BufferSize;
	BufferSize = (BufferSize >= 128) ? BufferSize : 128;
	BufferSize += sizeof(FHost) + HostName.Len();
	FActivity* Activity = Activity_Alloc(BufferSize);

	FBuffer& Buffer = Activity->Buffer;

	FHost* Host = Activity->Host = Buffer.Alloc<FHost>();
	Activity->IsKeepAlive = 0;
	Activity->bFollow30x = (Params->bAutoRedirect == true);
	Activity->bAllowChunked = (Params->bAllowChunked == true);

	uint32 HostNameLength = HostName.Len();
	char* HostNamePtr = Buffer.Alloc<char>(HostNameLength + 1);

	Buffer.Fix();

	memcpy(HostNamePtr, HostName.GetData(), HostNameLength);
	HostNamePtr[HostNameLength] = '\0';
	new (Host) FHost({
		.HostName	= HostNamePtr,
		.Port		= Port,
		.VerifyCert = VerifyCert,
	});

	return Impl->Request(Method, Path, Activity);
}

////////////////////////////////////////////////////////////////////////////////
FRequest FEventLoop::Request(
	FAnsiStringView Method,
	FAnsiStringView Path,
	FConnectionPool& Pool,
	const FRequestParams* Params)
{
	check(Pool.Ptr != nullptr);
	check(Params == nullptr || Params->VerifyCert == ECertRootsRefType::None); // add cert to FConPool instead

	Params = (Params != nullptr) ? Params : &GDefaultParams;

	uint32 BufferSize = Params->BufferSize;
	BufferSize = (BufferSize >= 128) ? BufferSize : 128;
	FActivity* Activity = Activity_Alloc(BufferSize);

	Activity->Host = Pool.Ptr;
	Activity->IsKeepAlive = 1;
	Activity->bFollow30x = (Params->bAutoRedirect == true);

	return Impl->Request(Method, Path, Activity);
}

////////////////////////////////////////////////////////////////////////////////
bool FEventLoop::Redirect(const FTicketStatus& Status, FTicketSink& OuterSink)
{
	const FResponse& Response = Status.GetResponse();

	switch (Response.GetStatusCode())
	{
		case 301:				// RedirectMoved
		case 302:				// RedirectFound
		case 307:				// RedirectTemp
		case 308: break;		// RedirectPerm
		default: return false;
	}

	FAnsiStringView Location = Response.GetHeader("Location");
	if (Location.IsEmpty())
	{
		// todo: turn source activity into an error?
		return false;
	}

	check(Response.GetContentLength() == 0); // should we ever hit this, we'll fix it

	const auto& Activity = (FActivity&)Response; // todo: yuk

	// Original method should remain unchanged
	const char* Data = Activity.Buffer.GetData();
	FAnsiStringView Method;
	for (uint32 i = 0; i < 5; ++i)
	{
		if (Data[i] <= ' ')
		{
			Method = FAnsiStringView(Data, i);
			break;
		}
	}
	check(!Method.IsEmpty());

	FRequest ForwardRequest;
	if (!Location.StartsWith("http://") && !Location.StartsWith("https://"))
	{
		if (Location[0] != '/')
		{
			return false;
		}

		FHost& Host = *(Activity.Host);

		TAnsiStringBuilder<256> Url;
		Url << ((Host.GetVerifyCert() != ECertRootsRefType::None) ? "https" : "http");
		Url << "://";
		Url << Host.GetHostName();
		Url << ":" << Host.GetPort();
		Url << Location;

		FRequestParams RequestParams = {
			.VerifyCert = Host.GetVerifyCert(),
		};
		new (&ForwardRequest) FRequest(Request(Method, Url, &RequestParams));
	}
	else
	{
		new (&ForwardRequest) FRequest(Request(Method, Location));
	}

	// Transfer original request headers
	check(Activity.State == FActivity::EState::RecvMessage);
	uint32 Length = 0;
	for (const char* End = Data + Activity.StateParam; Data + Length < End; ++Length)
	{
		if (Data[Length] != '\n')
		{
			continue;
		}

		Data = Data + Length + 1;
		Length = uint32(ptrdiff_t(End - Data));
		break;
	}

	FAnsiStringView OriginalHeaders(Data, Length);
	EnumerateHeaders(OriginalHeaders, [&ForwardRequest] (FAnsiStringView Name, FAnsiStringView Value)
	{
		if (Name != "Host" && Name != "Connection")
		{
			ForwardRequest.Header(Name, Value);
		}
		return true;
	});

	// Send the request
	Send(MoveTemp(ForwardRequest), MoveTemp(OuterSink), Status.GetParam());

	// todo: activity slots should be swapped so original slot matches ticket

	return true;
}

////////////////////////////////////////////////////////////////////////////////
FTicket FEventLoop::Send(FRequest&& Request, FTicketSink Sink, UPTRINT SinkParam)
{
	FActivity* Activity = nullptr;
	Swap(Activity, Request.Ptr);
	Activity->SinkParam = SinkParam;
	Activity->Sink = MoveTemp(Sink);

	// Intercept sink calls to catch 30x status codes and follow them
	if (Activity->bFollow30x)
	{
		auto RedirectSink = [
				this,
				OuterSink=MoveTemp(Activity->Sink)
			] (const FTicketStatus& Status) mutable
		{
			if (Status.GetId() == FTicketStatus::EId::Response)
			{
				if (Redirect(Status, OuterSink))
				{
					return;
				}
			}

			if (OuterSink)
			{
				return OuterSink(Status);
			}
		};
		Activity->Sink = RedirectSink;
	}

	return Impl->Send(Activity);
}

// }}}

} // namespace UE::IoStore::HTTP

