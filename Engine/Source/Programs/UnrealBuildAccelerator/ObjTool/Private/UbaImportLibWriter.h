// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaLogger.h"
#include "UbaStringBuffer.h"

namespace uba
{
	class ObjectFile;
	
	class ImportLibWriter
	{
	public:
		bool Write(Logger& logger, const Vector<ObjectFile*>& objFiles, const char* libName, const tchar* libFile);
	};
}
