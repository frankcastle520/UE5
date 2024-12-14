// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// {{{1 platforms ..............................................................

#if PLATFORM_MICROSOFT
#	if !defined(NO_UE_INCLUDES)
#	include "Windows/AllowWindowsPlatformTypes.h"
#		include <winsock2.h>
#		include <ws2tcpip.h>
#	include "Windows/HideWindowsPlatformTypes.h"
#	else
#		include <winsock2.h>
#		include <ws2tcpip.h>
#		pragma comment(lib, "Ws2_32.lib")
#	endif // NO_UE_INCLUDES

	// Winsock defines "PF_MAX" indicating the protocol family count. This
	// however competes with UE definitions related to pixel formats.
	#if defined(PF_MAX)
	#	undef PF_MAX
	#endif

	namespace UE::IoStore::HTTP
	{
		using SocketType	= SOCKET;
		using MsgFlagType	= int;

		enum { SHUT_RDWR = SD_BOTH };

#	define IAS_HTTP_USE_POLL
		template <typename... ArgTypes> auto poll(ArgTypes... Args)
		{
			return WSAPoll(Forward<ArgTypes>(Args)...);
		}
	} // namespace UE::IoStore::HTTP
#elif PLATFORM_APPLE | PLATFORM_UNIX | PLATFORM_ANDROID
#	include <arpa/inet.h>
#	include <fcntl.h>
#	include <netdb.h>
#	include <netinet/tcp.h>
#	include <poll.h>
#	include <sys/select.h>
#	include <sys/socket.h>
#	include <unistd.h>
	namespace UE::IoStore::HTTP
	{
		using SocketType	= int;
		using MsgFlagType	= int;
		static int32 closesocket(int32 Socket) { return close(Socket); }
	}
#	define IAS_HTTP_USE_POLL
#else
#	include "CoreHttp/Http.inl"
#endif

// }}}

namespace UE::IoStore::HTTP
{

// {{{1 socket .................................................................

////////////////////////////////////////////////////////////////////////////////
static const SocketType InvalidSocket = ~SocketType(0);

static_assert(sizeof(sockaddr_in::sin_addr) == sizeof(uint32));

#if PLATFORM_MICROSOFT
	static int32 LastSocketResult() { return WSAGetLastError(); }
#	define IsSocketResult(err)		(LastSocketResult() == WSA##err)
#else
	static int32 LastSocketResult() { return errno; }
#	define IsSocketResult(err)		(LastSocketResult() == err)
	static_assert(EWOULDBLOCK == EAGAIN);
#endif

////////////////////////////////////////////////////////////////////////////////
class FSocket
{
public:
	struct FWaiter
	{
		enum class EWhat { Send = 0b01, Recv = 0b10, Both = Send|Recv };
				FWaiter() { std::memset(this, 0, sizeof(*this)); }
				FWaiter(const FSocket& Socket, EWhat InWaitOn);
		bool	IsValid() const { UPTRINT x{0}; return std::memcmp(this, &x, sizeof(*this)) != 0; }
		bool	operator == (FSocket& Rhs) const { return UPTRINT(&Rhs) == Candidate; }
		UPTRINT	Candidate : 60;
		UPTRINT	WaitOn : 2;
		UPTRINT	Ready : 2;
	};

				FSocket() = default;
				~FSocket()					{ Destroy(); }
				FSocket(FSocket&& Rhs)		{ Move(MoveTemp(Rhs)); }
	FSocket&	operator = (FSocket&& Rhs)	{ Move(MoveTemp(Rhs)); return *this; }
	bool		IsValid() const				{ return Socket != InvalidSocket; }
	bool		Create();
	void		Destroy();
	FOutcome	Connect(uint32 Ip, uint32 Port);
	void		Disconnect();
	FOutcome	Send(const char* Data, uint32 Size);
	FOutcome	Recv(char* Dest, uint32 Size);
	bool		SetBlocking(bool bBlocking);
	bool		SetSendBufSize(int32 Size);
	bool		SetRecvBufSize(int32 Size);
	static int	Wait(TArrayView<FWaiter> Waiters, int32 TimeoutMs);

private:
	void		Move(FSocket&& Rhs);
	SocketType	Socket = InvalidSocket;

