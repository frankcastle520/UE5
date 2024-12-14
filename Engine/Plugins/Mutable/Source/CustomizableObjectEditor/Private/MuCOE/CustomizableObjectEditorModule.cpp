// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/CustomizableObjectEditorModule.h"

#include "AssetToolsModule.h"
#include "GameFramework/Pawn.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "MessageLogModule.h"
#include "Misc/MessageDialog.h"
#include "Misc/DateTime.h"
#include "Misc/Timespan.h"
#include "HAL/FileManager.h"
#include "MuCO/CustomizableObjectSystem.h"		// For defines related to memory function replacements.
#include "MuCO/CustomizableObjectInstanceUsage.h"
#include "MuCO/CustomizableSkeletalMeshActor.h"
#include "MuCO/ICustomizableObjectModule.h"		// For instance editor command utility function
#include "MuCO/UnrealPortabilityHelpers.h"
#include "MuCOE/CustomizableInstanceDetails.h"
#include "MuCOE/CustomizableObjectCustomSettings.h"
#include "MuCOE/CustomizableObjectCustomSettingsDetails.h"
#include "MuCOE/CustomizableObjectDetails.h"
#include "MuCOE/CustomizableObjectEditor.h"
#include "MuCOE/CustomizableObjectEditorLogger.h"
#include "MuCOE/CustomizableObjectEditorSettings.h"
#include "MuCOE/CustomizableObjectEditorStyle.h"
#include "MuCOE/CustomizableObjectIdentifierCustomization.h"
#include "MuCOE/CustomizableObjectInstanceEditor.h"
#include "MuCOE/CustomizableObjectInstanceFactory.h"
#include "MuCOE/CustomizableObjectVersionBridge.h"
#include "MuCOE/CustomizableObjectNodeObjectGroupDetails.h"
#include "MuCOE/Nodes/CustomizableObjectNodeCopyMaterial.h"
#include "MuCOE/Nodes/CustomizableObjectNodeExternalPin.h"
#include "MuCOE/Nodes/CustomizableObjectNodeExternalPinDetails.h"
#include "MuCOE/Nodes/CustomizableObjectNodeLayoutBlocks.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMaterial.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMeshSectionDetails.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierEditMeshSection.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierEditMeshSectionDetails.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierExtendMeshSection.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierExtendMeshSectionDetails.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierMorphMeshSection.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierMorphMeshSectionDetails.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierRemoveMeshBlocks.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierRemoveMeshBlocksDetails.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierRemoveMesh.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierRemoveMeshDetails.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierClipMorph.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierClipMorphDetails.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierClipWithMesh.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierClipWithMeshDetails.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierClipWithUVMask.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierClipDeform.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierTransformInMesh.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMeshMorph.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMeshMorphDetails.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMeshReshapeCommon.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMeshReshapeSelectionDetails.h"
#include "MuCOE/Nodes/CustomizableObjectNodeObjectDetails.h"
#include "MuCOE/Nodes/CustomizableObjectNodeObjectGroup.h"
#include "MuCOE/Nodes/CustomizableObjectNodeDetails.h"
#include "MuCOE/Nodes/CustomizableObjectNodeProjectorConstant.h"
#include "MuCOE/Nodes/CustomizableObjectNodeProjectorParameterDetails.h"
#include "MuCOE/Nodes/CustomizableObjectNodeSkeletalMesh.h"
#include "MuCOE/Nodes/CustomizableObjectNodeSkeletalMeshDetails.h"
#include "MuCOE/Nodes/CustomizableObjectNodeStaticMesh.h"
#include "MuCOE/Nodes/CustomizableObjectNodeTable.h"
#include "MuCOE/Nodes/CustomizableObjectNodeTableDetails.h"
#include "MuCOE/Nodes/CustomizableObjectNodeComponentMeshDetails.h"
#include "MuCOE/Nodes/CustomizableObjectNodeVariation.h"
#include "MuCOE/Nodes/CustomizableObjectNodeTextureVariation.h"
#include "MuCOE/Widgets/CustomizableObjectVariationCustomization.h"
#include "MuCOE/Widgets/CustomizableObjectLODReductionSettings.h"
#include "PropertyEditorModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "MuCO/CustomizableObjectInstance.h"
#include "MuCOE/Nodes/CustomizableObjectNodeGroupProjectorParameter.h"
#include "UObject/UObjectIterator.h"
#include "Subsystems/PlacementSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "MuCOE/GraphTraversal.h"
#include "MuCOE/CustomizableObjectInstanceBaker.h"
#include "Editor.h"
#include "MuCO/CustomizableObjectSystemPrivate.h"
#include "MuCOE/CustomizableObjectGraph.h"
#include "Nodes/CustomizableObjectNodeComponentMeshDetails.h"
#include "Nodes/CustomizableObjectNodeModifierTransformInMeshDetails.h"

class AActor;
class FString;
class ICustomizableObjectDebugger;
class ICustomizableObjectEditor;
class ICustomizableObjectInstanceEditor;
class IToolkitHost;
class UObject;


