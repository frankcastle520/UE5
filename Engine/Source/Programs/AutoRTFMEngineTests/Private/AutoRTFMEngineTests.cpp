// Copyright Epic Games, Inc. All Rights Reserved.

#include "AutoRTFMEngineTests.h"
#include "RequiredProgramMainCPPInclude.h"
#include "Styling/UMGCoreStyle.h"
#include "AutomationTestRunner.h"
#include "NullTestRunner.h"
#include "Materials/Material.h"
#include "AutoRTFM/AutoRTFM.h"
#include "AutoRTFMTestEngine.h"
#include "UObject/CoreRedirects.h"

#if WITH_AUTOMATION_WORKER
namespace UE::AutoRTFM
{
	class FAutomationTestRunner;
}

typedef UE::AutoRTFM::FAutomationTestRunner TestRunner;
#else
typedef FNullTestRunner TestRunner;
#endif

DEFINE_LOG_CATEGORY(LogAutoRTFMEngineTests);

IMPLEMENT_APPLICATION(AutoRTFMEngineTests, "AutoRTFMEngineTests");

static void PreInit();
static void LoadModules();
static void PostInit();
static void TearDown();

#if defined(PLATFORM_XBOXONE) && PLATFORM_XBOXONE
int TestMain()
#else
INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
#endif
{
	FString OriginalCmdLine;

#if !(defined(PLATFORM_XBOXONE) && PLATFORM_XBOXONE)
	// Parse original cmdline if there is one
	OriginalCmdLine = FCommandLine::BuildFromArgV(nullptr, ArgC, ArgV, nullptr);
#endif

	// Due to some code not respecting nullrhi etc.
	PRIVATE_GIsRunningCommandlet = true;
	PRIVATE_GAllowCommandletRendering = false;
	PRIVATE_GAllowCommandletAudio = false;

	// Init cmd line used for test
	{
		FString CmdLineOverride(TEXT(
				"-nullrhi "
				"-NoAsyncLoadingThread "
				"-NoAsyncPostLoad "
				"-NoZenLoader " // Required for the PreventLoadingOfEditorOnlyData() bodge
				"-noedl "
				"-unattended "
				"-LogCmds=\""
					"LogSlate off, "
					"LogSlateStyle off, "
					"LogUObjectBase off, "
					"LogUObjectGlobals off, "
					"LogConsoleResponse off, "
					"LogPackageLocalizationManager off, "
					"LogStreaming off, "
					"LogCsvProfiler off, "
					"LogDeviceProfileManager off, "
					"LogConfig off, "
					"AutoRTFMEngineTests on\""));
		FCommandLine::Set(ToCStr(CmdLineOverride));
	}

	FLogSuppressionInterface::Get().ProcessConfigAndCommandLine();

	PreInit();
	LoadModules();
	PostInit();

	GEngine = NewObject<UAutoRTFMTestEngine>(GetTransientPackage());
	GEngine->DefaultPhysMaterial = NewObject<UPhysicalMaterial>();

	UE_LOG(LogAutoRTFMEngineTests, Display, TEXT("AutoRTFMEngineTests"));

	bool TestsPassed = false;

	{
		TUniquePtr<TestRunner> Runner(new TestRunner());

		FString TestFilter;

		if (FParse::Value(ToCStr(OriginalCmdLine), TEXT("TestFilter="), TestFilter))
		{
			TestsPassed = Runner->RunTests(ToCStr(TestFilter));
		}
		else
		{
			TestsPassed = Runner->RunTests();
		}

	}

	TearDown();

	return TestsPassed ? 0 : 1;
}

static void TearDown()
{
	RequestEngineExit(TEXT("Shutting down AutoRTFMEngineTests"));

	FPlatformApplicationMisc::TearDown();
	FPlatformMisc::PlatformTearDown();

	if (GLog)
	{
		GLog->TearDown();
	}

	FCoreDelegates::OnExit.Broadcast();
	FModuleManager::Get().UnloadModulesAtShutdown();

#if STATS
	FThreadStats::StopThread();
#endif

	FTaskGraphInterface::Shutdown();

	if (GConfig)
	{
		GConfig->Exit();
		delete GConfig;
		GConfig = nullptr;
	}

	FTraceAuxiliary::Shutdown();
}

