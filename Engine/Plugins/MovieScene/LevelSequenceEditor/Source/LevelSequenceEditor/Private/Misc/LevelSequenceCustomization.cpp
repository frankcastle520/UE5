// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/LevelSequenceCustomization.h"

#include "ClassViewerModule.h"
#include "Engine/LevelStreaming.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "ISequencer.h"
#include "ISequencerModule.h"
#include "LevelSequenceDirector.h"
#include "LevelSequenceEditorCommands.h"
#include "LevelSequenceFBXInterop.h"
#include "Modules/ModuleManager.h"
#include "MovieScene.h"
#include "MVVM/Extensions/IObjectBindingExtension.h"
#include "MVVM/Selection/Selection.h"
#include "MVVM/ViewModels/ObjectBindingModel.h"
#include "MVVM/ViewModels/SequencerEditorViewModel.h"
#include "ScopedTransaction.h"
#include "SequencerCommands.h"
#include "SequencerUtilities.h"
#include "MovieSceneBindingReferences.h"
#include "Bindings/MovieSceneSpawnableBinding.h"
#include "ClassViewerFilter.h"

#define LOCTEXT_NAMESPACE "LevelSequenceCustomization"

namespace UE::Sequencer
{

void FLevelSequenceCustomization::RegisterSequencerCustomization(FSequencerCustomizationBuilder& Builder)
{
	WeakSequencer = Builder.GetSequencer().AsShared();

	const FLevelSequenceEditorCommands& Commands = FLevelSequenceEditorCommands::Get();

	// Build the extender for the actions menu.
	ActionsMenuCommandList = MakeShared<FUICommandList>().ToSharedPtr();
	ActionsMenuCommandList->MapAction(
		Commands.ImportFBX,
		FExecuteAction::CreateRaw( this, &FLevelSequenceCustomization::ImportFBX ),
		FCanExecuteAction::CreateLambda( [] { return true; } ) );
	ActionsMenuCommandList->MapAction(
		Commands.ExportFBX,
		FExecuteAction::CreateRaw( this, &FLevelSequenceCustomization::ExportFBX ),
		FCanExecuteAction::CreateLambda( [] { return true; } ) );

	ActionsMenuExtender = MakeShared<FExtender>();
	ActionsMenuExtender->AddMenuExtension(
			"SequenceOptions", EExtensionHook::First, nullptr,
			FMenuExtensionDelegate::CreateRaw(this, &FLevelSequenceCustomization::ExtendActionsMenu));

	ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
	SequencerModule.GetActionsMenuExtensibilityManager()->AddExtender(ActionsMenuExtender);

	// Add a customization callback for the object binding context menu.
	FSequencerCustomizationInfo Customization;
	Customization.OnBuildObjectBindingContextMenu = FOnGetSequencerMenuExtender::CreateRaw(this, &FLevelSequenceCustomization::CreateObjectBindingContextMenuExtender);
	Customization.OnBuildSidebarMenu = FOnGetSequencerMenuExtender::CreateRaw(this, &FLevelSequenceCustomization::CreateObjectBindingSidebarMenuExtender);
	Builder.AddCustomization(Customization);
}

void FLevelSequenceCustomization::UnregisterSequencerCustomization()
{
	ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
	SequencerModule.GetActionsMenuExtensibilityManager()->RemoveExtender(ActionsMenuExtender);

	ActionsMenuCommandList = nullptr;
	WeakSequencer = nullptr;
}

void FLevelSequenceCustomization::ExtendActionsMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.PushCommandList(ActionsMenuCommandList.ToSharedRef());
	{
		const FLevelSequenceEditorCommands& Commands = FLevelSequenceEditorCommands::Get();
		
		MenuBuilder.AddMenuEntry(
				LOCTEXT("SaveAs", "Save As..."),
				LOCTEXT("SaveAsTooltip", "Saves the current sequence under a different name"),
				FSlateIcon(Commands.GetStyleSetName(), "LevelSequenceEditor.SaveAs"),
				FUIAction(FExecuteAction::CreateRaw(this, &FLevelSequenceCustomization::OnSaveMovieSceneAsClicked)));

		MenuBuilder.AddMenuEntry(Commands.ImportFBX);
		MenuBuilder.AddMenuEntry(Commands.ExportFBX);
	}
	MenuBuilder.PopCommandList();
}

