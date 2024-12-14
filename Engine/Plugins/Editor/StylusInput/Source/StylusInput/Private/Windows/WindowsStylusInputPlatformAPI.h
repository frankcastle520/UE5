// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <HAL/Platform.h>

#if PLATFORM_WINDOWS

#include <Containers/Array.h>

#include <Windows/AllowWindowsPlatformTypes.h>
	#include <windef.h>
	#include <wtypes.h>
	#include <Unknwn.h>
#include <Windows/HideWindowsPlatformTypes.h>

namespace UE::StylusInput::Private::Windows
{
	class FWindowsStylusInputPlatformAPI
	{
	public:
		static const FWindowsStylusInputPlatformAPI& GetInstance();

		bool SatisfiesRequirements() const;

		using FFuncGetClientRect = BOOL(HWND, LPRECT);
		FFuncGetClientRect* GetClientRect = nullptr;

		using FFuncGetDC = HDC(HWND);
		FFuncGetDC* GetDC = nullptr;

		using FFuncReleaseDC = int(HWND, HDC);
		FFuncReleaseDC* ReleaseDC = nullptr;

		using FFuncGetDeviceCaps = int(HDC, int);
		FFuncGetDeviceCaps* GetDeviceCaps = nullptr;

		using FFuncCoCreateInstance = HRESULT(REFCLSID, IUnknown*, DWORD, REFIID, LPVOID);
		FFuncCoCreateInstance* CoCreateInstance = nullptr;

		using FFuncCoCreateFreeThreadedMarshaler = HRESULT(LPUNKNOWN, LPUNKNOWN*);
		FFuncCoCreateFreeThreadedMarshaler* CoCreateFreeThreadedMarshaler = nullptr;

		using FFuncCoTaskMemFree = void(LPVOID);
		FFuncCoTaskMemFree* CoTaskMemFree = nullptr;

		using FFuncStringFromGUID2 = int(REFGUID, LPOLESTR, int);
		FFuncStringFromGUID2* StringFromGUID2 = nullptr;

		using FFuncSysFreeString = void(BSTR);
		FFuncSysFreeString* SysFreeString = nullptr;

		using FFuncVariantClear = HRESULT(VARIANTARG*);
		FFuncVariantClear* VariantClear = nullptr;

		using FFuncVariantInit = void(VARIANTARG*);
		FFuncVariantInit* VariantInit = nullptr;

	private:

		FWindowsStylusInputPlatformAPI();
		~FWindowsStylusInputPlatformAPI();

		TArray<void*> DllHandles;

		bool bInitializedComLibrary = false;
		bool bHasRTSComDllHandle = false;
		bool bHasInkObjDllHandle = false;
		
	};
}

#endif
