// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ClassViewerFilter.h"
#include "UObject/ObjectMacros.h"

class UUserWidget;
class UUIComponent;
class UUIComponentContainer;
class UWidgetBlueprint;
/**  */

class UMGEDITOR_API FUIComponentUtils
{
public:
	class FUIComponentClassFilter : public IClassViewerFilter
	{
	public:
		/** All children of these classes will be included unless filtered out by another setting. */
		TSet <const UClass*> AllowedChildrenOfClasses;
		TSet <const UClass*> ExcludedChildrenOfClasses;

		/** Disallowed class flags. */
		EClassFlags DisallowedClassFlags;

		virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override;

		virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override;
	};

	static UUIComponent* CreateUIComponent(TSubclassOf<UUIComponent> ComponentClass, UUserWidget* Outer);
	static UUIComponentContainer* GetOrCreateComponentsContainerForUserWidget(UUserWidget* UserWidget);
	static FClassViewerInitializationOptions CreateClassViewerInitializationOptions();

	static UUIComponentContainer* GetUIComponentContainerFromWidgetBlueprint(UWidgetBlueprint* WidgetBlueprint);
};