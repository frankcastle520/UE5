// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Detail/Trace.h"

////////////////////////////////////////////////////////////////////////////////
#if TRACE_PRIVATE_MINIMAL_ENABLED
#	define UE_TRACE_IMPL(...)
#	define UE_TRACE_API			TRACELOG_API
#else
#	define UE_TRACE_IMPL(...)	{ return __VA_ARGS__; }
#	define UE_TRACE_API			inline
#endif

// msvc seems to have a strange behaviour when it comes to expanding macros.
#if defined(_MSC_VER)

#if TRACE_PRIVATE_FULL_ENABLED
#	define TRACE_IMPL(Macro, ...)		TRACE_PRIVATE_EXPAND(TRACE_PRIVATE_##Macro(__VA_ARGS__))
#else
#	define TRACE_IMPL(Macro, ...)		TRACE_PRIVATE_EXPAND(TRACE_PRIVATE_NOP_##Macro(__VA_ARGS__))
#endif

#if TRACE_PRIVATE_MINIMAL_ENABLED
#	define TRACE_IMPL_MINIMAL(Macro, ...)	TRACE_PRIVATE_EXPAND(TRACE_PRIVATE_##Macro(__VA_ARGS__))
#else
#	define TRACE_IMPL_MINIMAL(Macro, ...)	TRACE_PRIVATE_EXPAND(TRACE_PRIVATE_NOP_##Macro(__VA_ARGS__))
#endif

#else  // defined(_MSC_VER)

#if TRACE_PRIVATE_FULL_ENABLED
#	define TRACE_IMPL(Macro, ...)		TRACE_PRIVATE_EXPAND(TRACE_PRIVATE_##Macro)(__VA_ARGS__)
#else
#	define TRACE_IMPL(Macro, ...)		TRACE_PRIVATE_EXPAND(TRACE_PRIVATE_NOP_##Macro)(__VA_ARGS__)
#endif

#if TRACE_PRIVATE_MINIMAL_ENABLED
#	define TRACE_IMPL_MINIMAL(Macro, ...)	TRACE_PRIVATE_EXPAND(TRACE_PRIVATE_##Macro)(__VA_ARGS__)
#else
#	define TRACE_IMPL_MINIMAL(Macro, ...)	TRACE_PRIVATE_EXPAND(TRACE_PRIVATE_NOP_##Macro)(__VA_ARGS__)
#endif

#endif // defined(_MSC_VER)

////////////////////////////////////////////////////////////////////////////////
namespace UE {
namespace Trace {

// Field types
enum AnsiString {};
enum WideString {};

// Reference to a definition event.
template<typename IdType>	
struct TEventRef 
{
	using ReferenceType = IdType;

	TEventRef(IdType InId, uint32 InTypeId)
		: Id(InId)
		, RefTypeId(InTypeId)
	{
	}

	IdType Id;
	uint32 RefTypeId;

