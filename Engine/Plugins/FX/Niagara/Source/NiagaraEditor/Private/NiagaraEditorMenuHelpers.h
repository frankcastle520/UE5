// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace NiagaraEditorMenuHelpers
{
	void RegisterToolMenus();
	void RegisterMenuExtensions();

	// Registers a runtime profile for the AssetViewOptions in the NiagaraAssetBrowser to hide entries we don't need to display
	void RegisterAssetBrowserViewOptionsProfile();
	// The standalone menu requires to have a UNiagaraTagsContentBrowserFilterContext context object to save and write data into
	void RegisterNiagaraAssetTagStandaloneMenu();
	// This does a mix of extending and registering menus for Niagara Emitters, Systems and Scripts
	void RegisterNiagaraAssetTagMenusForAssets();
}