// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSequenceEditorSubsystem.h"
#include "MVVM/ViewModels/SequencerEditorViewModel.h"
#include "Scripting/SequencerModuleScriptingLayer.h"
#include "SequencerCurveEditorObject.h"
#include "Evaluation/MovieScenePlayback.h"
#include "ISequencerModule.h"
#include "Framework/Commands/UICommandList.h"
#include "LevelSequence.h"
#include "ISceneOutliner.h"
#include "LevelSequenceEditorCommands.h"
#include "MovieScenePossessable.h"
#include "SequencerSettings.h"
#include "MovieScene.h"
#include "MovieSceneSpawnable.h"
#include "SequencerUtilities.h"
#include "Sections/MovieScene3DConstraintSection.h"
#include "Selection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Tracks/MovieScene3DConstraintTrack.h"
#include "Tracks/MovieSceneCameraShakeTrack.h"

#include "ActorTreeItem.h"
#include "Editor.h"
#include "PropertyEditorModule.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSpacer.h"
#include "Modules/ModuleManager.h"

#include "Camera/CameraComponent.h"
#include "ClassViewerFilter.h"
#include "ClassViewerModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Modules/ModuleManager.h"
#include "SceneOutlinerModule.h"
#include "ScopedTransaction.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "BakingAnimationKeySettings.h"
#include "IStructureDetailsView.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/NumericTypeInterface.h"
#include "FrameNumberDetailsCustomization.h"
#include "MovieSceneToolHelpers.h"
#include "ActorForWorldTransforms.h"
#include "KeyParams.h"
#include "Sections/MovieSceneBindingLifetimeSection.h"
#include "Tracks/MovieSceneBindingLifetimeTrack.h"
#include "Bindings/MovieSceneSpawnableBindingCustomization.h"
#include "Bindings/MovieSceneSpawnableBinding.h"
#include "Bindings/MovieSceneSpawnableActorBinding.h"
#include "Bindings/MovieSceneSpawnableActorBindingCustomization.h"
#include "Bindings/MovieSceneCustomBinding.h"
#include "DetailWidgetRow.h"
#include "IPropertyTypeCustomization.h"
#include "IDetailChildrenBuilder.h"
#include "Widgets/Input/SComboBox.h"
#include "DetailLayoutBuilder.h"
#include "IPropertyTypeCustomization.h"
#include "IPropertyUtilities.h"
#include "MovieSceneDynamicBindingCustomization.h"
#include "Conditions/MovieSceneConditionCustomization.h"
#include "Conditions/MovieSceneDirectorBlueprintConditionCustomization.h"
#include "MVVM/Selection/Selection.h"
#include "UnrealEdGlobals.h"
#include "Misc/FeedbackContext.h"
#include "Variants/MovieSceneTimeWarpVariant.h"
#include "Variants/MovieSceneTimeWarpGetter.h"
#include "UObject/UObjectIterator.h"
#include "ObjectTools.h"
#include "Bindings/MovieSceneSpawnableDirectorBlueprintBinding.h"
#include "Bindings/MovieSceneReplaceableDirectorBlueprintBinding.h"
#include "Bindings/MovieSceneReplaceableActorBinding.h"
#include "AssetToolsModule.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "MovieSceneSequencePlayer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LevelSequenceEditorSubsystem)

DEFINE_LOG_CATEGORY(LogLevelSequenceEditor);

#define LOCTEXT_NAMESPACE "LevelSequenceEditor"

class FMovieSceneBindingPropertyInfoListCustomization : public IDetailCustomization
{
public:

	FMovieSceneBindingPropertyInfoListCustomization(TWeakPtr<ISequencer> InSequencer, UMovieScene* InMovieScene, FGuid InBindingGuid, ULevelSequenceEditorSubsystem* InLevelSequenceEditorSubsystem);

	static TSharedRef<IDetailCustomization> MakeInstance(TWeakPtr<ISequencer> InSequencer, UMovieScene* InMovieScene, FGuid InBindingGuid, ULevelSequenceEditorSubsystem* InLevelSequenceEditorSubsystem);
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;


private:

	TSharedRef<SWidget> OnGetConvertMenuContent(IDetailLayoutBuilder& DetailBuilder);

	TSharedRef<SWidget> GenerateBindingTypePicker(IDetailLayoutBuilder& DetailBuilder);
	const FSlateBrush* GetBindingTypeIcon() const;
	FText GetBindingTypeValueAsString() const;

	TWeakPtr<ISequencer> SequencerPtr;
	TObjectPtr<UMovieScene> MovieScene = nullptr;
	TObjectPtr<ULevelSequenceEditorSubsystem> LevelSequenceEditorSubsystem;
	FGuid BindingGuid;
	TObjectPtr<UMovieSceneBindingPropertyInfoList> BindingList;

	TArray<TSharedPtr<FText>> BindingTypeNames;
};

FMovieSceneBindingPropertyInfoListCustomization::FMovieSceneBindingPropertyInfoListCustomization(TWeakPtr<ISequencer> InSequencer, UMovieScene* InMovieScene, FGuid InBindingGuid, ULevelSequenceEditorSubsystem* InLevelSequenceEditorSubsystem)
	: SequencerPtr(InSequencer)
	, MovieScene(InMovieScene)
	, LevelSequenceEditorSubsystem(InLevelSequenceEditorSubsystem)
	, BindingGuid(InBindingGuid)
{

}

TSharedRef<IDetailCustomization> FMovieSceneBindingPropertyInfoListCustomization::MakeInstance(TWeakPtr<ISequencer> InSequencer, UMovieScene* InMovieScene, FGuid InBindingGuid, ULevelSequenceEditorSubsystem* InLevelSequenceEditorSubsystem)
{
	return MakeShareable(new FMovieSceneBindingPropertyInfoListCustomization(InSequencer, InMovieScene, InBindingGuid, InLevelSequenceEditorSubsystem));
}

void FMovieSceneBindingPropertyInfoListCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& SectionCategory = DetailBuilder.EditCategory("Binding Properties", FText(), ECategoryPriority::Important);

	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	TSharedPtr<ISequencer> Sequencer = SequencerPtr.Pin();

	if (Objects.Num() > 0)
	{
		BindingList = Cast<UMovieSceneBindingPropertyInfoList>(Objects[0].Get());
		if (BindingList)
		{
			if (Sequencer.IsValid())
			{
				UMovieSceneSequence* Sequence = MovieScene->GetTypedOuter<UMovieSceneSequence>();
				if (BindingList->Bindings.Num() > 0)
				{
					// Grab the first one- we guarantee the binding types are the same.
					bool bShowConvert = true;
					if (FMovieScenePossessable* Possessable = MovieScene->FindPossessable(BindingGuid))
					{
						if (Possessable && Possessable->GetParent().IsValid())
						{
							bShowConvert = false;
						}
					}
					bool bHasBoundObject = MovieSceneHelpers::GetSingleBoundObject(Sequence, BindingGuid, Sequencer->GetSharedPlaybackState(), 0) != nullptr;

					FDetailWidgetRow& BindingTypeRow = SectionCategory.AddCustomRow(FText::GetEmpty());
					BindingTypeRow.NameContent()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("BindingPropertyType", "Binding Type"))
						.ToolTipText(LOCTEXT("BindingPropertyType_Tooltip", "The type of binding for this object binding track entry"))
						.Font(IDetailLayoutBuilder::GetDetailFont())
						];
					BindingTypeRow.ValueContent()
						[
							SNew(SComboButton)
							.OnGetMenuContent(FOnGetContent::CreateLambda([this, &DetailBuilder]() { return GenerateBindingTypePicker(DetailBuilder);}))
							.ContentPadding(0.0f)
							.ButtonContent()
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								.Padding(0.0f, 0.0f, 4.0f, 0.0f)
								[
									SNew(SImage)
									.Image(this, &FMovieSceneBindingPropertyInfoListCustomization::GetBindingTypeIcon)
								]
							+ SHorizontalBox::Slot()
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
									.Text(this, &FMovieSceneBindingPropertyInfoListCustomization::GetBindingTypeValueAsString)
								]
							]
							.IsEnabled(!bHasBoundObject)
						];

					// Only show certain menus if we have a currently bound object
					if (bShowConvert && bHasBoundObject)
					{
						FDetailWidgetRow& ConvertToRow = SectionCategory.AddCustomRow(LOCTEXT("ConvertBindingTo", "Convert Binding(s) To..."));
						ConvertToRow.WholeRowContent()
							[
								SNew(SComboButton)
								.OnGetMenuContent(FOnGetContent::CreateLambda([this, &DetailBuilder]() { return OnGetConvertMenuContent(DetailBuilder); }))
							.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
							.ButtonContent()
							[
								SNew(STextBlock).Text(LOCTEXT("ConvertBindingTo", "Convert Binding(s) To..."))
							]
							];
					}
				}
			}
		}
	}
}

TSharedRef<SWidget> FMovieSceneBindingPropertyInfoListCustomization::GenerateBindingTypePicker(IDetailLayoutBuilder& DetailBuilder)
{
	FMenuBuilder MenuBuilder(true, nullptr, nullptr, true);
	if (SequencerPtr.IsValid())
	{
		TSharedRef<ISequencer> Sequencer = SequencerPtr.Pin().ToSharedRef();

		TArray<FSequencerChangeBindingInfo> Bindings;
		for (int32 BindingIndex = 0; BindingIndex < BindingList->Bindings.Num(); ++BindingIndex)
		{
			Bindings.Add({ BindingGuid, BindingIndex });
		}
		LevelSequenceEditorSubsystem->AddChangeBindingTypeMenu(MenuBuilder, Sequencer, Bindings, false, [this, Sequencer, &DetailBuilder]()
			{
				if (IDetailsView* DetailsView = DetailBuilder.GetDetailsView())
				{
					LevelSequenceEditorSubsystem->RefreshBindingDetails(DetailsView, BindingGuid);
					LevelSequenceEditorSubsystem->OnFinishedChangingLocators(FPropertyChangedEvent(nullptr), StaticCastSharedRef<IDetailsView>(DetailsView->AsShared()), BindingGuid);
				}
			});
	}
	return MenuBuilder.MakeWidget();
}

FText FMovieSceneBindingPropertyInfoListCustomization::GetBindingTypeValueAsString() const
{
	TSharedPtr<ISequencer> Sequencer = SequencerPtr.Pin();
	if (BindingList)
	{
		if (Sequencer.IsValid())
		{
			UMovieSceneSequence* Sequence = MovieScene->GetTypedOuter<UMovieSceneSequence>();
			if (BindingList->Bindings.Num() > 0)
			{
				// All bindings will be the same type
				if (!BindingList->Bindings[0].CustomBinding)
				{
					// Possessable
					return LOCTEXT("BindingType_Possessable", "Possessable");
				}
				else
				{
					 return BindingList->Bindings[0].CustomBinding->GetBindingTypePrettyName();
				}
			}
		}
	}
	return FText();
}

const FSlateBrush* FMovieSceneBindingPropertyInfoListCustomization::GetBindingTypeIcon() const
{
	TSharedPtr<ISequencer> Sequencer = SequencerPtr.Pin();
	if (BindingList)
	{
		if (Sequencer.IsValid())
		{
			UMovieSceneSequence* Sequence = MovieScene->GetTypedOuter<UMovieSceneSequence>();
			if (BindingList->Bindings.Num() > 0)
			{
				// All bindings will be the same type
				if (!BindingList->Bindings[0].CustomBinding)
				{
					// Possessable
					return nullptr;
				}
				else
				{
					return BindingList->Bindings[0].CustomBinding->GetBindingTrackCustomIconOverlay().GetIcon();
				}
			}
		}
	}

	return nullptr;
}

TSharedRef<SWidget> FMovieSceneBindingPropertyInfoListCustomization::OnGetConvertMenuContent(IDetailLayoutBuilder& DetailBuilder)
{
	FMenuBuilder MenuBuilder(true, nullptr, nullptr, true);


	if (SequencerPtr.IsValid() && BindingList)
	{
		TSharedRef<ISequencer> Sequencer = SequencerPtr.Pin().ToSharedRef();
		TArray<FSequencerChangeBindingInfo> Bindings;
		for (int32 BindingIndex = 0; BindingIndex < BindingList->Bindings.Num(); ++BindingIndex)
		{
			Bindings.Add({ BindingGuid, BindingIndex });
		}

		LevelSequenceEditorSubsystem->AddChangeBindingTypeMenu(MenuBuilder, Sequencer, Bindings, true, [this, &DetailBuilder]()
			{
				if (IDetailsView* DetailsView = DetailBuilder.GetDetailsView())
				{
					LevelSequenceEditorSubsystem->RefreshBindingDetails(DetailsView, BindingGuid);
				}
			});
	}
	return MenuBuilder.MakeWidget();
}


class FMovieSceneBindingPropertyInfoDetailCustomization : public IPropertyTypeCustomization
{
public:
	FMovieSceneBindingPropertyInfoDetailCustomization(TWeakPtr<ISequencer> InSequencer, UMovieScene* InMovieScene, FGuid InBindingGuid, ULevelSequenceEditorSubsystem* InLevelSequenceEditorSubsystem);

	// Begin IDetailCustomization interface
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ End IDetailCustomization interface

private:

	TSharedRef<SWidget> OnGetChangeClassMenuContent(IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils);

	TWeakPtr<ISequencer> SequencerPtr;
	TObjectPtr<UMovieScene> MovieScene = nullptr;
	TObjectPtr<ULevelSequenceEditorSubsystem> LevelSequenceEditorSubsystem;
	FGuid BindingGuid;
	int32 BindingIndex = 0;

	TSharedPtr<IPropertyHandle> StructPropertyHandle;
};

FMovieSceneBindingPropertyInfoDetailCustomization::FMovieSceneBindingPropertyInfoDetailCustomization(TWeakPtr<ISequencer> InSequencer, UMovieScene* InMovieScene, FGuid InBindingGuid, ULevelSequenceEditorSubsystem* InLevelSequenceEditorSubsystem)
	: SequencerPtr(InSequencer)
	, MovieScene(InMovieScene)
	, LevelSequenceEditorSubsystem(InLevelSequenceEditorSubsystem)
	, BindingGuid(InBindingGuid)
{

}

void FMovieSceneBindingPropertyInfoDetailCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	HeaderRow.ShouldAutoExpand(true);

	HeaderRow.NameContent()
		[
			InStructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			InStructPropertyHandle->CreatePropertyValueWidget()
		];

	StructPropertyHandle = InStructPropertyHandle;
}

void FMovieSceneBindingPropertyInfoDetailCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<ISequencer> Sequencer = SequencerPtr.Pin();
	if (Sequencer.IsValid() && InStructPropertyHandle->IsValidHandle())
	{
		UMovieSceneSequence* Sequence = MovieScene->GetTypedOuter<UMovieSceneSequence>();
		TArray<void*> StructPtrs;
		StructPropertyHandle->AccessRawData(StructPtrs);
		FMovieSceneBindingPropertyInfo* BindingPropertyInfo = (StructPtrs.Num() == 1) ? reinterpret_cast<FMovieSceneBindingPropertyInfo*>(StructPtrs[0]) : nullptr;
		if (BindingPropertyInfo)
		{
			BindingIndex = InStructPropertyHandle->GetArrayIndex();

			// Show Change class and save default state menus for spawnables
			if (MovieSceneHelpers::SupportsObjectTemplate(Sequence, BindingGuid, Sequencer->GetSharedPlaybackState(), BindingIndex))
			{
				FDetailWidgetRow& ChangeClassRow = StructBuilder.AddCustomRow(LOCTEXT("ChangeClass", "Change Class..."));
				ChangeClassRow.WholeRowContent()
					[
						SNew(SComboButton)
						.OnGetMenuContent(FOnGetContent::CreateLambda([this, &StructBuilder, &CustomizationUtils]() { return OnGetChangeClassMenuContent(StructBuilder, CustomizationUtils); }))
					.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
					.ButtonContent()
					[
						SNew(STextBlock).Text(LOCTEXT("ChangeClass", "Change Class..."))
					]
					];

				// Save Default State
				FDetailWidgetRow& SaveDefaultStateRow = StructBuilder.AddCustomRow(LOCTEXT("SaveDefaultState", "Save Default State"));
				SaveDefaultStateRow.WholeRowContent()
					[
						SNew(SButton)
						.Text(LOCTEXT("SaveDefaultState", "Save Default State"))
					.ToolTipText(LOCTEXT("SaveDefaultState_Tooltip", "Save the current state of this spawnable as default properties"))
					.OnClicked_Lambda([this]()
						{
							TSharedPtr<ISequencer> Sequencer = SequencerPtr.Pin();
							if (Sequencer.IsValid())
							{
								Sequencer->GetSpawnRegister().SaveDefaultSpawnableState(BindingGuid, BindingIndex, Sequencer->GetFocusedTemplateID(), Sequencer->GetSharedPlaybackState());
							}
							return FReply::Handled();
						})
					];
			}

			if (!BindingPropertyInfo->CustomBinding)
			{
				// Show locator property
				TSharedPtr<IPropertyHandle> LocatorProperty = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMovieSceneBindingPropertyInfo, Locator));
				if (LocatorProperty.IsValid())
				{
					StructBuilder.AddProperty(LocatorProperty.ToSharedRef());
				}
			}
			else
			{
				// Show instanced binding type property
				TSharedPtr<IPropertyHandle> CustomBindingProperty = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMovieSceneBindingPropertyInfo, CustomBinding));
				if (CustomBindingProperty.IsValid())
				{
					StructBuilder.AddProperty(CustomBindingProperty.ToSharedRef()).CustomWidget(true)
						.NameContent()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("BindingProperties", "Binding Properties"))
							.Font(IDetailLayoutBuilder::GetDetailFont())
						];
				}
			}
		}
	}
}


