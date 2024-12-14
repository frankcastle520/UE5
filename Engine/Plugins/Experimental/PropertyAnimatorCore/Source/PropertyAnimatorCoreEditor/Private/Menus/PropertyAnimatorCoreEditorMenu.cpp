// Copyright Epic Games, Inc. All Rights Reserved.

#include "Menus/PropertyAnimatorCoreEditorMenu.h"

#include "Animators/PropertyAnimatorCoreBase.h"
#include "Components/PropertyAnimatorCoreComponent.h"
#include "Engine/World.h"
#include "Misc/Optional.h"
#include "PropertyHandle.h"
#include "Presets/PropertyAnimatorCoreAnimatorPreset.h"
#include "Presets/PropertyAnimatorCorePresetBase.h"
#include "Presets/PropertyAnimatorCorePropertyPreset.h"
#include "Styling/SlateIconFinder.h"
#include "Subsystems/PropertyAnimatorCoreEditorSubsystem.h"
#include "Subsystems/PropertyAnimatorCoreSubsystem.h"

#define LOCTEXT_NAMESPACE "PropertyAnimatorCoreEditorMenu"

void UE::PropertyAnimatorCoreEditor::Menu::FillNewAnimatorSection(UToolMenu* InMenu, TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData)
{
	if (!InMenu || InMenuData->GetContext().IsEmpty())
	{
		return;
	}

	const UPropertyAnimatorCoreSubsystem* Subsystem = UPropertyAnimatorCoreSubsystem::Get();
	if (!Subsystem)
	{
		return;
	}

	TSet<UPropertyAnimatorCoreBase*> NewAvailableAnimators = Subsystem->GetAvailableAnimators();
	for (const FPropertyAnimatorCoreData& Property : InMenuData->GetContext().GetProperties())
	{
		NewAvailableAnimators = NewAvailableAnimators.Intersect(Subsystem->GetAvailableAnimators(&Property));
	}

	constexpr bool bCloseMenuAfterSelection = false;
	constexpr bool bOpenOnClick = false;
	const bool bAdvancedMenu = InMenuData->GetOptions().IsMenuType(EPropertyAnimatorCoreEditorMenuType::NewAdvanced);
	const TSet<AActor*>& ContextActors = InMenuData->GetContext().GetActors();

	FToolMenuSection& NewAnimatorsSection = InMenu->FindOrAddSection(TEXT("NewAnimators"), LOCTEXT("NewAnimators.Label", "New Animators"));

	TMap<UPropertyAnimatorCoreBase*, TArray<UPropertyAnimatorCorePresetBase*>> AvailablePresetAnimators;

	UPropertyAnimatorCorePresetBase* EmptyPreset = nullptr;
	for (UPropertyAnimatorCoreBase* NewAnimator : NewAvailableAnimators)
	{
		AvailablePresetAnimators.FindOrAdd(NewAnimator).Add(EmptyPreset);
	}

	for (UPropertyAnimatorCorePresetBase* Preset : Subsystem->GetAvailablePresets(UPropertyAnimatorCoreAnimatorPreset::StaticClass()))
	{
		if (UPropertyAnimatorCoreAnimatorPreset* AnimatorPreset = Cast<UPropertyAnimatorCoreAnimatorPreset>(Preset))
		{
			if (UPropertyAnimatorCoreBase* AnimatorTemplate = AnimatorPreset->GetAnimatorTemplate())
			{
				AvailablePresetAnimators.FindOrAdd(AnimatorTemplate).Add(AnimatorPreset);
			}
		}
	}

	for (const TPair<UPropertyAnimatorCoreBase*, TArray<UPropertyAnimatorCorePresetBase*>>& NewPresetAnimator : AvailablePresetAnimators)
	{
		UPropertyAnimatorCoreBase* NewAnimator = NewPresetAnimator.Key;

		const FName MenuName = NAME_None;
		FName MenuCategory = NewAnimator->GetAnimatorCategory();
		FText MenuLabel = FText::FromName(NewAnimator->GetAnimatorOriginalName());
		const FText MenuTooltip = LOCTEXT("NewAnimator.Tooltip", "Create a new animator");
		const FSlateIcon MenuIcon = FSlateIconFinder::FindIconForClass(NewAnimator->GetClass());

		if (bAdvancedMenu)
		{
			NewAnimatorsSection.AddSubMenu(
				MenuName
				, MenuLabel
				, MenuTooltip
				, FNewToolMenuDelegate::CreateLambda(&FillNewAnimatorSubmenu, NewAnimator, InMenuData)
				, bOpenOnClick
				, MenuIcon
				, bCloseMenuAfterSelection
			);
		}
		else
		{
			for (UPropertyAnimatorCorePresetBase* Preset : NewPresetAnimator.Value)
			{
				if (Preset)
				{
					MenuLabel = FText::FromString(Preset->GetPresetDisplayName() + TEXT(" (") + NewAnimator->GetAnimatorOriginalName().ToString() + TEXT(")"));
					MenuCategory = TEXT("Presets");
				}

				FToolMenuSection& AnimatorCategorySection = InMenu->FindOrAddSection(MenuCategory, FText::FromName(MenuCategory));

				AnimatorCategorySection.AddMenuEntry(
					MenuName
					, MenuLabel
					, MenuTooltip
					, MenuIcon
					, FExecuteAction::CreateLambda(&ExecuteNewAnimatorPresetAction, NewAnimator, ContextActors, Preset, InMenuData)
				);
			}
		}
	}
}

