// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundEditorSubsystem.h"

#include "AudioPropertiesSheetAssetBase.h"
#include "IAssetTools.h"
#include "Kismet/KismetSystemLibrary.h"
#include "MetasoundDocumentBuilderRegistry.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphSchema.h"
#include "MetasoundEditorGraphMemberDefaults.h"
#include "MetasoundEditorModule.h"
#include "MetasoundEditorSettings.h"
#include "MetasoundFactory.h"
#include "MetasoundSettings.h"
#include "MetasoundUObjectRegistry.h"
#include "NodeTemplates/MetasoundFrontendNodeTemplateInput.h"
#include "ScopedTransaction.h"
#include "Sound/SoundSourceBusSend.h"
#include "Sound/SoundSubmixSend.h"
#include "Subsystems/AssetEditorSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetasoundEditorSubsystem)

#define LOCTEXT_NAMESPACE "MetaSoundEditor"


TScriptInterface<IMetaSoundDocumentInterface> UMetaSoundEditorSubsystem::BuildToAsset(
	UMetaSoundBuilderBase* InBuilder,
	const FString& Author,
	const FString& AssetName,
	const FString& PackagePath,
	EMetaSoundBuilderResult& OutResult,
	const USoundWave* TemplateSoundWave
)
{
	using namespace Metasound;
	using namespace Metasound::Engine;
	using namespace Metasound::Frontend;

	OutResult = EMetaSoundBuilderResult::Failed;

	if (InBuilder)
	{
		// AddToRoot to avoid builder getting gc'ed during CreateAsset call below, as the builder 
		// may be unreferenced by other UObjects and it must be persistent to finish initializing.
		const bool bWasRooted = InBuilder->IsRooted();
		if (!bWasRooted)
		{
			InBuilder->AddToRoot();
		}

		constexpr UFactory* Factory = nullptr;

		// Not about to follow this lack of const correctness down a multidecade in the works rabbit hole.
		UClass& MetaSoundUClass = const_cast<UClass&>(InBuilder->GetBaseMetaSoundUClass());
		if (UObject* NewMetaSound = IAssetTools::Get().CreateAsset(AssetName, PackagePath, &MetaSoundUClass, Factory))
		{
			InBuilder->InitNodeLocations();
			InBuilder->SetAuthor(Author);

			// Initialize and Build
			{
				constexpr bool bForceUniqueClassName = true;
				constexpr bool bAddToRegistry = true;
				const FMetaSoundBuilderOptions BuilderOptions { FName(*AssetName), bForceUniqueClassName, bAddToRegistry, NewMetaSound };
				InBuilder->Build(BuilderOptions);
			}

			UMetaSoundBuilderBase& NewDocBuilder = FDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(*NewMetaSound);

			EMetaSoundBuilderResult InjectResult = EMetaSoundBuilderResult::Failed;
			constexpr bool bForceNodeCreation = true;
			NewDocBuilder.InjectInputTemplateNodes(bForceNodeCreation, InjectResult);

			FMetasoundAssetBase& Asset = NewDocBuilder.GetBuilder().GetMetasoundAsset();
			Asset.RebuildReferencedAssetClasses();

			// Apply template SoundWave settings 
			// (must be post rebuilding referenced asset classes to find referenced asset to copy settings from)
			{
				const bool bIsSource = &MetaSoundUClass == UMetaSoundSource::StaticClass();
				if (NewDocBuilder.IsPreset())
				{
					// Only use referenced UObject's SoundWave settings for sources if not overridden 
					if (TemplateSoundWave == nullptr && bIsSource)
					{
						const UObject* ReferencedObject = NewDocBuilder.GetReferencedPresetAsset();
						if (ensureMsgf(ReferencedObject, TEXT("Preset builder %s does not have a referenced asset to apply soundwave settings from."), *NewDocBuilder.GetName()))
						{
							TemplateSoundWave = CastChecked<USoundWave>(ReferencedObject);
						}
					}
				}

				// Template SoundWave settings only apply to sources
				if (TemplateSoundWave != nullptr && bIsSource)
				{
					SetSoundWaveSettingsFromTemplate(*CastChecked<USoundWave>(NewMetaSound), *TemplateSoundWave);
				}
			}

			if (!bWasRooted)
			{
				InBuilder->RemoveFromRoot();
			}

			OutResult = EMetaSoundBuilderResult::Succeeded;
			return NewMetaSound;
		}

		if (!bWasRooted)
		{
			InBuilder->RemoveFromRoot();
		}
	}

	return nullptr;
}

