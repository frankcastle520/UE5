// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IObjectMixerSelectionInterface.h"

/**
 * Provides an interface for the ObjectMixer to synchronize with the level editor's selections (via GEditor)
 */
class OBJECTMIXEREDITOR_API FLevelEditorObjectMixerSelectionInterface : public IObjectMixerSelectionInterface
{
public:

	FLevelEditorObjectMixerSelectionInterface();
	virtual ~FLevelEditorObjectMixerSelectionInterface() override;

	//~ Begin IObjectMixerSelectionInterface interface
	virtual void SelectActors(const TArray<AActor*>& InSelectedActors, bool bShouldSelect, bool bSelectEvenIfHidden) override;
	virtual void SelectComponents(const TArray<UActorComponent*>& InSelectedComponents, bool bShouldSelect, bool bSelectEvenIfHidden) override;
	virtual TArray<AActor*> GetSelectedActors() const override;
	virtual TArray<UActorComponent*> GetSelectedComponents() const override;
	virtual FOnSelectionChanged& OnSelectionChanged() override { return SelectionChanged; }
	//~ End IObjectMixerSelectionInterface interface

private:
	void OnLevelSelectionChanged(UObject* Obj);

	FOnSelectionChanged SelectionChanged;
};