void UE::PropertyAnimatorCoreEditor::Menu::FillExistingAnimatorSection(UToolMenu* InMenu, TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData)
{
	if (!InMenu || InMenuData->GetContext().IsEmpty())
	{
		return;
	}

	const UPropertyAnimatorCoreSubsystem* Subsystem = UPropertyAnimatorCoreSubsystem::Get();
	if (!Subsystem)
	{
		return;
	}

	FToolMenuSection& ExistingAnimatorsSection = InMenu->FindOrAddSection(TEXT("ExistingAnimators"), LOCTEXT("ExistingAnimators.Label", "Existing Animators"));

	constexpr bool bCloseMenuAfterSelection = true;
	constexpr bool bOpenOnClick = false;

	for (const FPropertyAnimatorCoreData& Property : InMenuData->GetContext().GetProperties())
	{
		for (UPropertyAnimatorCoreBase* Animator : Subsystem->GetExistingAnimators(Property))
		{
			const FName MenuName = Animator->GetAnimatorDisplayName();
			const FText MenuLabel = FText::FromName(MenuName);
			const FSlateIcon MenuIcon = FSlateIconFinder::FindIconForClass(Animator->GetClass());

			ExistingAnimatorsSection.AddSubMenu(
				MenuName
				, MenuLabel
				, LOCTEXT("ExistingAnimatorSection.Tooltip", "Link or unlink properties for this animator")
				, FNewToolMenuDelegate::CreateLambda(&FillLinkAnimatorSubmenu, Animator, InMenuData)
				, bOpenOnClick
				, MenuIcon
				, bCloseMenuAfterSelection
			);
		}
	}
}

void UE::PropertyAnimatorCoreEditor::Menu::FillLinkAnimatorSection(UToolMenu* InMenu, TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData)
{
	if (!InMenu || InMenuData->GetContext().IsEmpty())
	{
		return;
	}

	const UPropertyAnimatorCoreSubsystem* Subsystem = UPropertyAnimatorCoreSubsystem::Get();
	if (!Subsystem)
	{
		return;
	}

	const TSet<UPropertyAnimatorCoreBase*> Animators = InMenuData->GetContext().GetAnimators();

	if (Animators.Num() != 1)
	{
		return;
	}

	UPropertyAnimatorCoreBase* Animator = Animators.Array()[0];

	if (!Animator)
	{
		return;
	}

	FillLinkAnimatorSubmenu(InMenu, Animator, InMenuData);
}

void UE::PropertyAnimatorCoreEditor::Menu::FillDeleteAnimatorSection(UToolMenu* InMenu, TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData)
{
	if (!InMenu || !InMenuData->GetContext().ContainsAnyComponent())
	{
		return;
	}

	const UPropertyAnimatorCoreSubsystem* Subsystem = UPropertyAnimatorCoreSubsystem::Get();
	if (!Subsystem)
	{
		return;
	}

	FToolMenuSection& ActorAnimatorsSection = InMenu->FindOrAddSection(TEXT("ActorAnimators"), LOCTEXT("ActorAnimators.Label", "Actor Animators"));

	ActorAnimatorsSection.AddMenuEntry(
		TEXT("DeleteActorAnimator")
		, LOCTEXT("DeleteActorAnimators.Label", "Delete actor animators")
		, LOCTEXT("DeleteActorAnimators.Tooltip", "Delete selected actor animators")
		, FSlateIcon()
		, FUIAction(
		  FExecuteAction::CreateLambda(&ExecuteDeleteActorAnimatorAction, InMenuData)
		)
	);

	const TSet<UPropertyAnimatorCoreBase*>& Animators = InMenuData->GetContext().GetAnimators();

	if (Animators.IsEmpty())
	{
		return;
	}

	ActorAnimatorsSection.AddSeparator(TEXT("ActorAnimatorSeparator"));

	for (UPropertyAnimatorCoreBase* Animator : Animators)
	{
		if (!IsValid(Animator))
		{
			continue;
		}

		ActorAnimatorsSection.AddMenuEntry(
			Animator->GetAnimatorDisplayName()
			, FText::Format(LOCTEXT("DeleteSingleActorAnimator.Label", "Delete {0}"), FText::FromName(Animator->GetAnimatorDisplayName()))
			, LOCTEXT("DeleteSingleActorAnimator.Tooltip", "Delete selected animator")
			, FSlateIcon()
			, FUIAction(
			  FExecuteAction::CreateLambda(&ExecuteDeleteAnimatorAction, Animator, InMenuData)
			)
		);
	}
}

void UE::PropertyAnimatorCoreEditor::Menu::FillEnableAnimatorSection(UToolMenu* InMenu, TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData)
{
	if (!InMenu
		|| InMenuData->GetContext().IsEmpty()
		|| !InMenuData->GetContext().ContainsAnyDisabledAnimator())
	{
		return;
	}

	const UPropertyAnimatorCoreSubsystem* Subsystem = UPropertyAnimatorCoreSubsystem::Get();
	if (!Subsystem)
	{
		return;
	}

	FToolMenuSection& ActorAnimatorsSection = InMenu->FindOrAddSection(TEXT("ActorAnimators"), LOCTEXT("ActorAnimators.Label", "Actor Animators"));

	constexpr bool bEnable = true;

	ActorAnimatorsSection.AddMenuEntry(
		TEXT("EnableActorAnimator")
		, LOCTEXT("EnableActorAnimator.Label", "Enable actor animators")
		, LOCTEXT("EnableActorAnimator.Tooltip", "Enable selected actor animators")
		, FSlateIcon()
		, FUIAction(
			FExecuteAction::CreateLambda(&ExecuteEnableActorAnimatorAction, InMenuData, bEnable)
		)
	);

	ActorAnimatorsSection.AddMenuEntry(
		TEXT("EnableLevelAnimator")
		, LOCTEXT("EnableLevelAnimator.Label", "Enable level animators")
		, LOCTEXT("EnableLevelAnimator.Tooltip", "Enable current level animators")
		, FSlateIcon()
		, FUIAction(
			FExecuteAction::CreateLambda(&ExecuteEnableLevelAnimatorAction, InMenuData, bEnable)
		)
	);

	const TSet<UPropertyAnimatorCoreBase*> DisabledAnimators = InMenuData->GetContext().GetDisabledAnimators();

	if (DisabledAnimators.IsEmpty())
	{
		return;
	}

	ActorAnimatorsSection.AddSeparator(TEXT("ActorAnimatorSeparator"));

	for (UPropertyAnimatorCoreBase* Animator : DisabledAnimators)
	{
		if (!IsValid(Animator))
		{
			continue;
		}

		ActorAnimatorsSection.AddMenuEntry(
			Animator->GetAnimatorDisplayName()
			, FText::Format(LOCTEXT("EnableAnimator.Label", "Enable {0}"), FText::FromName(Animator->GetAnimatorDisplayName()))
			, LOCTEXT("EnableAnimator.Tooltip", "Enable selected animator")
			, FSlateIconFinder::FindIconForClass(Animator->GetClass())
			, FUIAction(
				FExecuteAction::CreateLambda(&ExecuteEnableAnimatorAction, Animator, bEnable, InMenuData)
			)
		);
	}
}