const FName CustomizableObjectEditorAppIdentifier = FName(TEXT("CustomizableObjectEditorApp"));
const FName CustomizableObjectInstanceEditorAppIdentifier = FName(TEXT("CustomizableObjectInstanceEditorApp"));
const FName CustomizableObjectDebuggerAppIdentifier = FName(TEXT("CustomizableObjectDebuggerApp"));

#define LOCTEXT_NAMESPACE "MutableSettings"

/** Max timespan in days before a Saved/MutableStreamedDataEditor file is deleted. */
constexpr int32 MaxAccessTimespan = 30;

constexpr float ShowOnScreenCompileWarningsTickerTime = 1.0f;


void ShowOnScreenCompileWarnings()
{
	TSet<const UCustomizableObject*> Objects;
	
	for (TObjectIterator<UCustomizableObjectInstanceUsage> CustomizableObjectInstanceUsage; CustomizableObjectInstanceUsage; ++CustomizableObjectInstanceUsage)
	{
		if (!IsValid(*CustomizableObjectInstanceUsage) || CustomizableObjectInstanceUsage->IsTemplate())
		{
			continue;
		}
		
		const UCustomizableObjectInstance* Instance = CustomizableObjectInstanceUsage->GetCustomizableObjectInstance();
		if (!Instance)
		{
			continue;
		}

		const UCustomizableObject* Object = Cast<UCustomizableObject>(Instance->GetCustomizableObject());
		if (!Object)
		{
			continue;
		}
		
		const USkeletalMeshComponent* Parent = Cast<USkeletalMeshComponent>(CustomizableObjectInstanceUsage->GetAttachParent());
		if (!Parent)
		{
			continue;
		}

		const UWorld* World = Parent->GetWorld();
		if (!World)
		{
			continue;
		}

		if (World->WorldType != EWorldType::PIE)
		{
			continue;
		}

		Objects.Add(Object);
	}

	for (const UCustomizableObject* Object : Objects)
	{
		if (Object->GetPrivate()->Status.Get() != FCustomizableObjectStatus::EState::ModelLoaded)
		{
			continue;
		}
	
		// Show a warning if the compilation was not done with optimizations.
		const uint64 KeyCompiledWithOptimization = reinterpret_cast<uint64>(Object);
		if (!Object->GetPrivate()->GetModelResources().bIsCompiledWithOptimization)
		{
			FString Msg = FString::Printf(TEXT("Customizable Object [%s] was compiled without optimization."), *Object->GetName());
			GEngine->AddOnScreenDebugMessage(KeyCompiledWithOptimization, ShowOnScreenCompileWarningsTickerTime * 2.0f, FColor::Yellow, Msg);
		}
		else
		{
			GEngine->RemoveOnScreenDebugMessage(KeyCompiledWithOptimization);
		}
		
		const uint64 KeyCompiledOutOfDate = reinterpret_cast<uint64>(Object) + KEY_OFFSET_COMPILATION_OUT_OF_DATE; // Offset added to avoid collision with bIsCompiledWithOptimization warning
		TArray<FName> OutOfDatePackages;
		TArray<FName> AddedPackages;
		TArray<FName> RemovedPackages;
		bool bReleaseVersion;
		if (Object->GetPrivate()->IsCompilationOutOfDate(true, OutOfDatePackages, AddedPackages, RemovedPackages, bReleaseVersion))
		{
			FString Msg = FString::Printf(TEXT("Customizable Object [%s] compilation out of date. See the Output Log for more information."), *Object->GetName());
			GEngine->AddOnScreenDebugMessage(KeyCompiledOutOfDate, ShowOnScreenCompileWarningsTickerTime * 2.0f, FColor::Yellow, Msg);
			
			if (!GEngine->OnScreenDebugMessageExists(KeyCompiledOutOfDate))
			{
				UE_LOG(LogMutable, Display, TEXT("Customizable Object [%s] compilation out of date. Changes since last compilation:"), *Object->GetName());

				PrintParticipatingPackagesDiff(OutOfDatePackages, AddedPackages, RemovedPackages, bReleaseVersion);
			}
		}
		else
		{
			GEngine->RemoveOnScreenDebugMessage(KeyCompiledOutOfDate);
		}
	}
}

void DeleteUnusedMutableStreamedDataEditorFiles()
{
	const FDateTime CurrentTime = FDateTime::Now();

	const FString CompiledDataFolder = UCustomizableObjectPrivate::GetCompiledDataFolderPath();
	const FString FileExtension = TEXT(".mut");

	TArray<FString> Files;
	IFileManager& FileManager = IFileManager::Get();
	FileManager.FindFiles(Files, *CompiledDataFolder, *FileExtension);
	
	for (const FString& File : Files)
	{
		const FString FullFilePath = CompiledDataFolder + File;
		const FDateTime AccessTimeStamp = FileManager.GetAccessTimeStamp(*(FullFilePath));
		if (AccessTimeStamp == FDateTime::MinValue())
		{
			continue;
		}

		// Delete files that remain unused for more than MaxAccessTimespan
		const FTimespan TimeSpan = CurrentTime - AccessTimeStamp;
		if (TimeSpan.GetDays() > MaxAccessTimespan)
		{
			FileManager.Delete(*FullFilePath);
		}
	}
}


