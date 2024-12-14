// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditMode/ControlRigEditModeSettings.h"
#include "EditorModeManager.h"
#include "EditMode/ControlRigEditMode.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ControlRigEditModeSettings)

UControlRigEditModeSettings::FOnUpdateSettings UControlRigEditModeSettings::OnSettingsChange;

void UControlRigEditModeSettings::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
}

void UControlRigEditModeSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
#if WITH_EDITOR
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigEditModeSettings, GizmoScale))
	{
		GizmoScaleDelegate.Broadcast(GizmoScale);
	}
#endif
	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		// Dragging spinboxes causes this to be called every frame so we wait until they've finished dragging before saving.
		SaveConfig();
	}
	OnSettingsChange.Broadcast(this);
}

#if WITH_EDITOR
void UControlRigEditModeSettings::PostEditUndo()
{
	GizmoScaleDelegate.Broadcast(GizmoScale);
	OnSettingsChange.Broadcast(this);
}
#endif