void UE::PropertyAnimatorCoreEditor::Menu::FillDisableAnimatorSection(UToolMenu* InMenu, TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData)
{
	if (!InMenu
		|| InMenuData->GetContext().IsEmpty()
		|| !InMenuData->GetContext().ContainsAnyEnabledAnimator())
	{
		return;
	}

	const UPropertyAnimatorCoreSubsystem* Subsystem = UPropertyAnimatorCoreSubsystem::Get();
	if (!Subsystem)
	{
		return;
	}

	FToolMenuSection& ActorAnimatorsSection = InMenu->FindOrAddSection(TEXT("ActorAnimators"), LOCTEXT("ActorAnimators.Label", "Actor Animators"));

	constexpr bool bEnable = false;

	ActorAnimatorsSection.AddMenuEntry(
		TEXT("DisableActorAnimator")
		, LOCTEXT("DisableActorAnimator.Label", "Disable actor animators")
		, LOCTEXT("DisableActorAnimator.Tooltip", "Disable selected actor animators")
		, FSlateIcon()
		, FUIAction(
			FExecuteAction::CreateLambda(&ExecuteEnableActorAnimatorAction, InMenuData, bEnable)
		)
	);

	ActorAnimatorsSection.AddMenuEntry(
		TEXT("DisableLevelAnimator")
		, LOCTEXT("DisableLevelAnimator.Label", "Disable level animators")
		, LOCTEXT("DisableLevelAnimator.Tooltip", "Disable current level animators")
		, FSlateIcon()
		, FUIAction(
			FExecuteAction::CreateLambda(&ExecuteEnableLevelAnimatorAction, InMenuData, bEnable)
		)
	);

	const TSet<UPropertyAnimatorCoreBase*> EnabledAnimators = InMenuData->GetContext().GetEnabledAnimators();

	if (EnabledAnimators.IsEmpty())
	{
		return;
	}

	ActorAnimatorsSection.AddSeparator(TEXT("ActorAnimatorSeparator"));

	for (UPropertyAnimatorCoreBase* Animator : EnabledAnimators)
	{
		if (!IsValid(Animator))
		{
			continue;
		}

		ActorAnimatorsSection.AddMenuEntry(
			Animator->GetAnimatorDisplayName()
			, FText::Format(LOCTEXT("DisableAnimator.Label", "Disable {0}"), FText::FromName(Animator->GetAnimatorDisplayName()))
			, LOCTEXT("DisableAnimator.Tooltip", "Disable selected animator")
			, FSlateIconFinder::FindIconForClass(Animator->GetClass())
			, FUIAction(
				FExecuteAction::CreateLambda(&ExecuteEnableAnimatorAction, Animator, bEnable, InMenuData)
			)
		);
	}
}

void UE::PropertyAnimatorCoreEditor::Menu::ExecuteNewAnimatorPresetAction(const UPropertyAnimatorCoreBase* InAnimator, const TSet<AActor*>& InActors, UPropertyAnimatorCorePresetBase* InPreset, TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData)
{
	const UPropertyAnimatorCoreSubsystem* Subsystem = UPropertyAnimatorCoreSubsystem::Get();

	if (!Subsystem
		|| !IsValid(InAnimator)
		|| !InAnimator->IsTemplate()
		|| InMenuData->GetContext().IsEmpty())
	{
		return;
	}

	InMenuData->SetLastCreatedAnimators(Subsystem->CreateAnimators(InActors, InAnimator->GetClass(), InPreset, InMenuData->GetOptions().ShouldTransact()));
}

void UE::PropertyAnimatorCoreEditor::Menu::ExecuteNewAnimatorPropertyAction(const UPropertyAnimatorCoreBase* InAnimator, FPropertyAnimatorCoreData InProperty, TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData)
{
	UPropertyAnimatorCoreSubsystem* Subsystem = UPropertyAnimatorCoreSubsystem::Get();

	if (!Subsystem
		|| !IsValid(InAnimator)
		|| !InAnimator->IsTemplate()
		|| InMenuData->GetContext().IsEmpty())
	{
		return;
	}

	AActor* OwningActor = InProperty.GetOwningActor();

	UPropertyAnimatorCoreBase* NewAnimator = Subsystem->CreateAnimator(OwningActor, InAnimator->GetClass(), nullptr, InMenuData->GetOptions().ShouldTransact());
	Subsystem->LinkAnimatorProperty(NewAnimator, InProperty, true);
	InMenuData->SetLastCreatedAnimator(NewAnimator);
}

void UE::PropertyAnimatorCoreEditor::Menu::ExecuteLinkLastCreatedAnimatorPropertyAction(const UPropertyAnimatorCoreBase* InAnimator, FPropertyAnimatorCoreData InProperty, TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData)
{
	UPropertyAnimatorCoreSubsystem* Subsystem = UPropertyAnimatorCoreSubsystem::Get();

	if (!Subsystem
		|| !IsValid(InAnimator)
		|| !InAnimator->IsTemplate())
	{
		return;
	}

	for (UPropertyAnimatorCoreBase* LastCreatedAnimator : InMenuData->GetLastCreatedAnimators())
	{
		if (LastCreatedAnimator->GetClass() != InAnimator->GetClass())
		{
			continue;
		}

		if (LastCreatedAnimator->IsPropertyLinked(InProperty))
		{
			Subsystem->UnlinkAnimatorProperty(LastCreatedAnimator, InProperty, true);
		}
		else
		{
			Subsystem->LinkAnimatorProperty(LastCreatedAnimator, InProperty, true);
		}
	}
}