IMPLEMENT_MODULE( FCustomizableObjectEditorModule, CustomizableObjectEditor );


static void* CustomMalloc(std::size_t Size_t, uint32_t Alignment)
{
	return FMemory::Malloc(Size_t, Alignment);
}


static void CustomFree(void* mem)
{
	return FMemory::Free(mem);
}


void FCustomizableObjectEditorModule::StartupModule()
{
	// Delete unused local compiled data
	DeleteUnusedMutableStreamedDataEditorFiles();

	// Register the thumbnail renderers
	//UThumbnailManager::Get().RegisterCustomRenderer(UCustomizableObject::StaticClass(), UCustomizableObjectThumbnailRenderer::StaticClass());
	//UThumbnailManager::Get().RegisterCustomRenderer(UCustomizableObjectInstance::StaticClass(), UCustomizableObjectInstanceThumbnailRenderer::StaticClass());
	
	// Property views
	// Nodes
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	RegisterCustomDetails(PropertyModule, UCustomizableObjectNodeModifierEditMeshSection::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableObjectNodeModifierEditMeshSectionDetails::MakeInstance));
	RegisterCustomDetails(PropertyModule, UCustomizableObjectNodeModifierExtendMeshSection::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableObjectNodeModifierExtendMeshSectionDetails::MakeInstance));
	RegisterCustomDetails(PropertyModule, UCustomizableObjectNodeModifierRemoveMesh::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableObjectNodeModifierRemoveMeshDetails::MakeInstance));
	RegisterCustomDetails(PropertyModule, UCustomizableObjectNodeModifierRemoveMeshBlocks::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableObjectNodeModifierRemoveMeshBlocksDetails::MakeInstance));
	RegisterCustomDetails(PropertyModule, UCustomizableObjectNodeModifierMorphMeshSection::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableObjectNodeModifierMorphMeshSectionDetails::MakeInstance));
	RegisterCustomDetails(PropertyModule, UCustomizableObjectNodeModifierClipMorph::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableObjectNodeModifierClipMorphDetails::MakeInstance));
	RegisterCustomDetails(PropertyModule, UCustomizableObjectNodeModifierClipWithMesh::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableObjectNodeModifierClipWithMeshDetails::MakeInstance));
	RegisterCustomDetails(PropertyModule, UCustomizableObjectNodeModifierClipWithUVMask::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableObjectNodeModifierBaseDetails::MakeInstance));
	RegisterCustomDetails(PropertyModule, UCustomizableObjectNodeModifierClipDeform::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableObjectNodeModifierBaseDetails::MakeInstance));
	RegisterCustomDetails(PropertyModule, UCustomizableObjectNodeModifierTransformInMesh::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableObjectNodeModifierTransformInMeshDetails::MakeInstance));
	RegisterCustomDetails(PropertyModule, UCustomizableObjectNodeObject::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableObjectNodeObjectDetails::MakeInstance));
	RegisterCustomDetails(PropertyModule, UCustomizableObjectNodeObjectGroup::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableObjectNodeObjectGroupDetails::MakeInstance));
	RegisterCustomDetails(PropertyModule, UCustomizableObjectNodeProjectorParameter::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableObjectNodeProjectorParameterDetails::MakeInstance));
	RegisterCustomDetails(PropertyModule, UCustomizableObjectNodeProjectorConstant::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableObjectNodeProjectorParameterDetails::MakeInstance));
	RegisterCustomDetails(PropertyModule, UCustomizableObjectNodeMeshMorph::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableObjectNodeMeshMorphDetails::MakeInstance));
	RegisterCustomDetails(PropertyModule, UCustomizableObjectNodeExternalPin::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableObjectNodeExternalPinDetails::MakeInstance));
	RegisterCustomDetails(PropertyModule, UCustomizableObjectNodeMaterial::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableObjectNodeMeshSectionDetails::MakeInstance));
	RegisterCustomDetails(PropertyModule, UCustomizableObjectNodeSkeletalMesh::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableObjectNodeSkeletalMeshDetails::MakeInstance));
	RegisterCustomDetails(PropertyModule, UCustomizableObjectNodeStaticMesh::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableObjectNodeDetails::MakeInstance));
	RegisterCustomDetails(PropertyModule, UCustomizableObjectNodeTable::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableObjectNodeTableDetails::MakeInstance));
	RegisterCustomDetails(PropertyModule, UCustomizableObjectNodeComponentMesh::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableObjectNodeComponentMeshDetails::MakeInstance));

	// Other Objects
	RegisterCustomDetails(PropertyModule, UCustomizableObject::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableObjectDetails::MakeInstance));
	RegisterCustomDetails(PropertyModule, UCustomizableObjectInstance::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableInstanceDetails::MakeInstance));
	RegisterCustomDetails(PropertyModule, UCustomSettings::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FCustomizableObjectCustomSettingsDetails::MakeInstance));

	// Custom properties
	PropertyModule.RegisterCustomPropertyTypeLayout("CustomizableObjectIdentifier", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FCustomizableObjectIdentifierCustomization::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout(FMeshReshapeBoneReference::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FMeshReshapeBonesReferenceCustomization::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout(FBoneToRemove::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FCustomizableObjectLODReductionSettings::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout(NAME_StrProperty, FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FCustomizableObjectStateParameterSelector::MakeInstance), MakeShared<FStatePropertyTypeIdentifier>());
	PropertyModule.RegisterCustomPropertyTypeLayout(FCustomizableObjectVariation::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FCustomizableObjectVariationCustomization::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout(FCustomizableObjectTextureVariation::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FCustomizableObjectVariationCustomization::MakeInstance));

	PropertyModule.NotifyCustomizationModuleChanged();

	// Register factory
	FCoreDelegates::OnPostEngineInit.AddRaw(this,&FCustomizableObjectEditorModule::RegisterFactory);

	// Additional UI style
	FCustomizableObjectEditorStyle::Initialize();

	RegisterSettings();

	// Create the message log category
	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	MessageLogModule.RegisterLogListing(FName("Mutable"), LOCTEXT("MutableLog", "Mutable"));

	CustomizableObjectEditor_ToolBarExtensibilityManager = MakeShareable(new FExtensibilityManager);
	CustomizableObjectEditor_MenuExtensibilityManager = MakeShareable(new FExtensibilityManager);

	LaunchCOIECommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("mutable.OpenCOIE"),
		TEXT("Looks for a Customizable Object Instance within the player pawn and opens its Customizable Object Instance Editor. Specify slot ID to control which component is edited."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&FCustomizableObjectEditorModule::OpenCOIE));

	WarningsTickerHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("ShowOnScreenCompileWarnings"), ShowOnScreenCompileWarningsTickerTime, [](float)
	{
		ShowOnScreenCompileWarnings();
		return true;
	});

	FEditorDelegates::PreBeginPIE.AddRaw(this, &FCustomizableObjectEditorModule::OnPreBeginPIE);
}