				FSocket(const FSocket&) = delete;
	FSocket&	operator = (const FSocket&) = delete;
};

////////////////////////////////////////////////////////////////////////////////
FSocket::FWaiter::FWaiter(const FSocket& Socket, EWhat InWaitOn)
: Candidate(UPTRINT(&Socket))
, WaitOn(UPTRINT(InWaitOn))
, Ready(0)
{
	static_assert(sizeof(*this) == sizeof(void*));
}

////////////////////////////////////////////////////////////////////////////////
void FSocket::Move(FSocket&& Rhs)
{
	check(!IsValid() || !Rhs.IsValid()); // currently we only want to pass one around
	Swap(Socket, Rhs.Socket);
}

////////////////////////////////////////////////////////////////////////////////
bool FSocket::Create()
{
	check(!IsValid());
	Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (!IsValid())
	{
		return false;
	}

	int32 Yes = 1;
	setsockopt(Socket, IPPROTO_TCP, TCP_NODELAY, &(char&)(Yes), sizeof(Yes));

	Trace(Socket, ETrace::SocketCreate);
	return true;
}

////////////////////////////////////////////////////////////////////////////////
void FSocket::Destroy()
{
	if (Socket == InvalidSocket)
	{
		return;
	}

	Trace(Socket, ETrace::SocketDestroy);

	closesocket(Socket);
	Socket = InvalidSocket;
}

////////////////////////////////////////////////////////////////////////////////
FOutcome FSocket::Connect(uint32 IpAddress, uint32 Port)
{
	check(IsValid());

	Trace(Socket, ETrace::Connect, IpAddress);

	IpAddress = htonl(IpAddress);

	sockaddr_in AddrInet = { sizeof(sockaddr_in) };
	AddrInet.sin_family = AF_INET;
	AddrInet.sin_port = htons(uint16(Port));
	memcpy(&(AddrInet.sin_addr), &IpAddress, sizeof(IpAddress));

	int32 Result = connect(Socket, &(sockaddr&)AddrInet, sizeof(AddrInet));

	if (IsSocketResult(EWOULDBLOCK) | IsSocketResult(EINPROGRESS))
	{
		return FOutcome::Waiting();
	}

	if (Result < 0)
	{
		return FOutcome::Error("Socket connect failed", Result);
	}

	return FOutcome::Ok();
}

////////////////////////////////////////////////////////////////////////////////
void FSocket::Disconnect()
{
	check(IsValid());
	shutdown(Socket, SHUT_RDWR);
}

////////////////////////////////////////////////////////////////////////////////
FOutcome FSocket::Send(const char* Data, uint32 Size)
{
	Trace(Socket, ETrace::Send, -1);
	int32 Result = send(Socket, Data, Size, MsgFlagType(0));
	Trace(Socket, ETrace::Send, FMath::Max(Result, 0));

	if (Result > 0)						return FOutcome::Ok(Result);
	if (Result == 0)					return FOutcome::Error("Send ATH0");
	if (IsSocketResult(EWOULDBLOCK))	return FOutcome::Waiting();

	if (IsSocketResult(ENOTCONN))
	{
		int32 Error = 0;
		socklen_t ErrorSize = sizeof(Error);
		Result = getsockopt(Socket, SOL_SOCKET, SO_ERROR, (char*)&Error, &ErrorSize);
		if (Result < 0 || Error != 0)
		{
			return FOutcome::Error("Error while connecting");
		}

		return FOutcome::Waiting();
	}

	Result = LastSocketResult();
	return FOutcome::Error("Send", Result);
}

////////////////////////////////////////////////////////////////////////////////
FOutcome FSocket::Recv(char* Dest, uint32 Size)
{
	Trace(Socket, ETrace::Recv, -1);
	int32 Result = recv(Socket, Dest, Size, MsgFlagType(0));
	Trace(Socket, ETrace::Recv, FMath::Max(0, Result));

	if (Result > 0)						return FOutcome::Ok(Result);
	if (Result == 0)					return FOutcome::Error("Recv ATH0");
	if (IsSocketResult(EWOULDBLOCK))	return FOutcome::Waiting();

	Result = LastSocketResult();
	return FOutcome::Error("Recv", Result);
}

////////////////////////////////////////////////////////////////////////////////
bool FSocket::SetBlocking(bool bBlocking)
{
#if defined(IAS_HTTP_HAS_NONBLOCK_IMPL)
	return SetNonBlockingSocket(Socket, !bBlocking);
#elif PLATFORM_MICROSOFT
	unsigned long NonBlockingMode = (bBlocking != true);
	return (ioctlsocket(Socket, FIONBIO, &NonBlockingMode) != SOCKET_ERROR);
#else
	int32 Flags = fcntl(Socket, F_GETFL, 0);
	if (Flags == -1)
	{
		return false;
	}

	int32 NewFlags = bBlocking
		? (Flags & ~int32(O_NONBLOCK))
		: (Flags |	int32(O_NONBLOCK));
	return (Flags == NewFlags) || (fcntl(Socket, F_SETFL, Flags) >= 0);
#endif
}

////////////////////////////////////////////////////////////////////////////////
bool FSocket::SetSendBufSize(int32 Size)
{
	return 0 == setsockopt(Socket, SOL_SOCKET, SO_SNDBUF, &(char&)Size, sizeof(Size));
}

////////////////////////////////////////////////////////////////////////////////
bool FSocket::SetRecvBufSize(int32 Size)
{
	return 0 == setsockopt(Socket, SOL_SOCKET, SO_RCVBUF, &(char&)Size, sizeof(Size));
}

////////////////////////////////////////////////////////////////////////////////
int32 FSocket::Wait(TArrayView<FWaiter> Waiters, int32 TimeoutMs)
{
#if !defined(IAS_HTTP_USE_POLL)
	struct FSelect
	{
		SocketType	fd;
		int32		events;
		int32		revents;
	};
	static const int32 POLLIN  = 1 << 0;
	static const int32 POLLOUT = 1 << 1;
	static const int32 POLLERR = 1 << 2;
	static const int32 POLLHUP = POLLERR;
	static const int32 POLLNVAL= POLLERR;
#else
	using FSelect = pollfd;
#endif

	// The following looks odd because POLLFD varies subtly from one platform
	// to the next. To cleanly set members to zero and to not get narrowing
	// warnings from the compiler, we list-init and don't assume POD types.
	using PollEventType = decltype(FSelect::events);
	PollEventType Events[] = { POLLERR, POLLOUT, POLLIN, POLLOUT|POLLOUT };
	TArray<FSelect, TFixedAllocator<64>> Selects;
	for (FWaiter& Waiter : Waiters)
	{
		Selects.Emplace_GetRef() = {
			((FSocket*)Waiter.Candidate)->Socket,
			Events[Waiter.WaitOn],
			/* 0 */
		};
	}

	// Poll the sockets
#if defined(IAS_HTTP_USE_POLL)
	int32 Result = poll(Selects.GetData(), Selects.Num(), TimeoutMs);
	if (Result <= 0)
	{
		return Result;
	}
#else
	timeval TimeVal = {};
	timeval* TimeValPtr = (TimeoutMs >= 0 ) ? &TimeVal : nullptr;
	if (TimeoutMs > 0)
	{
		TimeVal = { TimeoutMs >> 10, TimeoutMs & ((1 << 10) - 1) };
	}

	fd_set FdSetRead;	FD_ZERO(&FdSetRead);
	fd_set FdSetWrite;	FD_ZERO(&FdSetWrite);
	fd_set FdSetExcept; FD_ZERO(&FdSetExcept);

	SocketType MaxFd = 0;
	for (FSelect& Select : Selects)
	{
		fd_set* RwSet = (Select.events & POLLIN) ? &FdSetRead : &FdSetWrite;
		FD_SET(Select.fd, RwSet);
		FD_SET(Select.fd, &FdSetExcept);
		MaxFd = FMath::Max(Select.fd, MaxFd);
	}

	int32 Result = select(int32(MaxFd + 1), &FdSetRead, &FdSetWrite, &FdSetExcept, TimeValPtr);
	if (Result <= 0)
	{
		return Result;
	}

	for (FSelect& Select : Selects)
	{
		if (FD_ISSET(Select.fd, &FdSetExcept))
		{
			Select.revents = POLLERR;
			continue;
		}

		fd_set* RwSet = (Select.events & POLLIN) ? &FdSetRead : &FdSetWrite;
		if (FD_ISSET(Select.fd, RwSet))
		{
			Select.revents = Select.events;
		}
	}
#endif // IAS_HTTP_USE_POLL

	// Transfer poll results to the input sockets. We don't transfer across error
	// states. Subsequent sockets ops can take care of that instead.
	static const auto TestBits = POLLIN|POLLOUT|POLLERR|POLLHUP|POLLNVAL;
	for (uint32 i = 0, n = Waiters.Num(); i < n; ++i)
	{
		auto RetEvents = Selects[i].revents;
		if (!(RetEvents & TestBits))
		{
			continue;
		}

		uint32 Value = 0;
		if (!!(RetEvents & POLLOUT)) Value |= uint32(FSocket::FWaiter::EWhat::Send);
		if (!!(RetEvents & POLLIN))	 Value |= uint32(FSocket::FWaiter::EWhat::Recv);
		Waiters[i].Ready = Value ? Value : uint32(FSocket::FWaiter::EWhat::Both);
	}

	return Result;
}



// }}}

} // namespace UE::IoStore::HTTP