UMetasoundEditorGraphMemberDefaultLiteral* UMetaSoundEditorSubsystem::CreateMemberMetadata(
	FMetaSoundFrontendDocumentBuilder& Builder,
	FName InMemberName,
	TSubclassOf<UMetasoundEditorGraphMemberDefaultLiteral> LiteralClass) const
{
	using namespace Metasound::Engine;

	// If preset and input inherits from default, copy member metadata from referenced graph
	if (Builder.IsPreset())
	{
		const FMetasoundFrontendGraphClass& RootGraph = Builder.GetConstDocumentChecked().RootGraph;
		const TSet<FName>& InputsInheritingDefault = RootGraph.PresetOptions.InputsInheritingDefault;
		if (InputsInheritingDefault.Contains(InMemberName))
		{
			// Get referenced asset to inherit metadata from
			FMetasoundAssetBase* ReferencedPresetAsset = Builder.GetReferencedPresetAsset();
			if (ReferencedPresetAsset)
			{
				FMetaSoundFrontendDocumentBuilder& ReferencedBuilder = FDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(ReferencedPresetAsset->GetOwningAsset());
				const FMetasoundFrontendClassInput* ClassInput = ReferencedBuilder.FindGraphInput(InMemberName);
				if (ClassInput)
				{
					if (UMetaSoundFrontendMemberMetadata* ReferencedMemberMetadata = ReferencedBuilder.FindMemberMetadata(ClassInput->NodeID))
					{
						return NewObject<UMetasoundEditorGraphMemberDefaultLiteral>(&Builder.CastDocumentObjectChecked<UObject>(), ReferencedMemberMetadata->GetClass(), FName(), RF_Transactional, ReferencedMemberMetadata);
					}
				}
			}
		}
	}
	// Otherwise, create brand new member metadata
	return NewObject<UMetasoundEditorGraphMemberDefaultLiteral>(&Builder.CastDocumentObjectChecked<UObject>(), LiteralClass, FName(), RF_Transactional, nullptr);
}

bool UMetaSoundEditorSubsystem::BindMemberMetadata(
	FMetaSoundFrontendDocumentBuilder& Builder,
	UMetasoundEditorGraphMember& InMember,
	TSubclassOf<UMetasoundEditorGraphMemberDefaultLiteral> LiteralClass,
	UMetasoundEditorGraphMemberDefaultLiteral* TemplateObject)
{
	UMetasoundEditorGraphMemberDefaultLiteral* NewLiteral = nullptr;
	const FGuid& MemberID = InMember.GetMemberID();

	if (TemplateObject)
	{
		Builder.ClearMemberMetadata(MemberID);
		NewLiteral = NewObject<UMetasoundEditorGraphMemberDefaultLiteral>(&Builder.CastDocumentObjectChecked<UObject>(), LiteralClass, FName(), RF_Transactional, TemplateObject);
	}
	else
	{
		if (UMetaSoundFrontendMemberMetadata* Literal = Builder.FindMemberMetadata(MemberID))
		{
			InMember.Literal = CastChecked<UMetasoundEditorGraphMemberDefaultLiteral>(Literal);
			return false;
		}

		NewLiteral = CreateMemberMetadata(Builder, InMember.GetMemberName(), LiteralClass);
	}

	if (NewLiteral)
	{
		NewLiteral->MemberID = MemberID;

		Builder.SetMemberMetadata(*NewLiteral);
		InMember.Literal = NewLiteral;
		return true;
	}

	checkNoEntry();
	return false;
}