// Adds redirects to a non-existent file for all the Engine .uasset files that
// will be automatically loaded by the various engine systems. This is done to
// prevent attempted deserialization of assets that can only be loaded when
// the project is built with WITH_EDITORONLY_DATA. Unfortunately turning this
// flag on also requires WITH_EDITOR which is currently extremely difficult to
// build outside of the editor.
// HACK: SOL-6723
static void PreventLoadingOfEditorOnlyData()
{
	const TCHAR* const IncompatiblePackages[] = {
		TEXT("/Engine/EngineResources/DefaultTexture"),
		TEXT("/Engine/EngineResources/DefaultTextureCube"),
		TEXT("/Engine/EngineResources/DefaultVolumeTexture"),
		TEXT("/Engine/EngineFonts/RobotoDistanceField"),
		TEXT("/Engine/EngineMaterials/DefaultTextMaterialOpaque"),
		TEXT("/Engine/EngineDamageTypes/DmgTypeBP_Environmental"),
		TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst"),
		TEXT("/Engine/EngineMeshes/Sphere"),
		TEXT("/Engine/EngineResources/WhiteSquareTexture"),
		TEXT("/Engine/EngineResources/GradientTexture0"),
		TEXT("/Engine/EngineResources/Black"),
		TEXT("/Engine/EngineDebugMaterials/VolumeToRender"),
		TEXT("/Engine/EngineDebugMaterials/M_VolumeRenderSphereTracePP"),
		TEXT("/Engine/EngineFonts/Roboto"),
		TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Translucent"),
		TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Translucent_OneSided"),
		TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Opaque"),
		TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Opaque_OneSided"),
		TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Masked"),
		TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Masked_OneSided")
	};
	FCoreRedirectObjectName InvalidName(NAME_None, NAME_None, TEXT("/Engine/DoesNotExist"));
	TArray<FCoreRedirect> NewRedirects;
	for (const TCHAR* PackageName : IncompatiblePackages)
	{
		NewRedirects.Emplace(ECoreRedirectFlags::Type_Package,
			FCoreRedirectObjectName(NAME_None, NAME_None, PackageName),
			InvalidName);
	}
	FCoreRedirects::AddRedirectList(NewRedirects, TEXT("AutoRTFMEngineTests.PreventLoadingOfEditorOnlyData"));
	FCoreRedirects::AddKnownMissing(ECoreRedirectFlags::Type_Package, InvalidName);
}

