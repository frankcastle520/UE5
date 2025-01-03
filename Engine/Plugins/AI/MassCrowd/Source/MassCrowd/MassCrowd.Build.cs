// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MassCrowd: ModuleRules
	{
		public MassCrowd(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"AIModule",
					"MassEntity",
					"Core",
					"CoreUObject",
					"Engine",
					"MassActors",
					"MassCommon",
					"MassLOD",
					"MassMovement",
					"MassNavigation",
					"MassZoneGraphNavigation",
					"MassReplication",
					"MassAIReplication",
					"MassSignals",
					"MassSimulation",
					"MassSpawner",
					"MassRepresentation",
					"MassAIBehavior",
					"NetCore",
					"StateTreeModule",
					"ZoneGraph",
					"ZoneGraphAnnotations",
					"ZoneGraphDebug"
				}
			);

			SetupIrisSupport(Target);
		}
	}
}