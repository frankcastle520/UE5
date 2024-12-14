// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaHash.h"
#include "UbaLogger.h"

namespace uba
{
	static constexpr u8 RootPathsVersion = 1;

	class RootPaths
	{
	public:

		bool RegisterRoot(Logger& logger, const tchar* rootPath, bool includeInKey = true, u8 id = 0);
		bool RegisterSystemRoots(Logger& logger, u8 startId = 0);

		struct Root
		{
			TString path;
			StringKey shortestPathKey;
			u8 index;
			bool includeInKey;
		};

		const Root* FindRoot(const StringBufferBase& path) const;
		const TString& GetRoot(u32 index) const;

		template<typename CharType, typename Func>
		bool NormalizeString(Logger& logger, const CharType* str, u64 strLen, const Func& func, const tchar* hint, const tchar* hint2 = TC("")) const;

		CasKey NormalizeAndHashFile(Logger& logger, const tchar* filename) const;

		static constexpr u8 RootStartByte = ' ';

	private:
		bool InternalRegisterRoot(Logger& logger, const tchar* rootPath, bool includeInKey, u8 index);

		Vector<Root> m_roots;
		u32 m_shortestRoot = 0;
		u32 m_longestRoot = 0;
	};



	template<typename CharType, typename Func>
	bool RootPaths::NormalizeString(Logger& logger, const CharType* str, u64 strLen, const Func& func, const tchar* hint, const tchar* hint2) const
	{
		auto strEnd = str + strLen;
		auto searchPos = str;

		u32 destPos = 0;

		while (true)
		{
			auto absPathChars = searchPos;
			CharType lastChar = 0;
			while (absPathChars < strEnd && !(lastChar == ':' && *absPathChars == PathSeparator))
			{
				lastChar = *absPathChars;
				++absPathChars;
			}
		
			if (absPathChars == strEnd)
			{
				func(searchPos, strEnd - searchPos, ~0u);
				return true;
			}

			auto pathStart = absPathChars - 2;

			auto pathEndOrMore = pathStart;
			while (pathEndOrMore < strEnd && *pathEndOrMore != '\n')
				++pathEndOrMore;

			u32 lenOrMore = u32(pathEndOrMore - pathStart);
			u32 toCopy = Min(lenOrMore, m_longestRoot);
			StringBuffer<512> path;
			path.Append(pathStart, toCopy);

			auto root = FindRoot(path);
			if (!root)
			{
				if (auto lastQuote = path.Last('\"'))
					path.Resize(lastQuote - path.data);
				if (auto lineEnd = path.Last('\r'))
					path.Resize(lineEnd - path.data);
				logger.Info(TC("PATH WITHOUT ROOT: %s (inside %s%s at offset %u)"), path.data, hint, hint2, destPos);
				return false;
			}

			if (u32 len = u32(pathStart - searchPos))
			{
				destPos += len;
				func(searchPos, len, ~0u);
			}
			CharType temp = RootStartByte + CharType(root->index);
			func(&temp, 1, destPos);
			destPos += 1;

			searchPos = pathStart + root->path.size();
		}
	}

	inline bool IsNormalized(const CasKey& key)
	{
		UBA_ASSERT(key != CasKeyZero);
		return (((u8*)&key)[19] & 2) == 2;
	}

	inline CasKey AsNormalized(const CasKey& key, bool normalized)
	{
		UBA_ASSERT(key != CasKeyZero);
		CasKey newKey = key;
		u8 flagField = ((u8*)&key)[19];
		((u8*)&newKey)[19] = normalized ? (flagField | 2) : (flagField & ~2);
		return newKey;
	}
}