void FCustomizableObjectEditorModule::ShutdownModule()
{
	FEditorDelegates::PreBeginPIE.RemoveAll(this);

	check(Compiler.GetNumRemainingWork() == 0);

	if( FModuleManager::Get().IsModuleLoaded( "PropertyEditor" ) )
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		
		// Unregister Property views
		for (const auto& ClassName : RegisteredCustomDetails)
		{
			PropertyModule.UnregisterCustomClassLayout(ClassName);
		}

		// Unregister Custom properties
		PropertyModule.UnregisterCustomPropertyTypeLayout("CustomizableObjectIdentifier");
		PropertyModule.UnregisterCustomPropertyTypeLayout(FMeshReshapeBoneReference::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FBoneToRemove::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(NAME_StrProperty);

		PropertyModule.NotifyCustomizationModuleChanged();
	}

	CustomizableObjectEditor_ToolBarExtensibilityManager.Reset();
	CustomizableObjectEditor_MenuExtensibilityManager.Reset();

	FCoreDelegates::OnPostEngineInit.RemoveAll(this);

	FCustomizableObjectEditorStyle::Shutdown();

	FTSTicker::GetCoreTicker().RemoveTicker(WarningsTickerHandle);
}


FCustomizableObjectEditorLogger& FCustomizableObjectEditorModule::GetLogger()
{
	return Logger;
}


bool FCustomizableObjectEditorModule::HandleSettingsSaved()
{
	UCustomizableObjectEditorSettings* CustomizableObjectSettings = GetMutableDefault<UCustomizableObjectEditorSettings>();

	if (CustomizableObjectSettings != nullptr)
	{
		CustomizableObjectSettings->SaveConfig();
		
		FEditorCompileSettings CompileSettings;
		CompileSettings.bIsMutableEnabled = !CustomizableObjectSettings->bDisableMutableCompileInEditor;
		CompileSettings.bEnableAutomaticCompilation = CustomizableObjectSettings->bEnableAutomaticCompilation;
		CompileSettings.bCompileObjectsSynchronously = CustomizableObjectSettings->bCompileObjectsSynchronously;
		CompileSettings.bCompileRootObjectsOnStartPIE = CustomizableObjectSettings->bCompileRootObjectsOnStartPIE;
		
		UCustomizableObjectSystem::GetInstance()->EditorSettingsChanged(CompileSettings);
	}

    return true;
}