void UE::PropertyAnimatorCoreEditor::Menu::ExecuteApplyLastCreatedAnimatorPresetAction(const UPropertyAnimatorCoreBase* InAnimator, UPropertyAnimatorCorePropertyPreset* InPreset, TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData)
{
	UPropertyAnimatorCoreSubsystem* Subsystem = UPropertyAnimatorCoreSubsystem::Get();

	if (!Subsystem
		|| !IsValid(InAnimator)
		|| !InAnimator->IsTemplate())
	{
		return;
	}

	for (UPropertyAnimatorCoreBase* LastCreatedAnimator : InMenuData->GetLastCreatedAnimators())
	{
		if (LastCreatedAnimator->GetClass() != InAnimator->GetClass())
		{
			continue;
		}

		if (InPreset->IsPresetApplied(LastCreatedAnimator))
		{
			Subsystem->UnapplyAnimatorPreset(LastCreatedAnimator, InPreset, true);
		}
		else
		{
			Subsystem->ApplyAnimatorPreset(LastCreatedAnimator, InPreset, true);
		}
	}
}

void UE::PropertyAnimatorCoreEditor::Menu::ExecuteLinkAnimatorPresetAction(UPropertyAnimatorCoreBase* InAnimator, UPropertyAnimatorCorePropertyPreset* InPreset, TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData)
{
	UPropertyAnimatorCoreSubsystem* Subsystem = UPropertyAnimatorCoreSubsystem::Get();

	if (!Subsystem
		|| !InPreset
		|| !IsValid(InAnimator)
		|| InAnimator->IsTemplate()
		|| InMenuData->GetContext().IsEmpty())
	{
		return;
	}

	if (InPreset->IsPresetApplied(InAnimator))
	{
		Subsystem->UnapplyAnimatorPreset(InAnimator, InPreset, InMenuData->GetOptions().ShouldTransact());
	}
	else
	{
		Subsystem->ApplyAnimatorPreset(InAnimator, InPreset, InMenuData->GetOptions().ShouldTransact());
	}
}

void UE::PropertyAnimatorCoreEditor::Menu::ExecuteLinkAnimatorPropertyAction(UPropertyAnimatorCoreBase* InAnimator, FPropertyAnimatorCoreData InProperty, UPropertyAnimatorCorePropertyPreset* InPreset, TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData)
{
	UPropertyAnimatorCoreSubsystem* Subsystem = UPropertyAnimatorCoreSubsystem::Get();

	if (!Subsystem
		|| !IsValid(InAnimator)
		|| InAnimator->IsTemplate()
		|| !InProperty.IsResolved()
		|| !InAnimator->HasPropertySupport(InProperty)
		|| InMenuData->GetContext().IsEmpty())
	{
		return;
	}

	if (InAnimator->IsPropertyLinked(InProperty))
	{
		Subsystem->UnlinkAnimatorProperty(InAnimator, InProperty, InMenuData->GetOptions().ShouldTransact());

		if (InPreset)
		{
			InPreset->OnPresetUnapplied(InAnimator, {InProperty});
		}
	}
	else
	{
		Subsystem->LinkAnimatorProperty(InAnimator, InProperty, InMenuData->GetOptions().ShouldTransact());

		if (InPreset)
		{
			InPreset->OnPresetApplied(InAnimator, {InProperty});
		}
	}
}

void UE::PropertyAnimatorCoreEditor::Menu::ExecuteEnableActorAnimatorAction(TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData, bool bInEnable)
{
	UPropertyAnimatorCoreSubsystem* Subsystem = UPropertyAnimatorCoreSubsystem::Get();

	if (InMenuData->GetContext().IsEmpty() || !Subsystem)
	{
		return;
	}

	Subsystem->SetActorAnimatorsEnabled(InMenuData->GetContext().GetActors(), bInEnable, InMenuData->GetOptions().ShouldTransact());
}

void UE::PropertyAnimatorCoreEditor::Menu::ExecuteEnableLevelAnimatorAction(TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData, bool bInEnable)
{
	UPropertyAnimatorCoreSubsystem* Subsystem = UPropertyAnimatorCoreSubsystem::Get();
	const UWorld* World = InMenuData->GetContext().GetWorld();

	if (!IsValid(World) || !Subsystem)
	{
		return;
	}

	Subsystem->SetLevelAnimatorsEnabled(World, bInEnable, InMenuData->GetOptions().ShouldTransact());
}

void UE::PropertyAnimatorCoreEditor::Menu::ExecuteEnableAnimatorAction(UPropertyAnimatorCoreBase* InAnimator, bool bInEnable, TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData)
{
	UPropertyAnimatorCoreSubsystem* Subsystem = UPropertyAnimatorCoreSubsystem::Get();

	if (!IsValid(InAnimator) || !Subsystem)
	{
		return;
	}

	Subsystem->SetAnimatorsEnabled({InAnimator}, bInEnable, InMenuData->GetOptions().ShouldTransact());
}

void UE::PropertyAnimatorCoreEditor::Menu::ExecuteDeleteActorAnimatorAction(TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData)
{
	const UPropertyAnimatorCoreSubsystem* Subsystem = UPropertyAnimatorCoreSubsystem::Get();

	if (!Subsystem || InMenuData->GetContext().IsEmpty())
	{
		return;
	}

	TSet<UPropertyAnimatorCoreBase*> Animators;

	for (const UPropertyAnimatorCoreComponent* Component : InMenuData->GetContext().GetComponents())
	{
		if (!IsValid(Component))
		{
			continue;
		}

		for (const TObjectPtr<UPropertyAnimatorCoreBase>& Animator : Component->GetAnimators())
		{
			if (Animator)
			{
				Animators.Add(Animator);
			}
		}
	}

	Subsystem->RemoveAnimators(Animators, InMenuData->GetOptions().ShouldTransact());
}

