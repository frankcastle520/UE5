// Copyright Epic Games, Inc. All Rights Reserved.

#include "UbaRootPaths.h"
#include "UbaFileAccessor.h"

#if PLATFORM_WINDOWS
#include <shlobj_core.h>
#endif

namespace uba
{
	bool RootPaths::RegisterRoot(Logger& logger, const tchar* rootPath, bool includeInKey, u8 id)
	{
		// Register rootPath both with single path separators and double path separators on windows because text files store them with double path separators
		#if PLATFORM_WINDOWS
		StringBuffer<> doubleSlash;
		StringBuffer<> spaceEscaped;
		bool hasSpace = false;
		for (const tchar* it=rootPath; *it; ++it)
		{
			if (*it == ' ')
			{
				hasSpace = true;
				spaceEscaped.Append('\\');
			}
			spaceEscaped.Append(*it);
			doubleSlash.Append(*it);
			if (*it == PathSeparator)
				doubleSlash.Append(PathSeparator);
		}

		const tchar* rootPaths[] = { rootPath, doubleSlash.data, hasSpace ? spaceEscaped.data : TC("") };
		#else
		const tchar* rootPaths[] = { rootPath };
		#endif

		for (const tchar* rp : rootPaths)
		{
			u8 index = id;
			if (!InternalRegisterRoot(logger, rp, includeInKey, index))
				return false;
			if (id)
				++id;
		}
		return true;
	}

	bool RootPaths::RegisterSystemRoots(Logger& logger, u8 startId)
	{
#if PLATFORM_WINDOWS

		static StringBuffer<64> systemDir;
		static StringBuffer<64> programW6432;
		static StringBuffer<64> programFiles86;
		static StringBuffer<64> programData;

		static bool init = []()
			{
				systemDir.count = GetSystemDirectory(systemDir.data, systemDir.capacity);
				systemDir.EnsureEndsWithSlash();
				programW6432.count = GetEnvironmentVariable(TC("ProgramW6432"), programW6432.data, programW6432.capacity);
				programW6432.EnsureEndsWithSlash();
				programFiles86.count = GetEnvironmentVariable(TC("ProgramFiles(x86)"), programFiles86.data, programFiles86.capacity);
				programFiles86.EnsureEndsWithSlash();

				PWSTR path;
				if (!SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, NULL, &path)))
					return false;
				programData.Append(path).EnsureEndsWithSlash();
				CoTaskMemFree(path);
				return true;
			}();

		if (!init)
			return false;

		u8 id = startId;
		auto GetId = [&]() { u8 res = id; if (id) id += 2; return res; };
		RegisterRoot(logger, systemDir.data, false, GetId()); // Ignore files from here.. we do expect them not to affect the output of a process
		RegisterRoot(logger, programW6432.data, true, GetId());
		RegisterRoot(logger, programFiles86.data, true, GetId());
		RegisterRoot(logger, programData.data, true, GetId());

#else
		// no system roots
#endif
		return true;
	}

	const RootPaths::Root* RootPaths::FindRoot(const StringBufferBase& path) const
	{
		if (path.count < m_shortestRoot)
			return nullptr;

		StringBuffer<MaxPath> shortPath;
		shortPath.Append(path.data, m_shortestRoot);
		if (CaseInsensitiveFs)
			shortPath.MakeLower();

		StringKey key = ToStringKeyNoCheck(shortPath.data, m_shortestRoot);
		for (u32 i=0, e=u32(m_roots.size()); i!=e; ++i)
		{
			auto& root = m_roots[i];
			if (key != root.shortestPathKey)
				continue;
			if (!path.StartsWith(root.path.c_str()))
				continue;
			return &m_roots[i];
		}
		return nullptr;
	}

	static TString& EmptyString = *new TString(); // Need to leak to prevent shutdown hangs when running in managed process

	const TString& RootPaths::GetRoot(u32 index) const
	{
		if (m_roots.size() <= index)
			return EmptyString;
		return m_roots[index].path;
	}

	CasKey RootPaths::NormalizeAndHashFile(Logger& logger, const tchar* filename) const
	{
		FileAccessor file(logger, filename);
		if (!file.OpenMemoryRead())
			return CasKeyZero;

		bool wasNormalized = false;
		CasKeyHasher hasher;
		auto hashString = [&](const char* str, u64 strLen, u32 rootPos)
			{
				wasNormalized |= rootPos != ~0u;
				hasher.Update(str, strLen);
			};
		if (!NormalizeString<char>(logger, (const char*)file.GetData(), file.GetSize(), hashString, filename))
			return CasKeyZero;

		return AsNormalized(ToCasKey(hasher, false), wasNormalized);
	}

	bool RootPaths::InternalRegisterRoot(Logger& logger, const tchar* rootPath, bool includeInKey, u8 index)
	{
		if (index == 0)
			index = u8(m_roots.size());
		if (index == '~' - ' ') // This is not really true.. as long as value is under 256 we're good
			return logger.Error(TC("Too many roots added (%u)"), index);

		if (index >= m_roots.size())
			m_roots.resize(index+1);

		if (!*rootPath)
			return true;

		auto& root = m_roots[index];
		if (!root.path.empty())
			return logger.Error(TC("Root at index %u already added (existing as %s, added as %s)"), index, root.path.c_str(), rootPath);

		root.index = index;
		root.path = rootPath;

		if (CaseInsensitiveFs)
			ToLower(root.path.data());

		root.includeInKey = includeInKey;

		m_longestRoot = Max(u32(root.path.size()), m_longestRoot);

		if (!m_shortestRoot || root.path.size() < m_shortestRoot)
		{
			m_shortestRoot = u32(root.path.size());
			for (auto& r : m_roots)
				r.shortestPathKey = ToStringKeyNoCheck(r.path.data(), m_shortestRoot);
		}
		else
			root.shortestPathKey = ToStringKeyNoCheck(root.path.data(), m_shortestRoot);
		return true;
	}
}