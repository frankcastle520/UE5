// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Engine/DirectionalLight.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersion.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "LevelEditorViewport.h"
#include "EditorModeManager.h"
#include "EditorModes.h"
#include "LandscapeEditorObject.h"
#include "LandscapeInfoMap.h"

#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#include "LandscapeEditorDetailCustomization_NewLandscape.h"
#include "LandscapeEditorDetailCustomization_ImportExport.h"

#if WITH_DEV_AUTOMATION_TESTS

#define LOCTEXT_NAMESPACE "LandscapeEditor.NewLandscape"

DEFINE_LOG_CATEGORY_STATIC(LogLandscapeAutomationTests, Log, All);

/**
* Landscape test helper functions
*/
namespace LandscapeTestUtils
{
	/**
	* Finds the viewport to use for the landscape tool
	*/
	static FLevelEditorViewportClient* FindSelectedViewport()
	{
		FLevelEditorViewportClient* SelectedViewport = NULL;

		for(FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
		{
			if (!ViewportClient->IsOrtho())
			{
				SelectedViewport = ViewportClient;
			}
		}

		return SelectedViewport;
	}

	struct LandscapeTestCommands
	{
		static void Import(const FString& HeightMapFilenamee);
		static bool CreateNewMapWithLandscape(int32 ComponentCountXY, int32 QuadsPerComponent);
	};

	void LandscapeTestCommands::Import(const FString& HeightMapFilename)
	{
		//Switch to the Landscape tool
		GLevelEditorModeTools().ActivateMode(FBuiltinEditorModes::EM_Landscape);
		FEdModeLandscape* LandscapeEdMode = (FEdModeLandscape*)GLevelEditorModeTools().GetActiveMode(FBuiltinEditorModes::EM_Landscape);

		TSharedPtr<FLandscapeEditorDetailCustomization_ImportExport> Customization_ImportExportLandscape = MakeShareable(new FLandscapeEditorDetailCustomization_ImportExport);

		LandscapeEdMode->UISettings->ImportLandscape_HeightmapFilename = HeightMapFilename;
		LandscapeEdMode->UISettings->bHeightmapSelected = true;
		LandscapeEdMode->UISettings->ImportExportMode = ELandscapeImportExportMode::All;
		LandscapeEdMode->UISettings->ImportType = ELandscapeImportTransformType::Resample;

		Customization_ImportExportLandscape->OnOriginResetButtonClicked();

		Customization_ImportExportLandscape->OnImportExportButtonClicked();
	}

	bool LandscapeTestCommands::CreateNewMapWithLandscape(int32 ComponentCountXY, int32 QuadsPerComponent)
	{
		UE_LOG(LogLandscapeAutomationTests, Display, TEXT("Creating a new map..."));
		UWorld* NewMap = FAutomationEditorCommonUtils::CreateNewMap();
		if (!NewMap)
		{
			UE_LOG(LogLandscapeAutomationTests, Error, TEXT("Unable to create new map"));
			return false;
		}

		UE_LOG(LogLandscapeAutomationTests, Display, TEXT("Switching to Landscape Editor Mode..."));
		GLevelEditorModeTools().ActivateMode(FBuiltinEditorModes::EM_Landscape);
		FEdModeLandscape* LandscapeEditMode = (FEdModeLandscape*)GLevelEditorModeTools().GetActiveMode(FBuiltinEditorModes::EM_Landscape);
		if (!LandscapeEditMode)
		{
			UE_LOG(LogLandscapeAutomationTests, Error, TEXT("Unable to enter Landscape Edit Mode"));
			return false;
		}

		UE_LOG(LogLandscapeAutomationTests, Display, TEXT("Creating a new landscape..."));
		LandscapeEditMode->UISettings->NewLandscape_QuadsPerSection = QuadsPerComponent;
		LandscapeEditMode->UISettings->NewLandscape_ComponentCount.X = ComponentCountXY;
		LandscapeEditMode->UISettings->NewLandscape_ComponentCount.Y = ComponentCountXY;
		LandscapeEditMode->UISettings->NewLandscape_ClampSize();

		TSharedPtr<FLandscapeEditorDetailCustomization_NewLandscape> Customization_NewLandscape = MakeShareable(new FLandscapeEditorDetailCustomization_NewLandscape);
		Customization_NewLandscape->OnCreateButtonClicked();

		if (!LandscapeEditMode->GetLandscape())
		{
			UE_LOG(LogLandscapeAutomationTests, Error, TEXT("Unable to create a new landscape"));
			return false;
		}

		return true;
	}
}

/**
* Latent command to create a new landscape
*/
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FCreateNewMapWithLandscapeCommand, int32, ComponentCountXY, int32, QuadsPerComponent);
bool FCreateNewMapWithLandscapeCommand::Update()
{
	LandscapeTestUtils::LandscapeTestCommands::CreateNewMapWithLandscape(ComponentCountXY, QuadsPerComponent);
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FImportLandscapeCommand, FString, HeightMapFilename);
bool FImportLandscapeCommand::Update()
{
	LandscapeTestUtils::LandscapeTestCommands::Import(HeightMapFilename);
	return true;
}


