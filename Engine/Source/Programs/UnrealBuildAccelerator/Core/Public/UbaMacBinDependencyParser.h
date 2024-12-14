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
		int fd = TRUE_WRAPPER(open)(fileName, O_RDONLY);
		if (fd == -1)
		{
			outError.Appendf("Open failed for file %s", fileName);
			return false;
		}
		auto closeFileHandle = MakeGuard([&]() { TRUE_WRAPPER(close)(fd); });
		struct stat sb;
		if (TRUE_WRAPPER(fstat)(fd, &sb) == -1)
		{
			outError.Appendf("Stat failed for file %s", fileName);
			return false;
		}
		u32 size = Min(u32(sb.st_size), 8048u);

		void* mem = TRUE_WRAPPER(mmap)(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (mem == MAP_FAILED)
		{
			outError.Appendf("Mmap failed for file %s", fileName);
			return false;
		}
		auto unmap = MakeGuard([&]() { TRUE_WRAPPER(munmap)(mem, size); });


		const char* libs[1024];
		u32 libsCount = 0;

		const char* loaderPaths[256];
		u32 loaderPathsCount = 0;

		auto it = (const u8*)mem;
		auto end = it + size - 14;
		while (it != end)
		{
			if (*it++ != '@')
				continue;

			if (memcmp(it, "rpath/", 6) == 0)
			{
				it += 6;
				auto importFile = (const char*)it;
				if (!strstr(importFile, ".dylib"))
				{
					outError.Appendf("Found @rpath in binary %s that did not end with .dylib (%s)", fileName, importFile);
					return false;
				}

				libs[libsCount++] = importFile;
			}
			else if (memcmp(it, "executable_path/", 16) == 0)
			{
				it += 16;
				auto executablePath = (const char*)it;
				UBA_ASSERT(*executablePath == 0);
			}
			else if (memcmp(it, "loader_path/", 12) == 0)
			{
				it += 12;
				loaderPaths[loaderPathsCount++] = (const char*)it;
			}
		}

		loaderPaths[loaderPathsCount] = nullptr;

		for (u32 i=0; i!=libsCount; ++i)
			func(libs[i], false, loaderPaths);
		return true;
	}
}