	uint64 GetHash() const;

private:
	TEventRef() = delete;
};

template <>
inline uint64 TEventRef<uint8>::GetHash() const
{
	return (uint64(RefTypeId) << 32) | Id;
}

template <>
inline uint64 TEventRef<uint16>::GetHash() const
{
	return (uint64(RefTypeId) << 32) | Id;
}

template <>
inline uint64 TEventRef<uint32>::GetHash() const
{
	return (uint64(RefTypeId) << 32) | Id;
}

template <>
inline uint64 TEventRef<uint64>::GetHash() const
{
	return (uint64(RefTypeId) << 32) ^ Id;
}

typedef TEventRef<uint8> FEventRef8;
typedef TEventRef<uint16> FEventRef16;	
typedef TEventRef<uint32> FEventRef32;
typedef TEventRef<uint64> FEventRef64;

template<typename IdType>
TEventRef<IdType> MakeEventRef(IdType InId, uint32 InTypeId)
{
	return TEventRef<IdType>(InId, InTypeId);
}

using OnConnectFunc = void(void);

enum class EMessageType : uint8
{
	Reserved			= 0,
	// Add to log
	Log,
	// For backwards compatibility
	Info = Log,
	// Display in console or similar
	Display,
	// Warnings to notify user
	WarningStart		= 0x04,
	// Errors are critical to the user, but application
	// can continue to run.
	ErrorStart			= 0x10,
	WriteError,
	ReadError,
	ConnectError,
	ListenError,
	EstablishError,
	FileOpenError,
	WriterError,
	// Fatal errors should cause application to stop
	FatalStart			= 0x40,
	OOMFatal,
};
	
struct FMessageEvent
{
	/** Type of message */
	EMessageType		Type;
	/** Type of message stringified */
	const char*			TypeStr;
	/** Clarifying message, may be null for some message types. Pointer only valide during callback. */
	const char*			Description;
};

using OnMessageFunc = void(const FMessageEvent&);

struct FInitializeDesc
{
	uint32			TailSizeBytes		= 4 << 20; // can be set to 0 to disable the tail buffer
	uint32			ThreadSleepTimeInMS = 0;
	bool			bUseWorkerThread	= true;
	bool			bUseImportantCache	= true;
	uint32			SessionGuid[4]		= {0,0,0,0}; // leave as zero to generate random
	OnConnectFunc*	OnConnectionFunc	= nullptr;
};

typedef uint32 FChannelId;

struct FChannelInfo
{
	const ANSICHAR* Name;
	const ANSICHAR* Desc;
	FChannelId Id;
	bool bIsEnabled;
	bool bIsReadOnly;
};

class FChannel;
	
typedef void*		AllocFunc(SIZE_T, uint32);
typedef void		FreeFunc(void*, SIZE_T);
typedef void		ChannelIterFunc(const ANSICHAR*, bool, void*);
/*  The callback provides information about a channel and a user provided pointer. 
	Returning false from the callback will stop the enumeration */
typedef bool		ChannelIterCallback(const FChannelInfo& Info, void*/*User*/);

struct FStatistics
{
	uint64		BytesSent		= 0;
	uint64		BytesTraced		= 0;
	uint64		MemoryUsed		= 0;
	uint32		CacheAllocated	= 0;	// Total memory allocated in cache buffers
	uint32		CacheUsed		= 0;	// Used cache memory; Important-marked events are stored in the cache.
	uint32		CacheWaste		= 0;	// Unused memory from retired cache buffers
};

struct FSendFlags
{
	static const uint16 None		= 0;
	static const uint16 ExcludeTail	= 1 << 0;	// do not send the tail of historical events
	static const uint16 _Reserved	= 1 << 15;	// this bit is used internally
};

UE_TRACE_API void	SetMemoryHooks(AllocFunc Alloc, FreeFunc Free) UE_TRACE_IMPL();
UE_TRACE_API void	SetMessageCallback(OnMessageFunc MessageFunc) UE_TRACE_IMPL();
UE_TRACE_API void	Initialize(const FInitializeDesc& Desc) UE_TRACE_IMPL();
UE_TRACE_API void	StartWorkerThread() UE_TRACE_IMPL();	
UE_TRACE_API void	Shutdown() UE_TRACE_IMPL();
UE_TRACE_API void	Panic() UE_TRACE_IMPL();
UE_TRACE_API void	Update() UE_TRACE_IMPL();
UE_TRACE_API void	GetStatistics(FStatistics& Out) UE_TRACE_IMPL();
UE_TRACE_API bool	SendTo(const TCHAR* Host, uint32 Port=0, uint16 Flags=FSendFlags::None) UE_TRACE_IMPL(false);
UE_TRACE_API bool	WriteTo(const TCHAR* Path, uint16 Flags=FSendFlags::None) UE_TRACE_IMPL(false);
UE_TRACE_API bool	WriteSnapshotTo(const TCHAR* Path) UE_TRACE_IMPL(false);
UE_TRACE_API bool	SendSnapshotTo(const TCHAR* Host, uint32 Port) UE_TRACE_IMPL(false);	
UE_TRACE_API bool	IsTracing() UE_TRACE_IMPL(false);
UE_TRACE_API bool	IsTracingTo(uint32 (&OutSessionGuid)[4], uint32 (&OutTraceGuid)[4]) UE_TRACE_IMPL(false);
UE_TRACE_API bool	Stop() UE_TRACE_IMPL(false);
UE_TRACE_API bool	IsChannel(const TCHAR* ChannelName) UE_TRACE_IMPL(false);
UE_TRACE_API bool	ToggleChannel(const TCHAR* ChannelName, bool bEnabled) UE_TRACE_IMPL(false);
UE_TRACE_API void	EnumerateChannels(ChannelIterFunc IterFunc, void* User) UE_TRACE_IMPL();
UE_TRACE_API void	EnumerateChannels(ChannelIterCallback IterFunc, void* User) UE_TRACE_IMPL();
UE_TRACE_API void	ThreadRegister(const TCHAR* Name, uint32 SystemId, int32 SortHint) UE_TRACE_IMPL();
UE_TRACE_API void	ThreadGroupBegin(const TCHAR* Name) UE_TRACE_IMPL();
UE_TRACE_API void	ThreadGroupEnd() UE_TRACE_IMPL();
	
UE_TRACE_API FChannel* FindChannel(const TCHAR* ChannelName) UE_TRACE_IMPL(nullptr);
UE_TRACE_API FChannel* FindChannel(FChannelId ChannelId) UE_TRACE_IMPL(nullptr);

} // namespace Trace
} // namespace UE


