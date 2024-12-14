// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "MVVM/ViewModelPtr.h"
#include "SequencerCustomizationManager.h"
#include "Toolkits/AssetEditorToolkit.h"

namespace UE::Sequencer
{
	class FObjectBindingModel;
	class FSequencerEditorViewModel;

/**
 * The sequencer customization for level sequences. 
 */
class FLevelSequenceCustomization : public ISequencerCustomization
{
protected:

	//~ ISequencerCustomization interface
	virtual void RegisterSequencerCustomization(FSequencerCustomizationBuilder& Builder) override;
	virtual void UnregisterSequencerCustomization() override;

private:

	// Actions menu extensions
	void ExtendActionsMenu(FMenuBuilder& MenuBuilder);
	void OnSaveMovieSceneAsClicked();
	void ImportFBX();
	void ExportFBX();

	// Object binding context menu extensions
	TSharedPtr<FExtender> CreateObjectBindingContextMenuExtender(FViewModelPtr InViewModel);
	void ExtendObjectBindingContextMenu(FMenuBuilder& MenuBuilder, TSharedPtr<FObjectBindingModel> ObjectBindingModel);

	// Object binding sidebar menu extensions
	TSharedPtr<FExtender> CreateObjectBindingSidebarMenuExtender(FViewModelPtr InViewModel);
	void ExtendObjectBindingSidebarMenu(FMenuBuilder& MenuBuilder, TSharedPtr<FObjectBindingModel> ObjectBindingModel);

private:

	TWeakPtr<ISequencer> WeakSequencer;

	TSharedPtr<FUICommandList> ActionsMenuCommandList;
	TSharedPtr<FExtender> ActionsMenuExtender;
};

} // namespace UE::Sequencer