TSharedRef<SWidget> FMovieSceneBindingPropertyInfoDetailCustomization::OnGetChangeClassMenuContent(IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	FMenuBuilder MenuBuilder(true, nullptr, nullptr, true);

	TSharedPtr<ISequencer> Sequencer = SequencerPtr.Pin();
	if (Sequencer.IsValid() && StructPropertyHandle->IsValidHandle())
	{
		TArray<FSequencerChangeBindingInfo> Bindings;
		Bindings.Add(FSequencerChangeBindingInfo(BindingGuid, BindingIndex));

		FSequencerUtilities::AddChangeClassMenu(MenuBuilder, Sequencer.ToSharedRef(), Bindings, [this, &StructBuilder, &CustomizationUtils]()
			{
				if (IDetailsView* DetailsView = StructBuilder.GetParentCategory().GetParentLayout().GetDetailsView())
				{
					LevelSequenceEditorSubsystem->RefreshBindingDetails(DetailsView, BindingGuid);
				}
			});
	}
	return MenuBuilder.MakeWidget();
}

void ULevelSequenceEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	UE_LOG(LogLevelSequenceEditor, Log, TEXT("LevelSequenceEditor subsystem initialized."));

	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
	OnSequencerCreatedHandle = SequencerModule.RegisterOnSequencerCreated(FOnSequencerCreated::FDelegate::CreateUObject(this, &ULevelSequenceEditorSubsystem::OnSequencerCreated));

	auto AreActorsSelected = [this]{
		TArray<AActor*> SelectedActors;
		GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors); 
		return SelectedActors.Num() > 0;
	};

	auto AreMovieSceneSectionsSelected = [this](const int32 MinSections = 1) {
		const TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
		if (!Sequencer)
		{
			return false;
		}

		TArray<UMovieSceneSection*> SelectedSections;
		Sequencer->GetSelectedSections(SelectedSections);
		return (SelectedSections.Num() >= MinSections);
	};

	/* Commands for this subsystem */
	CommandList = MakeShareable(new FUICommandList);

	CommandList->MapAction(FLevelSequenceEditorCommands::Get().SnapSectionsToTimelineUsingSourceTimecode,
		FExecuteAction::CreateUObject(this, &ULevelSequenceEditorSubsystem::SnapSectionsToTimelineUsingSourceTimecodeInternal),
		FCanExecuteAction::CreateLambda(AreMovieSceneSectionsSelected)
	);

	CommandList->MapAction(FLevelSequenceEditorCommands::Get().SyncSectionsUsingSourceTimecode,
		FExecuteAction::CreateUObject(this, &ULevelSequenceEditorSubsystem::SyncSectionsUsingSourceTimecodeInternal),
		FCanExecuteAction::CreateLambda(AreMovieSceneSectionsSelected, 2)
	);

	CommandList->MapAction(FLevelSequenceEditorCommands::Get().BakeTransform,
		FExecuteAction::CreateUObject(this, &ULevelSequenceEditorSubsystem::BakeTransformInternal)
	);

	CommandList->MapAction(FLevelSequenceEditorCommands::Get().FixActorReferences,
		FExecuteAction::CreateUObject(this, &ULevelSequenceEditorSubsystem::FixActorReferences)
	);

	CommandList->MapAction(FLevelSequenceEditorCommands::Get().AddActorsToBinding,
		FExecuteAction::CreateUObject(this, &ULevelSequenceEditorSubsystem::AddActorsToBindingInternal),
		FCanExecuteAction::CreateLambda(AreActorsSelected));

	CommandList->MapAction(FLevelSequenceEditorCommands::Get().ReplaceBindingWithActors,
		FExecuteAction::CreateUObject(this, &ULevelSequenceEditorSubsystem::ReplaceBindingWithActorsInternal),
		FCanExecuteAction::CreateLambda(AreActorsSelected));

	CommandList->MapAction(FLevelSequenceEditorCommands::Get().RemoveActorsFromBinding,
		FExecuteAction::CreateUObject(this, &ULevelSequenceEditorSubsystem::RemoveActorsFromBindingInternal),
		FCanExecuteAction::CreateLambda(AreActorsSelected));

	CommandList->MapAction(FLevelSequenceEditorCommands::Get().RemoveAllBindings,
		FExecuteAction::CreateUObject(this, &ULevelSequenceEditorSubsystem::RemoveAllBindingsInternal));

	CommandList->MapAction(FLevelSequenceEditorCommands::Get().RemoveInvalidBindings,
		FExecuteAction::CreateUObject(this, &ULevelSequenceEditorSubsystem::RemoveInvalidBindingsInternal));

	/* Menu extenders */
	TransformMenuExtender = MakeShareable(new FExtender);
	TransformMenuExtender->AddMenuExtension("Transform", EExtensionHook::After, CommandList, FMenuExtensionDelegate::CreateLambda([this](FMenuBuilder& MenuBuilder) {
		// Only add menu entries where the focused sequence is a ULevelSequence
		if (!GetActiveSequencer())
		{
			return;
		}
		
		MenuBuilder.AddMenuEntry(FLevelSequenceEditorCommands::Get().SnapSectionsToTimelineUsingSourceTimecode);
		MenuBuilder.AddMenuEntry(FLevelSequenceEditorCommands::Get().SyncSectionsUsingSourceTimecode);
		MenuBuilder.AddMenuEntry(FLevelSequenceEditorCommands::Get().BakeTransform);
		}));

	SequencerModule.GetActionsMenuExtensibilityManager()->AddExtender(TransformMenuExtender);

	FixActorReferencesMenuExtender = MakeShareable(new FExtender);
	FixActorReferencesMenuExtender->AddMenuExtension("Bindings", EExtensionHook::First, CommandList, FMenuExtensionDelegate::CreateLambda([this](FMenuBuilder& MenuBuilder) {
		// Only add menu entries where the focused sequence is a ULevelSequence
		if (!GetActiveSequencer())
		{
			return;
		}
		
		MenuBuilder.AddMenuEntry(FLevelSequenceEditorCommands::Get().FixActorReferences);
		}));

	SequencerModule.GetActionsMenuExtensibilityManager()->AddExtender(FixActorReferencesMenuExtender);

	AssignActorMenuExtender = MakeShareable(new FExtender);
	AssignActorMenuExtender->AddMenuExtension("Possessable", EExtensionHook::First, CommandList, FMenuExtensionDelegate::CreateLambda([this](FMenuBuilder& MenuBuilder) {

		if (!IsSelectedBindingRootPossessable())
		{
			return;
		}
		
		FFormatNamedArguments Args;
		MenuBuilder.AddSubMenu(
			FText::Format(LOCTEXT("AssignActor", "Assign Actor"), Args),
			FText::Format(LOCTEXT("AssignActorTooltip", "Assign an actor to this track"), Args),
			FNewMenuDelegate::CreateLambda([this](FMenuBuilder& SubMenuBuilder) { AddAssignActorMenu(SubMenuBuilder); } ));
		}));

	SequencerModule.GetObjectBindingContextMenuExtensibilityManager()->AddExtender(AssignActorMenuExtender);

	// For now we have the binding properties being a separate menu. When the UX is worked out we will likely merge the AssignActor menu away.
	BindingPropertiesMenuExtender = MakeShareable(new FExtender);
	BindingPropertiesMenuExtender->AddMenuExtension("Possessable", EExtensionHook::First, CommandList, FMenuExtensionDelegate::CreateLambda([this](FMenuBuilder& MenuBuilder) {

		FFormatNamedArguments Args;
		MenuBuilder.AddSubMenu(
			FText::Format(LOCTEXT("BindingProperties", "Binding Properties"), Args),
			FText::Format(LOCTEXT("BindingPropertiesTooltip", "Modify the actor and object bindings for this track"), Args),
			FNewMenuDelegate::CreateLambda([this](FMenuBuilder& SubMenuBuilder) { AddBindingPropertiesMenu(SubMenuBuilder); }));
		}));

	SequencerModule.GetObjectBindingContextMenuExtensibilityManager()->AddExtender(BindingPropertiesMenuExtender);

	RebindComponentMenuExtender = MakeShareable(new FExtender);
	RebindComponentMenuExtender->AddMenuExtension("Possessable", EExtensionHook::First, CommandList, FMenuExtensionDelegate::CreateLambda([this](FMenuBuilder& MenuBuilder) {
		// Only add menu entries where the focused sequence is a ULevelSequence
		if (!GetActiveSequencer())
		{
			return;
		}

		TArray<FName> ComponentNames;
		GetRebindComponentNames(ComponentNames);
		if (ComponentNames.Num() > 0)
		{
			FFormatNamedArguments Args;
			MenuBuilder.AddSubMenu(
				FText::Format(LOCTEXT("RebindComponent", "Rebind Component"), Args),
				FText::Format(LOCTEXT("RebindComponentTooltip", "Rebind component by moving the tracks from one component to another component."), Args),
				FNewMenuDelegate::CreateLambda([this](FMenuBuilder& SubMenuBuilder) { RebindComponentMenu(SubMenuBuilder); }));
		}
		}));

	BindingPropertiesMenuExtender->AddMenuExtension("CustomBinding", EExtensionHook::First, CommandList, FMenuExtensionDelegate::CreateLambda([this](FMenuBuilder& MenuBuilder) {

		FFormatNamedArguments Args;
		MenuBuilder.AddSubMenu(
			FText::Format(LOCTEXT("BindingProperties", "Binding Properties"), Args),
			FText::Format(LOCTEXT("BindingPropertiesTooltip", "Modify the actor and object bindings for this track"), Args),
			FNewMenuDelegate::CreateLambda([this](FMenuBuilder& SubMenuBuilder) { AddBindingPropertiesMenu(SubMenuBuilder); }));
		}));

	BindingPropertiesMenuExtender->AddMenuExtension("ConvertBinding", EExtensionHook::First, CommandList, FMenuExtensionDelegate::CreateLambda([this](FMenuBuilder& MenuBuilder) {
		 AddConvertBindingsMenu(MenuBuilder);
		}));

	SequencerModule.GetObjectBindingContextMenuExtensibilityManager()->AddExtender(RebindComponentMenuExtender);

	SidebarMenuExtender = MakeShared<FExtender>();

	SidebarMenuExtender->AddMenuExtension(TEXT("Possessable"), EExtensionHook::First, CommandList,
		FMenuExtensionDelegate::CreateLambda([this](FMenuBuilder& MenuBuilder)
			{
				// Only add menu entries where the focused sequence is a ULevelSequence
				if (!GetActiveSequencer())
				{
					return;
				}

				AddBindingPropertiesSidebar(MenuBuilder);
			}));

	SidebarMenuExtender->AddMenuExtension(TEXT("CustomBinding"), EExtensionHook::First, CommandList,
		FMenuExtensionDelegate::CreateLambda([this](FMenuBuilder& MenuBuilder)
			{
				AddBindingPropertiesMenu(MenuBuilder);
			}));

	SidebarMenuExtender->AddMenuExtension(TEXT("TrackRowMetadata"), EExtensionHook::First, CommandList,
		FMenuExtensionDelegate::CreateLambda([this](FMenuBuilder& MenuBuilder)
			{
				AddTrackRowMetadataMenu(MenuBuilder);
			}));

	SequencerModule.GetSidebarExtensibilityManager()->AddExtender(SidebarMenuExtender);
}

void ULevelSequenceEditorSubsystem::Deinitialize()
{
	UE_LOG(LogLevelSequenceEditor, Log, TEXT("LevelSequenceEditor subsystem deinitialized."));

	ISequencerModule* SequencerModulePtr = FModuleManager::Get().GetModulePtr<ISequencerModule>("Sequencer");
	if (SequencerModulePtr)
	{
		SequencerModulePtr->UnregisterOnSequencerCreated(OnSequencerCreatedHandle);
	}

	BindingPropertyInfoList = nullptr;
	TrackRowMetadataHelperList.Empty();
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnMenuBeingDestroyed().RemoveAll(this);
	}

}

void ULevelSequenceEditorSubsystem::OnSequencerCreated(TSharedRef<ISequencer> InSequencer)
{
	UE_LOG(LogLevelSequenceEditor, VeryVerbose, TEXT("ULevelSequenceEditorSubsystem::OnSequencerCreated"));

	Sequencers.Add(TWeakPtr<ISequencer>(InSequencer));
	InSequencer->OnCloseEvent().AddUObject(this, &ULevelSequenceEditorSubsystem::OnSequencerClosed);
}

void ULevelSequenceEditorSubsystem::OnSequencerClosed(TSharedRef<ISequencer> InSequencer)
{
	BindingPropertyInfoList = nullptr;
	TrackRowMetadataHelperList.Empty();
}

void ULevelSequenceEditorSubsystem::AddBindingDetailCustomizations(TSharedRef<IDetailsView> DetailsView, TSharedPtr<ISequencer> ActiveSequencer, FGuid BindingGuid)
{
	// TODO: Do we want to create a generalized way for folks to add instanced property layouts for other custom binding types so they can have access to sequencer context?
	if (ActiveSequencer.IsValid())
	{
		UMovieSceneSequence* Sequence = ActiveSequencer->GetFocusedMovieSceneSequence();
		UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
		if (MovieScene)
		{
			FPropertyEditorModule& PropertyEditor = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

			DetailsView->RegisterInstancedCustomPropertyLayout(UMovieSceneBindingPropertyInfoList::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FMovieSceneBindingPropertyInfoListCustomization::MakeInstance, ActiveSequencer.ToWeakPtr(), MovieScene, BindingGuid, this));
			
			DetailsView->RegisterInstancedCustomPropertyTypeLayout(FMovieSceneBindingPropertyInfo::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateLambda([](TWeakPtr<ISequencer> InSequencer, UMovieScene* InMovieScene, FGuid InBindingGuid, ULevelSequenceEditorSubsystem* LevelSequenceEditorSubsystem)
				{
					return MakeShared<FMovieSceneBindingPropertyInfoDetailCustomization>(InSequencer, InMovieScene, InBindingGuid, LevelSequenceEditorSubsystem);
				}, ActiveSequencer.ToWeakPtr(), MovieScene, BindingGuid, this));

			DetailsView->RegisterInstancedCustomPropertyTypeLayout(FMovieSceneDynamicBinding::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FMovieSceneDynamicBindingCustomization::MakeInstance, MovieScene, BindingGuid, 0));
			
			DetailsView->RegisterInstancedCustomPropertyLayout(UMovieSceneSpawnableActorBinding::StaticClass(), FOnGetDetailCustomizationInstance::CreateStatic(&FMovieSceneSpawnableActorBindingBaseCustomization::MakeInstance, ActiveSequencer.ToWeakPtr(), MovieScene, BindingGuid));
		}
	}
}

void ULevelSequenceEditorSubsystem::AddTrackRowMetadataCustomizations(TSharedRef<IDetailsView> DetailsView, TSharedPtr<ISequencer> ActiveSequencer, UMovieSceneSequence* Sequence)
{
	if (ActiveSequencer.IsValid())
	{
		UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
		if (MovieScene)
		{
			// Although we normally customize this type, we need to do it instanced here to pass in the sequence information, as it won't be part of an outer sequence UObject
			DetailsView->RegisterInstancedCustomPropertyTypeLayout("MovieSceneConditionContainer", FOnGetPropertyTypeCustomizationInstance::CreateLambda([Sequence, WeakSequencer=ActiveSequencer.ToWeakPtr()]() {
				return FMovieSceneConditionCustomization::MakeInstance(Sequence, WeakSequencer); }));

			DetailsView->RegisterInstancedCustomPropertyTypeLayout("MovieSceneDirectorBlueprintConditionData", FOnGetPropertyTypeCustomizationInstance::CreateLambda([=]() {
				return FMovieSceneDirectorBlueprintConditionCustomization::MakeInstance(MovieScene); }));
		}
	}
}

void ULevelSequenceEditorSubsystem::OnBindingPropertyMenuBeingDestroyed(const TSharedRef<IMenu>& Menu, TSharedRef<IDetailsView> DetailsView)
{
	TSharedPtr<SWidget> ContentWidget = Menu->GetContent();
	TSharedPtr<SWidget> ParentWidget = DetailsView;
	while (ParentWidget)
	{
		if (ParentWidget == ContentWidget)
		{
			// Binding Properties Menu has closed, clear the binding property list
			BindingPropertyInfoList = nullptr;
			FSlateApplication::Get().OnMenuBeingDestroyed().RemoveAll(this);
			break;
		}
		ParentWidget = ParentWidget->GetParentWidget();
	}
}

void ULevelSequenceEditorSubsystem::OnTrackRowMetadataMenuBeingDestroyed(const TSharedRef<IMenu>& Menu, TSharedRef<IDetailsView> DetailsView)
{
	TSharedPtr<SWidget> ContentWidget = Menu->GetContent();
	TSharedPtr<SWidget> ParentWidget = DetailsView;
	while (ParentWidget)
	{
		if (ParentWidget == ContentWidget)
		{
			// Track Row Metadata menu has closed, clear the metadata helper list
			TrackRowMetadataHelperList.Empty();
			FSlateApplication::Get().OnMenuBeingDestroyed().RemoveAll(this);
			break;
		}
		ParentWidget = ParentWidget->GetParentWidget();
	}
}