void FCustomizableObjectEditorModule::RegisterSettings()
{
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

	if (SettingsModule != nullptr)
    {
        ISettingsSectionPtr SettingsSectionPtr = SettingsModule->RegisterSettings("Project", "Plugins", "CustomizableObjectSettings",
            LOCTEXT("MutableSettings_Setting", "Mutable"),
            LOCTEXT("MutableSettings_Setting_Desc", "Mutable Settings"),
            GetMutableDefault<UCustomizableObjectEditorSettings>()
        );

        if (SettingsSectionPtr.IsValid())
        {
            SettingsSectionPtr->OnModified().BindRaw(this, &FCustomizableObjectEditorModule::HandleSettingsSaved);
        }

		if (UCustomizableObjectSystem::GetInstance() != nullptr)
		{
			UCustomizableObjectEditorSettings* CustomizableObjectSettings = GetMutableDefault<UCustomizableObjectEditorSettings>();
			if (CustomizableObjectSettings != nullptr)
			{
				FEditorCompileSettings CompileSettings;
				CompileSettings.bIsMutableEnabled = !CustomizableObjectSettings->bDisableMutableCompileInEditor;
				CompileSettings.bEnableAutomaticCompilation = CustomizableObjectSettings->bEnableAutomaticCompilation;
				CompileSettings.bCompileObjectsSynchronously = CustomizableObjectSettings->bCompileObjectsSynchronously;
				CompileSettings.bCompileRootObjectsOnStartPIE = CustomizableObjectSettings->bCompileRootObjectsOnStartPIE;
				
				UCustomizableObjectSystem::GetInstance()->EditorSettingsChanged(CompileSettings);
			}
		}
    }
}


void FCustomizableObjectEditorModule::RegisterCustomDetails(FPropertyEditorModule& PropertyModule, const UClass* Class, FOnGetDetailCustomizationInstance DetailLayoutDelegate)
{
	const FName ClassName = FName(Class->GetName());
	PropertyModule.RegisterCustomClassLayout(ClassName, DetailLayoutDelegate);

	RegisteredCustomDetails.Add(ClassName);
}


void FCustomizableObjectEditorModule::OpenCOIE(const TArray<FString>& Arguments)
{
	int32 SlotID = INDEX_NONE;
	if (Arguments.Num() >= 1)
	{
		SlotID = FCString::Atoi(*Arguments[0]);
	}

	const UWorld* CurrentWorld = []() -> const UWorld*
	{
		UWorld* WorldForCurrentCOI = nullptr;
		const TIndirectArray<FWorldContext>& WorldContexts = GEngine->GetWorldContexts();
		for (const FWorldContext& Context : WorldContexts)
		{
			if ((Context.WorldType == EWorldType::Game) && (Context.World() != NULL))
			{
				WorldForCurrentCOI = Context.World();
			}
		}
		// Fall back to GWorld if we don't actually have a world.
		if (WorldForCurrentCOI == nullptr)
		{
			WorldForCurrentCOI = GWorld;
		}
		return WorldForCurrentCOI;
	}();
	const int32 PlayerIndex = 0;

	// Open the Customizable Object Instance Editor
	if (UCustomizableObjectInstanceUsage* SelectedCustomizableObjectInstanceUsage = GetPlayerCustomizableObjectInstanceUsage(SlotID, CurrentWorld, PlayerIndex))
	{
		if (UCustomizableObjectInstance* COInstance = SelectedCustomizableObjectInstanceUsage->GetCustomizableObjectInstance())
		{
			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
			TWeakPtr<IAssetTypeActions> WeakAssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(UCustomizableObjectInstance::StaticClass());

			if (TSharedPtr<IAssetTypeActions> AssetTypeActions = WeakAssetTypeActions.Pin())
			{
				TArray<UObject*> AssetsToEdit;
				AssetsToEdit.Add(COInstance);
				AssetTypeActions->OpenAssetEditor(AssetsToEdit);
			}
		}
	}
}


void FCustomizableObjectEditorModule::RegisterFactory()
{
	if (GEditor)
	{
		GEditor->ActorFactories.Add(NewObject<UCustomizableObjectInstanceFactory>());
		if (UPlacementSubsystem* PlacementSubsystem = GEditor->GetEditorSubsystem<UPlacementSubsystem>())
		{
			PlacementSubsystem->RegisterAssetFactory(NewObject<UCustomizableObjectInstanceFactory>());
		}
	}
}


/** Recursively get all Customizable Objects that reference the given Customizable Object. */
void GetReferencingCustomizableObjects(FName CustomizableObjectName, TArray<FName>& VisitedObjectNames, TArray<FAssetData>& ReferencingAssets)
{
	if (VisitedObjectNames.Contains(CustomizableObjectName))
	{
		return;
	}

	VisitedObjectNames.Add(CustomizableObjectName);

	TArray<FName> ReferencedObjectNames;
	
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistryModule.Get().GetReferencers(CustomizableObjectName, ReferencedObjectNames, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::EDependencyQuery::Hard);

	// Required to be deterministic.
	ReferencedObjectNames.Sort([](const FName& A, const FName& B)
	{
		return A.LexicalLess(B);
	});
	
	TArray<FAssetData> AssetDataArray;

	FARFilter Filter;
	Filter.PackageNames = MoveTemp(ReferencedObjectNames);

	AssetRegistryModule.Get().GetAssets(Filter, AssetDataArray);

	for (FAssetData AssetData : AssetDataArray)
	{
		if (AssetData.GetClass() == UCustomizableObject::StaticClass())
		{
			FName ReferencedObjectName = AssetData.GetPackage()->GetFName();
	
			ReferencingAssets.Add(AssetData);

			GetReferencingCustomizableObjects(ReferencedObjectName, VisitedObjectNames, ReferencingAssets);
		}
	}
}