void UE::PropertyAnimatorCoreEditor::Menu::ExecuteDeleteAnimatorAction(UPropertyAnimatorCoreBase* InAnimator, TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData)
{
	const UPropertyAnimatorCoreSubsystem* Subsystem = UPropertyAnimatorCoreSubsystem::Get();

	if (!IsValid(InAnimator) || !Subsystem)
	{
		return;
	}

	Subsystem->RemoveAnimator(InAnimator, InMenuData->GetOptions().ShouldTransact());
}

ECheckBoxState UE::PropertyAnimatorCoreEditor::Menu::GetAnimatorPresetState(const UPropertyAnimatorCoreBase* InAnimator, UPropertyAnimatorCorePropertyPreset* InPreset)
{
	if (!IsValid(InAnimator) || InAnimator->IsTemplate() || !InPreset)
	{
		return ECheckBoxState::Unchecked;
	}

	TSet<FPropertyAnimatorCoreData> SupportedProperties;
	TSet<FPropertyAnimatorCoreData> AppliedProperties;
	InPreset->GetAppliedPresetProperties(InAnimator, SupportedProperties, AppliedProperties);

	if (!SupportedProperties.IsEmpty() && SupportedProperties.Num() == AppliedProperties.Num())
	{
		return ECheckBoxState::Checked;
	}

	return !AppliedProperties.IsEmpty() ? ECheckBoxState::Undetermined : ECheckBoxState::Unchecked;
}

ECheckBoxState UE::PropertyAnimatorCoreEditor::Menu::GetLastAnimatorCreatedPresetState(const UPropertyAnimatorCoreBase* InAnimator, UPropertyAnimatorCorePropertyPreset* InPreset, TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData)
{
	if (!IsValid(InAnimator)
		|| !InAnimator->IsTemplate()
		|| !InPreset
		|| !InMenuData->ContainsAnyLastCreatedAnimator())
	{
		return ECheckBoxState::Unchecked;
	}

	TOptional<ECheckBoxState> AnimatorsState;

	for (const UPropertyAnimatorCoreBase* LastCreatedAnimator : InMenuData->GetLastCreatedAnimators())
	{
		if (LastCreatedAnimator->GetClass() == InAnimator->GetClass())
		{
			if (!AnimatorsState.IsSet())
			{
				AnimatorsState = GetAnimatorPresetState(LastCreatedAnimator, InPreset);
			}
			else if (GetAnimatorPresetState(LastCreatedAnimator, InPreset) != AnimatorsState.GetValue())
			{
				AnimatorsState = ECheckBoxState::Undetermined;
				break;
			}
		}
	}

	return AnimatorsState.Get(ECheckBoxState::Unchecked);
}

bool UE::PropertyAnimatorCoreEditor::Menu::IsAnimatorPresetLinked(UPropertyAnimatorCoreBase* InAnimator, UPropertyAnimatorCorePropertyPreset* InPreset)
{
	if (!IsValid(InAnimator) || InAnimator->IsTemplate() || !InPreset)
	{
		return false;
	}

	return InPreset->IsPresetApplied(InAnimator);
}