TSharedPtr<ISequencer> ULevelSequenceEditorSubsystem::GetActiveSequencer()
{
	for (TWeakPtr<ISequencer> Ptr : Sequencers)
	{
		if (Ptr.IsValid())
		{
			UMovieSceneSequence* Sequence = Ptr.Pin()->GetFocusedMovieSceneSequence();
			if (Sequence && Sequence->IsA<ULevelSequence>())
			{
				return Ptr.Pin();
			}
		}
	}

	return nullptr;
}

USequencerModuleScriptingLayer* ULevelSequenceEditorSubsystem::GetScriptingLayer()
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer)
	{
		return Cast<USequencerModuleScriptingLayer>(Sequencer->GetViewModel()->GetScriptingLayer());
	}
	return nullptr;
}

USequencerCurveEditorObject* ULevelSequenceEditorSubsystem::GetCurveEditor()
{
	TObjectPtr<USequencerCurveEditorObject> CurveEditorObject;
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer)
	{
		TObjectPtr<USequencerCurveEditorObject> *ExistingCurveEditorObject = CurveEditorObjects.Find(Sequencer);
		if (ExistingCurveEditorObject)
		{
			CurveEditorObject = *ExistingCurveEditorObject;
		}
		else
		{
			CurveEditorObject = NewObject<USequencerCurveEditorObject>(this);
			CurveEditorObject->SetSequencer(Sequencer);
			CurveEditorObjects.Add(Sequencer, CurveEditorObject);
			CurveEditorArray.Add(CurveEditorObject);
		}
	}
	return CurveEditorObject;
}

TArray<FMovieSceneBindingProxy> ULevelSequenceEditorSubsystem::AddActors(const TArray<AActor*>& InActors)
{
	TArray<FMovieSceneBindingProxy> BindingProxies;

	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return BindingProxies;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return BindingProxies;
	}

	TArray<TWeakObjectPtr<AActor> > Actors;
	for (AActor* Actor : InActors)
	{
		Actors.Add(Actor);
	}

	TArray<FGuid> Guids = FSequencerUtilities::AddActors(Sequencer.ToSharedRef(), Actors);
	
	for (const FGuid& Guid : Guids)
	{
		BindingProxies.Add(FMovieSceneBindingProxy(Guid, Sequence));
	}

	return BindingProxies;
}

FMovieSceneBindingProxy ULevelSequenceEditorSubsystem::CreateCamera(bool bSpawnable, ACineCameraActor*& OutActor)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return FMovieSceneBindingProxy();
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return FMovieSceneBindingProxy();
	}

	FGuid Guid = FSequencerUtilities::CreateCamera(Sequencer.ToSharedRef(), bSpawnable, OutActor);

	return FMovieSceneBindingProxy(Guid, Sequence);
}

TArray<FMovieSceneBindingProxy> ULevelSequenceEditorSubsystem::ConvertToSpawnable(const FMovieSceneBindingProxy& ObjectBinding)
{
	TArray<FMovieSceneBindingProxy> SpawnableProxies;

	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return SpawnableProxies;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return SpawnableProxies;
	}

	FMovieScenePossessable* NewPossessable = nullptr;

	if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
	{
		int32 NumBindings = BindingReferences->GetReferences(ObjectBinding.BindingID).Num();
		for (int32 BindingIndex = 0; BindingIndex < NumBindings; BindingIndex++)
		{
			NewPossessable = FSequencerUtilities::ConvertToCustomBinding(Sequencer.ToSharedRef(), ObjectBinding.BindingID, UMovieSceneSpawnableActorBinding::StaticClass(), BindingIndex);
		}
	}

	if (NewPossessable)
	{
		SpawnableProxies.Add(FMovieSceneBindingProxy(NewPossessable->GetGuid(), Sequence));
	}

	return SpawnableProxies;
}

FMovieSceneBindingProxy ULevelSequenceEditorSubsystem::ConvertToPossessable(const FMovieSceneBindingProxy& ObjectBinding)
{
	FMovieSceneBindingProxy PossessableProxy;

	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return PossessableProxy;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return PossessableProxy;
	}

	FMovieScenePossessable* NewPossessable = nullptr;

	if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
	{
		int32 NumBindings = BindingReferences->GetReferences(ObjectBinding.BindingID).Num();
		for (int32 BindingIndex = 0; BindingIndex < NumBindings; BindingIndex++)
		{
			NewPossessable = FSequencerUtilities::ConvertToPossessable(Sequencer.ToSharedRef(), ObjectBinding.BindingID, BindingIndex);
		}
	}

	if (NewPossessable)
	{
		PossessableProxy = FMovieSceneBindingProxy(NewPossessable->GetGuid(), Sequence);
	}

	return PossessableProxy;
}


FMovieSceneBindingProxy ULevelSequenceEditorSubsystem::ConvertToCustomBinding(const FMovieSceneBindingProxy& ObjectBinding, TSubclassOf<UMovieSceneCustomBinding> BindingType)
{
	FMovieSceneBindingProxy PossessableProxy;

	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return PossessableProxy;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return PossessableProxy;
	}

	FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences();
	if (!BindingReferences)
	{
		return PossessableProxy;
	}

	FMovieScenePossessable* NewPossessable = nullptr;

	if (FSequencerUtilities::CanConvertToCustomBinding(Sequencer.ToSharedRef(), ObjectBinding.BindingID, BindingType))
	{
		int32 NumBindings = BindingReferences->GetReferences(ObjectBinding.BindingID).Num();
		for (int32 BindingIndex = 0; BindingIndex < NumBindings; BindingIndex++)
		{
			NewPossessable = FSequencerUtilities::ConvertToCustomBinding(Sequencer.ToSharedRef(), ObjectBinding.BindingID, BindingType, BindingIndex);
		}

		if (NewPossessable)
		{
			PossessableProxy = FMovieSceneBindingProxy(NewPossessable->GetGuid(), Sequence);
		}
	}

	return PossessableProxy;
}

TArray<UMovieSceneCustomBinding*> ULevelSequenceEditorSubsystem::GetCustomBindingObjects(const FMovieSceneBindingProxy& ObjectBinding)
{
	TArray<UMovieSceneCustomBinding*> CustomBindings;
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return CustomBindings;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return CustomBindings;
	}

	if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
	{
		for (const FMovieSceneBindingReference& BindingReference : BindingReferences->GetReferences(ObjectBinding.BindingID))
		{
			if (BindingReference.CustomBinding)
			{
				CustomBindings.Add(BindingReference.CustomBinding);
			}
		}
	}

	return CustomBindings;
}

TArray<FMovieSceneBindingProxy> ULevelSequenceEditorSubsystem::GetCustomBindingsOfType(TSubclassOf<UMovieSceneCustomBinding> CustomBindingType)
{
	TArray<FMovieSceneBindingProxy> Bindings;
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return Bindings;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return Bindings;
	}

	if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
	{
		for (const FMovieSceneBindingReference& BindingReference : BindingReferences->GetAllReferences())
		{
			if (BindingReference.CustomBinding && BindingReference.CustomBinding->IsA(CustomBindingType))
			{
				Bindings.AddUnique(FMovieSceneBindingProxy(BindingReference.ID, Sequence));
			}
		}
	}

	return Bindings;
}

TSubclassOf<UMovieSceneCustomBinding> ULevelSequenceEditorSubsystem::GetCustomBindingType(const FMovieSceneBindingProxy& ObjectBinding)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return nullptr;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return nullptr;
	}

	if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
	{
		if (UMovieSceneCustomBinding* CustomBinding = BindingReferences->GetCustomBinding(ObjectBinding.BindingID, 0))
		{
			return CustomBinding->GetClass();
		}
	}

	return nullptr;
}

bool ULevelSequenceEditorSubsystem::ChangeActorTemplateClass(const FMovieSceneBindingProxy& ObjectBinding, TSubclassOf<AActor> ActorClass)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return false;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return false;
	}

	bool bSuccess = false;

	TArray<FSequencerChangeBindingInfo> Bindings;
	Bindings.Add(FSequencerChangeBindingInfo(ObjectBinding.BindingID, 0));

	FSequencerUtilities::HandleTemplateActorClassPicked(ActorClass, Sequencer.ToSharedRef(), Bindings, [&bSuccess]() {bSuccess = true; });

	return bSuccess;
}

void ULevelSequenceEditorSubsystem::SaveDefaultSpawnableState(const FMovieSceneBindingProxy& ObjectBinding)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return;
	}

	Sequencer->GetSpawnRegister().SaveDefaultSpawnableState(ObjectBinding.BindingID, 0, Sequencer->GetFocusedTemplateID(), Sequencer->GetSharedPlaybackState());
}

void ULevelSequenceEditorSubsystem::CopyFolders(const TArray<UMovieSceneFolder*>& Folders, FString& ExportedText)
{
	FSequencerUtilities::CopyFolders(Folders, ExportedText);

	FPlatformApplicationMisc::ClipboardCopy(*ExportedText);
}

bool ULevelSequenceEditorSubsystem::PasteFolders(const FString& InTextToImport, FMovieScenePasteFoldersParams PasteFoldersParams, TArray<UMovieSceneFolder*>& OutFolders)
{
	FString TextToImport = InTextToImport;
	if (TextToImport.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardPaste(TextToImport);
	}

	TArray<FNotificationInfo> PasteErrors;
	if (!FSequencerUtilities::PasteFolders(TextToImport, PasteFoldersParams, OutFolders, PasteErrors))
	{
		for (FNotificationInfo PasteError : PasteErrors)
		{
			UE_LOG(LogLevelSequenceEditor, Error, TEXT("%s"), *PasteError.Text.Get().ToString());
		}
		return false;
	}

	return true;
}

void ULevelSequenceEditorSubsystem::CopySections(const TArray<UMovieSceneSection*>& Sections, FString& ExportedText)
{
	FSequencerUtilities::CopySections(Sections, ExportedText);

	FPlatformApplicationMisc::ClipboardCopy(*ExportedText);
}

bool ULevelSequenceEditorSubsystem::PasteSections(const FString& InTextToImport, FMovieScenePasteSectionsParams PasteSectionsParams, TArray<UMovieSceneSection*>& OutSections)
{
	FString TextToImport = InTextToImport;
	if (TextToImport.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardPaste(TextToImport);
	}

	TArray<FNotificationInfo> PasteErrors;
	if (!FSequencerUtilities::PasteSections(TextToImport, PasteSectionsParams, OutSections, PasteErrors))
	{
		for (FNotificationInfo PasteError : PasteErrors)
		{
			UE_LOG(LogLevelSequenceEditor, Error, TEXT("%s"), *PasteError.Text.Get().ToString());
		}
		return false;
	}

	return true;
}

void ULevelSequenceEditorSubsystem::CopyTracks(const TArray<UMovieSceneTrack*>& Tracks, FString& ExportedText)
{
	TArray<UMovieSceneFolder*> Folders;
	FSequencerUtilities::CopyTracks(Tracks, Folders, ExportedText);
	FPlatformApplicationMisc::ClipboardCopy(*ExportedText);
}

bool ULevelSequenceEditorSubsystem::PasteTracks(const FString& InTextToImport, FMovieScenePasteTracksParams PasteTracksParams, TArray<UMovieSceneTrack*>& OutTracks)
{
	FString TextToImport = InTextToImport;
	if (TextToImport.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardPaste(TextToImport);
	}

	TArray<FNotificationInfo> PasteErrors;
	if (!FSequencerUtilities::PasteTracks(TextToImport, PasteTracksParams, OutTracks, PasteErrors))
	{
		for (FNotificationInfo PasteError : PasteErrors)
		{
			UE_LOG(LogLevelSequenceEditor, Error, TEXT("%s"), *PasteError.Text.Get().ToString());
		}
		return false;
	}

	return true;
}

void ULevelSequenceEditorSubsystem::CopyBindings(const TArray<FMovieSceneBindingProxy>& Bindings, FString& ExportedText)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	TArray<UMovieSceneFolder*> Folders;
	FSequencerUtilities::CopyBindings(Sequencer.ToSharedRef(), Bindings, Folders, ExportedText);
	FPlatformApplicationMisc::ClipboardCopy(*ExportedText);
}

bool ULevelSequenceEditorSubsystem::PasteBindings(const FString& InTextToImport, FMovieScenePasteBindingsParams PasteBindingsParams, TArray<FMovieSceneBindingProxy>& OutObjectBindings)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return false;
	}

	FString TextToImport = InTextToImport;
	if (TextToImport.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardPaste(TextToImport);
	}

	TArray<FNotificationInfo> PasteErrors;
	if (!FSequencerUtilities::PasteBindings(TextToImport, Sequencer.ToSharedRef(), PasteBindingsParams, OutObjectBindings, PasteErrors))
	{
		for (FNotificationInfo PasteError : PasteErrors)
		{
			UE_LOG(LogLevelSequenceEditor, Error, TEXT("%s"), *PasteError.Text.Get().ToString());
		}
		return false;
	}

	return true;
}

void ULevelSequenceEditorSubsystem::SnapSectionsToTimelineUsingSourceTimecodeInternal()
{
	const TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (!Sequencer)
	{
		return;
	}

	const UMovieScene* FocusedMovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
	if (!FocusedMovieScene)
	{
		return;
	}

	if (FocusedMovieScene->IsReadOnly())
	{
		FSequencerUtilities::ShowReadOnlyError();
		return;
	}

	TArray<UMovieSceneSection*> Sections;
	Sequencer->GetSelectedSections(Sections);
	if (Sections.IsEmpty())
	{
		return;
	}

	SnapSectionsToTimelineUsingSourceTimecode(Sections);
}

void ULevelSequenceEditorSubsystem::SnapSectionsToTimelineUsingSourceTimecode(const TArray<UMovieSceneSection*>& Sections)
{
	const TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (!Sequencer)
	{
		return;
	}

	const UMovieScene* FocusedMovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
	if (!FocusedMovieScene)
	{
		return;
	}

	if (FocusedMovieScene->IsReadOnly())
	{
		FSequencerUtilities::ShowReadOnlyError();
		return;
	}

	FScopedTransaction SnapSectionsToTimelineUsingSourceTimecodeTransaction(LOCTEXT("SnapSectionsToTimelineUsingSourceTimecode_Transaction", "Snap Sections to Timeline using Source Timecode"));
	bool bAnythingChanged = false;

	const FFrameRate TickResolution = Sequencer->GetFocusedTickResolution();
	const FFrameRate DisplayRate = Sequencer->GetFocusedDisplayRate();

	for (UMovieSceneSection* Section : Sections)
	{
		if (!Section || !Section->HasStartFrame())
		{
			continue;
		}

		const FTimecode SectionSourceTimecode = Section->TimecodeSource.Timecode;
		if (SectionSourceTimecode == FTimecode())
		{
			// Do not move sections with default values for source timecode.
			continue;
		}

		const FFrameNumber SectionSourceStartFrameNumber = SectionSourceTimecode.ToFrameNumber(DisplayRate);

		// Account for any trimming at the start of the section when computing the
		// target frame number to move this section to.
		const FFrameNumber SectionOffsetFrames = Section->GetOffsetTime().Get(FFrameTime()).FloorToFrame();
		const FFrameNumber TargetFrameNumber = SectionSourceStartFrameNumber + SectionOffsetFrames;

		const FFrameNumber SectionCurrentStartFrameNumber = Section->GetInclusiveStartFrame();

		const FFrameNumber Delta = -(SectionCurrentStartFrameNumber - ConvertFrameTime(TargetFrameNumber, DisplayRate, TickResolution).GetFrame().Value);

		Section->MoveSection(Delta);

		bAnythingChanged |= (Delta.Value != 0);
	}

	if (bAnythingChanged)
	{
		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
	}
}

void ULevelSequenceEditorSubsystem::SyncSectionsUsingSourceTimecodeInternal()
{
	const TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (!Sequencer)
	{
		return;
	}

	const UMovieScene* FocusedMovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
	if (!FocusedMovieScene)
	{
		return;
	}

	if (FocusedMovieScene->IsReadOnly())
	{
		FSequencerUtilities::ShowReadOnlyError();
		return;
	}

	TArray<UMovieSceneSection*> Sections;
	Sequencer->GetSelectedSections(Sections);
	if (Sections.Num() < 2)
	{
		return;
	}

	SyncSectionsUsingSourceTimecode(Sections);
}

void ULevelSequenceEditorSubsystem::SyncSectionsUsingSourceTimecode(const TArray<UMovieSceneSection*>& Sections)
{
	const TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (!Sequencer)
	{
		return;
	}

	const UMovieScene* FocusedMovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
	if (!FocusedMovieScene)
	{
		return;
	}

	if (FocusedMovieScene->IsReadOnly())
	{
		FSequencerUtilities::ShowReadOnlyError();
		return;
	}

	// Pull out all of the valid sections that have a start frame and verify
	// we have at least two sections to sync.
	TArray<UMovieSceneSection*> SectionsToSync;
	for (UMovieSceneSection* Section : Sections)
	{
		if (Section && Section->HasStartFrame())
		{
			SectionsToSync.Add(Section);
		}
	}

	if (SectionsToSync.Num() < 2)
	{
		return;
	}

	FScopedTransaction SyncSectionsUsingSourceTimecodeTransaction(LOCTEXT("SyncSectionsUsingSourceTimecode_Transaction", "Sync Sections Using Source Timecode"));
	bool bAnythingChanged = false;

	const FFrameRate TickResolution = Sequencer->GetFocusedTickResolution();
	const FFrameRate DisplayRate = Sequencer->GetFocusedDisplayRate();

	const UMovieSceneSection* FirstSection = SectionsToSync[0];
	const FFrameNumber FirstSectionSourceTimecode = FirstSection->TimecodeSource.Timecode.ToFrameNumber(DisplayRate);

	const FFrameNumber FirstSectionCurrentStartFrame = FirstSection->GetInclusiveStartFrame();
	const FFrameNumber FirstSectionOffsetFrames = FirstSection->GetOffsetTime().Get(FFrameTime()).FloorToFrame();
	SectionsToSync.RemoveAt(0);

	for (UMovieSceneSection* Section : SectionsToSync)
	{
		const FFrameNumber SectionSourceTimecode = Section->TimecodeSource.Timecode.ToFrameNumber(DisplayRate);
		const FFrameNumber SectionCurrentStartFrame = Section->GetInclusiveStartFrame();
		const FFrameNumber SectionOffsetFrames = Section->GetOffsetTime().Get(FFrameTime()).FloorToFrame();

		const FFrameNumber TimecodeDelta = ConvertFrameTime(SectionSourceTimecode - FirstSectionSourceTimecode, DisplayRate, TickResolution).GetFrame().Value;
		const FFrameNumber CurrentDelta = (SectionCurrentStartFrame - SectionOffsetFrames) - (FirstSectionCurrentStartFrame - FirstSectionOffsetFrames);
		const FFrameNumber Delta = -CurrentDelta + TimecodeDelta;

		Section->MoveSection(Delta);

		bAnythingChanged |= (Delta.Value != 0);
	}

	if (bAnythingChanged)
	{
		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::TrackValueChanged);
	}
}