void GetReferencingPackages(const UCustomizableObject& Object, TArray<FAssetData>& ReferencingAssets)
{
	// Gather all child CustomizableObjects
	TArray<FName> VisitedObjectNames;
	GetReferencingCustomizableObjects(Object.GetPackage()->GetFName(), VisitedObjectNames, ReferencingAssets);

	// Gather all tables which will composite the final tables
	TArray<FAssetData> ReferencingCustomizableObjects = ReferencingAssets;
	for (const FAssetData& ReferencingCustomizableObject : ReferencingCustomizableObjects)
	{
		const TSoftObjectPtr SoftObjectPtr(ReferencingCustomizableObject.ToSoftObjectPath());

		const UCustomizableObject* ChildCustomizableObject = Cast<UCustomizableObject>(SoftObjectPtr.LoadSynchronous());
		if (!ChildCustomizableObject)
		{
			continue;
		}

		TArray<UCustomizableObjectNodeTable*> TableNodes;
		ChildCustomizableObject->GetPrivate()->GetSource()->GetNodesOfClass(TableNodes);

		FARFilter Filter;
		Filter.ClassPaths.Add(FTopLevelAssetPath(UDataTable::StaticClass()));

		for (const UCustomizableObjectNodeTable* TableNode : TableNodes)
		{
			TArray<FAssetData> DataTableAssets = TableNode->GetParentTables();

			for (const FAssetData& DataTableAsset : DataTableAssets)
			{
				if (DataTableAsset.IsValid())
				{
					ReferencingAssets.AddUnique(DataTableAsset);
				}
			}
		}		
	}
}


bool FCustomizableObjectEditorModule::IsCompilationOutOfDate(const UCustomizableObject& Object, bool bSkipIndirectReferences, TArray<FName>& OutOfDatePackages, TArray<FName>& AddedPackages, TArray<FName>& RemovedPackages, bool& bReleaseVersion) const
{
	MUTABLE_CPUPROFILER_SCOPE(FCustomizableObjectEditorModule::IsCompilationOutOfDate)
	
	// TODO CO Custom version
	// TODO List of plugins and their custom versions
	// Maybe use BuildDerivedDataKey? BuildDerivedDataKey should also consider bSkipIndirectReferences
	
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	const TMap<FName, FGuid>& OldParticipatingObjects = Object.GetPrivate()->GetModelResources().ParticipatingObjects;
	
	for (const TTuple<FName, FGuid>& ParticipatingObject : OldParticipatingObjects)
	{
		TSoftObjectPtr SoftObjectPtr(FSoftObjectPath(ParticipatingObject.Key.ToString()));
		if (SoftObjectPtr) // If loaded
		{
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			const FGuid PackageGuid = SoftObjectPtr->GetPackage()->GetGuid();
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
			
			if (PackageGuid != ParticipatingObject.Value)
			{
				OutOfDatePackages.AddUnique(ParticipatingObject.Key);
			}
		}
		else // Not loaded
		{
			FAssetPackageData AssetPackageData;
			const UE::AssetRegistry::EExists Result = AssetRegistryModule.Get().TryGetAssetPackageData(ParticipatingObject.Key, AssetPackageData);
				
			if (Result != UE::AssetRegistry::EExists::Exists)
			{
				OutOfDatePackages.AddUnique(ParticipatingObject.Key);
			}

			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			const FGuid PackageGuid = AssetPackageData.PackageGuid;
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
			
			if (PackageGuid != ParticipatingObject.Value)
			{
				OutOfDatePackages.AddUnique(ParticipatingObject.Key);
			}
		}
	}
	
	// Check that we have the exact same set of participating object as before. This can change due to indirect references and versioning.
	if (!bSkipIndirectReferences)
	{
		// Due to performance issues, we will skip loading all objects. We can do that since loading/not loading objects do not affect the number of indirect objects discovered
		// (e.g., we will traverse the same number of COs/Tables regardless if we do not load meshes/textures...). 
		TMap<FName, FGuid> ParticipatingObjects = GetParticipatingObjects(&Object, false);
		
		for (const TTuple<FName, FGuid>& ParticipatingObject : ParticipatingObjects)
		{
			// Since here we are if the smaller set (objects found now without loading all objects) is contained in the larger set (objects found in the compilation pass),
			// there is no need to check if the asset is a indirect reference (CO or Table).
			if (!OldParticipatingObjects.Contains(ParticipatingObject.Key))
			{
				AddedPackages.AddUnique(ParticipatingObject.Key);
			}
		}

		for (const TTuple<FName, FGuid>& OldParticipatingObject : OldParticipatingObjects)
		{
			FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(OldParticipatingObject.Key.ToString()));
			if (AssetData.AssetClassPath == UCustomizableObject::StaticClass()->GetClassPathName() ||
				AssetData.AssetClassPath == UDataTable::StaticClass()->GetClassPathName())
			{
				if (!ParticipatingObjects.Contains(OldParticipatingObject.Key))
				{
					RemovedPackages.AddUnique(OldParticipatingObject.Key);
				}
			}
		}
	}

	bReleaseVersion = false;
	if (ICustomizableObjectVersionBridgeInterface* VersionBridge = Cast<ICustomizableObjectVersionBridgeInterface>(Object.VersionBridge))
	{
		bReleaseVersion = Object.GetPrivate()->GetModelResources().ReleaseVersion != VersionBridge->GetCurrentVersionAsString();
	}

	return bReleaseVersion || !OutOfDatePackages.IsEmpty() || !AddedPackages.IsEmpty() || !RemovedPackages.IsEmpty();
}


