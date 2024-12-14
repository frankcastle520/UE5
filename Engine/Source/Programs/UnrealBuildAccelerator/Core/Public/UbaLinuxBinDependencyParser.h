// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaStringBuffer.h"

#if !defined(UBA_IS_DETOURED_INCLUDE)
#define TRUE_WRAPPER(func) func
#endif

namespace uba
{
	inline bool IsKnownSystemFile(const tchar* fileName)
	{
		return false;
	}

	inline bool FindImports(const tchar* fileName, const Function<void(const tchar* import, bool isKnown, const char* const* loaderPaths)>& func, StringBufferBase& outError)
	{
		return true;
	}
}