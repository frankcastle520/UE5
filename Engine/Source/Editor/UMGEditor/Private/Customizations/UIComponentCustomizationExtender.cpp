// Copyright Epic Games, Inc. All Rights Reserved.

#include "UIComponentCustomizationExtender.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "Extensions/UIComponent.h"
#include "Extensions/UIComponentContainer.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyHandle.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditor.h"
#include "UIComponentUtils.h"

#define LOCTEXT_NAMESPACE "UIComponentCustomizationExtender"


TSharedPtr<FUIComponentCustomizationExtender> FUIComponentCustomizationExtender::MakeInstance()
{
	return MakeShared<FUIComponentCustomizationExtender>();
}

void FUIComponentCustomizationExtender::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout, const TArrayView<UWidget*> InWidgets, const TSharedRef<FWidgetBlueprintEditor>& InWidgetBlueprintEditor)
{
	if (InWidgets.Num() == 1)
	{
		Widget = InWidgets[0];
		WidgetBlueprintEditor = InWidgetBlueprintEditor;

		if (UUIComponentContainer* UIComponentContainer = FUIComponentUtils::GetUIComponentContainerFromWidgetBlueprint(WidgetBlueprintEditor.Pin()->GetWidgetBlueprintObj()))
		{
			TArray<UUIComponent*> ComponentsOnWidget = UIComponentContainer->GetExtensionsFor(Widget->GetFName());

			for (int32 Index = ComponentsOnWidget.Num() - 1; Index >= 0; Index--)
			{
				if (UUIComponent* Component = ComponentsOnWidget[Index])
				{
					IDetailCategoryBuilder& ComponentCategory = InDetailLayout.EditCategory(Component->GetFName(), FText::GetEmpty(), ECategoryPriority::Important);

					IDetailPropertyRow* PropertyRow = ComponentCategory.AddExternalObjects({ Component }, EPropertyLocation::Default,
						FAddPropertyParams()
						.CreateCategoryNodes(false)
						.AllowChildren(true)
						.HideRootObjectNode(true));
				}
			}
		}
	}
}
#undef LOCTEXT_NAMESPACE