bool FCustomizableObjectEditorModule::IsRootObject(const UCustomizableObject& Object) const
{
	return GraphTraversal::IsRootObject(Object);
}

FString FCustomizableObjectEditorModule::GetCurrentReleaseVersionForObject(const UCustomizableObject& Object) const
{
	if (Object.VersionBridge && Object.VersionBridge->GetClass()->ImplementsInterface(UCustomizableObjectVersionBridgeInterface::StaticClass()))
	{
		ICustomizableObjectVersionBridgeInterface* CustomizableObjectVersionBridgeInterface = Cast<ICustomizableObjectVersionBridgeInterface>(Object.VersionBridge);

		if (CustomizableObjectVersionBridgeInterface)
		{
			return CustomizableObjectVersionBridgeInterface->GetCurrentVersionAsString();
		}
	}

	return FString();
}


UCustomizableObject* FCustomizableObjectEditorModule::GetRootObject(UCustomizableObject* ChildObject) const
{
	return GraphTraversal::GetRootObject(ChildObject);
}


const UCustomizableObject* FCustomizableObjectEditorModule::GetRootObject(const UCustomizableObject* ChildObject) const
{
	return GraphTraversal::GetRootObject(ChildObject);
}


void FCustomizableObjectEditorModule::BakeCustomizableObjectInstance(UCustomizableObjectInstance* InTargetInstance, const FBakingConfiguration& InBakingConfig)
{
	UCustomizableObjectInstanceBaker* InstanceBaker = NewObject<UCustomizableObjectInstanceBaker>();

	// Add the heap object to the root so we prevent it from being removed. It will get removed from there once it finishes it's work.
	InstanceBaker->AddToRoot();
	
	// On baker operation completed just remove it from the root so it gets eventually destroyed by the GC system
	const TSharedPtr<FOnBakerFinishedWork> OnBakerFinishedWorkCallback = MakeShared<FOnBakerFinishedWork>();
	OnBakerFinishedWorkCallback->BindLambda([InstanceBaker]
	{
		InstanceBaker->RemoveFromRoot();
	});
	
	// Ask for the baking of the instance
	InstanceBaker->BakeInstance(InTargetInstance, InBakingConfig, OnBakerFinishedWorkCallback);
}


USkeletalMesh* FCustomizableObjectEditorModule::GetReferenceSkeletalMesh(const UCustomizableObject& Object, const FName& ComponentName) const
{
	UCustomizableObject* RootObject = GetRootObject(const_cast<UCustomizableObject*>(&Object));

	TSet<UCustomizableObject*> Objects;
	GetAllObjectsInGraph(RootObject, Objects);
	
	for (const UCustomizableObject* CurrentObject : Objects)
	{
		for (UEdGraphNode* Node : CurrentObject->GetPrivate()->GetSource()->Nodes)
		{
			if (UCustomizableObjectNodeComponentMesh* NodeComponentMesh = Cast<UCustomizableObjectNodeComponentMesh>(Node))
			{
				if (NodeComponentMesh->ComponentName == ComponentName)
				{
					return NodeComponentMesh->ReferenceSkeletalMesh;
				}
			}
		}	
	}

	return {};
}


TMap<FName, FGuid> FCustomizableObjectEditorModule::GetParticipatingObjects(const UCustomizableObject* Object, bool bLoadObjects, const FCompilationOptions* InOptions) const
{
	MUTABLE_CPUPROFILER_SCOPE(FCustomizableObjectEditorModule::GetParticipatingObjects)

	FCompilationOptions Options = InOptions ? *InOptions : Object->GetPrivate()->GetCompileOptions();
		
	FMutableGraphGenerationContext Context(Object, nullptr, Options);
	Context.bParticipatingObjectsPass = true;
	Context.bLoadObjects = bLoadObjects;

	GenerateMutableRoot(Object, Context);

	return Context.ParticipatingObjects;
}