UMetaSoundBuilderBase* UMetaSoundEditorSubsystem::FindOrBeginBuilding(TScriptInterface<IMetaSoundDocumentInterface> MetaSound, EMetaSoundBuilderResult& OutResult) const
{
	using namespace Metasound::Engine;

	if (UObject* Object = MetaSound.GetObject(); Object && Object->IsAsset())
	{
		OutResult = EMetaSoundBuilderResult::Succeeded;
		return &FDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(*Object);
	}

	OutResult = EMetaSoundBuilderResult::Failed;
	return nullptr;
}

UMetaSoundFrontendMemberMetadata* UMetaSoundEditorSubsystem::FindOrCreateGraphInputMetadata(UMetaSoundBuilderBase* InBuilder, FName InputName, EMetaSoundBuilderResult& OutResult)
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend; 
	
	if (InBuilder)
	{
		FMetaSoundNodeHandle GraphInputNodeHandle = InBuilder->FindGraphInputNode(InputName, OutResult);
		if (OutResult == EMetaSoundBuilderResult::Succeeded)
		{
			FMetaSoundFrontendDocumentBuilder& DocBuilder = InBuilder->GetBuilder();
			// Look for existing metadata
			if (UMetaSoundFrontendMemberMetadata* MemberMetadata = DocBuilder.FindMemberMetadata(GraphInputNodeHandle.NodeID))
			{
				OutResult = EMetaSoundBuilderResult::Succeeded;
				return MemberMetadata;
			}
			// Create new metadata
			else
			{
				// Get literal class
				const FName TypeName = DocBuilder.FindGraphInput(InputName)->TypeName;
				TSubclassOf<UMetasoundEditorGraphMemberDefaultLiteral> LiteralClass = GetLiteralClassForType(TypeName);

				if (InBuilder->IsPreset())
				{
					// Ensure this MetaSound's dependencies are registered to lookup inherited metadata
					// Needed in the case where this function is called from a BP,
					// metadata wasn't created previously, and the MetaSound editor was never opened this session
					UObject* MetaSound = InBuilder->GetConstBuilder().GetMetasoundAsset().GetOwningAsset();
					check(MetaSound);
					RegisterGraphWithFrontend(*MetaSound);
				}

				// Create new literal and setup
				UMetasoundEditorGraphMemberDefaultLiteral* NewLiteral = CreateMemberMetadata(DocBuilder, InputName, LiteralClass);
				if (NewLiteral)
				{
					NewLiteral->MemberID = GraphInputNodeHandle.NodeID;
					NewLiteral->Initialize();
					DocBuilder.SetMemberMetadata(*NewLiteral);
					
					OutResult = EMetaSoundBuilderResult::Succeeded;
					return NewLiteral;
				}
			}
		}
		else
		{
			UE_LOG(LogMetaSound, Display, TEXT("Failed to find graph input node for input '%s' with builder '%s'."), *InputName.ToString(), *InBuilder->GetName());
		}
	}

	OutResult = EMetaSoundBuilderResult::Failed;
	return nullptr;
}

TSubclassOf<UMetasoundEditorGraphMemberDefaultLiteral> UMetaSoundEditorSubsystem::GetLiteralClassForType(FName TypeName) const
{
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	// Get literal class
	FDataTypeRegistryInfo DataTypeInfo;
	IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<IMetasoundEditorModule>("MetaSoundEditor");
	IDataTypeRegistry::Get().GetDataTypeInfo(TypeName, DataTypeInfo);
	const EMetasoundFrontendLiteralType LiteralType = static_cast<EMetasoundFrontendLiteralType>(DataTypeInfo.PreferredLiteralType);

	TSubclassOf<UMetasoundEditorGraphMemberDefaultLiteral> LiteralClass = EditorModule.FindDefaultLiteralClass(LiteralType);
	if (!LiteralClass)
	{
		LiteralClass = UMetasoundEditorGraphMemberDefaultLiteral::StaticClass();
	}
	return LiteralClass;
}

