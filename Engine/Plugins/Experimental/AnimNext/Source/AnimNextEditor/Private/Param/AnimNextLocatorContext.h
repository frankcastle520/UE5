// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UniversalObjectLocatorEditorContext.h"
#include "Component/AnimNextComponent.h"
#include "Modules/ModuleManager.h"
#include "AnimNextEditorModule.h"

namespace UE::AnimNext::Editor
{

class FLocatorContext : public UE::UniversalObjectLocator::ILocatorFragmentEditorContext
{
	// ILocatorFragmentEditorContext interface
	virtual UObject* GetContext(const IPropertyHandle& InPropertyHandle) const override
	{
		// TODO: This needs to defer to project/schedule/workspace defaults similar to SParameterPicker
		return UAnimNextComponent::StaticClass()->GetDefaultObject();
	}

	virtual bool IsFragmentAllowed(FName InFragmentName) const override
	{
		FAnimNextEditorModule& EditorModule = FModuleManager::GetModuleChecked<FAnimNextEditorModule>("AnimNextEditor");
		return EditorModule.LocatorFragmentEditorNames.Contains(InFragmentName);
	}
};

}