//////////////////////////////////////////////////////////////
/// SBakeTransformWidget
///////////////////////////////////////////////////////////

DECLARE_DELEGATE_RetVal_OneParam(FReply, SBakeTransformOnBake, FBakingAnimationKeySettings);

/** Widget allowing baking controls from one space to another */
class  SBakeTransformWidget : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SBakeTransformWidget)
		: _Sequencer(nullptr)
	{}
		SLATE_ARGUMENT(ISequencer*, Sequencer)
		SLATE_ARGUMENT(FBakingAnimationKeySettings, Settings)
		SLATE_EVENT(SBakeTransformOnBake, OnBake)
		SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SBakeTransformWidget() override {}

	FReply OpenDialog(bool bModal = true);
	void CloseDialog();

private:

	//used for setting up the details
	TSharedPtr<TStructOnScope<FBakingAnimationKeySettings>> Settings;

	ISequencer* Sequencer;

	TWeakPtr<SWindow> DialogWindow;
	TSharedPtr<IStructureDetailsView> DetailsView;
};

void SBakeTransformWidget::Construct(const FArguments& InArgs)
{
	check(InArgs._Sequencer);
	check(InArgs._OnBake.IsBound());

	Settings = MakeShared<TStructOnScope<FBakingAnimationKeySettings>>();
	Settings->InitializeAs<FBakingAnimationKeySettings>();
	*Settings = InArgs._Settings;
	//always setting space to be parent as default, since stored space may not be available.
	Sequencer = InArgs._Sequencer;

	FStructureDetailsViewArgs StructureViewArgs;
	StructureViewArgs.bShowObjects = true;
	StructureViewArgs.bShowAssets = true;
	StructureViewArgs.bShowClasses = true;
	StructureViewArgs.bShowInterfaces = true;

	FDetailsViewArgs ViewArgs;
	ViewArgs.bAllowSearch = false;
	ViewArgs.bHideSelectionTip = false;
	ViewArgs.bShowObjectLabel = false;

	FPropertyEditorModule& PropertyEditor = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	DetailsView = PropertyEditor.CreateStructureDetailView(ViewArgs, StructureViewArgs, TSharedPtr<FStructOnScope>());
	TSharedPtr<INumericTypeInterface<double>> NumericTypeInterface = Sequencer->GetNumericTypeInterface();
	DetailsView->GetDetailsView()->RegisterInstancedCustomPropertyTypeLayout("FrameNumber",
		FOnGetPropertyTypeCustomizationInstance::CreateLambda([=]() {return MakeShared<FFrameNumberDetailsCustomization>(NumericTypeInterface); }));
	DetailsView->SetStructureData(Settings);

	ChildSlot
		[
			SNew(SBorder)
			.Visibility(EVisibility::Visible)
		[
			SNew(SVerticalBox)

			
			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 0.f)
				[
					DetailsView->GetWidget().ToSharedRef()
				]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 16.f, 0.f, 16.f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			[
				SNew(SSpacer)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(8.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
			.ContentPadding(FAppStyle::GetMargin("StandardDialog.ContentPadding"))
			.Text(LOCTEXT("OK", "OK"))
			.OnClicked_Lambda([this, InArgs]()
				{
					FReply Reply = InArgs._OnBake.Execute( *(Settings->Get()));
					CloseDialog();
					return Reply;

				})
			]

		+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(8.f, 0.f, 16.f, 0.f)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.ContentPadding(FAppStyle::GetMargin("StandardDialog.ContentPadding"))
				.Text(LOCTEXT("Cancel", "Cancel"))
				.OnClicked_Lambda([this]()
				{
					CloseDialog();
					return FReply::Handled();
				})
			]
		]
	]
	];
}

class SBakeTransformDialogWindow : public SWindow
{
};

FReply SBakeTransformWidget::OpenDialog(bool bModal)
{
	check(!DialogWindow.IsValid());

	const FVector2D CursorPos = FSlateApplication::Get().GetCursorPos();

	TSharedRef<SBakeTransformDialogWindow> Window = SNew(SBakeTransformDialogWindow)
		.Title(LOCTEXT("SBakeTransformWidgetTitle", "Bake Transforms"))
		.CreateTitleBar(true)
		.Type(EWindowType::Normal)
		.SizingRule(ESizingRule::Autosized)
		.ScreenPosition(CursorPos)
		.FocusWhenFirstShown(true)
		.ActivationPolicy(EWindowActivationPolicy::FirstShown)
		[
			AsShared()
		];

	Window->SetWidgetToFocusOnActivate(AsShared());

	DialogWindow = Window;

	Window->MoveWindowTo(CursorPos);

	if (bModal)
	{
		GEditor->EditorAddModalWindow(Window);
	}
	else
	{
		FSlateApplication::Get().AddWindow(Window);
	}

	return FReply::Handled();
}

void SBakeTransformWidget::CloseDialog()
{
	if (DialogWindow.IsValid())
	{
		DialogWindow.Pin()->RequestDestroyWindow();
		DialogWindow.Reset();
	}
}

void ULevelSequenceEditorSubsystem::BakeTransformInternal()
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	UMovieScene* FocusedMovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
	if (!FocusedMovieScene)
	{
		return;
	}

	FFrameRate TickResolution = FocusedMovieScene->GetTickResolution();
	FFrameRate DisplayRate = FocusedMovieScene->GetDisplayRate();

	TArray<FGuid> ObjectBindings;
	Sequencer->GetSelectedObjects(ObjectBindings);

	TArray<FMovieSceneBindingProxy> BindingProxies;
	for (FGuid Guid : ObjectBindings)
	{
		BindingProxies.Add(FMovieSceneBindingProxy(Guid, Sequencer->GetFocusedMovieSceneSequence()));
	}

	static FBakingAnimationKeySettings Settings; //reuse the settings except for the range
	Settings.StartFrame = UE::MovieScene::DiscreteInclusiveLower(FocusedMovieScene->GetPlaybackRange());
	Settings.EndFrame = UE::MovieScene::DiscreteExclusiveUpper(FocusedMovieScene->GetPlaybackRange());

	TSharedRef<SBakeTransformWidget> BakeWidget =
		SNew(SBakeTransformWidget)
		.Settings(Settings)
		.Sequencer(Sequencer.Get())
		.OnBake_Lambda([this, &BindingProxies, Sequencer](FBakingAnimationKeySettings InSettings)
			{
				FMovieSceneScriptingParams Params;
				Params.TimeUnit = EMovieSceneTimeUnit::TickResolution;
				BakeTransformWithSettings(BindingProxies, InSettings, Params);
				return FReply::Handled();
			});

	BakeWidget->OpenDialog(true);
}

void ULevelSequenceEditorSubsystem::BakeTransform(const TArray<FMovieSceneBindingProxy>& ObjectBindings, const FFrameTime& BakeInTime, const FFrameTime& BakeOutTime, const FFrameTime& BakeInterval, const FMovieSceneScriptingParams& Params)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	UMovieScene* FocusedMovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
	if (!FocusedMovieScene)
	{
		return;
	}

	if (FocusedMovieScene->IsReadOnly())
	{
		FSequencerUtilities::ShowReadOnlyError();
		return;
	}

	if (ObjectBindings.Num() == 0)
	{
		return;
	}

	FFrameRate TickResolution = FocusedMovieScene->GetTickResolution();
	FFrameRate DisplayRate = FocusedMovieScene->GetDisplayRate();
	FFrameTime InFrame = BakeInTime;
	FFrameTime OutFrame = BakeOutTime;
	FFrameTime Interval = BakeInterval;

	if (Params.TimeUnit == EMovieSceneTimeUnit::DisplayRate)
	{
		InFrame = ConvertFrameTime(BakeInTime, DisplayRate, TickResolution);
		OutFrame = ConvertFrameTime(BakeOutTime, DisplayRate, TickResolution);
		Interval = ConvertFrameTime(BakeInterval, DisplayRate, TickResolution);
	}

	FBakingAnimationKeySettings Settings;
	FFrameTime OneFrame = ConvertFrameTime(1, DisplayRate, TickResolution);
	//we never want subframes when baking so use frame increments
	Settings.FrameIncrement = Interval.FrameNumber.Value / OneFrame.FrameNumber.Value;
	Settings.FrameIncrement = Settings.FrameIncrement <= 0 ? 1 : Settings.FrameIncrement;
	Settings.StartFrame = InFrame.GetFrame();
	Settings.EndFrame = OutFrame.GetFrame();
	Settings.BakingKeySettings = EBakingKeySettings::AllFrames;
	FMovieSceneScriptingParams NewParams;
	NewParams.TimeUnit = EMovieSceneTimeUnit::TickResolution;
	BakeTransformWithSettings(ObjectBindings, Settings, NewParams);
}

void ULevelSequenceEditorSubsystem::CalculateFramesPerGuid(TSharedPtr<ISequencer>& Sequencer, const FBakingAnimationKeySettings& InSettings, TMap<FGuid, ULevelSequenceEditorSubsystem::FBakeData>& OutBakeDataMap,
	TSortedMap<FFrameNumber, FFrameNumber>& OutFrameMap)
{
	OutFrameMap.Reset();
	TArray<FFrameNumber> Frames;
	//we get all frames since we need to get the Actor PER FRAME in order to handle spanwables
	MovieSceneToolHelpers::CalculateFramesBetween(Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene(), InSettings.StartFrame, InSettings.EndFrame, InSettings.FrameIncrement, Frames);
	if (InSettings.BakingKeySettings == EBakingKeySettings::AllFrames)
	{
		for (FFrameNumber& Frame : Frames)
		{
			OutFrameMap.Add(Frame,Frame);
		}
		for (TPair<FGuid, FBakeData>& BakeData : OutBakeDataMap)
		{
			BakeData.Value.KeyTimes.Reset();
			BakeData.Value.KeyTimes = OutFrameMap;
		}
	}
	else
	{
		for (TPair<FGuid, FBakeData>& BakeData : OutBakeDataMap)
		{
			FActorForWorldTransforms ActorForWorldTransforms;
			FGuid Guid = BakeData.Key;

			for (TWeakObjectPtr<> RuntimeObject : Sequencer->FindObjectsInCurrentSequence(Guid))
			{
				UActorComponent* ActorComponent = nullptr;
				AActor* Actor = Cast<AActor>(RuntimeObject.Get());
				if (!Actor)
				{
					ActorComponent = Cast<UActorComponent>(RuntimeObject.Get());
					if (ActorComponent)
					{
						Actor = ActorComponent->GetOwner();
					}
				}
				ActorForWorldTransforms.Actor = Actor;
				ActorForWorldTransforms.Component = Cast<USceneComponent>(ActorComponent);
				BakeData.Value.KeyTimes.Reset();
				MovieSceneToolHelpers::GetActorsAndParentsKeyFrames(Sequencer.Get(), ActorForWorldTransforms,
					InSettings.StartFrame, InSettings.EndFrame, BakeData.Value.KeyTimes);
				OutFrameMap.Append(BakeData.Value.KeyTimes);
			}
		}
	}
}

