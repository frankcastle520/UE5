// Copyright Epic Games, Inc. All Rights Reserved.

#include "WindowsStylusInputInterface.h"

#if PLATFORM_WINDOWS

#include <StylusInput.h>
#include <StylusInputUtils.h>

#include <GenericPlatform/GenericWindow.h>
#include <Templates/SharedPointer.h>
#include <Widgets/SWindow.h>

#include <Windows/AllowWindowsPlatformTypes.h>
	#include <RTSCOM_i.c>
#include <Windows/HideWindowsPlatformTypes.h>

#include "WindowsStylusInputInstance.h"
#include "WindowsStylusInputPlatformAPI.h"

namespace UE::StylusInput::Private::Windows
{
	IStylusInputInstance* FWindowsStylusInputInterface::CreateInstance(SWindow& Window)
	{
		if (FRefCountedInstance* ExistingRefCountedInstance = Instances.Find(&Window))
		{
			++ExistingRefCountedInstance->RefCount;
			return ExistingRefCountedInstance->Instance.Get();
		}

		HWND OSWindowHandle = [&Window]
		{
			const TSharedPtr<const FGenericWindow> NativeWindow = Window.GetNativeWindow();
			return NativeWindow.IsValid() ? static_cast<HWND>(NativeWindow->GetOSWindowHandle()) : nullptr;
		}();

		if (!OSWindowHandle)
		{
			LogError("WindowsStylusInputInterface", "Could not get native window handle.");
			return nullptr;
		}

		FWindowsStylusInputInstance* NewInstance = Instances.Emplace(&Window, {MakeUnique<FWindowsStylusInputInstance>(OSWindowHandle), 1}).Instance.Get();
		if (!ensureMsgf(NewInstance, TEXT("WindowsStylusInputInterface: Failed to create stylus input instance.")))
		{
			Instances.Remove(&Window);
			return nullptr;
		}

		return NewInstance;
	}

	bool FWindowsStylusInputInterface::ReleaseInstance(IStylusInputInstance* Instance)
	{
		check(Instance);

		FWindowsStylusInputInstance *const WindowsInstance = static_cast<FWindowsStylusInputInstance*>(Instance);

		// Find existing instance
		for (TTuple<SWindow*, FRefCountedInstance>& Entry : Instances)
		{
			FRefCountedInstance& RefCountedInstance = Entry.Get<1>();

			if (RefCountedInstance.Instance.Get() == WindowsInstance)
			{
				// Decrease reference count
				check(RefCountedInstance.RefCount > 0);
				if (--RefCountedInstance.RefCount == 0)
				{
					// Delete if there are no references left
					Instances.Remove(Entry.Key);
				}
					
				return true;
			}
		}

		ensureMsgf(false, TEXT("WindowsStylusInputInterface: Failed to find provied instance."));
		return false;
	}

	TUniquePtr<IStylusInputInterface> FWindowsStylusInputInterface::Create()
	{
		if (FWindowsStylusInputPlatformAPI::GetInstance().SatisfiesRequirements())
		{
			TUniquePtr<FWindowsStylusInputInterface> WindowsImpl = MakeUnique<FWindowsStylusInputInterface>();
			return TUniquePtr<IStylusInputInterface>(MoveTemp(WindowsImpl));
		}

		return nullptr;
	}
}

#endif
