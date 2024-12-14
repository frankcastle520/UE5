// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaConfig.h"

namespace uba
{
	bool TestConfig(Logger& logger, const StringBufferBase& rootDir)
	{
		static const char* configText =
			"RootDir = \"e:\\foo\"\r\n"
			"[CacheClient]\r\n"
			"UseDirectoryPreparsing = true\r\n"
			"# Comment = true\r\n"
			"";

		Config config;
		if (!config.LoadFromText(logger, configText, strlen(configText)))
			return false;

		const ConfigTable* tablePtr = config.GetTable(TC("CacheClient"));
		if (!tablePtr)
			return false;
		const ConfigTable& table = *tablePtr;
		bool test = false;
		if (!table.GetValueAsBool(test, TC("UseDirectoryPreparsing")))
			return false;
		if (test != true)
			return false;
		const tchar* str = nullptr;
		if (!table.GetValueAsString(str, TC("RootDir")))
			return false;
		if (TStrcmp(str, TC("e:\\foo")) != 0)
			return false;
		if (table.GetValueAsBool(test, TC("Comment")))
			return false;
		return true;
	}
}