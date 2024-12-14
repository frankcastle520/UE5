// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class EventLoopUnitTests : TestModuleRules
{
    static EventLoopUnitTests()
    {
		TestMetadata = new Metadata();
        TestMetadata.TestName = "EventLoop";
        TestMetadata.TestShortName = "EventLoop";
    }

    public EventLoopUnitTests(ReadOnlyTargetRules Target) : base(Target)
	{
		OptimizeCode = CodeOptimization.Never;

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"EventLoop",
				"Sockets"
			});
	}
}