void UE::PropertyAnimatorCoreEditor::Menu::FillNewAnimatorSubmenu(UToolMenu* InMenu, UPropertyAnimatorCoreBase* InAnimator, TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData)
{
	const UPropertyAnimatorCoreSubsystem* Subsystem = UPropertyAnimatorCoreSubsystem::Get();

	if (!InMenu
		|| !Subsystem
		|| !InAnimator
		|| !InAnimator->IsTemplate())
	{
		return;
	}

	if (InMenuData->GetContext().ContainsAnyActor())
	{
		const TSet<AActor*>& ContextActors = InMenuData->GetContext().GetActors();

		FToolMenuSection& PresetSection = InMenu->FindOrAddSection(TEXT("Presets"), LOCTEXT("NewAnimatorPresetsSection.Label", "Presets"));

		UPropertyAnimatorCorePropertyPreset* EmptyPreset = nullptr;

		PresetSection.AddMenuEntry(
			TEXT("EmptyPreset")
			, LOCTEXT("NewAnimatorEmptyPresetSection.Label", "Empty")
			, LOCTEXT("NewAnimatorEmptyPresetSection.Tooltip", "Create an empty animator")
			, FSlateIcon()
			, FUIAction(
				FExecuteAction::CreateLambda(&ExecuteNewAnimatorPresetAction, InAnimator, ContextActors, EmptyPreset, InMenuData)
				, FCanExecuteAction()
				, FIsActionChecked()
				, FIsActionButtonVisible::CreateLambda(&IsLastAnimatorCreatedActionHidden, InAnimator, InMenuData)
			)
		);

		TSet<UPropertyAnimatorCorePresetBase*> SupportedPresets = Subsystem->GetAvailablePresets(UPropertyAnimatorCorePropertyPreset::StaticClass());

		for (const AActor* Actor : ContextActors)
		{
			SupportedPresets = SupportedPresets.Intersect(Subsystem->GetSupportedPresets(Actor, InAnimator, UPropertyAnimatorCorePropertyPreset::StaticClass()));
		}

		constexpr bool bCloseMenuAfterSelection = false;
		constexpr bool bOpenOnClick = false;

		for (UPropertyAnimatorCorePresetBase* SupportedPreset : SupportedPresets)
		{
			if (!SupportedPreset)
			{
				continue;
			}

			const FString MenuName = SupportedPreset->GetPresetName().ToString();
			const FText MenuLabel = FText::FromString(SupportedPreset->GetPresetDisplayName());

			PresetSection.AddSubMenu(
				FName(TEXT("Create") + MenuName)
				, MenuLabel
				, LOCTEXT("NewAnimatorPresetSection.Tooltip", "Create this animator using this preset")
				, FNewToolMenuDelegate::CreateLambda(&FillNewPresetAnimatorSubmenu, InAnimator, Cast<UPropertyAnimatorCorePropertyPreset>(SupportedPreset), InMenuData)
				, bOpenOnClick
				, FSlateIcon()
				, bCloseMenuAfterSelection
			);
		}
	}

	if (InMenuData->GetContext().ContainsAnyProperty())
	{
		FToolMenuSection& PropertySection = InMenu->FindOrAddSection(TEXT("Properties"), LOCTEXT("NewAnimatorPropertiesSection.Label", "Properties"));

		TSet<FPropertyAnimatorCoreData> SupportedProperties;
		for (const FPropertyAnimatorCoreData& Property : InMenuData->GetContext().GetProperties())
		{
			InAnimator->GetPropertiesSupported(Property, SupportedProperties, /** SearchDepth */3);
		}

		for (const FPropertyAnimatorCoreData& SupportedProperty : SupportedProperties)
		{
			const FString MenuName = SupportedProperty.GetPropertyDisplayName();
			const FText MenuLabel = FText::FromString(MenuName + TEXT(" (") + SupportedProperty.GetLeafPropertyTypeName().ToString() + TEXT(")"));

			// Create action (creates an animator and links the property)
			PropertySection.AddMenuEntry(
				FName(TEXT("Create") + MenuName)
				, MenuLabel
				, LOCTEXT("NewAnimatorPropertySection.Tooltip", "Create this animator using this property")
				, FSlateIcon()
				, FUIAction(
					FExecuteAction::CreateLambda(&ExecuteNewAnimatorPropertyAction, InAnimator, SupportedProperty, InMenuData)
					, FCanExecuteAction()
					, FIsActionChecked()
					, FIsActionButtonVisible::CreateLambda(&IsLastAnimatorCreatedActionHidden, InAnimator, InMenuData)
				)
			);

			// Link action (links the property to last created animator)
			PropertySection.AddMenuEntry(
				FName(TEXT("Link") + MenuName)
				, MenuLabel
				, LOCTEXT("LinkLastCreatedAnimatorPropertySection.Tooltip", "Link this property to the last created animator")
				, FSlateIcon()
				, FUIAction(
					FExecuteAction::CreateLambda(&ExecuteLinkLastCreatedAnimatorPropertyAction, InAnimator, SupportedProperty, InMenuData)
					, FCanExecuteAction()
					, FIsActionChecked::CreateLambda(&IsLastAnimatorCreatedPropertyLinked, InAnimator, SupportedProperty, InMenuData)
					, FIsActionButtonVisible::CreateLambda(&IsLastAnimatorCreatedActionVisible, InAnimator, InMenuData)
				)
				, EUserInterfaceActionType::ToggleButton
			);
		}
	}
}

void UE::PropertyAnimatorCoreEditor::Menu::FillLinkAnimatorSubmenu(UToolMenu* InMenu, UPropertyAnimatorCoreBase* InAnimator, TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData)
{
	if (!InMenu
		|| !IsValid(InAnimator)
		|| InAnimator->IsTemplate()
		|| InMenuData->GetContext().IsEmpty())
	{
		return;
	}

	const UPropertyAnimatorCoreSubsystem* Subsystem = UPropertyAnimatorCoreSubsystem::Get();
	if (!Subsystem)
	{
		return;
	}

	FToolMenuSection& PresetSection = InMenu->FindOrAddSection(TEXT("Presets"), LOCTEXT("LinkAnimatorPresetsSection.Label", "Presets"));

	TSet<UPropertyAnimatorCorePresetBase*> SupportedPresets = Subsystem->GetAvailablePresets(UPropertyAnimatorCorePropertyPreset::StaticClass());

	for (const AActor* Actor : InMenuData->GetContext().GetActors())
	{
		SupportedPresets = SupportedPresets.Intersect(Subsystem->GetSupportedPresets(Actor, InAnimator, UPropertyAnimatorCorePropertyPreset::StaticClass()));
	}

	constexpr bool bCloseMenuAfterSelection = false;
    constexpr bool bOpenOnClick = false;

	for (UPropertyAnimatorCorePresetBase* SupportedPreset : SupportedPresets)
	{
		if (!SupportedPreset)
		{
			continue;
		}

		const FName MenuName = SupportedPreset->GetPresetName();
		const FText MenuLabel = FText::FromString(SupportedPreset->GetPresetDisplayName());
		const FText MenuTooltip = LOCTEXT("LinkAnimatorPresetSection.Tooltip", "Link or unlink a preset from this animator");

		PresetSection.AddSubMenu(
			MenuName
			, MenuLabel
			, MenuTooltip
			, FNewToolMenuDelegate::CreateLambda(&FillPresetAnimatorSubmenu, InAnimator, Cast<UPropertyAnimatorCorePropertyPreset>(SupportedPreset), InMenuData)
			, bOpenOnClick
			, FSlateIcon()
			, bCloseMenuAfterSelection
		);
	}

	FToolMenuSection& PropertySection = InMenu->FindOrAddSection(TEXT("Properties"), LOCTEXT("LinkAnimatorPropertiesSection.Label", "Properties"));

	TSet<FPropertyAnimatorCoreData> SupportedProperties;
	for (const FPropertyAnimatorCoreData& Property : InMenuData->GetContext().GetProperties())
	{
		InAnimator->GetPropertiesSupported(Property, SupportedProperties, /** SearchDepth */3);
	}

	UPropertyAnimatorCorePropertyPreset* Empty = nullptr;
	for (const FPropertyAnimatorCoreData& SupportedProperty : SupportedProperties)
	{
		const FString MenuName = SupportedProperty.GetPropertyDisplayName();
		const FText MenuLabel = FText::FromString(MenuName + TEXT(" (") + SupportedProperty.GetLeafPropertyTypeName().ToString() + TEXT(")"));

		PropertySection.AddMenuEntry(
			FName(MenuName)
			, MenuLabel
			, LOCTEXT("LinkAnimatorPropertySection.Tooltip", "Link or unlink this property from the animator")
			, FSlateIcon()
			, FUIAction(
				FExecuteAction::CreateLambda(&ExecuteLinkAnimatorPropertyAction, InAnimator, SupportedProperty, Empty, InMenuData)
				, FCanExecuteAction::CreateLambda(&IsAnimatorLinkPropertyAllowed, InAnimator, SupportedProperty)
				, FIsActionChecked::CreateLambda(&IsAnimatorPropertyLinked, InAnimator, SupportedProperty)
			)
			, EUserInterfaceActionType::ToggleButton
		);
	}
}

