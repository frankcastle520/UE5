// Copyright Epic Games, Inc. All Rights Reserved.

#include "StylusInput.h"

#include "StylusInputInterface.h"
#include "StylusInputUtils.h"

#if PLATFORM_WINDOWS
#include <Windows/WindowsStylusInputInterface.h>
#endif

namespace UE::StylusInput
{
	namespace Private
	{
		class FStylusInputImpl;

		class FNotSupportedStylusInputInterface final : public IStylusInputInterface
		{
		public:
			virtual ~FNotSupportedStylusInputInterface() override = default;

			virtual IStylusInputInstance* CreateInstance(SWindow& Window) override { return nullptr; }

			virtual bool ReleaseInstance(IStylusInputInstance* Instance) override { return false; }

		private:
			friend FStylusInputImpl;
			static TUniquePtr<IStylusInputInterface> Create()
			{
				UE_LOG(LogStylusInput, Warning, TEXT("Stylus input is not supported for this platform."));
				return nullptr;
			}
		};

		class FStylusInputImpl
		{
		public:
			FStylusInputImpl()
				: Interface(FPlatformInterface::Create())
			{
			}

			IStylusInputInstance* CreateInstance(SWindow& Window) const
			{
				if (!Interface)
				{
					UE_LOG(LogStylusInput, Error, TEXT("Platform interface not available."));
					return nullptr;
				}

				return Interface ? Interface->CreateInstance(Window) : nullptr;
			}

			bool ReleaseInstance(IStylusInputInstance* Instance) const
			{
				return Interface ? Interface->ReleaseInstance(Instance) : false;
			}

		private:
			using FPlatformInterface =
#if PLATFORM_WINDOWS
				Windows::FWindowsStylusInputInterface
#else
				FNotSupportedStylusInputInterface
#endif
				;

			TUniquePtr<IStylusInputInterface> Interface;
		};
	}

	Private::FStylusInputImpl& GetImplSingleton()
	{
		static Private::FStylusInputImpl ImplSingleton;
		return ImplSingleton;
	}

	IStylusInputInstance* CreateInstance(SWindow& Window)
	{
		return GetImplSingleton().CreateInstance(Window);
	}

	bool ReleaseInstance(IStylusInputInstance* Instance)
	{
		if (!Instance)
		{
			UE_LOG(Private::LogStylusInput, Warning, TEXT("Nullptr passed into FStylusInput::ReleaseInstance()."));
			return false;
		}

		return GetImplSingleton().ReleaseInstance(Instance);
	}
}