bool ULevelSequenceEditorSubsystem::BakeTransformWithSettings(const TArray<FMovieSceneBindingProxy>& ObjectBindings, const FBakingAnimationKeySettings& InSettings, const FMovieSceneScriptingParams& Params)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		UE_LOG(LogLevelSequenceEditor, Warning, TEXT("Bake Transform failed."));
		return false;
	}

	UMovieScene* FocusedMovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
	if (!FocusedMovieScene)
	{
		UE_LOG(LogLevelSequenceEditor, Warning, TEXT("Bake Transform failed."));
		return false;
	}

	if (FocusedMovieScene->IsReadOnly())
	{
		UE_LOG(LogLevelSequenceEditor, Warning, TEXT("Bake Transform failed."));
		FSequencerUtilities::ShowReadOnlyError();
		return false;
	}

	if (ObjectBindings.Num() == 0)
	{
		UE_LOG(LogLevelSequenceEditor, Warning, TEXT("Bake Transform failed."));
		return false;
	}

	FScopedTransaction BakeTransform(LOCTEXT("BakeTransform", "Bake Transform"));

	FocusedMovieScene->Modify();

	FQualifiedFrameTime ResetTime = Sequencer->GetLocalTime();

	FFrameRate TickResolution = FocusedMovieScene->GetTickResolution();
	FFrameRate DisplayRate = FocusedMovieScene->GetDisplayRate();

	FBakingAnimationKeySettings SettingsInTick = InSettings;
	
	if (Params.TimeUnit == EMovieSceneTimeUnit::DisplayRate)
	{
		SettingsInTick.StartFrame = ConvertFrameTime(SettingsInTick.StartFrame, DisplayRate, TickResolution).GetFrame();
		SettingsInTick.EndFrame = ConvertFrameTime(SettingsInTick.EndFrame, DisplayRate, TickResolution).GetFrame();
	}

	TSortedMap<FFrameNumber, FFrameNumber> TotalFrameMap;
	TMap<FGuid, FBakeData> BakeDataMap;
	for (const FMovieSceneBindingProxy& ObjectBinding : ObjectBindings)
	{
		BakeDataMap.Add(ObjectBinding.BindingID);
	}
	CalculateFramesPerGuid(Sequencer, SettingsInTick, BakeDataMap, TotalFrameMap);

	FMovieSceneInverseSequenceTransform LocalToRootTransform = Sequencer->GetFocusedMovieSceneSequenceTransform().Inverse();
	
	TArray<FFrameNumber> AllFrames;
	TotalFrameMap.GenerateKeyArray(AllFrames);

	UWorld* PlaybackContext = Sequencer->GetPlaybackContext()->GetWorld();
	ensure(PlaybackContext);
	const FConstraintsManagerController& Controller = FConstraintsManagerController::Get(PlaybackContext);

	for (FFrameNumber KeyTime: AllFrames)
	{
		TOptional<FFrameTime> NewGlobalTime = LocalToRootTransform.TryTransformTime(KeyTime);
		if (!NewGlobalTime)
		{
			continue;
		}

		Sequencer->SetGlobalTime(NewGlobalTime.GetValue());
		Controller.EvaluateAllConstraints();

		for (const FMovieSceneBindingProxy& ObjectBinding : ObjectBindings)
		{
			FGuid Guid = ObjectBinding.BindingID;

			for (TWeakObjectPtr<> RuntimeObject : Sequencer->FindObjectsInCurrentSequence(Guid) )
			{
				const FFrameNumber* Number = BakeDataMap[Guid].KeyTimes.FindKey(KeyTime);
				if (Number == nullptr)
				{
					continue;
				}
				AActor* Actor = Cast<AActor>(RuntimeObject.Get());
				if (!Actor)
				{
					UActorComponent* ActorComponent = Cast<UActorComponent>(RuntimeObject.Get());
					if (ActorComponent)
					{
						Actor = ActorComponent->GetOwner();
					}
				}

				if (!Actor)
				{
					continue;
				}

				UCameraComponent* CameraComponent = MovieSceneHelpers::CameraComponentFromRuntimeObject(RuntimeObject.Get());

				// Cache transforms
				USceneComponent* Parent = nullptr;
				if (CameraComponent)
				{
					Parent = CameraComponent->GetAttachParent();
				} 
				else if (Actor->GetRootComponent())
				{
					Parent = Actor->GetRootComponent()->GetAttachParent();
				}
				
				// The CameraRig_rail updates the spline position tick, so it needs to be ticked manually while baking the frames
				while (Parent && Parent->GetOwner())
				{
					Parent->GetOwner()->Tick(0.03f);
					if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(Parent))
					{
						SkeletalMeshComponent->TickAnimation(0.f, false);

						SkeletalMeshComponent->RefreshBoneTransforms();
						SkeletalMeshComponent->RefreshFollowerComponents();
						SkeletalMeshComponent->UpdateComponentToWorld();
						SkeletalMeshComponent->FinalizeBoneTransform();
						SkeletalMeshComponent->MarkRenderTransformDirty();
						SkeletalMeshComponent->MarkRenderDynamicDataDirty();
					}
					Parent = Parent->GetAttachParent();
				}

				if (CameraComponent)
				{
					FTransform AdditiveOffset;
					float AdditiveFOVOffset;
					CameraComponent->GetAdditiveOffset(AdditiveOffset, AdditiveFOVOffset);

					FTransform Transform(Actor->GetActorRotation(), Actor->GetActorLocation());
					FTransform TransformWithAdditiveOffset = AdditiveOffset * Transform;
					FVector LocalTranslation = TransformWithAdditiveOffset.GetTranslation();
					FRotator LocalRotation = TransformWithAdditiveOffset.GetRotation().Rotator();

					BakeDataMap[Guid].Locations.Add(LocalTranslation);
					BakeDataMap[Guid].Rotations.Add(LocalRotation);
					BakeDataMap[Guid].Scales.Add(FVector::OneVector);
				}
				else
				{
					BakeDataMap[Guid].Locations.Add(Actor->GetActorLocation());
					BakeDataMap[Guid].Rotations.Add(Actor->GetActorRotation());
					BakeDataMap[Guid].Scales.Add(Actor->GetActorScale());
				}

			}
		}
	}

	const bool bDisableSectionsAfterBaking = Sequencer->GetSequencerSettings()->GetDisableSectionsAfterBaking();

	for (TPair<FGuid, FBakeData>& BakeData : BakeDataMap)
	{
		FGuid Guid = BakeData.Key;
		TArray<FFrameNumber> KeyTimes;
		BakeData.Value.KeyTimes.GenerateKeyArray(KeyTimes);
		// Disable or delete any constraint (attach/path) tracks
		AActor* AttachParentActor = nullptr;
		for (UMovieSceneTrack* Track : FocusedMovieScene->FindTracks(UMovieScene3DConstraintTrack::StaticClass(), Guid))
		{
			if (UMovieScene3DConstraintTrack* ConstraintTrack = Cast<UMovieScene3DConstraintTrack>(Track))
			{
				for (UMovieSceneSection* ConstraintSection : ConstraintTrack->GetAllSections())
				{
					FMovieSceneObjectBindingID ConstraintBindingID = (Cast<UMovieScene3DConstraintSection>(ConstraintSection))->GetConstraintBindingID();
					for (TWeakObjectPtr<> ParentObject : ConstraintBindingID.ResolveBoundObjects(Sequencer->GetFocusedTemplateID(), *Sequencer))
					{
						AttachParentActor = Cast<AActor>(ParentObject.Get());
						break;
					}
				}

				if (bDisableSectionsAfterBaking)
				{
					for (UMovieSceneSection* ConstraintSection : ConstraintTrack->GetAllSections())
					{
						ConstraintSection->Modify();
						ConstraintSection->SetIsActive(false);
					}
				}
				else
				{
					FocusedMovieScene->RemoveTrack(*ConstraintTrack);
				}
			}
		}

		// Disable or delete any transform tracks
		for (UMovieSceneTrack* Track : FocusedMovieScene->FindTracks(UMovieScene3DTransformTrack::StaticClass(), Guid))
		{
			if (UMovieScene3DTransformTrack* TransformTrack = Cast<UMovieScene3DTransformTrack>(Track))
			{
				if (bDisableSectionsAfterBaking)
				{
					for (UMovieSceneSection* TransformSection : TransformTrack->GetAllSections())
					{
						TransformSection->Modify();
						TransformSection->SetIsActive(false);
					}
				}
				else
				{
					FocusedMovieScene->RemoveTrack(*TransformTrack);
				}
			}
		}

		// Disable or delete any camera shake tracks
		for (UMovieSceneTrack* Track : FocusedMovieScene->FindTracks(UMovieSceneCameraShakeTrack::StaticClass(), Guid))
		{
			if (UMovieSceneCameraShakeTrack* CameraShakeTrack = Cast<UMovieSceneCameraShakeTrack>(Track))
			{
				if (bDisableSectionsAfterBaking)
				{
					for (UMovieSceneSection* CameraShakeSection : CameraShakeTrack->GetAllSections())
					{
						CameraShakeSection->Modify();
						CameraShakeSection->SetIsActive(false);
					}
				}
				else
				{
					FocusedMovieScene->RemoveTrack(*CameraShakeTrack);
				}
			}
		}

		// Reset position
		Sequencer->SetLocalTimeDirectly(ResetTime.Time);
		Sequencer->ForceEvaluate();

		FVector DefaultLocation = FVector::ZeroVector;
		FVector DefaultRotation = FVector::ZeroVector;
		FVector DefaultScale = FVector::OneVector;

		for (TWeakObjectPtr<> RuntimeObject : Sequencer->FindObjectsInCurrentSequence(Guid))
		{
			AActor* Actor = Cast<AActor>(RuntimeObject.Get());
			if (!Actor)
			{
				UActorComponent* ActorComponent = Cast<UActorComponent>(RuntimeObject.Get());
				if (ActorComponent)
				{
					Actor = ActorComponent->GetOwner();
				}
			}

			if (!Actor)
			{
				continue;
			}

			DefaultLocation = Actor->GetActorLocation();
			DefaultRotation = Actor->GetActorRotation().Euler();
			DefaultScale = Actor->GetActorScale();

			// Always detach from any existing parent
			Actor->DetachFromActor(FDetachmentTransformRules::KeepRelativeTransform);
		}
			
		// Create new transform track and section
		UMovieScene3DTransformTrack* TransformTrack = Cast<UMovieScene3DTransformTrack>(FocusedMovieScene->AddTrack(UMovieScene3DTransformTrack::StaticClass(), Guid));

		if (TransformTrack)
		{
			UMovieScene3DTransformSection* TransformSection = CastChecked<UMovieScene3DTransformSection>(TransformTrack->CreateNewSection());
			TransformTrack->AddSection(*TransformSection);

			TransformSection->SetRange(TRange<FFrameNumber>::All());

			TArrayView<FMovieSceneDoubleChannel*> DoubleChannels = TransformSection->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
			DoubleChannels[0]->SetDefault(DefaultLocation.X);
			DoubleChannels[1]->SetDefault(DefaultLocation.Y);
			DoubleChannels[2]->SetDefault(DefaultLocation.Z);
			DoubleChannels[3]->SetDefault(DefaultRotation.X);
			DoubleChannels[4]->SetDefault(DefaultRotation.Y);
			DoubleChannels[5]->SetDefault(DefaultRotation.Z);
			DoubleChannels[6]->SetDefault(DefaultScale.X);
			DoubleChannels[7]->SetDefault(DefaultScale.Y);
			DoubleChannels[8]->SetDefault(DefaultScale.Z);

			TArray<FVector> LocalTranslations, LocalRotations, LocalScales;
			LocalTranslations.SetNum(KeyTimes.Num());
			LocalRotations.SetNum(KeyTimes.Num());
			LocalScales.SetNum(KeyTimes.Num());

			for (int32 Counter = 0; Counter < KeyTimes.Num(); ++Counter)
			{
				FVector LocalTranslation = DefaultLocation;
				FVector LocalScale = DefaultScale;
				FRotator LocalRotation = DefaultRotation.Rotation();

				if (Counter < BakeData.Value.Locations.Num())
				{
					LocalTranslation = BakeData.Value.Locations[Counter];
				}
				if (Counter < BakeData.Value.Rotations.Num())
				{
					LocalRotation = BakeData.Value.Rotations[Counter];
				}
				if (Counter < BakeData.Value.Scales.Num())
				{
					LocalScale = BakeData.Value.Scales[Counter];
				}

				FTransform LocalTransform(LocalRotation, LocalTranslation, LocalScale);
				LocalTranslations[Counter] = LocalTransform.GetTranslation();
				LocalRotations[Counter] = LocalTransform.GetRotation().Euler();
				LocalScales[Counter] = LocalTransform.GetScale3D();
			}

			// Euler filter
			for (int32 Counter = 0; Counter < LocalRotations.Num() - 1; ++Counter)
			{
				FMath::WindRelativeAnglesDegrees(LocalRotations[Counter].X, LocalRotations[Counter + 1].X);
				FMath::WindRelativeAnglesDegrees(LocalRotations[Counter].Y, LocalRotations[Counter + 1].Y);
				FMath::WindRelativeAnglesDegrees(LocalRotations[Counter].Z, LocalRotations[Counter + 1].Z);							
			}
			if (SettingsInTick.BakingKeySettings == EBakingKeySettings::KeysOnly)
			{
				const EMovieSceneKeyInterpolation KeyInterpolation = Sequencer->GetSequencerSettings()->GetKeyInterpolation();

				for (int32 Counter = 0; Counter < KeyTimes.Num(); ++Counter)
				{
					int ChannelIndex = 0;
					FFrameNumber KeyTime = KeyTimes[Counter];
					{
						TMovieSceneChannelData<FMovieSceneDoubleValue> ChannelData = DoubleChannels[ChannelIndex++]->GetData();
						MovieSceneToolHelpers::SetOrAddKey(ChannelData, KeyTime, LocalTranslations[Counter].X);
					}
					{
						TMovieSceneChannelData<FMovieSceneDoubleValue> ChannelData = DoubleChannels[ChannelIndex++]->GetData();
						MovieSceneToolHelpers::SetOrAddKey(ChannelData, KeyTime, LocalTranslations[Counter].Y);
					}
					{
						TMovieSceneChannelData<FMovieSceneDoubleValue> ChannelData = DoubleChannels[ChannelIndex++]->GetData();
						MovieSceneToolHelpers::SetOrAddKey(ChannelData, KeyTime, LocalTranslations[Counter].Z);
					}
					{
						TMovieSceneChannelData<FMovieSceneDoubleValue> ChannelData = DoubleChannels[ChannelIndex++]->GetData();
						MovieSceneToolHelpers::SetOrAddKey(ChannelData, KeyTime, LocalRotations[Counter].X);
					}
					{
						TMovieSceneChannelData<FMovieSceneDoubleValue> ChannelData = DoubleChannels[ChannelIndex++]->GetData();
						MovieSceneToolHelpers::SetOrAddKey(ChannelData, KeyTime, LocalRotations[Counter].Y);

					}
					{
						TMovieSceneChannelData<FMovieSceneDoubleValue> ChannelData = DoubleChannels[ChannelIndex++]->GetData();
						MovieSceneToolHelpers::SetOrAddKey(ChannelData, KeyTime, LocalRotations[Counter].Z);
					}
					{
						TMovieSceneChannelData<FMovieSceneDoubleValue> ChannelData = DoubleChannels[ChannelIndex++]->GetData();
						MovieSceneToolHelpers::SetOrAddKey(ChannelData, KeyTime, LocalScales[Counter].X);
					}
					{
						TMovieSceneChannelData<FMovieSceneDoubleValue> ChannelData = DoubleChannels[ChannelIndex++]->GetData();
						MovieSceneToolHelpers::SetOrAddKey(ChannelData, KeyTime, LocalScales[Counter].Y);
					}
					{
						TMovieSceneChannelData<FMovieSceneDoubleValue> ChannelData = DoubleChannels[ChannelIndex++]->GetData();
						MovieSceneToolHelpers::SetOrAddKey(ChannelData, KeyTime, LocalScales[Counter].Z);
					}
				}
			}
			else
			{
				for (int32 Counter = 0; Counter < KeyTimes.Num(); ++Counter)
				{
					FFrameNumber KeyTime = KeyTimes[Counter];
					DoubleChannels[0]->AddLinearKey(KeyTime, LocalTranslations[Counter].X);
					DoubleChannels[1]->AddLinearKey(KeyTime, LocalTranslations[Counter].Y);
					DoubleChannels[2]->AddLinearKey(KeyTime, LocalTranslations[Counter].Z);
					DoubleChannels[3]->AddLinearKey(KeyTime, LocalRotations[Counter].X);
					DoubleChannels[4]->AddLinearKey(KeyTime, LocalRotations[Counter].Y);
					DoubleChannels[5]->AddLinearKey(KeyTime, LocalRotations[Counter].Z);
					DoubleChannels[6]->AddLinearKey(KeyTime, LocalScales[Counter].X);
					DoubleChannels[7]->AddLinearKey(KeyTime, LocalScales[Counter].Y);
					DoubleChannels[8]->AddLinearKey(KeyTime, LocalScales[Counter].Z);
				}
				if (SettingsInTick.bReduceKeys == true)
				{
					FKeyDataOptimizationParams Param;
					Param.bAutoSetInterpolation = true;
					Param.Tolerance = SettingsInTick.Tolerance;
					TRange<FFrameNumber> Range(SettingsInTick.StartFrame, SettingsInTick.EndFrame);
					Param.Range = Range;
					MovieSceneToolHelpers::OptimizeSection(Param, TransformSection);
				}
			}
		}
	}
	
	Sequencer->NotifyMovieSceneDataChanged( EMovieSceneDataChangeType::MovieSceneStructureItemsChanged );
	return true;
}

void ULevelSequenceEditorSubsystem::FixActorReferences()
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	UWorld* PlaybackContext = Sequencer->GetPlaybackContext()->GetWorld();
	if (!PlaybackContext)
	{
		return;
	}

	UMovieScene* FocusedMovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
	if (!FocusedMovieScene)
	{
		return;
	}

	if (FocusedMovieScene->IsReadOnly())
	{
		FSequencerUtilities::ShowReadOnlyError();
		return;
	}

	FScopedTransaction FixActorReferencesTransaction(LOCTEXT("FixActorReferences", "Fix Actor References"));

	TMap<FString, AActor*> ActorNameToActorMap;

	for (TActorIterator<AActor> ActorItr(PlaybackContext); ActorItr; ++ActorItr)
	{
		// Same as with the Object Iterator, access the subclass instance with the * or -> operators.
		AActor* Actor = *ActorItr;
		ActorNameToActorMap.Add(Actor->GetActorLabel(), Actor);
	}

	// Cache the possessables to fix up first since the bindings will change as the fix ups happen.
	TArray<FMovieScenePossessable> ActorsPossessablesToFix;
	for (int32 i = 0; i < FocusedMovieScene->GetPossessableCount(); i++)
	{
		FMovieScenePossessable& Possessable = FocusedMovieScene->GetPossessable(i);
		// Possessables with parents are components so ignore them.
		if (Possessable.GetParent().IsValid() == false)
		{
			if (Sequencer->FindBoundObjects(Possessable.GetGuid(), Sequencer->GetFocusedTemplateID()).Num() == 0)
			{
				ActorsPossessablesToFix.Add(Possessable);
			}
		}
	}

	// For the possessables to fix, look up the actors by name and reassign them if found.
	TMap<FGuid, FGuid> OldGuidToNewGuidMap;
	for (const FMovieScenePossessable& ActorPossessableToFix : ActorsPossessablesToFix)
	{
		AActor* ActorPtr = ActorNameToActorMap.FindRef(ActorPossessableToFix.GetName());
		if (ActorPtr != nullptr)
		{
			FGuid OldGuid = ActorPossessableToFix.GetGuid();

			// The actor might have an existing guid while the possessable with the same name might not. 
			// In that case, make sure we also replace the existing guid with the new guid 
			FGuid ExistingGuid = Sequencer->FindObjectId(*ActorPtr, Sequencer->GetFocusedTemplateID());

			FGuid NewGuid = FSequencerUtilities::AssignActor(Sequencer.ToSharedRef(), ActorPtr, ActorPossessableToFix.GetGuid());

			OldGuidToNewGuidMap.Add(OldGuid, NewGuid);

			if (ExistingGuid.IsValid())
			{
				OldGuidToNewGuidMap.Add(ExistingGuid, NewGuid);
			}
		}
	}

	for (TPair<FGuid, FGuid> GuidPair : OldGuidToNewGuidMap)
	{
		FSequencerUtilities::UpdateBindingIDs(Sequencer.ToSharedRef(), GuidPair.Key, GuidPair.Value);
	}

	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
}

void ULevelSequenceEditorSubsystem::AddActorsToBindingInternal()
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	TArray<FGuid> ObjectBindings;
	Sequencer->GetSelectedObjects(ObjectBindings);
	if (ObjectBindings.Num() == 0)
	{
		return;
	}

	FMovieSceneBindingProxy BindingProxy(ObjectBindings[0], Sequencer->GetFocusedMovieSceneSequence());

	TArray<AActor*> SelectedActors;
	GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors); 
	AddActorsToBinding(SelectedActors, BindingProxy);
}

void ULevelSequenceEditorSubsystem::AddActorsToBinding(const TArray<AActor*>& Actors, const FMovieSceneBindingProxy& ObjectBinding)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	FSequencerUtilities::AddActorsToBinding(Sequencer.ToSharedRef(), Actors, ObjectBinding);
}

void ULevelSequenceEditorSubsystem::ReplaceBindingWithActorsInternal()
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	TArray<FGuid> ObjectBindings;
	Sequencer->GetSelectedObjects(ObjectBindings);
	if (ObjectBindings.Num() == 0)
	{
		return;
	}

	FMovieSceneBindingProxy BindingProxy(ObjectBindings[0], Sequencer->GetFocusedMovieSceneSequence());

	TArray<AActor*> SelectedActors;
	GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors); 
	ReplaceBindingWithActors(SelectedActors, BindingProxy);
}

void ULevelSequenceEditorSubsystem::ReplaceBindingWithActors(const TArray<AActor*>& Actors, const FMovieSceneBindingProxy& ObjectBinding)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	FSequencerUtilities::ReplaceBindingWithActors(Sequencer.ToSharedRef(), Actors, ObjectBinding);
}

void ULevelSequenceEditorSubsystem::RemoveActorsFromBindingInternal()
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	TArray<FGuid> ObjectBindings;
	Sequencer->GetSelectedObjects(ObjectBindings);
	if (ObjectBindings.Num() == 0)
	{
		return;
	}

	FMovieSceneBindingProxy BindingProxy(ObjectBindings[0], Sequencer->GetFocusedMovieSceneSequence());

	TArray<AActor*> SelectedActors;
	GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors); 
	RemoveActorsFromBinding(SelectedActors, BindingProxy);
}