UMetaSoundEditorSubsystem& UMetaSoundEditorSubsystem::GetChecked()
{
	checkf(GEditor, TEXT("Cannot access UMetaSoundEditorSubsystem without editor loaded"));
	UMetaSoundEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UMetaSoundEditorSubsystem>();
	checkf(EditorSubsystem, TEXT("Failed to find initialized 'UMetaSoundEditorSubsystem"));
	return *EditorSubsystem;
}

const UMetaSoundEditorSubsystem& UMetaSoundEditorSubsystem::GetConstChecked()
{
	checkf(GEditor, TEXT("Cannot access UMetaSoundEditorSubsystem without editor loaded"));
	UMetaSoundEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UMetaSoundEditorSubsystem>();
	checkf(EditorSubsystem, TEXT("Failed to find initialized 'UMetaSoundEditorSubsystem"));
	return *EditorSubsystem;
}

const FString UMetaSoundEditorSubsystem::GetDefaultAuthor()
{
	FString Author = UKismetSystemLibrary::GetPlatformUserName();
	if (const UMetasoundEditorSettings* EditorSettings = GetDefault<UMetasoundEditorSettings>())
	{
		if (!EditorSettings->DefaultAuthor.IsEmpty())
		{
			Author = EditorSettings->DefaultAuthor;
		}
	}
	return Author;
}

const TArray<TSharedRef<FExtender>>& UMetaSoundEditorSubsystem::GetToolbarExtenders() const
{
	return EditorToolbarExtenders;
}

void UMetaSoundEditorSubsystem::InitAsset(UObject& InNewMetaSound, UObject* InReferencedMetaSound)
{
	using namespace Metasound;
	using namespace Metasound::Editor;
	using namespace Metasound::Frontend;

	TScriptInterface<IMetaSoundDocumentInterface> DocInterface = &InNewMetaSound;
	FMetaSoundFrontendDocumentBuilder Builder(DocInterface);

	Builder.InitDocument();
	Builder.InitNodeLocations();

	constexpr bool bForceNodeCreation = true;
	FInputNodeTemplate::GetChecked().Inject(Builder, bForceNodeCreation);

	const FString& Author = GetDefaultAuthor();
	Builder.SetAuthor(Author);

	// Initialize asset as a preset
	if (InReferencedMetaSound)
	{
		// Ensure the referenced MetaSound is registered already
		RegisterGraphWithFrontend(*InReferencedMetaSound);

		// Initialize preset with referenced Metasound 
		TScriptInterface<IMetaSoundDocumentInterface> ReferencedDocInterface = InReferencedMetaSound;
		Builder.ConvertToPreset(ReferencedDocInterface->GetConstDocument());

		// Copy sound wave settings to preset for sources
		if (&ReferencedDocInterface->GetBaseMetaSoundUClass() == UMetaSoundSource::StaticClass())
		{
			SetSoundWaveSettingsFromTemplate(*CastChecked<USoundWave>(&InNewMetaSound), *CastChecked<USoundWave>(InReferencedMetaSound));
		}
	}
}

void UMetaSoundEditorSubsystem::InitEdGraph(UObject& InMetaSound)
{
	using namespace Metasound::Frontend;
	Metasound::Editor::FGraphBuilder::BindEditorGraph(IDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(&InMetaSound));
}

bool UMetaSoundEditorSubsystem::IsPageAuditionPlatformCookTarget(FName InPageName) const
{
	if (const UMetaSoundSettings* Settings = GetDefault<UMetaSoundSettings>())
	{
		if (const FMetaSoundPageSettings* PageSettings = Settings->FindPageSettings(InPageName))
		{
			return IsPageAuditionPlatformCookTarget(PageSettings->UniqueId);
		}
	}

	return false;
}

