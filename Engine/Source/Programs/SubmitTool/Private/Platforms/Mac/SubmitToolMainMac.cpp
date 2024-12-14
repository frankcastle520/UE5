// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubmitToolApp.h"
#include "Mac/MacProgramDelegate.h"
#include "LaunchEngineLoop.h"

int RunSubmitToolWrapper(const TCHAR* Commandline)
{
	return RunSubmitTool(Commandline, FGuid());
}

int main(int argc, char *argv[])
{
	[MacProgramDelegate mainWithArgc:argc argv:argv programMain:RunSubmitToolWrapper programExit:FEngineLoop::AppExit];
}