void FLevelSequenceCustomization::OnSaveMovieSceneAsClicked()
{
	FSequencerUtilities::SaveCurrentMovieSceneAs(WeakSequencer.Pin().ToSharedRef());
}

void FLevelSequenceCustomization::ImportFBX()
{
	FLevelSequenceFBXInterop Interop(WeakSequencer.Pin().ToSharedRef());
	Interop.ImportFBX();
}

void FLevelSequenceCustomization::ExportFBX()
{
	FLevelSequenceFBXInterop Interop(WeakSequencer.Pin().ToSharedRef());
	Interop.ExportFBX();
}

TSharedPtr<FExtender> FLevelSequenceCustomization::CreateObjectBindingContextMenuExtender(FViewModelPtr InViewModel)
{
	TSharedRef<FExtender> Extender = MakeShared<FExtender>();
	TSharedPtr<FObjectBindingModel> ObjectBindingModel = InViewModel->CastThisShared<FObjectBindingModel>();
	Extender->AddMenuExtension(
			"ObjectBindingActions", EExtensionHook::Before, nullptr,
			FMenuExtensionDelegate::CreateRaw(this, &FLevelSequenceCustomization::ExtendObjectBindingContextMenu, ObjectBindingModel));
	return Extender.ToSharedPtr();
}