bool UMetaSoundEditorSubsystem::IsPageAuditionPlatformCookTarget(const FGuid& InPageID) const
{
#if WITH_EDITORONLY_DATA
	if (const UMetaSoundSettings* Settings = GetDefault<UMetaSoundSettings>())
	{
		if (const UMetasoundEditorSettings* EditorSettings = GetDefault<UMetasoundEditorSettings>())
		{
			bool bIsAuditionable = false;
			auto PageIsTargetable = [&InPageID, &bIsAuditionable](const FGuid& PlatformTargetPageID)
			{
				bIsAuditionable |= PlatformTargetPageID == InPageID;
			};
			Settings->IterateCookedTargetPageIDs(EditorSettings->AuditionPlatform, PageIsTargetable);
			return bIsAuditionable;
		}
	}
#endif // WITH_EDITORONLY_DATA

	return false;
}

void UMetaSoundEditorSubsystem::RegisterGraphWithFrontend(UObject& InMetaSound, bool bInForceViewSynchronization) const
{
	Metasound::Editor::FGraphBuilder::RegisterGraphWithFrontend(InMetaSound, bInForceViewSynchronization);
}

void UMetaSoundEditorSubsystem::RegisterToolbarExtender(TSharedRef<FExtender> InExtender)
{
	EditorToolbarExtenders.AddUnique(InExtender);
}

void UMetaSoundEditorSubsystem::SetFocusedPage(UMetaSoundBuilderBase* Builder, FName PageName, bool bOpenEditor, EMetaSoundBuilderResult& OutResult) const
{
	using namespace Metasound::Frontend;
	if (!Builder)
	{
		OutResult = EMetaSoundBuilderResult::Failed;
		return;
	}

	const UMetaSoundSettings* Settings = GetDefault<UMetaSoundSettings>();
	check(Settings);
	if (const FMetaSoundPageSettings* PageSettings = Settings->FindPageSettings(PageName))
	{
		constexpr bool bPostTransaction = true;
		const bool bFocusedPage = SetFocusedPageInternal(PageSettings->Name, PageSettings->UniqueId, *Builder, bOpenEditor, bPostTransaction);
		if (bFocusedPage)
		{
			OutResult = EMetaSoundBuilderResult::Succeeded;
			return;
		}
	}

	OutResult = EMetaSoundBuilderResult::Failed;
}

bool UMetaSoundEditorSubsystem::SetFocusedPage(UMetaSoundBuilderBase& Builder, const FGuid& InPageID, bool bOpenEditor, bool bPostTransaction) const
{
	using namespace Metasound::Frontend;

	const UMetaSoundSettings* Settings = GetDefault<UMetaSoundSettings>();
	check(Settings);

	FName PageName;
	if (const FMetaSoundPageSettings* PageSettings = Settings->FindPageSettings(InPageID))
	{
		PageName = PageSettings->Name;
	}

	return SetFocusedPageInternal(PageName, InPageID, Builder, bOpenEditor, bPostTransaction);
}

bool UMetaSoundEditorSubsystem::SetFocusedPageInternal(FName PageName, const FGuid& InPageID, UMetaSoundBuilderBase& Builder, bool bOpenEditor, bool bPostTransaction) const
{
	using namespace Metasound::Frontend;

	const FScopedTransaction Transaction(FText::Format(LOCTEXT("SetFocusedPageTransactionFormat", "Set Focused Page '{0}'"), FText::FromName(PageName)), bPostTransaction);
	bool bAuditionPageSet = false;
	// Must set audition target page before setting build page ID as listeners
	// to build page ID changes need to reliably be able to adjust to newly assigned
	// audition target page.
	UMetasoundEditorSettings* EditorSettings = GetMutableDefault<UMetasoundEditorSettings>();
	check(EditorSettings);
	if (EditorSettings->AuditionPageMode == EAuditionPageMode::Focused)
	{
		if (EditorSettings->AuditionPage != PageName)
		{
			EditorSettings->Modify();
			EditorSettings->AuditionPage = PageName;
			bAuditionPageSet = true;
		}
	}

	const FMetaSoundFrontendDocumentBuilder& DocBuilder = Builder.GetConstBuilder();
	if (DocBuilder.GetBuildPageID() != InPageID)
	{
		Builder.Modify();
		UObject& MetaSound = DocBuilder.CastDocumentObjectChecked<UObject>();
		if (Builder.GetBuilder().SetBuildPageID(InPageID))
		{
			// Reregister to ensure all future audible instances are using the new page implementation.
			RegisterGraphWithFrontend(MetaSound);
		}

		if (GEditor && bOpenEditor)
		{
			if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				AssetEditorSubsystem->OpenEditorForAsset(&MetaSound);
			}
		}
	}

	return bAuditionPageSet;
}