DEFINE_LATENT_AUTOMATION_COMMAND(FAddDirectionalLight);
bool FAddDirectionalLight::Update()
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	
	ADirectionalLight*  DirectionalLight = World->SpawnActor<ADirectionalLight>();

	return true;
}

/**
* Latent command to start using the sculpting tool
*/
DEFINE_LATENT_AUTOMATION_COMMAND(FBeginModifyLandscapeCommand);
bool FBeginModifyLandscapeCommand::Update()
{
	//Find the landscape
	FEdModeLandscape* LandscapeEdMode = (FEdModeLandscape*)GLevelEditorModeTools().GetActiveMode(FBuiltinEditorModes::EM_Landscape);

	//Find a location on the edge of the landscape along the x axis so the default camera can see it in the distance.
	FVector LandscapeSizePerComponent = LandscapeEdMode->UISettings->NewLandscape_QuadsPerSection * LandscapeEdMode->UISettings->NewLandscape_SectionsPerComponent * LandscapeEdMode->UISettings->NewLandscape_Scale;
	FVector TargetLoctaion(0);
	TargetLoctaion.X = -LandscapeSizePerComponent.X * (LandscapeEdMode->UISettings->NewLandscape_ComponentCount.X / 2.f);

	ALandscapeProxy* Proxy = LandscapeEdMode->CurrentToolTarget.LandscapeInfo.Get()->GetCurrentLevelLandscapeProxy(true);
	if (Proxy)
	{
		TargetLoctaion = Proxy->LandscapeActorToWorld().InverseTransformPosition(TargetLoctaion);
	}

	//Begin using the sculpting tool
	FLevelEditorViewportClient* SelectedViewport = LandscapeTestUtils::FindSelectedViewport();
	LandscapeEdMode->CurrentTool->BeginTool(SelectedViewport, LandscapeEdMode->CurrentToolTarget, TargetLoctaion);
	SelectedViewport->Invalidate();

	UE_LOG(LogLandscapeAutomationTests, Display, TEXT("Modified the landscape using the sculpt tool"));

	return true;
}