void FLevelSequenceCustomization::ExtendObjectBindingContextMenu(FMenuBuilder& MenuBuilder, TSharedPtr<FObjectBindingModel> ObjectBindingModel)
{
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	TSharedPtr<FSequencerEditorViewModel> EditorViewModel = Sequencer->GetViewModel();

	FGuid ObjectBindingID = ObjectBindingModel->GetObjectGuid();
	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	UMovieScene* MovieScene = Sequence->GetMovieScene();

	if (!MovieScene || !ObjectBindingID.IsValid())
	{
		return;
	}
	bool bShowConvert = true;

	if (FMovieScenePossessable* Possessable = MovieScene->FindPossessable(ObjectBindingID))
	{
		// We can't convert sub-objects to different binding types for now.
		if (Possessable->GetParent().IsValid())
		{
			bShowConvert = false;
		}
		bool bCustomBinding = false;
		bool bMultipleBindings = false;
		UObject* ResolutionContext = MovieSceneHelpers::GetResolutionContext(Sequence, ObjectBindingID, Sequencer->GetFocusedTemplateID(), Sequencer->GetSharedPlaybackState());

		if (const FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
		{
			bCustomBinding = Algo::AnyOf(BindingReferences->GetReferences(ObjectBindingID), [](const FMovieSceneBindingReference& Reference) { return Reference.CustomBinding; });
			bMultipleBindings = BindingReferences->GetReferences(ObjectBindingID).Num() > 1;
			UE::UniversalObjectLocator::FResolveParams LocatorResolveParams(ResolutionContext);
			FMovieSceneBindingResolveParams BindingResolveParams{ Sequence, ObjectBindingID, Sequencer->GetFocusedTemplateID(), ResolutionContext };

			// Can convert to possessable
			int32 BindingIndex = 0;
			bool bAnyValidConversions = false;
			if (Algo::AnyOf(BindingReferences->GetReferences(ObjectBindingID), [&BindingIndex, Sequencer](const FMovieSceneBindingReference& BindingReference) {
				return FSequencerUtilities::CanConvertToPossessable(Sequencer.ToSharedRef(), BindingReference.ID, BindingIndex++);
				}))
			{
				bAnyValidConversions = true;
			}
			else
			{
				TArrayView<const TSubclassOf<UMovieSceneCustomBinding>> PrioritySortedCustomBindingTypes = Sequencer->GetSupportedCustomBindingTypes();
				for (const TSubclassOf<UMovieSceneCustomBinding>& CustomBindingType : PrioritySortedCustomBindingTypes)
				{
					BindingIndex = 0;
					if (Algo::AllOf(BindingReferences->GetReferences(ObjectBindingID), [&BindingIndex, &CustomBindingType, Sequencer](const FMovieSceneBindingReference& BindingReference)
						{
							return FSequencerUtilities::CanConvertToCustomBinding(Sequencer.ToSharedRef(), BindingReference.ID, CustomBindingType, BindingIndex++);
						}))
					{
						bAnyValidConversions = true;
						break;
					}
				}
			}
			if (!bAnyValidConversions)
			{
				bShowConvert = false;
			}
		}

		// Regular possessable
		if (!bCustomBinding)
		{
			// Regular possessable
			// We don't add anything here, but the extension will
			MenuBuilder.BeginSection("Possessable");
			MenuBuilder.EndSection();
		}
		else
		{
			MenuBuilder.BeginSection("CustomBinding");
			bool bCustomSpawnable = MovieSceneHelpers::SupportsObjectTemplate(Sequence, ObjectBindingID, Sequencer->GetSharedPlaybackState());
			// Check for custom binding types

			if (bCustomSpawnable)
			{
				MenuBuilder.AddMenuEntry(FSequencerCommands::Get().SaveCurrentSpawnableState);

				if (!bMultipleBindings)
				{
					MenuBuilder.AddSubMenu(
						LOCTEXT("ChangeClassLabel", "Change Class"),
						LOCTEXT("ChangeClassTooltip", "Change the class (object template) that this spawns from"),
						FNewMenuDelegate::CreateLambda([this](FMenuBuilder& MenuBuilder)
						{
							const TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
							if (!Sequencer.IsValid())
							{
								return;
							}

							UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();

							TArray<FSequencerChangeBindingInfo> Bindings;
							const FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences();
							for (TViewModelPtr<IObjectBindingExtension> ObjectBindingNode : Sequencer->GetViewModel()->GetSelection()->Outliner.Filter<IObjectBindingExtension>())
							{
								int32 BindingIndex = 0;
								for (const FMovieSceneBindingReference& Reference : BindingReferences->GetReferences(ObjectBindingNode->GetObjectGuid()))
								{
									Bindings.Add({ Reference.ID, BindingIndex++ });
								}
							}

							FSequencerUtilities::AddChangeClassMenu(MenuBuilder, Sequencer.ToSharedRef(), Bindings, TFunction<void()>());
						}));
				}
			}

			MenuBuilder.EndSection();
		}
	}
	
	if (bShowConvert)
	{
		// We don't add anything here, but the extension will
		MenuBuilder.BeginSection("ConvertBinding");
		MenuBuilder.EndSection();
	}

	MenuBuilder.BeginSection("Import/Export", LOCTEXT("ImportExportMenuSectionName", "Import/Export"));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("ImportFBX", "Import..."),
		LOCTEXT("ImportFBXTooltip", "Import FBX animation to this object"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this] {
				const TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
				if (!Sequencer.IsValid())
				{
					return;
				}
				
				FLevelSequenceFBXInterop Interop(Sequencer.ToSharedRef());
					Interop.ImportFBXOntoSelectedNodes();
				})
		));

	MenuBuilder.AddMenuEntry(
		LOCTEXT("ExportFBX", "Export..."),
		LOCTEXT("ExportFBXTooltip", "Export FBX animation from this object"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this] {
				const TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
				if (!Sequencer.IsValid())
				{
					return;
				}
				
				FLevelSequenceFBXInterop Interop(Sequencer.ToSharedRef());
					Interop.ExportFBX();
				})
		));

	MenuBuilder.EndSection();
}

TSharedPtr<FExtender> FLevelSequenceCustomization::CreateObjectBindingSidebarMenuExtender(FViewModelPtr InViewModel)
{
	TSharedRef<FExtender> Extender = MakeShared<FExtender>();
	
	TSharedPtr<FObjectBindingModel> ObjectBindingModel = InViewModel->CastThisShared<FObjectBindingModel>();
	
	Extender->AddMenuExtension(TEXT("ObjectBindingActions"), EExtensionHook::Before, nullptr,
		FMenuExtensionDelegate::CreateRaw(this, &FLevelSequenceCustomization::ExtendObjectBindingSidebarMenu, ObjectBindingModel));
	
	return Extender.ToSharedPtr();
}

void FLevelSequenceCustomization::ExtendObjectBindingSidebarMenu(FMenuBuilder& MenuBuilder, TSharedPtr<FObjectBindingModel> ObjectBindingModel)
{
	ExtendObjectBindingContextMenu(MenuBuilder, ObjectBindingModel);
}

} // namespace UE::Sequencer

#undef LOCTEXT_NAMESPACE
