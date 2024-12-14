// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ISCSEditorUICustomization.h"
#include "Containers/Array.h"
#include "Templates/SharedPointer.h"

class FActorDetailsSCSEditorUICustomization : public ISCSEditorUICustomization
{
public:
	static TSharedPtr<FActorDetailsSCSEditorUICustomization> GetInstance();
	
	virtual ~FActorDetailsSCSEditorUICustomization() {}

	virtual bool HideComponentsTree(TArrayView<UObject*> Context) const override;

	virtual bool HideComponentsFilterBox(TArrayView<UObject*> Context) const override;

	virtual bool HideAddComponentButton(TArrayView<UObject*> Context) const override;

	virtual bool HideBlueprintButtons(TArrayView<UObject*> Context) const override;

	virtual EChildActorComponentTreeViewVisualizationMode GetChildActorVisualizationMode() const override;

	virtual TSubclassOf<UActorComponent> GetComponentTypeFilter(TArrayView<UObject*> Context) const override;

	void AddCustomization(TSharedPtr<ISCSEditorUICustomization> Customization);
	void RemoveCustomization(TSharedPtr<ISCSEditorUICustomization> Customization);

private:
	FActorDetailsSCSEditorUICustomization() {}

	static TSharedPtr<FActorDetailsSCSEditorUICustomization> Instance;
	TArray<TSharedPtr<ISCSEditorUICustomization>> Customizations;
};