// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Config.h"
#include "Trace/Trace.h"

#if TRACE_PRIVATE_MINIMAL_ENABLED

#include "CoreTypes.h"

namespace UE {
namespace Trace {


typedef bool ChannelIterCallback(const FChannelInfo& OutChannelInfo, void*);

/*
	A named channel which can be used to filter trace events. Channels can be
	combined using the '|' operator which allows expressions like

	```
	UE_TRACE_LOG(FooWriter, FooEvent, FooChannel|BarChannel);
	```

	Note that this works as an AND operator, similar to how a bitmask is constructed.

	Channels are by default enabled until FChannel::Initialize() is called. This is to allow
	events to be emitted during static initialization. In fact all events during
	this phase are always emitted.
*/
class FChannel
{
public:
	struct Iter
	{
						~Iter();
		const FChannel*	GetNext();
		void*			Inner[3];
	};

	struct InitArgs
	{
		const ANSICHAR*		Desc;		// User facing description string
		bool				bReadOnly;	// If set, channel cannot be changed during a run, only set through command line.
	};

	TRACELOG_API void	Setup(const ANSICHAR* InChannelName, const InitArgs& Args);
	TRACELOG_API static void Initialize();
	static Iter			ReadNew();
	void				Announce() const;
	static bool			Toggle(const ANSICHAR* ChannelName, bool bEnabled);
	static void			ToggleAll(bool bEnabled);
	static void			PanicDisableAll(); // Disabled channels wont be logged with UE_TRACE_LOG
	static FChannel* FindChannel(const ANSICHAR* ChannelName);
	static FChannel* FindChannel(FChannelId ChannelId);
	static void			EnumerateChannels(ChannelIterCallback Func, void* User);
	TRACELOG_API bool	Toggle(bool bEnabled);
	bool	IsEnabled() const;
	bool	IsReadOnly() const { return Args.bReadOnly; };
	uint32	GetName(const ANSICHAR** OutName) const;
	explicit			operator bool () const;
	bool				operator | (const FChannel& Rhs) const;

private:
	FChannel*			Next;
	struct
	{
		const ANSICHAR*	Ptr;
		uint32			Len;
		uint32			Hash;
	}					Name;
	volatile int32		Enabled;
	InitArgs			Args;
};

inline uint32 FChannel::GetName(const ANSICHAR** OutName) const
{
	if (OutName != nullptr)
	{
		*OutName = Name.Ptr;
		return Name.Len;
	}
	return 0;
}


} // namespace Trace
} // namespace UE

#else

// Since we use this type in macros we need
// provide an empty definition when trace is
// not enabled.
namespace UE::Trace { class FChannel {}; }

#endif // TRACE_PRIVATE_MINIMAL_ENABLED
