// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "StatusBarSubsystem.h"
#include "Toolkits/BaseToolkit.h"

class IToolkitHost;
class STextBlock;
class SButton;

namespace UE::MLDeformer
{
	class FMLDeformerEditorToolkit;

	class MLDEFORMERFRAMEWORKEDITOR_API FMLDeformerPaintModeToolkit
		: public FModeToolkit
	{
	public:
		virtual ~FMLDeformerPaintModeToolkit() override;
	
		// IToolkit overrides
		virtual void Init(const TSharedPtr<IToolkitHost>& InToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode) override;
		virtual FName GetToolkitFName() const override;
		virtual FText GetBaseToolkitName() const override;
		virtual TSharedPtr<SWidget> GetInlineContent() const override			{ return ToolkitWidget; }

		// FModeToolkit overrides
		virtual void OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool) override;
		virtual void OnToolEnded(UInteractiveToolManager* Manager, UInteractiveTool* Tool) override;
		virtual FText GetActiveToolDisplayName() const override;
		virtual FText GetActiveToolMessage() const override;

		void SetMLDeformerEditor(FMLDeformerEditorToolkit* Editor)				{ MLDeformerEditor = Editor; }
		UE::MLDeformer::FMLDeformerEditorToolkit* GetMLDeformerEditor() const	{ return MLDeformerEditor; }

	private:
		void PostNotification(const FText& Message);
		void ClearNotification();

		void PostWarning(const FText& Message);
		void ClearWarning();

		void UpdateActiveToolProperties(UInteractiveTool* Tool);

		void RegisterPalettes();
		FDelegateHandle ActivePaletteChangedHandle;

		void MakeToolAcceptCancelWidget();

		FText ActiveToolName;
		FText ActiveToolMessage;
		FStatusBarMessageHandle ActiveToolMessageHandle;

		TSharedPtr<SWidget> ToolkitWidget;

		FMLDeformerEditorToolkit* MLDeformerEditor = nullptr;

		TSharedPtr<SWidget> ViewportOverlayWidget;
		const FSlateBrush* ActiveToolIcon = nullptr;
	
		TSharedPtr<STextBlock> ModeWarningArea;
		TSharedPtr<STextBlock> ModeHeaderArea;
		TSharedPtr<STextBlock> ToolWarningArea;
	};
} // namespace UE::MLDeformer
