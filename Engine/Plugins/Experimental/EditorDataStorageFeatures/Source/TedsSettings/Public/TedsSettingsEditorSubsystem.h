// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorSubsystem.h"

#include "TedsSettingsEditorSubsystem.generated.h"

class FTedsSettingsManager;

UCLASS()
class TEDSSETTINGS_API UTedsSettingsEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:

	UTedsSettingsEditorSubsystem();

	const bool IsEnabled() const;

	DECLARE_MULTICAST_DELEGATE(FOnEnabledChanged)
	FOnEnabledChanged& OnEnabledChanged();

protected: // USubsystem

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:

	TSharedPtr<FTedsSettingsManager> SettingsManager;
	FOnEnabledChanged EnabledChangedDelegate;

};