static void PreInit()
{
	PreventLoadingOfEditorOnlyData();

	// We enable the AutoRTFM runtime as the tests depend on it.
	AutoRTFM::ForTheRuntime::SetAutoRTFMRuntime(AutoRTFM::ForTheRuntime::AutoRTFM_Enabled);

	FGenericPlatformOutputDevices::SetupOutputDevices();

	GError = FPlatformApplicationMisc::GetErrorOutputDevice();
	GWarn = FPlatformApplicationMisc::GetFeedbackContext();

	FPlatformMisc::PlatformInit();
#if WITH_APPLICATION_CORE
	FPlatformApplicationMisc::Init();
#endif
	FPlatformMemory::Init();

#if WITH_COREUOBJECT
	// Initialize the PackageResourceManager, which is needed to load any (non-script) Packages. It is first used in ProcessNewlyLoadedObjects (due to the loading of asset references in Class Default Objects)
	// It has to be intialized after the AssetRegistryModule; the editor implementations of PackageResourceManager relies on it
	IPackageResourceManager::Initialize();
#endif

	FDelayedAutoRegisterHelper::RunAndClearDelayedAutoRegisterDelegates(EDelayedRegisterRunPhase::FileSystemReady);

	FConfigCacheIni::InitializeConfigSystem();

	// Config overrides
	GConfig->SetInt(TEXT("/Script/Engine.GarbageCollectionSettings"), TEXT("gc.MaxObjectsNotConsideredByGC"), 0, GEngineIni);
	GConfig->SetInt(TEXT("/Script/Engine.GarbageCollectionSettings"), TEXT("gc.MaxObjectsInProgram"), 500000, GEngineIni);
	GConfig->SetInt(TEXT("/Script/Engine.GarbageCollectionSettings"), TEXT("gc.MaxObjectsInGame"), 500000, GEngineIni);
	GConfig->SetInt(TEXT("/Script/Engine.GarbageCollectionSettings"), TEXT("gc.MaxObjectsInEditor"), 500000, GEngineIni);
	GConfig->SetString(TEXT("/Script/Engine.Engine"), TEXT("AIControllerClassName"), TEXT("/Script/AIModule.AIController"), GEngineIni);
	GConfig->SetString(TEXT("/Script/Engine.Engine"), TEXT("DefaultMaterialName"), TEXT("/Engine/Transient.MockDefaultMaterial"), GEngineIni);
	GConfig->SetString(TEXT("/Script/Engine.Engine"), TEXT("DefaultLightFunctionMaterialName"), TEXT("/Engine/Transient.MockDefaultMaterial"), GEngineIni);
	GConfig->SetString(TEXT("/Script/Engine.Engine"), TEXT("DefaultDeferredDecalMaterialName"), TEXT("/Engine/Transient.MockDefaultMaterial"), GEngineIni);
	GConfig->SetString(TEXT("/Script/Engine.Engine"), TEXT("DefaultPostProcessMaterialName"), TEXT("/Engine/Transient.MockDefaultMaterial"), GEngineIni);

	GGameThreadId = FPlatformTLS::GetCurrentThreadId();
	FTaskGraphInterface::Startup(FPlatformMisc::NumberOfCores());
	FTaskGraphInterface::Get().AttachToThread(ENamedThreads::GameThread);

	FDelayedAutoRegisterHelper::RunAndClearDelayedAutoRegisterDelegates(EDelayedRegisterRunPhase::TaskGraphSystemReady);

#if STATS
	FThreadStats::StartThread();
#endif

	FDelayedAutoRegisterHelper::RunAndClearDelayedAutoRegisterDelegates(EDelayedRegisterRunPhase::StatSystemReady);


	// Ensure GCachedScalabilityCVars.bInitialized is set (required by some mesh components)
	FTaskTagScope::SwapTag(ETaskTag::EGameThread);
	extern void ScalabilityCVarsSinkCallback();
	ScalabilityCVarsSinkCallback();
}

static void LoadModules()
{
	// Always attempt to load CoreUObject. It requires additional pre-init which is called from its module's StartupModule method.
#if WITH_COREUOBJECT
	// Always register the UObjects callback for VNI and general consistency with the callbacks ProcessNewlyLoadedUObjects calls.
	RegisterProcessNewlyLoadedUObjects();
	FModuleManager::Get().LoadModule(TEXT("CoreUObject"));

	FCoreDelegates::OnInit.Broadcast();
#endif

	FCoreStyle::ResetToDefault();
	FUMGCoreStyle::ResetToDefault();

	// Create a mock default material to keep the material system happy
	UMaterial* MockMaterial = NewObject<UMaterial>(GetTransientPackage(), UMaterial::StaticClass(), TEXT("MockDefaultMaterial"), RF_Transient | RF_MarkAsRootSet);

	// ChaosEngineSolvers requires ChaosSolvers. We're not able to call ProcessNewlyLoadedObjects before this module is loaded.
	FModuleManager::Get().LoadModule(TEXT("ChaosSolvers"));
	ProcessNewlyLoadedUObjects();

	//FModuleManager::Get().LoadModule(TEXT("IrisCore"));
}

static void PostInit()
{
#if WITH_COREUOBJECT
	// Required for GC to be allowed.
	if (GUObjectArray.IsOpenForDisregardForGC())
	{
		GUObjectArray.CloseDisregardForGC();
	}
#endif

	// Disable ini file operations
	GConfig->DisableFileOperations();
}