////////////////////////////////////////////////////////////////////////////////
/// Tracing macros
/// Use these to define event types, channel and emit events.
////////////////////////////////////////////////////////////////////////////////
#define UE_TRACE_EVENT_DEFINE(LoggerName, EventName)											TRACE_IMPL(EVENT_DEFINE, LoggerName, EventName)
#define UE_TRACE_EVENT_BEGIN(LoggerName, EventName, ...)										TRACE_IMPL(EVENT_BEGIN, LoggerName, EventName, ##__VA_ARGS__)
#define UE_TRACE_EVENT_BEGIN_EXTERN(LoggerName, EventName, ...)									TRACE_IMPL(EVENT_BEGIN_EXTERN, LoggerName, EventName, ##__VA_ARGS__)
#define UE_TRACE_EVENT_FIELD(FieldType, FieldName)												TRACE_IMPL(EVENT_FIELD, FieldType, FieldName)
#define UE_TRACE_EVENT_REFERENCE_FIELD(RefLogger, RefEvent, FieldName)							TRACE_IMPL(EVENT_REFFIELD, RefLogger, RefEvent, FieldName)
#define UE_TRACE_EVENT_END()																	TRACE_IMPL(EVENT_END)
#define UE_TRACE_LOG(LoggerName, EventName, ChannelsExpr, ...)									TRACE_IMPL(LOG, LoggerName, EventName, ChannelsExpr, ##__VA_ARGS__)
#define UE_TRACE_LOG_SCOPED(LoggerName, EventName, ChannelsExpr, ...)							TRACE_IMPL(LOG_SCOPED, LoggerName, EventName, ChannelsExpr, ##__VA_ARGS__)
#define UE_TRACE_LOG_SCOPED_CONDITIONAL(LoggerName, EventName, ChannelsExpr, Condition, ...)	TRACE_IMPL(LOG_SCOPED_CONDITIONAL, LoggerName, EventName, ChannelsExpr, Condition, ##__VA_ARGS__)
#define UE_TRACE_LOG_SCOPED_T(LoggerName, EventName, ChannelsExpr, ...)							TRACE_IMPL(LOG_SCOPED_T, LoggerName, EventName, ChannelsExpr, ##__VA_ARGS__)
#define UE_TRACE_LOG_SCOPED_T_CONDITIONAL(LoggerName, EventName, ChannelsExpr, Condition, ...)	TRACE_IMPL(LOG_SCOPED_T_CONDITIONAL, LoggerName, EventName, ChannelsExpr, Condition, ##__VA_ARGS__)
#define UE_TRACE_GET_DEFINITION_TYPE_ID(LoggerName, EventName)									TRACE_IMPL(GET_DEFINITION_TYPE_ID, LoggerName, EventName)
#define UE_TRACE_LOG_DEFINITION(LoggerName, EventName, Id, ChannelsExpr, ...)					TRACE_IMPL(LOG_DEFINITION, LoggerName, EventName, Id, ChannelsExpr, ##__VA_ARGS__)
#define UE_TRACE_CHANNEL(ChannelName, ...)														TRACE_IMPL(CHANNEL, ChannelName, ##__VA_ARGS__)
#define UE_TRACE_CHANNEL_EXTERN(ChannelName, ...)												TRACE_IMPL(CHANNEL_EXTERN, ChannelName, ##__VA_ARGS__)
#define UE_TRACE_CHANNEL_DEFINE(ChannelName, ...)												TRACE_IMPL(CHANNEL_DEFINE, ChannelName, ##__VA_ARGS__)
#define UE_TRACE_CHANNELEXPR_IS_ENABLED(ChannelsExpr)											TRACE_IMPL(CHANNELEXPR_IS_ENABLED, ChannelsExpr)


