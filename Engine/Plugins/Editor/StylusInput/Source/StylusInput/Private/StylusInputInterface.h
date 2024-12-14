// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class SWindow;

namespace UE::StylusInput
{
	class IStylusInputEventHandler;
	class IStylusInputInstance;
}

namespace UE::StylusInput::Private
{
	class IStylusInputInterface
	{
	public:
		virtual ~IStylusInputInterface() = default;

		virtual IStylusInputInstance* CreateInstance(SWindow& Window) = 0;
		virtual bool ReleaseInstance(IStylusInputInstance* Instance) = 0;
	};
}
