// Copyright Epic Games, Inc. All Rights Reserved.

#include "SourceControlViewportUtils.h"
#include "Settings/EditorStyleSettings.h"
#include "LevelEditorViewport.h"

namespace SourceControlViewportUtils
{

bool GetOverlaySetting(FViewportClient* ViewportClient, ESourceControlStatus Status)
{
	FString CVarName;
	switch (Status)
	{
	case ESourceControlStatus::CheckedOutByOtherUser:
		CVarName = TEXT("RevisionControl.Overlays.CheckedOutByOtherUser.Enable");
		break;
	case ESourceControlStatus::NotAtHeadRevision:
		CVarName = TEXT("RevisionControl.Overlays.NotAtHeadRevision.Enable");
		break;
	case ESourceControlStatus::CheckedOut:
		CVarName = TEXT("RevisionControl.Overlays.CheckedOut.Enable");
		break;
	case ESourceControlStatus::OpenForAdd:
		CVarName = TEXT("RevisionControl.Overlays.OpenForAdd.Enable");
		break;
	default:
		checkNoEntry();
		break;
	}

	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*CVarName); ensure(CVar))
	{
		return CVar->GetBool();
	}

	return false;
}

void SetOverlaySetting(FViewportClient* ViewportClient, ESourceControlStatus Status, bool bEnabled)
{
	FString CVarName;
	switch (Status)
	{
	case ESourceControlStatus::CheckedOutByOtherUser:
		CVarName = TEXT("RevisionControl.Overlays.CheckedOutByOtherUser.Enable");
		break;
	case ESourceControlStatus::NotAtHeadRevision:
		CVarName = TEXT("RevisionControl.Overlays.NotAtHeadRevision.Enable");
		break;
	case ESourceControlStatus::CheckedOut:
		CVarName = TEXT("RevisionControl.Overlays.CheckedOut.Enable");
		break;
	case ESourceControlStatus::OpenForAdd:
		CVarName = TEXT("RevisionControl.Overlays.OpenForAdd.Enable");
		break;
	default:
		checkNoEntry();
		break;
	}

	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*CVarName); ensure(CVar))
	{
		CVar->Set(bEnabled);
	}
}

bool GetFeedbackEnabled(FViewportClient* ViewportClient, ESourceControlStatus Status)
{
	return GetOverlaySetting(ViewportClient, Status);
}

void SetFeedbackEnabled(FViewportClient* ViewportClient, ESourceControlStatus Status, bool bEnabled)
{
	SetOverlaySetting(ViewportClient, Status, bEnabled);

	GEditor->RedrawLevelEditingViewports();
}

uint8 GetFeedbackOpacity(FViewportClient* ViewportClient)
{
	uint8 Opacity = 0;

	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("RevisionControl.Overlays.Alpha")); ensure(CVar))
	{
		Opacity = CVar->GetInt();
	}

	return Opacity;
}

void SetFeedbackOpacity(FViewportClient* ViewportClient, uint8 Opacity)
{
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("RevisionControl.Overlays.Alpha")); ensure(CVar))
	{
		CVar->Set(Opacity);
	}

	GEditor->RedrawLevelEditingViewports();
}

}