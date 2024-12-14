// Copyright Epic Games, Inc. All Rights Reserved.

#include "Extensions/UIComponentContainerDesignerExtension.h"
#include "Blueprint/UserWidget.h"
#include "Extensions/UIComponentContainer.h"
#include "WidgetBlueprint.h"
#include "UIComponentUtils.h"

TSharedRef<FDesignerExtension> FUIComponentContainerDesignerExtensionFactory::CreateDesignerExtension() const
{
	return StaticCastSharedRef<FUIComponentContainerDesignerExtension>(MakeShared<FUIComponentContainerDesignerExtension>());
}

void FUIComponentContainerDesignerExtension::PreviewContentChanged(TSharedRef<SWidget> NewContent)
{
	if (UUIComponentContainer* UIComponentContainer = FUIComponentUtils::GetUIComponentContainerFromWidgetBlueprint(Blueprint.Get()))
	{
		UIComponentContainer->OnPreviewContentChanged(NewContent);
	}
}