////////////////////////////////////////////////////////////////////////////////
/// Shipping variants of the macros.
/// With these macros users can provide a subset of events that are available
/// both in development and in shipping configurations (provided UE_TRACE_MINIMAL_ENABLED is set).
////////////////////////////////////////////////////////////////////////////////
#define UE_TRACE_MINIMAL_EVENT_DEFINE(LoggerName, EventName)											TRACE_IMPL_MINIMAL(EVENT_DEFINE, LoggerName, EventName)
#define UE_TRACE_MINIMAL_EVENT_BEGIN(LoggerName, EventName, ...)										TRACE_IMPL_MINIMAL(EVENT_BEGIN, LoggerName, EventName, ##__VA_ARGS__)
#define UE_TRACE_MINIMAL_EVENT_BEGIN_EXTERN(LoggerName, EventName, ...)									TRACE_IMPL_MINIMAL(EVENT_BEGIN_EXTERN, LoggerName, EventName, ##__VA_ARGS__)
#define UE_TRACE_MINIMAL_EVENT_FIELD(FieldType, FieldName)												TRACE_IMPL_MINIMAL(EVENT_FIELD, FieldType, FieldName)
#define UE_TRACE_MINIMAL_EVENT_REFERENCE_FIELD(RefLogger, RefEvent, FieldName)							TRACE_IMPL_MINIMAL(EVENT_REFFIELD, RefLogger, RefEvent, FieldName)
#define UE_TRACE_MINIMAL_EVENT_END()																	TRACE_IMPL_MINIMAL(EVENT_END)
#define UE_TRACE_MINIMAL_LOG(LoggerName, EventName, ChannelsExpr, ...)									TRACE_IMPL_MINIMAL(LOG, LoggerName, EventName, ChannelsExpr, ##__VA_ARGS__)
#define UE_TRACE_MINIMAL_LOG_SCOPED(LoggerName, EventName, ChannelsExpr, ...)							TRACE_IMPL_MINIMAL(LOG_SCOPED, LoggerName, EventName, ChannelsExpr, ##__VA_ARGS__)
#define UE_TRACE_MINIMAL_LOG_SCOPED_CONDITIONAL(LoggerName, EventName, ChannelsExpr, Condition, ...)	TRACE_IMPL(LOG_SCOPED_CONDITIONAL, LoggerName, EventName, ChannelsExpr, Condition, ##__VA_ARGS__)
#define UE_TRACE_MINIMAL_LOG_SCOPED_T(LoggerName, EventName, ChannelsExpr, ...)							TRACE_IMPL_MINIMAL(LOG_SCOPED_T, LoggerName, EventName, ChannelsExpr, ##__VA_ARGS__)
#define UE_TRACE_MINIMAL_LOG_SCOPED_T_CONDITIONAL(LoggerName, EventName, ChannelsExpr, Condition, ...)	TRACE_IMPL(LOG_SCOPED_T_CONDITIONAL, LoggerName, EventName, ChannelsExpr, Condition, ##__VA_ARGS__)
#define UE_TRACE_MINIMAL_GET_DEFINITION_TYPE_ID(LoggerName, EventName)									TRACE_IMPL_MINIMAL(GET_DEFINITION_TYPE_ID, LoggerName, EventName)
#define UE_TRACE_MINIMAL_LOG_DEFINITION(LoggerName, EventName, Id, ChannelsExpr, ...)					TRACE_IMPL_MINIMAL(LOG_DEFINITION, LoggerName, EventName, Id, ChannelsExpr, ##__VA_ARGS__)
#define UE_TRACE_MINIMAL_CHANNEL(ChannelName, ...)														TRACE_IMPL_MINIMAL(CHANNEL, ChannelName, ##__VA_ARGS__)
#define UE_TRACE_MINIMAL_CHANNEL_EXTERN(ChannelName, ...)												TRACE_IMPL_MINIMAL(CHANNEL_EXTERN, ChannelName, ##__VA_ARGS__)
#define UE_TRACE_MINIMAL_CHANNEL_DEFINE(ChannelName, ...)												TRACE_IMPL_MINIMAL(CHANNEL_DEFINE, ChannelName, ##__VA_ARGS__)
#define UE_TRACE_MINIMAL_CHANNELEXPR_IS_ENABLED(ChannelsExpr)											TRACE_IMPL_MINIMAL(CHANNELEXPR_IS_ENABLED, ChannelsExpr)
