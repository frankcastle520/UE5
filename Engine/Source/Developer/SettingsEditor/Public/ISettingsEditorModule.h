// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/DelegateCombinations.h"
#include "Modules/ModuleInterface.h"
#include "Widgets/SWidget.h"

class ISettingsContainer;
class ISettingsEditorModel;
class UDeveloperSettings;

DECLARE_DELEGATE_RetVal_OneParam(bool, FShouldRegisterSettingsDelegate , UDeveloperSettings* /*Settings*/);

/**
 * Interface for settings editor modules.
 */
class ISettingsEditorModule
	: public IModuleInterface
{
public:

	/**
	 * Creates a settings editor widget.
	 *
	 * @param Model The view model.
	 * @return The new widget.
	 * @see CreateModel
	 */
	virtual TSharedRef<SWidget> CreateEditor( const TSharedRef<ISettingsEditorModel>& Model ) = 0;

	/**
	 * Creates a view model for the settings editor widget.
	 *
	 * @param SettingsContainer The settings container.
	 * @return The controller.
	 * @see CreateEditor
	 */
	virtual TSharedRef<ISettingsEditorModel> CreateModel( const TSharedRef<ISettingsContainer>& SettingsContainer ) = 0;

	/**
	 * Called when the settings have been changed such that an application restart is required for them to be fully applied
	 */
	virtual void OnApplicationRestartRequired() = 0;

	/**
	 * Set the delegate that should be called when a setting editor needs to restart the application
	 *
	 * @param InRestartApplicationDelegate The new delegate to call
	 */
	virtual void SetRestartApplicationCallback( FSimpleDelegate InRestartApplicationDelegate ) = 0;

	/**
	 * Set the delegate that should be called when a setting editor checks whether a settings object should be registered.
	 * 
	 * @param InShouldRegisterSettingDelegate The new delegate to call.
	 */
	virtual void SetShouldRegisterSettingCallback(FShouldRegisterSettingsDelegate InShouldRegisterSettingDelegate) = 0;

public:

	/** Virtual destructor. */
	virtual ~ISettingsEditorModule() { }
};
