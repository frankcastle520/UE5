// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Rundown/Pages/AvaRundownPageControllerContext.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtrTemplatesFwd.h"

class FAvaRundownEditor;
class FAvaRundownRCPropertyItem;
class FUICommandList;
class SAvaRundownPageRemoteControlProps;
class SWidget;
class UAvaRundownPagePropertyContext;
class UToolMenu;
struct FAvaRundownPage;

class FAvaRundownPagePropertyContextMenu : public TSharedFromThis<FAvaRundownPagePropertyContextMenu>
{
public:
	FAvaRundownPagePropertyContextMenu(const TWeakPtr<FUICommandList>& InCommandListWeak);

	TSharedRef<SWidget> GeneratePageContextMenuWidget(const TWeakPtr<FAvaRundownEditor>& InRundownEditorWeak
		, const FAvaRundownPage& InRundownPage
		, const TWeakPtr<SAvaRundownPageRemoteControlProps>& InPropertyListWidgetWeak);

	bool HasValidSelectedItems() const;

private:
	void PopulatePageContextMenu(UToolMenu& InMenu, UAvaRundownPagePropertyContext* const InContext);

	void BindCommands(const TWeakPtr<FUICommandList>& InCommandListWeak);

	void ResetValuesToDefaults(const bool bInUseTemplateValues);
	bool CanResetValuesToDefaults(const bool bInUseTemplateValues) const;

	TArray<TWeakPtr<FAvaRundownRCPropertyItem>> GetSelectedPropertyItems() const;

	UAvaRundown* GetContextRundown() const;

	TWeakPtr<FUICommandList> CommandListWeak;

	TWeakObjectPtr<UAvaRundownPagePropertyContext> CurrentContext;
};