void ULevelSequenceEditorSubsystem::RemoveActorsFromBinding(const TArray<AActor*>& Actors, const FMovieSceneBindingProxy& ObjectBinding)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	FSequencerUtilities::RemoveActorsFromBinding(Sequencer.ToSharedRef(), Actors, ObjectBinding);
}

void ULevelSequenceEditorSubsystem::RemoveAllBindingsInternal()
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	TArray<FGuid> ObjectBindings;
	Sequencer->GetSelectedObjects(ObjectBindings);
	if (ObjectBindings.Num() == 0)
	{
		return;
	}

	FMovieSceneBindingProxy BindingProxy(ObjectBindings[0], Sequencer->GetFocusedMovieSceneSequence());

	RemoveAllBindings(BindingProxy);
}

void ULevelSequenceEditorSubsystem::RemoveAllBindings(const FMovieSceneBindingProxy& ObjectBinding)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return;
	}

	FScopedTransaction RemoveAllBindings(LOCTEXT("RemoveAllBindings", "Remove All Bound Objects"));

	Sequence->Modify();
	MovieScene->Modify();

	// Unbind objects
	FGuid Guid = ObjectBinding.BindingID;
	Sequence->UnbindPossessableObjects(Guid);

	Sequencer->RestorePreAnimatedState();

	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
}

void ULevelSequenceEditorSubsystem::RemoveInvalidBindingsInternal()
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	TArray<FGuid> ObjectBindings;
	Sequencer->GetSelectedObjects(ObjectBindings);
	if (ObjectBindings.Num() == 0)
	{
		return;
	}

	FMovieSceneBindingProxy BindingProxy(ObjectBindings[0], Sequencer->GetFocusedMovieSceneSequence());

	RemoveInvalidBindings(BindingProxy);
}

void ULevelSequenceEditorSubsystem::RemoveInvalidBindings(const FMovieSceneBindingProxy& ObjectBinding)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return;
	}
	
	FScopedTransaction RemoveInvalidBindings(LOCTEXT("RemoveMissing", "Remove Missing Objects"));

	Sequence->Modify();
	MovieScene->Modify();

	// Unbind objects
	FGuid Guid = ObjectBinding.BindingID;
	Sequence->UnbindInvalidObjects(Guid, Sequencer->GetPlaybackContext());

	// Update label
	UClass* ActorClass = nullptr;

	TArray<AActor*> ValidActors;
	for (TWeakObjectPtr<> Ptr : Sequencer->FindObjectsInCurrentSequence(Guid))
	{
		if (AActor* Actor = Cast<AActor>(Ptr.Get()))
		{
			ActorClass = Actor->GetClass();
			ValidActors.Add(Actor);
		}
	}

	FMovieScenePossessable* Possessable = MovieScene->FindPossessable(Guid);
	if (Possessable && ActorClass != nullptr && ValidActors.Num() != 0)
	{
		if (ValidActors.Num() > 1)
		{
			FString NewLabel = ActorClass->GetName() + FString::Printf(TEXT(" (%d)"), ValidActors.Num());

			Possessable->SetName(NewLabel);
		}
		else
		{
			Possessable->SetName(ValidActors[0]->GetActorLabel());
		}
	}

	Sequencer->RestorePreAnimatedState();

	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
}

void ULevelSequenceEditorSubsystem::AddAssignActorMenu(FMenuBuilder& MenuBuilder)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	TArray<FGuid> ObjectBindings;
	Sequencer->GetSelectedObjects(ObjectBindings);
	if (ObjectBindings.Num() == 0)
	{
		return;
	}

	MenuBuilder.AddMenuEntry(FLevelSequenceEditorCommands::Get().AddActorsToBinding);
	MenuBuilder.AddMenuEntry(FLevelSequenceEditorCommands::Get().ReplaceBindingWithActors);
	MenuBuilder.AddMenuEntry(FLevelSequenceEditorCommands::Get().RemoveActorsFromBinding);
	MenuBuilder.AddMenuEntry(FLevelSequenceEditorCommands::Get().RemoveAllBindings);
	MenuBuilder.AddMenuEntry(FLevelSequenceEditorCommands::Get().RemoveInvalidBindings);

	FMovieSceneBindingProxy BindingProxy(ObjectBindings[0], Sequencer->GetFocusedMovieSceneSequence());

	TSet<const AActor*> BoundObjects;
	{
		for (TWeakObjectPtr<> Ptr : Sequencer->FindObjectsInCurrentSequence(ObjectBindings[0]))
		{
			if (const AActor* Actor = Cast<AActor>(Ptr.Get()))
			{
				BoundObjects.Add(Actor);
			}
		}
	}

	auto IsActorValidForAssignment = [BoundObjects](const AActor* InActor){
		return !BoundObjects.Contains(InActor);
	};

	// Set up a menu entry to assign an actor to the object binding node
	FSceneOutlinerInitializationOptions InitOptions;
	{
		// We hide the header row to keep the UI compact.
		InitOptions.bShowHeaderRow = false;
		InitOptions.bShowSearchBox = true;
		InitOptions.bShowCreateNewFolder = false;
		InitOptions.bFocusSearchBoxWhenOpened = true;
		// Only want the actor label column
		InitOptions.ColumnMap.Add(FSceneOutlinerBuiltInColumnTypes::Label(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 0));
		
		// Only display actors that are not possessed already
		InitOptions.Filters->AddFilterPredicate<FActorTreeItem>(FActorTreeItem::FFilterPredicate::CreateLambda( IsActorValidForAssignment ) );
	}

	const float WidthOverride = Sequencer.IsValid() ? Sequencer->GetSequencerSettings()->GetAssetBrowserWidth() : 500.f;
	const float HeightOverride = Sequencer.IsValid() ? Sequencer->GetSequencerSettings()->GetAssetBrowserHeight() : 400.f;

	// actor selector to allow the user to choose an actor
	FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");
	TSharedRef< SWidget > MiniSceneOutliner =
		SNew( SBox )
		.WidthOverride(WidthOverride)
		.HeightOverride(HeightOverride)
		[
			SceneOutlinerModule.CreateActorPicker(
				InitOptions,
				FOnActorPicked::CreateLambda([=](AActor* Actor){
					// Create a new binding for this actor
					FSlateApplication::Get().DismissAllMenus();
					FSequencerUtilities::AssignActor(Sequencer.ToSharedRef(), Actor, ObjectBindings[0]);
				})
			)
		];

	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddWidget(MiniSceneOutliner, FText::GetEmpty(), true);
}

void ULevelSequenceEditorSubsystem::AddBindingPropertiesMenu(FMenuBuilder& MenuBuilder)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	TArray<FGuid> ObjectBindings;
	Sequencer->GetSelectedObjects(ObjectBindings);
	if (ObjectBindings.Num() == 0)
	{
		return;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return;
	}
	if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
	{
		MenuBuilder.AddMenuSeparator();

		NotifyHook = FBindingPropertiesNotifyHook(Sequence);
		// Set up a details panel for the list of locators
		FDetailsViewArgs DetailsViewArgs;
		{
			DetailsViewArgs.bAllowSearch = false;
			DetailsViewArgs.bCustomFilterAreaLocation = true;
			DetailsViewArgs.bCustomNameAreaLocation = true;
			DetailsViewArgs.bHideSelectionTip = true;
			DetailsViewArgs.bLockable = false;
			DetailsViewArgs.bSearchInitialKeyFocus = true;
			DetailsViewArgs.bUpdatesFromSelection = false;
			DetailsViewArgs.bShowOptions = false;
			DetailsViewArgs.bShowModifiedPropertiesOption = false;
			DetailsViewArgs.bShowScrollBar = false;
			DetailsViewArgs.bAllowMultipleTopLevelObjects = false;
			DetailsViewArgs.NotifyHook = &NotifyHook;
			DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;
		}

		TSharedRef<IDetailsView> DetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);

		AddBindingDetailCustomizations(DetailsView, Sequencer, ObjectBindings[0]);

		RefreshBindingDetails(&DetailsView.Get(), ObjectBindings[0]);
		DetailsView->OnFinishedChangingProperties().AddUObject(this, &ULevelSequenceEditorSubsystem::OnFinishedChangingLocators, DetailsView, ObjectBindings[0]);

		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().OnMenuBeingDestroyed().AddUObject(this, &ULevelSequenceEditorSubsystem::OnBindingPropertyMenuBeingDestroyed, DetailsView);
		}
		MenuBuilder.AddWidget(DetailsView, FText::GetEmpty(), true);
	}
}

void ULevelSequenceEditorSubsystem::AddConvertBindingsMenu(FMenuBuilder& MenuBuilder)
{
	// Binding conversion
	
	MenuBuilder.AddSubMenu(
		LOCTEXT("ConvertBindingLabel", "Convert Selected Binding(s) To..."),
		LOCTEXT("ConvertBindingLabelTooltip", "Convert selected bindings into another binding type"),
		FNewMenuDelegate::CreateLambda([this](FMenuBuilder& MenuBuilder)
			{
				TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
				if (Sequencer == nullptr)
				{
					return;
				}

				UMovieSceneSequence* const Sequence = Sequencer->GetFocusedMovieSceneSequence();
				if (!IsValid(Sequence))
				{
					return;
				}

				TArray<FGuid> ObjectBindings;
				Sequencer->GetSelectedObjects(ObjectBindings);
				if (ObjectBindings.Num() == 0)
				{
					return;
				}

				TArray<FSequencerChangeBindingInfo> Bindings;
				const FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences();
				for (FGuid ObjectGuid : ObjectBindings)
				{
					int32 BindingIndex = 0;
					for (const FMovieSceneBindingReference& Reference : BindingReferences->GetReferences(ObjectGuid))
					{
						Bindings.Add({ Reference.ID, BindingIndex++ });
					}
				}

				AddChangeBindingTypeMenu(MenuBuilder, Sequencer.ToSharedRef(), Bindings, true, TFunction<void()>());
			}));
}

void ULevelSequenceEditorSubsystem::AddBindingPropertiesSidebar(FMenuBuilder& MenuBuilder)
{
	AddBindingPropertiesMenu(MenuBuilder);
}

void ULevelSequenceEditorSubsystem::AddTrackRowMetadataMenu(FMenuBuilder& MenuBuilder)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}


	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return;
	}

	TArray<TPair<UMovieSceneTrack*, int32>> SelectedTrackRows;

	Sequencer->GetSelectedTrackRows(SelectedTrackRows);

	if (SelectedTrackRows.IsEmpty())
	{
		return;
	}

	NotifyHook = FBindingPropertiesNotifyHook(Sequence);

	// Set up a details panel for the list of selected track row metadata
	FDetailsViewArgs DetailsViewArgs;
	{
		DetailsViewArgs.bAllowSearch = false;
		DetailsViewArgs.bCustomFilterAreaLocation = false;
		DetailsViewArgs.bCustomNameAreaLocation = false;
		DetailsViewArgs.bHideSelectionTip = true;
		DetailsViewArgs.bLockable = false;
		DetailsViewArgs.bSearchInitialKeyFocus = true;
		DetailsViewArgs.bUpdatesFromSelection = false;
		DetailsViewArgs.bShowOptions = false;
		DetailsViewArgs.bShowModifiedPropertiesOption = false;
		DetailsViewArgs.bShowScrollBar = false;
		DetailsViewArgs.bAllowMultipleTopLevelObjects = false;
		DetailsViewArgs.NotifyHook = &NotifyHook;
	}

	TSharedRef<IDetailsView> DetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);

	AddTrackRowMetadataCustomizations(DetailsView, Sequencer, Sequence);

	RefreshTrackRowMetadataDetails(&DetailsView.Get());
	DetailsView->OnFinishedChangingProperties().AddUObject(this, &ULevelSequenceEditorSubsystem::OnFinishedChangingTrackRowMetadata, DetailsView);

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnMenuBeingDestroyed().AddUObject(this, &ULevelSequenceEditorSubsystem::OnTrackRowMetadataMenuBeingDestroyed, DetailsView);
	}
	MenuBuilder.AddWidget(DetailsView, FText::GetEmpty(), true);
}

void ULevelSequenceEditorSubsystem::FBindingPropertiesNotifyHook::NotifyPreChange(FProperty* PropertyAboutToChange)
{
	if (PropertyAboutToChange != nullptr)
	{
		GEditor->BeginTransaction(FText::Format(LOCTEXT("EditProperty", "Edit {0}"), PropertyAboutToChange->GetDisplayNameText()));

		ObjectToModify->Modify();
	}
}

void ULevelSequenceEditorSubsystem::FBindingPropertiesNotifyHook::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	GEditor->EndTransaction();
}


void ULevelSequenceEditorSubsystem::OnFinishedChangingLocators(const FPropertyChangedEvent& PropertyChangedEvent, TSharedRef<IDetailsView> DetailsView, FGuid ObjectBindingID)
{
	const FScopedTransaction Transaction(LOCTEXT("ChangeBindingProperties", "Change Binding Properties"));

	if (!BindingPropertyInfoList)
	{
		return;
	}

	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}
	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return;
	}
	if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
	{
		// A bit hacky, but saves a complicated detail customization. If the change we've just made is to add a new entry, ensure the new entry is initialized
		// to the same binding type as previous entries.
		TArrayView<const FMovieSceneBindingReference> PreviousReferences = BindingReferences->GetReferences(ObjectBindingID);
		if (PreviousReferences.Num() > 0 && PreviousReferences.Num() == BindingPropertyInfoList->Bindings.Num() - 1)
		{
			if (UMovieSceneCustomBinding* PreviousCustomBinding = BindingPropertyInfoList->Bindings[BindingPropertyInfoList->Bindings.Num() - 2].CustomBinding)
			{
				BindingPropertyInfoList->Bindings[BindingPropertyInfoList->Bindings.Num() - 1].CustomBinding = NewObject<UMovieSceneCustomBinding>(MovieScene, PreviousCustomBinding->GetClass());
			}
		}

		MovieScene->Modify();
		Sequence->Modify();
		// Clear the previous binding
		BindingReferences->RemoveBinding(ObjectBindingID);

		// Add the new updated bindings
		for (FMovieSceneBindingPropertyInfo& LocatorInfo : BindingPropertyInfoList->Bindings)
		{
			UMovieSceneCustomBinding* CopiedBinding = nullptr;
			if (LocatorInfo.CustomBinding)
			{
				CopiedBinding = Cast<UMovieSceneCustomBinding>(StaticDuplicateObject(LocatorInfo.CustomBinding, MovieScene));
			}
			BindingReferences->AddBinding(ObjectBindingID, MoveTemp(LocatorInfo.Locator), LocatorInfo.ResolveFlags, CopiedBinding);
			if (CopiedBinding)
			{
				CopiedBinding->OnBindingAddedOrChanged(*MovieScene);
			}
		}

		Sequencer->State.Invalidate(ObjectBindingID, Sequencer->GetFocusedTemplateID());

		// Update the object class and DisplayName
		TArrayView<TWeakObjectPtr<>> ObjectsInCurrentSequence = Sequencer->FindObjectsInCurrentSequence(ObjectBindingID);
		UClass* ObjectClass = nullptr;

		for (TWeakObjectPtr<> Ptr : ObjectsInCurrentSequence)
		{
			if (UObject* BoundObject = Ptr.Get())
			{
				if (ObjectClass == nullptr)
				{
					ObjectClass = BoundObject->GetClass();
				}
				else
				{
					ObjectClass = UClass::FindCommonBase(BoundObject->GetClass(), ObjectClass);
				}
			}
		}

		// Update label
		if (ObjectsInCurrentSequence.Num() > 0)
		{
			FMovieScenePossessable* Possessable = MovieScene->FindPossessable(ObjectBindingID);
			if (Possessable && ObjectClass != nullptr)
			{
				if (ObjectsInCurrentSequence.Num() > 1)
				{
					FString NewLabel = ObjectClass->GetName() + FString::Printf(TEXT(" (%d)"), ObjectsInCurrentSequence.Num());
					Possessable->SetName(NewLabel);
				}
				else if (AActor* Actor = Cast<AActor>(ObjectsInCurrentSequence[0].Get()))
				{
					Possessable->SetName(Actor->GetActorLabel());
				}
				else
				{
					Possessable->SetName(ObjectClass->GetName());
				}

				Possessable->SetPossessedObjectClass(ObjectClass);
			}
		}

		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);

		// Destroy any previous spawnables- they'll get recreated on the force evaluate below
		for (int32 BindingIndex = 0; BindingIndex < BindingPropertyInfoList->Bindings.Num(); ++BindingIndex)
		{
			Sequencer->GetSpawnRegister().DestroySpawnedObject(ObjectBindingID, Sequencer->GetFocusedTemplateID(), Sequencer->GetSharedPlaybackState(), BindingIndex);
		}

		// Force evaluate the Sequencer after clearing the cache (which the above will do) so that any newly loaded actors will be loaded as part of the transaction
		Sequencer->ForceEvaluate();

		// Send the OnAddBinding message, which will add a Binding Lifetime Track if necessary
		Sequencer->OnAddBinding(ObjectBindingID, MovieScene);

		// Re-copy the locator info back into the struct details
		BindingPropertyInfoList->Bindings.Empty();
		Algo::Transform(BindingReferences->GetReferences(ObjectBindingID), BindingPropertyInfoList->Bindings, [this](const FMovieSceneBindingReference& Reference)
			{
				UMovieSceneCustomBinding* CopiedBinding = nullptr;
				if (Reference.CustomBinding)
				{
					CopiedBinding = Cast<UMovieSceneCustomBinding>(StaticDuplicateObject(Reference.CustomBinding, this));
				}

				return FMovieSceneBindingPropertyInfo{ Reference.Locator, Reference.ResolveFlags, CopiedBinding };
			});


		// Force the struct details view to refresh
		DetailsView->InvalidateCachedState();
	}
}

