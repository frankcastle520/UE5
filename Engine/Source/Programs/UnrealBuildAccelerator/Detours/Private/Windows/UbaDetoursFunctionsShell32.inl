// Copyright Epic Games, Inc. All Rights Reserved.


//typedef struct _MODLOAD_DATA {} *PMODLOAD_DATA;
using SHGetKnownFolderPathFunc = HRESULT(REFKNOWNFOLDERID rfid, DWORD dwFlags, HANDLE hToken, PWSTR* ppszPath);
SHGetKnownFolderPathFunc* True_SHGetKnownFolderPath;

BOOL Detoured_SHGetKnownFolderPath(REFKNOWNFOLDERID rfid, DWORD dwFlags, HANDLE hToken, PWSTR* ppszPath)
{
	if (g_runningRemote)
	{
		UBA_ASSERT(hToken == NULL);
		TimerScope ts(g_stats.getFullFileName);
		SCOPED_WRITE_LOCK(g_communicationLock, pcs);
		BinaryWriter writer;
		writer.WriteByte(MessageType_SHGetKnownFolderPath);
		writer.WriteBytes(&rfid, sizeof(KNOWNFOLDERID));
		writer.WriteU32(dwFlags);
		writer.Flush();
		BinaryReader reader;
		HRESULT res = reader.ReadU32();
		*ppszPath = NULL;
		if (res == S_OK)
		{
			StringBuffer<> path;
			reader.ReadString(path);
			u32 memSize = (path.count+1)*2;
			void* mem = CoTaskMemAlloc(memSize);
			memcpy(mem, path.data, memSize);
			*ppszPath = (PWSTR)mem;
		}
		DEBUG_LOG_DETOURED(L"SHGetKnownFolderPath", L"(%ls) -> %ls", *ppszPath, ToString(res == S_OK));
		return res;
	}

	SuppressDetourScope _;
	HRESULT res = True_SHGetKnownFolderPath(rfid, dwFlags, hToken, ppszPath);
	DEBUG_LOG_TRUE(L"SHGetKnownFolderPath", L"(%ls) -> %ls", *ppszPath, ToString(res == S_OK));
	return res;
}
