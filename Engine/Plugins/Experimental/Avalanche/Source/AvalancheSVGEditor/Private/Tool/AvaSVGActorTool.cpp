// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tool/AvaSVGActorTool.h"
#include "AvaInteractiveToolsSettings.h"
#include "Builders/AvaInteractiveToolsToolBuilder.h"
#include "Factories/SVGActorFactory.h"
#include "SVGActor.h"
#include "SVGImporterEditorCommands.h"

UAvaSVGActorTool::UAvaSVGActorTool()
{
	ActorClass = ASVGActor::StaticClass();
}

FName UAvaSVGActorTool::GetCategoryName()
{
	return IAvalancheInteractiveToolsModule::CategoryNameActor;
}

FAvaInteractiveToolsToolParameters UAvaSVGActorTool::GetToolParameters() const
{
	return {
		FSVGImporterEditorCommands::GetExternal().SpawnSVGActor,
		TEXT("SVG Actor Tool"),
		6000,
		FAvalancheInteractiveToolsCreateBuilder::CreateLambda(
			[](UEdMode* InEdMode)
			{
				return UAvaInteractiveToolsToolBuilder::CreateToolBuilder<UAvaSVGActorTool>(InEdMode);
			}),
		ActorClass,
		CreateActorFactory<USVGActorFactory>()
	};
}
