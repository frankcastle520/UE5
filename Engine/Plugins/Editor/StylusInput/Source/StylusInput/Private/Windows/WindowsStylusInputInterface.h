// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <HAL/Platform.h>

#if PLATFORM_WINDOWS

#include <StylusInputInterface.h>
#include <Containers/Array.h>
#include <Containers/Map.h>
#include <Templates/UniquePtr.h>

namespace UE::StylusInput::Private
{
	class FStylusInputImpl;
}

namespace UE::StylusInput::Private::Windows
{
	class FWindowsStylusInputInstance;

	class FWindowsStylusInputInterface : public IStylusInputInterface
	{
	public:
		virtual IStylusInputInstance* CreateInstance(SWindow& Window) override;
		virtual bool ReleaseInstance(IStylusInputInstance* Instance) override;

	private:
		friend FStylusInputImpl;

		static TUniquePtr<IStylusInputInterface> Create();

		struct FRefCountedInstance
		{
			TUniquePtr<FWindowsStylusInputInstance> Instance;
			int32 RefCount;
		};

		TMap<SWindow*, FRefCountedInstance> Instances;
	};
}

#endif