void UE::PropertyAnimatorCoreEditor::Menu::FillPresetAnimatorSubmenu(UToolMenu* InMenu, UPropertyAnimatorCoreBase* InAnimator, UPropertyAnimatorCorePropertyPreset* InPreset, TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData)
{
	if (!InMenu)
	{
		return;
	}

	const FToolMenuEntry AllPropertiesEntry = FToolMenuEntry::InitMenuEntry(
		TEXT("All")
		, LOCTEXT("LinkAllPresetProperty.Label", "All")
		, LOCTEXT("LinkAllPresetProperty.Tooltip", "Link all properties from this preset")
		, FSlateIcon()
		, FUIAction(
			FExecuteAction::CreateLambda(&ExecuteLinkAnimatorPresetAction, InAnimator, InPreset, InMenuData)
			, FCanExecuteAction()
			, FGetActionCheckState::CreateLambda(&GetAnimatorPresetState, InAnimator, InPreset)
		)
		, EUserInterfaceActionType::ToggleButton
	);

	// Add all preset properties option
	InMenu->AddMenuEntry(
		AllPropertiesEntry.Name,
		AllPropertiesEntry
	);

	const FToolMenuEntry SeparatorEntry = FToolMenuEntry::InitSeparator(TEXT("PresetSeparator"));
	InMenu->AddMenuEntry(SeparatorEntry.Name, SeparatorEntry);

	TSet<FPropertyAnimatorCoreData> SupportedProperties;
	InPreset->GetSupportedPresetProperties(InAnimator->GetAnimatorActor(), InAnimator, SupportedProperties);

	for (const FPropertyAnimatorCoreData& SupportedProperty : SupportedProperties)
	{
		const FString MenuName = SupportedProperty.GetPropertyDisplayName();
		const FText MenuLabel = FText::FromString(MenuName + TEXT(" (") + SupportedProperty.GetLeafPropertyTypeName().ToString() + TEXT(")"));
		const FText MenuTooltip = LOCTEXT("LinkPresetProperty.Tooltip", "Link this preset property");

		const FToolMenuEntry SupportedPropertyEntry = FToolMenuEntry::InitMenuEntry(
			FName(MenuName)
			, MenuLabel
			, MenuTooltip
			, FSlateIcon()
			, FUIAction(
				FExecuteAction::CreateLambda(&ExecuteLinkAnimatorPropertyAction, InAnimator, SupportedProperty, InPreset, InMenuData)
				, FCanExecuteAction::CreateLambda(&IsAnimatorLinkPropertyAllowed, InAnimator, SupportedProperty)
				, FIsActionChecked::CreateLambda(&IsAnimatorPropertyLinked, InAnimator, SupportedProperty)
			)
			, EUserInterfaceActionType::ToggleButton
		);

		InMenu->AddMenuEntry(SupportedPropertyEntry.Name, SupportedPropertyEntry);
	}
}