void ULevelSequenceEditorSubsystem::OnFinishedChangingTrackRowMetadata(const FPropertyChangedEvent& PropertyChangedEvent, TSharedRef<IDetailsView> DetailsView)
{
	const FScopedTransaction Transaction(LOCTEXT("ChangeTrackRowMetadata", "Change Track Row Metadata"));

	if (TrackRowMetadataHelperList.IsEmpty())
	{
		return;
	}

	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}
	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return;
	}

	MovieScene->Modify();
	Sequence->Modify();

	TArray<TPair<UMovieSceneTrack*, int32>> SelectedTrackRows;

	Sequencer->GetSelectedTrackRows(SelectedTrackRows);

	ensure(SelectedTrackRows.Num() == TrackRowMetadataHelperList.Num());
	
	// Copy over the new metadata, but duplicate any condition trees over to new ownership in the sequence
	for (int32 Index = 0; Index < TrackRowMetadataHelperList.Num(); ++Index)
	{
		UMovieSceneTrack* Track = SelectedTrackRows[Index].Key;
		UMovieSceneTrackRowMetadataHelper* Helper = TrackRowMetadataHelperList[Index];
		if (Track && Helper)
		{
			FMovieSceneTrackRowMetadata& Metadata = Track->FindOrAddTrackRowMetadata(SelectedTrackRows[Index].Value);
			Metadata = Helper->TrackRowMetadata;
			if (Helper->TrackRowMetadata.ConditionContainer.Condition)
			{
				Metadata.ConditionContainer.Condition = Cast<UMovieSceneCondition>(StaticDuplicateObject(Helper->TrackRowMetadata.ConditionContainer.Condition, MovieScene));
			}
		}
	}

	Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);

	// Re-copy the metadata info back into the struct details
	RefreshTrackRowMetadataDetails(&DetailsView.Get());

	// Force the struct details view to refresh
	DetailsView->InvalidateCachedState();
}

void ULevelSequenceEditorSubsystem::GetRebindComponentNames(TArray<FName>& OutComponentNames)
{
	OutComponentNames.Empty();

	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return;
	}

	TArray<FGuid> ObjectBindings;
	Sequencer->GetSelectedObjects(ObjectBindings);
	if (ObjectBindings.Num() == 0)
	{
		return;
	}

	FGuid ComponentGuid = ObjectBindings[0];

	FMovieScenePossessable* ComponentPossessable = MovieScene->FindPossessable(ComponentGuid);

	FGuid ActorParentGuid = ComponentPossessable ? ComponentPossessable->GetParent() : FGuid();

	TArrayView<TWeakObjectPtr<>> ObjectsInCurrentSequence = Sequencer->FindObjectsInCurrentSequence(ActorParentGuid);

	const AActor* Actor = nullptr;
	for (TWeakObjectPtr<> Ptr : ObjectsInCurrentSequence)
	{
		Actor = Cast<AActor>(Ptr.Get());
		if (Actor)
		{
			break;
		}
	}

	if (!Actor)
	{
		return;
	}
		
	FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");
	const TSharedPtr<IClassViewerFilter>& GlobalClassFilter = ClassViewerModule.GetGlobalClassViewerFilter();
	TSharedRef<FClassViewerFilterFuncs> ClassFilterFuncs = ClassViewerModule.CreateFilterFuncs();
	FClassViewerInitializationOptions ClassViewerOptions = {};

	for (UActorComponent* Component : Actor->GetComponents())
	{
		if (Component && ComponentPossessable && Component->GetName() != ComponentPossessable->GetName())
		{
			bool bValidComponent = !Component->IsVisualizationComponent();

			if (GlobalClassFilter.IsValid())
			{
				// Hack - forcibly allow USkeletalMeshComponentBudgeted until FORT-527888
				static const FName SkeletalMeshComponentBudgetedClassName(TEXT("SkeletalMeshComponentBudgeted"));
				if (Component->GetClass()->GetName() == SkeletalMeshComponentBudgetedClassName)
				{
					bValidComponent = true;
				}
				else
				{
					bValidComponent = GlobalClassFilter->IsClassAllowed(ClassViewerOptions, Component->GetClass(), ClassFilterFuncs);
				}
			}

			if (bValidComponent)
			{
				OutComponentNames.Add(Component->GetFName());
			}
		}
	}
	OutComponentNames.Sort(FNameFastLess());
}

void ULevelSequenceEditorSubsystem::RebindComponentMenu(FMenuBuilder& MenuBuilder)
{
	TArray<FName> ComponentNames;
	GetRebindComponentNames(ComponentNames);

	for (const FName& ComponentName : ComponentNames)
	{
		FText RebindComponentLabel = FText::FromName(ComponentName);
		MenuBuilder.AddMenuEntry(
			RebindComponentLabel, 
			FText(), 
			FSlateIcon(), 
			FUIAction(FExecuteAction::CreateLambda([this, ComponentName]() { RebindComponentInternal(ComponentName); } ) ) );
	}
}

void ULevelSequenceEditorSubsystem::RebindComponentInternal(const FName& ComponentName)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	TArray<FGuid> ObjectBindings;
	Sequencer->GetSelectedObjects(ObjectBindings);
	if (ObjectBindings.Num() == 0)
	{
		return;
	}

	TArray<FMovieSceneBindingProxy> BindingProxies;
	for (const FGuid& ObjectBinding : ObjectBindings)
	{
		FMovieSceneBindingProxy BindingProxy(ObjectBinding, Sequencer->GetFocusedMovieSceneSequence());
		BindingProxies.Add(BindingProxy);
	}

	RebindComponent(BindingProxies, ComponentName);
}

void ULevelSequenceEditorSubsystem::RebindComponent(const TArray<FMovieSceneBindingProxy>& PossessableBindings, const FName& ComponentName)
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return;
	}

	FScopedTransaction RebindComponent(LOCTEXT("RebindComponent", "Rebind Component"));

	Sequence->Modify();
	MovieScene->Modify();

	bool bAnythingChanged = false;
	for (const FMovieSceneBindingProxy& PossessableBinding : PossessableBindings)
	{
		FMovieScenePossessable* ComponentPossessable = MovieScene->FindPossessable(PossessableBinding.BindingID);

		FGuid ActorParentGuid = ComponentPossessable ? ComponentPossessable->GetParent() : FGuid();

		TArrayView<TWeakObjectPtr<>> ObjectsInCurrentSequence = Sequencer->FindObjectsInCurrentSequence(ActorParentGuid);

		for (TWeakObjectPtr<> Ptr : ObjectsInCurrentSequence)
		{
			if (const AActor* Actor = Cast<AActor>(Ptr.Get()))
			{
				for (UActorComponent* Component : Actor->GetComponents())
				{
					if (Component->GetFName() == ComponentName)
					{
						FGuid ComponentBinding = Sequence->CreatePossessable(Component);
						
						if (PossessableBinding.BindingID.IsValid() && ComponentBinding.IsValid())
						{
							MovieScene->MoveBindingContents(PossessableBinding.BindingID, ComponentBinding);

							bAnythingChanged = true;
						}
					}
				}
			}
		}
	}

	if (bAnythingChanged)
	{
		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
	}
}

bool ULevelSequenceEditorSubsystem::IsSelectedBindingRootPossessable()
{
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer)
	{
		TArray<FGuid> ObjectBindings;
		Sequencer->GetSelectedObjects(ObjectBindings);
		if (ObjectBindings.Num() > 0)
		{
			UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
			UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
			if (MovieScene)
			{
				if (FMovieScenePossessable* Possessable = MovieScene->FindPossessable(ObjectBindings[0]))
				{
					if (!Possessable->GetParent().IsValid() && !Possessable->GetSpawnableObjectBindingID().IsValid())
					{
						return true;
					}
				}
			}
		}
	}
	return false;
}

void ULevelSequenceEditorSubsystem::RefreshBindingDetails(IDetailsView* DetailsView, FGuid ObjectBindingID)
{
	if (DetailsView == nullptr)
	{
		return;
	}
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return;
	}
	if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
	{
		BindingPropertyInfoList = NewObject<UMovieSceneBindingPropertyInfoList>(this);
		Algo::Transform(BindingReferences->GetReferences(ObjectBindingID), BindingPropertyInfoList->Bindings, [this](const FMovieSceneBindingReference& Reference)
			{
				UMovieSceneCustomBinding* CopiedBinding = nullptr;
				if (Reference.CustomBinding)
				{
					CopiedBinding = Cast<UMovieSceneCustomBinding>(StaticDuplicateObject(Reference.CustomBinding, this));
				}
				return FMovieSceneBindingPropertyInfo{ Reference.Locator, Reference.ResolveFlags, CopiedBinding };
			});

		DetailsView->SetObject(BindingPropertyInfoList.Get(), true);
	}
}

void ULevelSequenceEditorSubsystem::RefreshTrackRowMetadataDetails(IDetailsView* DetailsView)
{
	if (DetailsView == nullptr)
	{
		return;
	}
	TSharedPtr<ISequencer> Sequencer = GetActiveSequencer();
	if (Sequencer == nullptr)
	{
		return;
	}

	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	if (!Sequence)
	{
		return;
	}


	TArray<TPair<UMovieSceneTrack*, int32>> SelectedTrackRows;

	Sequencer->GetSelectedTrackRows(SelectedTrackRows);

	TrackRowMetadataHelperList.Empty();

	// Copy over the metadata, but duplicate any condition trees over to new ownership
	for (int32 Index = 0; Index < SelectedTrackRows.Num(); ++Index)
	{
		UMovieSceneTrack* Track = SelectedTrackRows[Index].Key;
		UMovieSceneTrackRowMetadataHelper* Helper = TrackRowMetadataHelperList.Add_GetRef(NewObject<UMovieSceneTrackRowMetadataHelper>(this));
		if (Track && Helper)
		{
			Helper->OwnerTrack = Track;
			if (const FMovieSceneTrackRowMetadata* Metadata = Track->FindTrackRowMetadata(SelectedTrackRows[Index].Value))
			{
				Helper->TrackRowMetadata = *Metadata;
				if (Metadata->ConditionContainer.Condition)
				{
					Helper->TrackRowMetadata.ConditionContainer.Condition = Cast<UMovieSceneCondition>(StaticDuplicateObject(Metadata->ConditionContainer.Condition, this));
				}
			}
		}
	}
	TArray<TWeakObjectPtr<UObject>> WeakHelpers;
	Algo::Transform(TrackRowMetadataHelperList, WeakHelpers, [](TObjectPtr<UMovieSceneTrackRowMetadataHelper> HelperPtr) { return HelperPtr;});
	DetailsView->SetObjects(WeakHelpers, true);
}


void ULevelSequenceEditorSubsystem::AddChangeBindingTypeMenu(FMenuBuilder& MenuBuilder, TSharedRef<ISequencer> Sequencer, const TArray<FSequencerChangeBindingInfo>& BindingsToChange, bool bConvert, TFunction<void()> OnBindingChanged)
{
	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;

	if (!MovieScene || BindingsToChange.IsEmpty())
	{
		return;
	}

	// Can convert to possessable
	if (!bConvert || Algo::AllOf(BindingsToChange, [this, Sequencer, Sequence, MovieScene](const FSequencerChangeBindingInfo& BindingInfo) {
		return FSequencerUtilities::CanConvertToPossessable(Sequencer, BindingInfo.BindingID, BindingInfo.BindingIndex);
		}))
	{
		const TWeakPtr<ISequencer> WeakSequencer = Sequencer;

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ConvertToPossessable", "Possessable"),
			bConvert ? LOCTEXT("ConvertToPossessableTooltip", "Convert selected binding(s) to a possessable") : LOCTEXT("ChangeToPossessableTooltip", "Reset selected binding(s) to a new possessable"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, WeakSequencer, Sequence, BindingsToChange, bConvert, OnBindingChanged]()
				{
					const TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
					if (!Sequencer)
					{
						return;
					}

					const TSharedRef<ISequencer> SequencerRef = Sequencer.ToSharedRef();

					ChangeBindingTypes(SequencerRef, BindingsToChange, [SequencerRef, Sequence, bConvert](FGuid BindingID, int32 BindingIndex)
						{
							if (bConvert)
							{
								return FSequencerUtilities::ConvertToPossessable(SequencerRef, BindingID, BindingIndex);
							}
							else if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
							{
								BindingReferences->AddOrReplaceBinding(BindingID, FUniversalObjectLocator(), BindingIndex);
								return Sequence->GetMovieScene()->FindPossessable(BindingID);
							}
							return (FMovieScenePossessable*)nullptr;
						},
						OnBindingChanged);

				})));
	}


	// Sort custom binding types by engine types vs. non-engine (custom user types)
	TArrayView<const TSubclassOf<UMovieSceneCustomBinding>> PrioritySortedCustomBindingTypes = Sequencer->GetSupportedCustomBindingTypes();
	TArray<const TSubclassOf<UMovieSceneCustomBinding>> EngineClasses;
	TArray<const TSubclassOf<UMovieSceneCustomBinding>> UserClasses;
	for (const TSubclassOf<UMovieSceneCustomBinding>& CustomBindingType : PrioritySortedCustomBindingTypes)
	{
		FString PackagePathName = CustomBindingType->GetPackage()->GetPathName();
		if (PackagePathName.StartsWith(TEXT("/Engine")) || PackagePathName.StartsWith(TEXT("/Script")))
		{
			EngineClasses.Add(CustomBindingType);
		}
		else
		{
			UserClasses.Add(CustomBindingType);
		}
	}

	// Show built-in classes first
	for (const TSubclassOf<UMovieSceneCustomBinding>& CustomBindingType : EngineClasses)
	{
		FString BindingTypePath = CustomBindingType->GetPathName();
		FString OtherPath = CustomBindingType->GetPackage()->GetPathName();

		// Can convert to custom bindings
		if (!bConvert || Algo::AllOf(BindingsToChange, [this, &Sequence, &MovieScene, CustomBindingType, Sequencer](const FSequencerChangeBindingInfo& BindingInfo)
			{
				return FSequencerUtilities::CanConvertToCustomBinding(Sequencer, BindingInfo.BindingID, CustomBindingType, BindingInfo.BindingIndex);
			}))
		{
			const TWeakPtr<ISequencer> WeakSequencer = Sequencer;

			// Special case director blueprint bindings to show sub-menu for setting endpoints
			if (CustomBindingType == UMovieSceneSpawnableDirectorBlueprintBinding::StaticClass()
				|| CustomBindingType == UMovieSceneReplaceableDirectorBlueprintBinding::StaticClass())
			{
				// Option to use a director blueprint condition and create or quick bind to an endpoint
				MenuBuilder.AddSubMenu(
					FText::Format(LOCTEXT("ConvertToDirectorBlueprintBinding", "{0}..."), CustomBindingType->GetDefaultObject<UMovieSceneCustomBinding>()->GetBindingTypePrettyName()),
					bConvert ? FText::Format(LOCTEXT("ConvertToCustomBindingTooltip", "Convert selected binding to {0}"), CustomBindingType->GetDefaultObject<UMovieSceneCustomBinding>()->GetBindingTypePrettyName())
					: FText::Format(LOCTEXT("ChangeToCustomBindingTooltip", "Reset selected binding to a new {0}"), CustomBindingType->GetDefaultObject<UMovieSceneCustomBinding>()->GetBindingTypePrettyName()),
					FNewMenuDelegate::CreateLambda([this, Sequencer, BindingsToChange, bConvert, OnBindingChanged, CustomBindingType](FMenuBuilder& SubMenuBuilder) { FillDirectorBlueprintBindingSubMenu(SubMenuBuilder, Sequencer, BindingsToChange, bConvert, OnBindingChanged, CustomBindingType); }),
					false,
					CustomBindingType->GetDefaultObject<UMovieSceneCustomBinding>()->GetBindingTrackCustomIconOverlay()
				);
			}
			else
			{
				MenuBuilder.AddMenuEntry(
					CustomBindingType->GetDefaultObject<UMovieSceneCustomBinding>()->GetBindingTypePrettyName(),
					bConvert ? FText::Format(LOCTEXT("ConvertToCustomBindingTooltip", "Convert selected binding to {0}"), CustomBindingType->GetDefaultObject<UMovieSceneCustomBinding>()->GetBindingTypePrettyName())
					: FText::Format(LOCTEXT("ChangeToCustomBindingTooltip", "Reset selected binding to a new {0}"), CustomBindingType->GetDefaultObject<UMovieSceneCustomBinding>()->GetBindingTypePrettyName()),
					CustomBindingType->GetDefaultObject<UMovieSceneCustomBinding>()->GetBindingTrackCustomIconOverlay(),
					FUIAction(FExecuteAction::CreateLambda([this, WeakSequencer, CustomBindingType, Sequence, bConvert, BindingsToChange, OnBindingChanged]()
						{
							const TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
							if (!Sequencer)
							{
								return;
							}

							const TSharedRef<ISequencer> SequencerRef = Sequencer.ToSharedRef();
							ChangeBindingTypes(SequencerRef, BindingsToChange, [SequencerRef, Sequence, bConvert, CustomBindingType](FGuid BindingID, int32 BindingIndex)
								{
									if (bConvert)
									{
										return FSequencerUtilities::ConvertToCustomBinding(SequencerRef, BindingID, CustomBindingType, BindingIndex);
									}
									else if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
									{
										BindingReferences->AddOrReplaceBinding(BindingID,
											NewObject<UMovieSceneCustomBinding>(Sequence->GetMovieScene(), CustomBindingType),
											BindingIndex);
										return Sequence->GetMovieScene()->FindPossessable(BindingID);
									}
									return (FMovieScenePossessable*)nullptr;
								},
								OnBindingChanged);
						})));
			}
		}
	}

	MenuBuilder.AddSeparator();

	// Custom classes
	MenuBuilder.AddSubMenu(
		LOCTEXT("CustomBindings", "Custom Bindings..."),
		LOCTEXT("CustomBindingsTooltip", "Choose or create a custom binding type"),
		FNewMenuDelegate::CreateLambda([this, Sequencer, BindingsToChange, bConvert, OnBindingChanged, UserClasses](FMenuBuilder& SubMenuBuilder) { FillBindingClassSubMenu(SubMenuBuilder, Sequencer, BindingsToChange, bConvert, OnBindingChanged, UserClasses); }),
		false
	);

}

