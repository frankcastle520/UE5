// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/MVVMBlueprintViewDesignerExtension.h"

#include "Extensions/MVVMBlueprintViewExtension.h"
#include "MVVMWidgetBlueprintExtension_View.h"
#include "WidgetBlueprintExtension.h"

namespace UE::MVVM
{
TSharedRef<FDesignerExtension> FBlueprintViewDesignerExtensionFactory::CreateDesignerExtension() const
{
	return StaticCastSharedRef<FBlueprintViewDesignerExtension>(MakeShared<FBlueprintViewDesignerExtension>());
}

void FBlueprintViewDesignerExtension::PreviewContentChanged(TSharedRef<SWidget> NewContent)
{
	const UWidgetBlueprint* WidgetBlueprint = Blueprint.Get();

	if (UMVVMWidgetBlueprintExtension_View* ExtensionView = WidgetBlueprint ? UWidgetBlueprintExtension::GetExtension<UMVVMWidgetBlueprintExtension_View>(WidgetBlueprint) : nullptr)
	{
		for (UMVVMBlueprintViewExtension* BlueprintViewExtension : ExtensionView->GetAllBlueprintExtensions())
		{
			if (ensure(BlueprintViewExtension))
			{
				BlueprintViewExtension->OnPreviewContentChanged(NewContent);
			}
		}
	}
}
}