void UE::PropertyAnimatorCoreEditor::Menu::FillNewPresetAnimatorSubmenu(UToolMenu* InMenu, UPropertyAnimatorCoreBase* InAnimator, UPropertyAnimatorCorePropertyPreset* InPreset, TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData)
{
	if (!InMenu)
	{
		return;
	}

	const TSet<AActor*>& ContextActors = InMenuData->GetContext().GetActors();

	int32 Index = 0;
	TSet<FPropertyAnimatorCoreData> PresetProperties;
	for (const AActor* ContextActor : ContextActors)
	{
		TSet<FPropertyAnimatorCoreData> SupportedProperties;
		InPreset->GetSupportedPresetProperties(ContextActor, InAnimator, SupportedProperties);

		if (Index++ == 0)
		{
			PresetProperties = SupportedProperties;
		}
		else
		{
			PresetProperties = PresetProperties.Intersect(SupportedProperties);
		}
	}

	const FToolMenuEntry CreateAllPropertiesEntry = FToolMenuEntry::InitMenuEntry(
		TEXT("CreateAllProperties")
		, LOCTEXT("NewAnimatorPresetSection.Label", "All")
		, LOCTEXT("NewAnimatorPresetSection.Tooltip", "Create this animator using this preset")
		, FSlateIcon()
		, FUIAction(
			FExecuteAction::CreateLambda(&ExecuteNewAnimatorPresetAction, InAnimator, ContextActors, InPreset, InMenuData)
			, FCanExecuteAction()
			, FIsActionChecked()
			, FIsActionButtonVisible::CreateLambda(&IsLastAnimatorCreatedActionHidden, InAnimator, InMenuData)
		)
	);

	// Create action (creates an animator and links the property)
	InMenu->AddMenuEntry(
		CreateAllPropertiesEntry.Name
		, CreateAllPropertiesEntry
	);

	const FToolMenuEntry LinkAllPropertiesEntry = FToolMenuEntry::InitMenuEntry(
		TEXT("LinkAllProperties")
		, LOCTEXT("ApplyLastCreatedAnimatorPresetSection.Label", "All")
		, LOCTEXT("ApplyLastCreatedAnimatorPresetSection.Tooltip", "Apply this preset to the last created animator")
		, FSlateIcon()
		, FUIAction(
			FExecuteAction::CreateLambda(&ExecuteApplyLastCreatedAnimatorPresetAction, InAnimator, InPreset, InMenuData)
			, FCanExecuteAction()
			, FGetActionCheckState::CreateLambda(&GetLastAnimatorCreatedPresetState, InAnimator, InPreset, InMenuData)
			, FIsActionButtonVisible::CreateLambda(&IsLastAnimatorCreatedActionVisible, InAnimator, InMenuData)
		)
		, EUserInterfaceActionType::ToggleButton
	);

	// Link action (links the property to last created animator)
	InMenu->AddMenuEntry(
		LinkAllPropertiesEntry.Name
		, LinkAllPropertiesEntry
	);

	const FToolMenuEntry SeparatorEntry = FToolMenuEntry::InitSeparator(TEXT("PresetSeparator"));
	InMenu->AddMenuEntry(SeparatorEntry.Name, SeparatorEntry);

	for (const FPropertyAnimatorCoreData& PresetProperty : PresetProperties)
	{
		const FString MenuName = PresetProperty.GetPropertyDisplayName();
		const FText MenuLabel = FText::FromString(MenuName + TEXT(" (") + PresetProperty.GetLeafPropertyTypeName().ToString() + TEXT(")"));

		const FToolMenuEntry CreatePropertyEntry = FToolMenuEntry::InitMenuEntry(
			FName(TEXT("CreateProperty") + MenuName)
			, MenuLabel
			, LOCTEXT("CreateAnimatorPresetProperty.Tooltip", "Create this animator using this preset property")
			, FSlateIcon()
			, FUIAction(
				FExecuteAction::CreateLambda(&ExecuteNewAnimatorPropertyAction, InAnimator, PresetProperty, InMenuData)
				, FCanExecuteAction()
				, FIsActionChecked()
				, FIsActionButtonVisible::CreateLambda(&IsLastAnimatorCreatedActionHidden, InAnimator, InMenuData)
			)
		);

		// Create action (creates an animator and links the property)
		InMenu->AddMenuEntry(
			CreatePropertyEntry.Name
			, CreatePropertyEntry
		);

		const FToolMenuEntry LinkPropertyEntry = FToolMenuEntry::InitMenuEntry(
			FName(TEXT("LinkProperty") + MenuName)
			, MenuLabel
			, LOCTEXT("LinkAnimatorPresetProperty.Tooltip", "Link this preset property to the last created animator")
			, FSlateIcon()
			, FUIAction(
				FExecuteAction::CreateLambda(&ExecuteLinkLastCreatedAnimatorPropertyAction, InAnimator, PresetProperty, InMenuData)
				, FCanExecuteAction()
				, FIsActionChecked::CreateLambda(&IsLastAnimatorCreatedPropertyLinked, InAnimator, PresetProperty, InMenuData)
				, FIsActionButtonVisible::CreateLambda(&IsLastAnimatorCreatedActionVisible, InAnimator, InMenuData)
			)
			, EUserInterfaceActionType::ToggleButton
		);

		// Link action (links the property to last created animator)
		InMenu->AddMenuEntry(
			LinkPropertyEntry.Name
			, LinkPropertyEntry
		);
	}
}

bool UE::PropertyAnimatorCoreEditor::Menu::IsAnimatorPropertyLinked(const UPropertyAnimatorCoreBase* InAnimator, const FPropertyAnimatorCoreData& InProperty)
{
	if (!IsValid(InAnimator) || InAnimator->IsTemplate())
	{
		return false;
	}

	return InAnimator->IsPropertyLinked(InProperty);
}

bool UE::PropertyAnimatorCoreEditor::Menu::IsAnimatorLinkPropertyAllowed(const UPropertyAnimatorCoreBase* InAnimator, const FPropertyAnimatorCoreData& InProperty)
{
	if (!IsValid(InAnimator) || InAnimator->IsTemplate())
	{
		return false;
	}

	// Only allow linking properties that are not yet linked and do not have any of their children linked
	return (!InAnimator->IsPropertyLinked(InProperty) && InAnimator->GetInnerPropertiesLinked(InProperty).IsEmpty())
		|| InAnimator->GetLinkedPropertyContext(InProperty) != nullptr;
}

bool UE::PropertyAnimatorCoreEditor::Menu::IsLastAnimatorCreatedPropertyLinked(const UPropertyAnimatorCoreBase* InAnimator, const FPropertyAnimatorCoreData& InProperty, TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData)
{
	if (!IsValid(InAnimator)
		|| !InAnimator->IsTemplate()
		|| !InMenuData->ContainsAnyLastCreatedAnimator())
	{
		return false;
	}

	for (const UPropertyAnimatorCoreBase* LastCreatedAnimator : InMenuData->GetLastCreatedAnimators())
	{
		if (LastCreatedAnimator->GetClass() != InAnimator->GetClass()
			|| !LastCreatedAnimator->IsPropertyLinked(InProperty))
		{
			return false;
		}
	}

	return true;
}

bool UE::PropertyAnimatorCoreEditor::Menu::IsLastAnimatorCreatedActionVisible(const UPropertyAnimatorCoreBase* InAnimator, TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData)
{
	if (!IsValid(InAnimator)
		|| !InAnimator->IsTemplate()
		|| !InMenuData->ContainsAnyLastCreatedAnimator())
	{
		return false;
	}

	for (const UPropertyAnimatorCoreBase* LastCreatedAnimator : InMenuData->GetLastCreatedAnimators())
	{
		if (LastCreatedAnimator->GetClass() != InAnimator->GetClass())
		{
			return false;
		}
	}

	return true;
}

bool UE::PropertyAnimatorCoreEditor::Menu::IsLastAnimatorCreatedActionHidden(const UPropertyAnimatorCoreBase* InAnimator, TSharedRef<FPropertyAnimatorCoreEditorMenuData> InMenuData)
{
	return !IsLastAnimatorCreatedActionVisible(InAnimator, InMenuData);
}

#undef LOCTEXT_NAMESPACE
