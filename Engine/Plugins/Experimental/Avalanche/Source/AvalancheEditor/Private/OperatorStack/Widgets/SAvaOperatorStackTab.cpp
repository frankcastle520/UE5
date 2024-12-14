// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAvaOperatorStackTab.h"

#include "Animators/PropertyAnimatorCoreBase.h"
#include "Components/PropertyAnimatorCoreComponent.h"
#include "Contexts/OperatorStackEditorContext.h"
#include "DetailView/IAvaDetailsProvider.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "Items/OperatorStackEditorItem.h"
#include "Items/OperatorStackEditorObjectItem.h"
#include "Modifiers/ActorModifierCoreStack.h"
#include "Selection.h"
#include "Selection/AvaEditorSelection.h"
#include "Subsystems/OperatorStackEditorSubsystem.h"
#include "Widgets/SOperatorStackEditorWidget.h"

#define LOCTEXT_NAMESPACE "SAvaModifierStackTab"

void SAvaOperatorStackTab::Construct(const FArguments& InArgs
	, const TSharedPtr<IAvaDetailsProvider>& InProvider)
{
	DetailsProviderWeak = InProvider;

	UOperatorStackEditorSubsystem* OperatorStackSubsystem = UOperatorStackEditorSubsystem::Get();

	check(InProvider.IsValid() && OperatorStackSubsystem);

	USelection::SelectionChangedEvent.AddSP(this, &SAvaOperatorStackTab::RefreshSelection);

	// Modifiers delegates
	UActorModifierCoreStack::OnModifierAdded().AddSP(this, &SAvaOperatorStackTab::OnModifierUpdated);
	UActorModifierCoreStack::OnModifierMoved().AddSP(this, &SAvaOperatorStackTab::OnModifierUpdated);
	UActorModifierCoreStack::OnModifierRemoved().AddSP(this, &SAvaOperatorStackTab::OnModifierUpdated);
	UActorModifierCoreStack::OnModifierReplaced().AddSP(this, &SAvaOperatorStackTab::OnModifierUpdated);

	// Property controllers delegates
	UPropertyAnimatorCoreBase::OnPropertyAnimatorAdded().AddSP(this, &SAvaOperatorStackTab::OnAnimatorUpdated);
	UPropertyAnimatorCoreBase::OnPropertyAnimatorRemoved().AddSP(this, &SAvaOperatorStackTab::OnAnimatorRemoved);
	UPropertyAnimatorCoreBase::OnPropertyAnimatorRenamed().AddSP(this, &SAvaOperatorStackTab::OnAnimatorUpdated);

	const TSharedPtr<IDetailKeyframeHandler> KeyframeHandler = InProvider->GetDetailsKeyframeHandler();

	OperatorStack = OperatorStackSubsystem->GenerateWidget();
	OperatorStack->SetKeyframeHandler(KeyframeHandler);
	OperatorStack->SetPanelTag(SAvaOperatorStackTab::PanelTag);

	ChildSlot
	[
		OperatorStack.ToSharedRef()
	];

	RefreshSelection(nullptr);
}

SAvaOperatorStackTab::~SAvaOperatorStackTab()
{
	USelection::SelectionChangedEvent.RemoveAll(this);

	UActorModifierCoreStack::OnModifierAdded().RemoveAll(this);
	UActorModifierCoreStack::OnModifierMoved().RemoveAll(this);
	UActorModifierCoreStack::OnModifierRemoved().RemoveAll(this);
	UActorModifierCoreStack::OnModifierReplaced().RemoveAll(this);

	UPropertyAnimatorCoreBase::OnPropertyAnimatorAdded().RemoveAll(this);
	UPropertyAnimatorCoreBase::OnPropertyAnimatorRemoved().RemoveAll(this);
	UPropertyAnimatorCoreBase::OnPropertyAnimatorRenamed().RemoveAll(this);
}

void SAvaOperatorStackTab::RefreshSelection(UObject* InSelectionObject) const
{
	const TSharedPtr<IAvaDetailsProvider> DetailsProvider = DetailsProviderWeak.Pin();
	if (!DetailsProvider.IsValid())
	{
		return;
	}

	FEditorModeTools* ModeTools = DetailsProvider->GetDetailsModeTools();
	if (!ModeTools)
	{
		return;
	}

	const FAvaEditorSelection EditorSelection(*ModeTools, InSelectionObject);
	if (!EditorSelection.IsValid())
	{
		return;
	}

	const TArray<UObject*> SelectedObjects = EditorSelection.GetSelectedObjects<UObject, EAvaSelectionSource::All>();

	TArray<FOperatorStackEditorItemPtr> SelectedItems;
	Algo::Transform(SelectedObjects, SelectedItems, [](UObject* InObject)
	{
		return MakeShared<FOperatorStackEditorObjectItem>(InObject);
	});

	const FOperatorStackEditorContext Context(SelectedItems);
	OperatorStack->SetContext(Context);
}

void SAvaOperatorStackTab::OnModifierUpdated(UActorModifierCoreBase* InUpdatedItem) const
{
	if (InUpdatedItem)
	{
		RefreshCurrentSelection(InUpdatedItem->GetRootModifierStack());
	}
}

void SAvaOperatorStackTab::OnAnimatorUpdated(UPropertyAnimatorCoreComponent* InComponent, UPropertyAnimatorCoreBase* InUpdatedItem) const
{
	if (InComponent)
	{
		RefreshCurrentSelection(InComponent);
	}
}

void SAvaOperatorStackTab::OnAnimatorRemoved(UPropertyAnimatorCoreComponent* InComponent, UPropertyAnimatorCoreBase* InRemovedItem) const
{
	if (!GEditor)
	{
		return;
	}

	if (USelection* SelectionSet = GEditor->GetSelectedObjects())
	{
		if (SelectionSet->CountSelections(UPropertyAnimatorCoreComponent::StaticClass())
			|| SelectionSet->CountSelections(UPropertyAnimatorCoreBase::StaticClass()))
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSPLambda(this, [this, SelectionSet](float)
			{
				RefreshSelection(SelectionSet);
				return false;
			}));
		}
		else
		{
			OnAnimatorUpdated(InComponent, InRemovedItem);
		}
	}
}

void SAvaOperatorStackTab::RefreshCurrentSelection(const UObject* InObject) const
{
	const TSharedPtr<IAvaDetailsProvider> DetailsProvider = DetailsProviderWeak.Pin();
	if (!DetailsProvider.IsValid())
	{
		return;
	}

	FEditorModeTools* ModeTools = DetailsProvider->GetDetailsModeTools();
	if (!InObject || !OperatorStack.IsValid() || !ModeTools)
	{
		return;
	}

	AActor* OwningActor = InObject->GetTypedOuter<AActor>();
	if (!OwningActor)
	{
		return;
	}

	const FOperatorStackEditorContextPtr Context = OperatorStack->GetContext();
	if (!Context.IsValid())
	{
		return;
	}

	const FAvaEditorSelection EditorSelection(*ModeTools, ModeTools->GetSelectedActors());
	if (!EditorSelection.IsValid())
	{
		return;
	}

	const TArray<UObject*> SelectedObjects = EditorSelection.GetSelectedObjects<UObject, EAvaSelectionSource::All>();
	if (!SelectedObjects.Contains(OwningActor) && !SelectedObjects.Contains(InObject))
	{
		return;
	}

	OperatorStack->RefreshContext();
}

#undef LOCTEXT_NAMESPACE