void FCustomizableObjectEditorModule::BackwardsCompatibleFixup(UEdGraph& Graph, int32 CustomizableObjectCustomVersion)
{
	if (UCustomizableObjectGraph* COGraph = Cast<UCustomizableObjectGraph>(&Graph))
	{
		COGraph->BackwardsCompatibleFixup(CustomizableObjectCustomVersion);
	}
}


void FCustomizableObjectEditorModule::PostBackwardsCompatibleFixup(UEdGraph& Graph)
{
	if (UCustomizableObjectGraph* COGraph = Cast<UCustomizableObjectGraph>(&Graph))
	{
		COGraph->PostBackwardsCompatibleFixup();
	}
}


void FCustomizableObjectEditorModule::CompileCustomizableObject(const TSharedRef<FCompilationRequest>& InCompilationRequest, bool bForceRequest)
{
	if (IsRunningGame())
	{
		return;
	}

	CompileCustomizableObjects({ InCompilationRequest }, bForceRequest);
}

void FCustomizableObjectEditorModule::CompileCustomizableObjects(const TArray<TSharedRef<FCompilationRequest>>& InCompilationRequests, bool bForceRequests)
{
	check(IsInGameThread());

	if (IsRunningGame())
	{
		return;
	}

	TArray<TSharedRef<FCompilationRequest>> FilteredAsyncRequests;
	FilteredAsyncRequests.Reserve(InCompilationRequests.Num());

	for (const TSharedRef<FCompilationRequest>& Request : InCompilationRequests)
	{
		const UCustomizableObject* CustomizableObject = Request->GetCustomizableObject();
		if (!CustomizableObject)
		{
			continue;
		}
		
		if (!Request->IsAsyncCompilation())
		{
			FCustomizableObjectCompiler* SyncCompiler = new FCustomizableObjectCompiler();
			SyncCompiler->Compile(Request);
			delete SyncCompiler;
		}

		else if (bForceRequests ||
			(!CustomizableObject->GetPrivate()->IsLocked() && !Compiler.IsRequestQueued(Request)))
		{
			FilteredAsyncRequests.Add(Request);
		}
	}

	Compiler.Compile(FilteredAsyncRequests);
}


int32 FCustomizableObjectEditorModule::Tick(bool bBlocking)
{
	Compiler.Tick(bBlocking);
	return Compiler.GetNumRemainingWork();
}


void FCustomizableObjectEditorModule::CancelCompileRequests()
{
	Compiler.ForceFinishCompilation();
	Compiler.ClearCompileRequests();
}


int32 FCustomizableObjectEditorModule::GetNumCompileRequests()
{
	return Compiler.GetNumRemainingWork();
}


void FCustomizableObjectEditorModule::OnPreBeginPIE(const bool bIsSimulatingInEditor)
{
	if (IsRunningGame() || !UCustomizableObjectSystem::IsActive())
	{
		return;
	}

	UCustomizableObjectSystem* System = UCustomizableObjectSystem::GetInstanceChecked();
	if (!System->EditorSettings.bCompileRootObjectsOnStartPIE)
	{
		return;
	}

	// Find root customizable objects
	FARFilter AssetRegistryFilter;
	UE_MUTABLE_GET_CLASSPATHS(AssetRegistryFilter).Add(UE_MUTABLE_TOPLEVELASSETPATH(TEXT("/Script/CustomizableObject"), TEXT("CustomizableObject")));
	AssetRegistryFilter.TagsAndValues.Add(FName("IsRoot"), FString::FromInt(1));

	TArray<FAssetData> OutAssets;
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistryModule.Get().GetAssets(AssetRegistryFilter, OutAssets);

	TArray<TSharedRef<FCompilationRequest>> Requests;
	for (const FAssetData& Asset : OutAssets)
	{
		// If it is referenced by PIE it should be loaded
		if (!Asset.IsAssetLoaded())
		{
			continue;
		}

		UCustomizableObject* Object = Cast<UCustomizableObject>(Asset.GetAsset());
		if (!Object || Object->IsCompiled() || Object->GetPrivate()->IsLocked())
		{
			continue;
		}

		// Add uncompiled objects to the objects to cook list
		TSharedRef<FCompilationRequest> NewRequest = MakeShared<FCompilationRequest>(*Object, true);
		NewRequest->GetCompileOptions().bSilentCompilation = true;
		Requests.Add(NewRequest);
	}

	if (!Requests.IsEmpty())
	{
		const FText Msg = FText::FromString(TEXT("Warning: one or more Customizable Objects used in PIE are uncompiled.\n\nDo you want to compile them?"));
		if (FMessageDialog::Open(EAppMsgType::OkCancel, Msg) == EAppReturnType::Ok)
		{
			CompileCustomizableObjects(Requests);
		}
	}
}

#undef LOCTEXT_NAMESPACE