void ULevelSequenceEditorSubsystem::FillDirectorBlueprintBindingSubMenu(FMenuBuilder& MenuBuilder, TSharedRef<ISequencer> Sequencer, const TArray<FSequencerChangeBindingInfo>& BindingsToChange, bool bConvert, TFunction<void()> OnBindingChanged, const TSubclassOf<UMovieSceneCustomBinding>& CustomBindingType)
{
	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
	const TWeakPtr<ISequencer> WeakSequencer = Sequencer;
	if (Sequence)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CreateBindingEndpoint_Text", "Create New Binding Endpoint"),
			LOCTEXT("CreateBindingEndpoint_Tooltip", "Creates a new binding endpoint in this sequence's blueprint."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Sequencer.CreateEventBinding"),
			FUIAction(
				FExecuteAction::CreateLambda([this, WeakSequencer, CustomBindingType, Sequence, bConvert, BindingsToChange, OnBindingChanged]()
					{
						const TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
						if (!Sequencer)
						{
							return;
						}

						const TSharedRef<ISequencer> SequencerRef = Sequencer.ToSharedRef();

						if (Sequence)
						{
							// Change or convert the binding
							ChangeBindingTypes(SequencerRef, BindingsToChange, [SequencerRef, Sequence, bConvert, CustomBindingType](FGuid BindingID, int32 BindingIndex)
								{
									FMovieScenePossessable* NewPossessable = nullptr;
									if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
									{
										if (bConvert)
										{
											NewPossessable = FSequencerUtilities::ConvertToCustomBinding(SequencerRef, BindingID, CustomBindingType, BindingIndex);
										}
										else
										{
											BindingReferences->AddOrReplaceBinding(BindingID,
												NewObject<UMovieSceneCustomBinding>(Sequence->GetMovieScene(), CustomBindingType),
												BindingIndex);
											NewPossessable = Sequence->GetMovieScene()->FindPossessable(BindingID);
										}

										UMovieSceneCustomBinding* NewCustomBinding = BindingReferences->GetCustomBinding(BindingID, BindingIndex);

										TArray<void*> RawData;
										if (UMovieSceneReplaceableDirectorBlueprintBinding* ReplaceableBinding = Cast<UMovieSceneReplaceableDirectorBlueprintBinding>(NewCustomBinding))
										{
											RawData.Add(&ReplaceableBinding->DynamicBinding);
										}
										else if (UMovieSceneSpawnableDirectorBlueprintBinding* SpawnableBinding = Cast<UMovieSceneSpawnableDirectorBlueprintBinding>(NewCustomBinding))
										{
											RawData.Add(&SpawnableBinding->DynamicBinding);
										}

										// Create temporary director blueprint binding customization for use in creating the endpoint
										TSharedRef<FMovieSceneDynamicBindingCustomization> BlueprintBindingCustomization = StaticCastSharedRef<FMovieSceneDynamicBindingCustomization>(FMovieSceneDynamicBindingCustomization::MakeInstance(Sequence->GetMovieScene(), BindingID, BindingIndex));
										BlueprintBindingCustomization->SetRawData(RawData);
										BlueprintBindingCustomization->CreateEndpoint();
									}

									return NewPossessable;
								},
								OnBindingChanged);
						}
					}
				)
			));

		MenuBuilder.AddSubMenu(
			LOCTEXT("CreateQuickBinding_Text", "Quick Bind"),
			LOCTEXT("CreateQuickBinding_Tooltip", "Shows a list of functions in this sequence's blueprint that can be used for bindings."),
			FNewMenuDelegate::CreateLambda([this, Sequencer, BindingsToChange, bConvert, OnBindingChanged, CustomBindingType](FMenuBuilder& SubMenuBuilder) { PopulateQuickBindSubMenu(SubMenuBuilder, Sequencer, BindingsToChange, bConvert, OnBindingChanged, CustomBindingType); }),
			false /* bInOpenSubMenuOnClick */,
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Sequencer.CreateQuickBinding"),
			false /* bInShouldWindowAfterMenuSelection */
		);
	}
}

void ULevelSequenceEditorSubsystem::PopulateQuickBindSubMenu(FMenuBuilder& MenuBuilder, TSharedRef<ISequencer> Sequencer, const TArray<FSequencerChangeBindingInfo>& BindingsToChange, bool bConvert, TFunction<void()> OnBindingChanged, const TSubclassOf<UMovieSceneCustomBinding>& CustomBindingType)
{
	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;

	if (!MovieScene || BindingsToChange.IsEmpty())
	{
		return;
	}

	TSharedRef<FMovieSceneDynamicBindingCustomization> BlueprintBindingCustomization = StaticCastSharedRef<FMovieSceneDynamicBindingCustomization>(FMovieSceneDynamicBindingCustomization::MakeInstance(Sequence->GetMovieScene(), BindingsToChange[0].BindingID, BindingsToChange[0].BindingIndex));

	BlueprintBindingCustomization->PopulateQuickBindSubMenu(MenuBuilder,
		Sequence,
		FOnQuickBindActionSelected::CreateLambda([this, Sequencer, BindingsToChange, Sequence, bConvert, OnBindingChanged, CustomBindingType]
		(
			const TArray<TSharedPtr<FEdGraphSchemaAction>>& SelectedAction,
			ESelectInfo::Type InSelectionType,
			UBlueprint* Blueprint,
			FMovieSceneDirectorBlueprintEndpointDefinition EndpointDefinition
			)
			{
				if (!SelectedAction.IsEmpty())
				{
					// Change or convert the binding
					ChangeBindingTypes(Sequencer, BindingsToChange, [Sequencer, Sequence, bConvert, OnBindingChanged, CustomBindingType, &SelectedAction, InSelectionType, Blueprint, EndpointDefinition](FGuid BindingID, int32 BindingIndex)
						{
							FMovieScenePossessable* NewPossessable = nullptr;
							if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
							{
								if (bConvert)
								{
									NewPossessable = FSequencerUtilities::ConvertToCustomBinding(Sequencer, BindingID, CustomBindingType, BindingIndex);
								}
								else
								{
									BindingReferences->AddOrReplaceBinding(BindingID,
										NewObject<UMovieSceneCustomBinding>(Sequence->GetMovieScene(), CustomBindingType),
										BindingIndex);
									NewPossessable = Sequence->GetMovieScene()->FindPossessable(BindingID);
								}

								UMovieSceneCustomBinding* NewCustomBinding = BindingReferences->GetCustomBinding(BindingID, BindingIndex);

								TArray<void*> RawData;
								if (UMovieSceneReplaceableDirectorBlueprintBinding* ReplaceableBinding = Cast<UMovieSceneReplaceableDirectorBlueprintBinding>(NewCustomBinding))
								{
									RawData.Add(&ReplaceableBinding->DynamicBinding);
								}
								else if (UMovieSceneSpawnableDirectorBlueprintBinding* SpawnableBinding = Cast<UMovieSceneSpawnableDirectorBlueprintBinding>(NewCustomBinding))
								{
									RawData.Add(&SpawnableBinding->DynamicBinding);
								}

								// Create temporary director blueprint binding customization for use in creating the endpoint
								TSharedRef<FMovieSceneDynamicBindingCustomization> BlueprintBindingCustomization = StaticCastSharedRef<FMovieSceneDynamicBindingCustomization>(FMovieSceneDynamicBindingCustomization::MakeInstance(Sequence->GetMovieScene(), BindingID, BindingIndex));
								BlueprintBindingCustomization->SetRawData(RawData);
								BlueprintBindingCustomization->HandleQuickBindActionSelected(SelectedAction, InSelectionType, Blueprint, EndpointDefinition);

							}
							return NewPossessable;
						},
						OnBindingChanged);
				}
			}));
}

void ULevelSequenceEditorSubsystem::FillBindingClassSubMenu(FMenuBuilder& MenuBuilder, TSharedRef<ISequencer> Sequencer, const TArray<FSequencerChangeBindingInfo>& BindingsToChange, bool bConvert, TFunction<void()> OnBindingsChanged, const TArray<const TSubclassOf<UMovieSceneCustomBinding>>& UserCustomBindingTypes)
{
	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();
	UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;

	if (!MovieScene || BindingsToChange.IsEmpty())
	{
		return;
	}

	// If we allow creating binding classes from this replaceable actor binding...
	if (MovieScene->IsCustomBindingClassAllowed(UMovieSceneReplaceableActorBinding_BPBase::StaticClass()))
	{
		// Create a new custom binding Class
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CreateNewCustomBindingClass", "Create new Replaceable Binding Class"),
			LOCTEXT("CreateNewCustomBindingClassTooltip", "Creates a new replaceable binding blueprint class"),
			UMovieSceneReplaceableActorBinding_BPBase::StaticClass()->GetDefaultObject<UMovieSceneReplaceableActorBinding_BPBase>()->GetBindingTrackCustomIconOverlay(),
			FUIAction(FExecuteAction::CreateLambda([this, Sequencer, Sequence, bConvert, BindingsToChange, OnBindingsChanged]()
				{
					FAssetToolsModule& AssetToolsModule = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools");

					FString NewBindingPath = Sequence->GetPathName();
					FString NewBindingName = Sequence->GetName() + TEXT("_BindingType");
					AssetToolsModule.Get().CreateUniqueAssetName(NewBindingPath + TEXT("/") + NewBindingName, TEXT(""), NewBindingPath, NewBindingName);

					UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprintFromClass(LOCTEXT("CreateNewBindingClass", "Create New Replaceable Binding Class"), UMovieSceneReplaceableActorBinding_BPBase::StaticClass(), NewBindingName);

					if (Blueprint != NULL && Blueprint->GeneratedClass)
					{
						GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);

						// Implement the ResolveRuntimeBindingInternal function

						UFunction* OverrideFunc = FindUField<UFunction>(UMovieSceneReplaceableActorBinding_BPBase::StaticClass(), GET_FUNCTION_NAME_CHECKED(UMovieSceneReplaceableActorBinding_BPBase, BP_ResolveRuntimeBinding));
						check(OverrideFunc);
						Blueprint->Modify();
						// Implement the function graph
						UEdGraph* const NewGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("BP_ResolveRuntimeBinding"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
						FBlueprintEditorUtils::AddFunctionGraph(Blueprint, NewGraph, /*bIsUserCreated=*/ false, UMovieSceneReplaceableActorBinding_BPBase::StaticClass());
						NewGraph->Modify();
						FKismetEditorUtilities::CompileBlueprint(Blueprint);

						// Change or convert the binding
						ChangeBindingTypes(Sequencer, BindingsToChange, [Blueprint, Sequencer, Sequence, bConvert, OnBindingsChanged](FGuid BindingID, int32 BindingIndex)
							{
								FMovieScenePossessable* NewPossessable = nullptr;
								if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
								{
									if (bConvert)
									{
										NewPossessable = FSequencerUtilities::ConvertToCustomBinding(Sequencer, BindingID, TSubclassOf<UMovieSceneCustomBinding>(Blueprint->GeneratedClass), BindingIndex);
									}
									else
									{
										BindingReferences->AddOrReplaceBinding(BindingID,
											NewObject<UMovieSceneCustomBinding>(Sequence->GetMovieScene(), Blueprint->GeneratedClass),
											BindingIndex);
										NewPossessable = Sequence->GetMovieScene()->FindPossessable(BindingID);
									}
								}
								return NewPossessable;
							},
							OnBindingsChanged);


						FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(NewGraph);
						Sequencer->RefreshSupportedCustomBindingTypes();
					}
				}
		)));

		class FUserCustomBindingFilter : public IClassViewerFilter
		{
		public:

			TArray<const TSubclassOf<UMovieSceneCustomBinding>> UserCustomBindingTypes;

			virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
			{
				return UserCustomBindingTypes.Contains(InClass);
			}

			virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InBlueprint, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
			{
				return InBlueprint->GetNativeParent() && InBlueprint->GetNativeParent()->IsChildOf(UMovieSceneReplaceableActorBinding_BPBase::StaticClass());
			}
		};

		MenuBuilder.BeginSection(TEXT("ChooseCustomBindingClass"), LOCTEXT("ChooseCustomBindingClass", "Choose Custom Binding Class"));
		{
			TSharedPtr<FUserCustomBindingFilter> BindingClassFilter = MakeShared<FUserCustomBindingFilter>();
			BindingClassFilter->UserCustomBindingTypes = UserCustomBindingTypes;

			FClassViewerInitializationOptions Options;
			Options.bShowBackgroundBorder = false;
			Options.bShowUnloadedBlueprints = true;
			Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::DisplayName;

			Options.ClassFilters.Add(BindingClassFilter.ToSharedRef());

			MenuBuilder.AddWidget(FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer").CreateClassViewer(Options,
				FOnClassPicked::CreateLambda([this, Sequencer, Sequence, bConvert, BindingsToChange, OnBindingsChanged](UClass* Class)
					{
						// Change or convert the binding
						ChangeBindingTypes(Sequencer, BindingsToChange, [Sequencer, Sequence, Class, bConvert, OnBindingsChanged](FGuid BindingID, int32 BindingIndex)
							{
								FMovieScenePossessable* NewPossessable = nullptr;
								if (FMovieSceneBindingReferences* BindingReferences = Sequence->GetBindingReferences())
								{
									if (bConvert)
									{
										NewPossessable = FSequencerUtilities::ConvertToCustomBinding(Sequencer, BindingID, TSubclassOf<UMovieSceneCustomBinding>(Class), BindingIndex);
									}
									else
									{
										BindingReferences->AddOrReplaceBinding(BindingID,
											NewObject<UMovieSceneCustomBinding>(Sequence->GetMovieScene(), Class),
											BindingIndex);
										NewPossessable = Sequence->GetMovieScene()->FindPossessable(BindingID);
									}
								}
								return NewPossessable;
							},
							OnBindingsChanged);
						FSlateApplication::Get().DismissAllMenus();
					})), FText::GetEmpty(), true);
		}
		MenuBuilder.EndSection();
	}
}

void ULevelSequenceEditorSubsystem::ChangeBindingTypes(const TSharedRef<ISequencer>& InSequencer
	, const TArray<FSequencerChangeBindingInfo>& InBindingsToChange
	, TFunction<FMovieScenePossessable* (FGuid, int32)> InDoChangeType
	, TFunction<void()> InOnBindingChanged)
{
	using namespace UE::Sequencer;

	UMovieSceneSequence* const Sequence = InSequencer->GetFocusedMovieSceneSequence();
	if (!IsValid(Sequence))
	{
		return;
	}

	UMovieScene* const MovieScene = Sequence->GetMovieScene();
	if (!IsValid(MovieScene))
	{
		return;
	}

	if (InBindingsToChange.Num() == 0)
	{
		return;
	}

	if (MovieScene->IsReadOnly())
	{
		FSequencerUtilities::ShowReadOnlyError();
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ConvertSelectedNodes", "Convert Selected Nodes Binding Type"));
	MovieScene->Modify();

	FScopedSlowTask SlowTask(InBindingsToChange.Num(), LOCTEXT("ConvertProgress", "Converting Selected Nodes Binding Type"));
	SlowTask.MakeDialog(true);

	TArray<AActor*> PossessedActors;
	for (const FSequencerChangeBindingInfo& BindingInfo : InBindingsToChange)
	{
		SlowTask.EnterProgressFrame();

		if (FMovieScenePossessable* Possessable = InDoChangeType(BindingInfo.BindingID, BindingInfo.BindingIndex))
		{
			InSequencer->ForceEvaluate();

			for (TWeakObjectPtr<> WeakObject : InSequencer->FindBoundObjects(Possessable->GetGuid(), InSequencer->GetFocusedTemplateID()))
			{
				if (AActor* PossessedActor = Cast<AActor>(WeakObject.Get()))
				{
					PossessedActors.Add(PossessedActor);
				}
			}

			if (GWarn->ReceivedUserCancel())
			{
				break;
			}
		}
	}

	if (PossessedActors.Num())
	{
		const bool bNotifySelectionChanged = true;
		const bool bDeselectBSP = true;
		const bool bWarnAboutTooManyActors = false;
		const bool bSelectEvenIfHidden = false;

		GEditor->GetSelectedActors()->Modify();
		GEditor->GetSelectedActors()->BeginBatchSelectOperation();
		GEditor->SelectNone(bNotifySelectionChanged, bDeselectBSP, bWarnAboutTooManyActors);
		for (auto PossessedActor : PossessedActors)
		{
			GEditor->SelectActor(PossessedActor, true, bNotifySelectionChanged, bSelectEvenIfHidden);
		}
		GEditor->GetSelectedActors()->EndBatchSelectOperation();
		GEditor->NoteSelectionChange();

		InSequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
	}

	if (InOnBindingChanged != nullptr)
	{
		InOnBindingChanged();
	}
}

#undef LOCTEXT_NAMESPACE