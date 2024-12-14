// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "LevelEditor.h"
#include "ToolMenus.h"
#include "IPixelStreaming2Module.h"

namespace UE::EditorPixelStreaming2
{
	class FPixelStreaming2Toolbar
	{
	public:
		FPixelStreaming2Toolbar();
		virtual ~FPixelStreaming2Toolbar();
		void StartStreaming();
		void StopStreaming();
		static TSharedRef<SWidget> GeneratePixelStreaming2MenuContent(TSharedPtr<FUICommandList> InCommandList);
		static FText GetActiveViewportName();
		static const FSlateBrush* GetActiveViewportIcon();

	private:
		void RegisterMenus();
		void RegisterEmbeddedSignallingServerConfig(FMenuBuilder& MenuBuilder);
		void RegisterRemoteSignallingServerConfig(FMenuBuilder& MenuBuilder);
		void RegisterSignallingServerURLs(FMenuBuilder& MenuBuilder);
		void RegisterStreamerControls(FMenuBuilder& MenuBuilder);
		void RegisterVCamControls(FMenuBuilder& MenuBuilder);
		void RegisterCodecConfig(FMenuBuilder& MenuBuilder);

		TSharedPtr<class FUICommandList> PluginCommands;
	};
} // namespace UE::EditorPixelStreaming2