bool UMetaSoundEditorSubsystem::UnregisterToolbarExtender(TSharedRef<FExtender> InExtender)
{
	const int32 NumRemoved = EditorToolbarExtenders.RemoveAllSwap([&InExtender](const TSharedRef<FExtender>& Extender) { return Extender == InExtender; });
	return NumRemoved > 0;
}

void UMetaSoundEditorSubsystem::SetNodeLocation(
	UMetaSoundBuilderBase* InBuilder,
	const FMetaSoundNodeHandle& InNode,
	const FVector2D& InLocation,
	EMetaSoundBuilderResult& OutResult)
{
	if (InBuilder)
	{
		InBuilder->SetNodeLocation(InNode, InLocation, OutResult);
	}
	else
	{
		OutResult = EMetaSoundBuilderResult::Failed;
	}
}

void UMetaSoundEditorSubsystem::SetSoundWaveSettingsFromTemplate(USoundWave& NewMetaSoundWave, const USoundWave& TemplateSoundWave) const
{
	// Sound 
	NewMetaSoundWave.Volume = TemplateSoundWave.Volume;
	NewMetaSoundWave.Pitch = TemplateSoundWave.Pitch;
	NewMetaSoundWave.SoundClassObject = TemplateSoundWave.SoundClassObject;

	// Attenuation 
	NewMetaSoundWave.AttenuationSettings = TemplateSoundWave.AttenuationSettings;
	NewMetaSoundWave.bDebug = TemplateSoundWave.bDebug;

	// Effects 
	NewMetaSoundWave.bEnableBusSends = TemplateSoundWave.bEnableBusSends;
	NewMetaSoundWave.SourceEffectChain = TemplateSoundWave.SourceEffectChain;
	NewMetaSoundWave.BusSends = TemplateSoundWave.BusSends;
	NewMetaSoundWave.PreEffectBusSends = TemplateSoundWave.PreEffectBusSends;

	NewMetaSoundWave.bEnableBaseSubmix = TemplateSoundWave.bEnableBaseSubmix;
	NewMetaSoundWave.SoundSubmixObject = TemplateSoundWave.SoundSubmixObject;
	NewMetaSoundWave.bEnableSubmixSends = TemplateSoundWave.bEnableSubmixSends;
	NewMetaSoundWave.SoundSubmixSends = TemplateSoundWave.SoundSubmixSends;

	// Modulation 
	NewMetaSoundWave.ModulationSettings = TemplateSoundWave.ModulationSettings;

	// Voice Management 
	NewMetaSoundWave.VirtualizationMode = TemplateSoundWave.VirtualizationMode;
	NewMetaSoundWave.bOverrideConcurrency = TemplateSoundWave.bOverrideConcurrency;
	NewMetaSoundWave.ConcurrencySet = TemplateSoundWave.ConcurrencySet;
	NewMetaSoundWave.ConcurrencyOverrides = TemplateSoundWave.ConcurrencyOverrides;

	NewMetaSoundWave.bBypassVolumeScaleForPriority = TemplateSoundWave.bBypassVolumeScaleForPriority;
	NewMetaSoundWave.Priority = TemplateSoundWave.Priority;

	//Property Sheets - keep this last so that properties in the sheet will be applied
	NewMetaSoundWave.AudioPropertiesSheet = TemplateSoundWave.AudioPropertiesSheet;

	if (NewMetaSoundWave.AudioPropertiesSheet)
	{
		NewMetaSoundWave.AudioPropertiesSheet->CopyToObjectProperties(&NewMetaSoundWave);
	}

	return;
}

#undef LOCTEXT_NAMESPACE // "MetaSoundEditor"