/**
*  Latent command stop using the sculpting tool
*/
DEFINE_LATENT_AUTOMATION_COMMAND(FEndModifyLandscapeCommand);
bool FEndModifyLandscapeCommand::Update()
{
	//Find the landscape
	FEdModeLandscape* LandscapeEdMode = (FEdModeLandscape*)GLevelEditorModeTools().GetActiveMode(FBuiltinEditorModes::EM_Landscape);

	//End using the sculpting tool
	FLevelEditorViewportClient* SelectedViewport = LandscapeTestUtils::FindSelectedViewport();
	LandscapeEdMode->CurrentTool->EndTool(SelectedViewport);
	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND(FCheckHeight);
bool FCheckHeight::Update()
{
	FAutomationTestBase* CurrentTest = FAutomationTestFramework::Get().GetCurrentTest();
	if (!CurrentTest)
	{
		return true;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return true;
	}

	
	for (const auto& InfoMapPair : ULandscapeInfoMap::GetLandscapeInfoMap(World).Map)
	{
		TOptional<float> Height = InfoMapPair.Value->GetLandscapeProxy()->GetHeightAtLocation(FVector(0, 0, 0), EHeightfieldSource::Editor);
		CurrentTest->TestEqual("Has Height Value at 0,0", Height.IsSet(), true);
		CurrentTest->TestNearlyEqual("Height Value at 0,0 is 0", Height.GetValue(), 0.0f, 1e-4f);
		break;
	}
	return true;
}

/**
* Landscape creation / edit test
*/
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandscapeEditorTest, "Editor.Landscape.Create and Modify New Landscape", EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter);
bool FLandscapeEditorTest::RunTest(const FString& Parameters)
{
	// Create a new map and landscape
	ADD_LATENT_AUTOMATION_COMMAND(FCreateNewMapWithLandscapeCommand(/* NumComponents = */ 8, /* QuadsPerComponent = */ 7));

	//For some reason the heightmap component takes a few ticks to register with the nav system.  We crash if we try to modify the heightmap before then.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));
	ADD_LATENT_AUTOMATION_COMMAND(FBeginModifyLandscapeCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FEndModifyLandscapeCommand());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandscapeEditorTestFooBar, "Editor.Landscape.Import Landscape", EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter);
bool FLandscapeEditorTestFooBar::RunTest(const FString& Parameters)
{
	// Create a new map and landscape
	ADD_LATENT_AUTOMATION_COMMAND(FCreateNewMapWithLandscapeCommand(/* NumComponents = */ 8, /* QuadsPerComponent = */ 7));

	//For some reason the heightmap component takes a few ticks to register with the nav system.  We crash if we try to modify the heightmap before then.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));

	FString HeightMapFilename = FPaths::Combine(FPaths::EngineContentDir(), FString(TEXT("FunctionalTesting\\height-505-flat.png")));
	ADD_LATENT_AUTOMATION_COMMAND(FImportLandscapeCommand(HeightMapFilename));

	ADD_LATENT_AUTOMATION_COMMAND(FAddDirectionalLight());

	ADD_LATENT_AUTOMATION_COMMAND(FCheckHeight());

	return true;
}

/*
 * Verify that we can create a new edit layer for a landscape
 * See: TR = 290778158
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandscapeEditorCreateNewLayer, "Editor.Landscape.Create New Edit Layer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter);
bool FLandscapeEditorCreateNewLayer::RunTest(const FString& Parameters)
{
	// Create a new map with landscape
	LandscapeTestUtils::LandscapeTestCommands::CreateNewMapWithLandscape(8, 63);

	UE_LOG(LogLandscapeAutomationTests, Display, TEXT("Verifying that new landscapes start with 1 layer..."));
	FEdModeLandscape* LandscapeEditMode = (FEdModeLandscape*)GLevelEditorModeTools().GetActiveMode(FBuiltinEditorModes::EM_Landscape);
	ALandscape* CurrentLandscape = LandscapeEditMode->GetLandscape();
	TestNotNull("Current landscape should not be null", CurrentLandscape);
	TestEqual("New landscapes should start with one edit layer", LandscapeEditMode->GetLayerCount(), 1);

	UE_LOG(LogLandscapeAutomationTests, Display, TEXT("Verifying that we successfully added a new layer..."));
	FName LayerName = TEXT("TestLayer");
	CurrentLandscape->CreateLayer(LayerName);
	TestEqual("The landscape should now have a new edit layer", LandscapeEditMode->GetLayerCount(), 2);
	TestNotNull("There should be a new layer with the specified name", CurrentLandscape->GetLayerConst(LayerName));

	return true;
}

#undef LOCTEXT_NAMESPACE

#endif //WITH_DEV_AUTOMATION_TESTS
