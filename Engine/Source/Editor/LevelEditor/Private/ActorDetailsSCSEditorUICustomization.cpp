// Copyright Epic Games, Inc. All Rights Reserved.
#include "ActorDetailsSCSEditorUICustomization.h"

TSharedPtr<FActorDetailsSCSEditorUICustomization> FActorDetailsSCSEditorUICustomization::Instance;

TSharedPtr<FActorDetailsSCSEditorUICustomization> FActorDetailsSCSEditorUICustomization::GetInstance()
{
	if (!Instance)
	{
		Instance = MakeShareable(new FActorDetailsSCSEditorUICustomization());
	}
	return Instance;
}

bool FActorDetailsSCSEditorUICustomization::HideComponentsTree(TArrayView<UObject*> Context) const
{
	for (const TSharedPtr<ISCSEditorUICustomization>& Customization : Customizations)
	{
		if (Customization->HideComponentsTree(Context))
		{
			return true;
		}
	}

	return false;
}

bool FActorDetailsSCSEditorUICustomization::HideComponentsFilterBox(TArrayView<UObject*> Context) const
{
	for (const TSharedPtr<ISCSEditorUICustomization>& Customization : Customizations)
	{
		if (Customization->HideComponentsFilterBox(Context))
		{
			return true;
		}
	}

	return false;
}

bool FActorDetailsSCSEditorUICustomization::HideAddComponentButton(TArrayView<UObject*> Context) const
{
	for (const TSharedPtr<ISCSEditorUICustomization>& Customization : Customizations)
	{
		if (Customization->HideAddComponentButton(Context))
		{
			return true;
		}
	}

	return false;
}

bool FActorDetailsSCSEditorUICustomization::HideBlueprintButtons(TArrayView<UObject*> Context) const
{
	for (const TSharedPtr<ISCSEditorUICustomization>& Customization : Customizations)
	{
		if (Customization->HideBlueprintButtons(Context))
		{
			return true;
		}
	}

	return false;
}

EChildActorComponentTreeViewVisualizationMode FActorDetailsSCSEditorUICustomization::GetChildActorVisualizationMode() const
{
	for (const TSharedPtr<ISCSEditorUICustomization>& Customization : Customizations)
	{
		EChildActorComponentTreeViewVisualizationMode VisualizationMode = Customization->GetChildActorVisualizationMode();
		if (VisualizationMode != EChildActorComponentTreeViewVisualizationMode::UseDefault)
		{
			return VisualizationMode;
		}
	}

	return EChildActorComponentTreeViewVisualizationMode::UseDefault;
}

TSubclassOf<UActorComponent> FActorDetailsSCSEditorUICustomization::GetComponentTypeFilter(TArrayView<UObject*> Context) const
{
	for (const TSharedPtr<ISCSEditorUICustomization>& Customization : Customizations)
	{
		if(TSubclassOf<UActorComponent> SubClass = Customization->GetComponentTypeFilter(Context))
		{
			return SubClass;
		}
	}

	return nullptr;
}

void FActorDetailsSCSEditorUICustomization::AddCustomization(TSharedPtr<ISCSEditorUICustomization> Customization)
{
	Customizations.AddUnique(Customization);
}

void FActorDetailsSCSEditorUICustomization::RemoveCustomization(TSharedPtr<ISCSEditorUICustomization> Customization)
{
	Customizations.Remove(Customization);
}


