// Copyright Epic Games, Inc. All Rights Reserved.

#include "WindowsStylusInputUtils.h"

#if PLATFORM_WINDOWS

#include <Windows/AllowWindowsPlatformAtomics.h>
	#include <comdef.h>
#include <Windows/HideWindowsPlatformAtomics.h>

#include <StylusInputUtils.h>

namespace UE::StylusInput::Private::Windows
{
	FString GetCOMErrorMessage(const HRESULT Result)
	{
		const _com_error COMError(Result);
		const LPCTSTR ErrorMessage = COMError.ErrorMessage();
		return ErrorMessage;
	}

	void LogCOMError(const FString& Preamble, const HRESULT Result)
	{
		LogError(Preamble, FString::Format(TEXT("COM Interface Error: {0}"), {GetCOMErrorMessage(Result)}));
	}
}

#endif
