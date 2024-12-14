// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UMGWidgetPreview/Public/IUMGWidgetPreviewModule.h"

namespace UE::UMGWidgetPreview::Private
{
	class FUMGWidgetPreviewModule
		: public IUMGWidgetPreviewModule
	{
	public:
		//~ Begin IModuleInterface
		virtual void StartupModule() override;
		virtual void ShutdownModule() override;
		//~ End IModuleInterface

		//~ Begin IUMGWidgetPreviewModule
		virtual FOnRegisterTabs& OnRegisterTabsForEditor() override;
		//~ End IUMGWidgetPreviewModule

	private:
		void RegisterMenus();

	private:
		FOnRegisterTabs RegisterTabsForEditorDelegate;
	};
}
