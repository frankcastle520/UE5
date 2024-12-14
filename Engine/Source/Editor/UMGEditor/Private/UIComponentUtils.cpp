// Copyright Epic Games, Inc. All Rights Reserved.

#include "UIComponentUtils.h"

#include "Blueprint/UserWidget.h"
#include "ClassViewerModule.h"
#include "CoreGlobals.h"
#include "Components/Widget.h"
#include "Extensions/UIComponent.h"
#include "Extensions/UIComponentContainer.h"
#include "WidgetBlueprint.h"

UUIComponent* FUIComponentUtils::CreateUIComponent(TSubclassOf<UUIComponent> ComponentClass, UUserWidget* Outer)
{
	ensure(Outer);
	if (UUIComponent* NewComponent = NewObject<UUIComponent>(Outer, ComponentClass))
	{
		NewComponent->SetFlags(RF_Transactional);
		return NewComponent;
	}
	return nullptr;
}

UUIComponentContainer* FUIComponentUtils::GetOrCreateComponentsContainerForUserWidget(UUserWidget* UserWidget)
{
	check(UserWidget);
	if (UUIComponentContainer* ExistingComponentsContainer = UserWidget->GetExtension<UUIComponentContainer>())
	{
		return ExistingComponentsContainer;
	}
	else if (UUIComponentContainer* AddedComponentContainer = UserWidget->AddExtension<UUIComponentContainer>())
	{
		AddedComponentContainer->SetFlags(RF_Transactional);
		return AddedComponentContainer;
	}
	return nullptr;
}

FClassViewerInitializationOptions FUIComponentUtils::CreateClassViewerInitializationOptions()
{
	FClassViewerInitializationOptions Options;
	Options.Mode = EClassViewerMode::ClassPicker;
	TSharedPtr<FUIComponentUtils::FUIComponentClassFilter> Filter = MakeShared<FUIComponentUtils::FUIComponentClassFilter>();
	Options.ClassFilters.Add(Filter.ToSharedRef());

	Filter->DisallowedClassFlags = CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_HideDropDown | CLASS_Abstract;
	Filter->AllowedChildrenOfClasses.Add(UUIComponent::StaticClass());

	return Options;
}

UUIComponentContainer* FUIComponentUtils::GetUIComponentContainerFromWidgetBlueprint(UWidgetBlueprint* WidgetBlueprint)
{
	if (WidgetBlueprint)
	{
		if (UUserWidget* CDO = WidgetBlueprint->GeneratedClass->GetDefaultObject<UUserWidget>())
		{
			return CDO->GetExtension<UUIComponentContainer>();
		}
	}
	return nullptr;
}

bool FUIComponentUtils::FUIComponentClassFilter::IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs)
{
	return !InClass->HasAnyClassFlags(DisallowedClassFlags)
		&& InFilterFuncs->IfInChildOfClassesSet(AllowedChildrenOfClasses, InClass) != EFilterReturn::Failed;
}

bool FUIComponentUtils::FUIComponentClassFilter::IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs)
{
	return !InUnloadedClassData->HasAnyClassFlags(DisallowedClassFlags)
		&& InFilterFuncs->IfInChildOfClassesSet(AllowedChildrenOfClasses, InUnloadedClassData) != EFilterReturn::Failed;
}