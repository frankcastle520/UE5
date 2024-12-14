// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubmitToolApp.h"
#include "UnixCommonStartup.h"

int RunSubmitToolWrapper(const TCHAR* Commandline)
{
    return RunSubmitTool(Commandline, FGuid());
}

int main(int argc, char *argv[])
{
	return CommonUnixMain(argc, argv, &RunSubmitToolWrapper);
}
