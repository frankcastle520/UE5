// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ConfirmDialogWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SSplitter.h"
#include "Logic/DialogFactory.h"

class FModelInterface;
class SMultiLineEditableTextBox;
class SDockTab;
class SWindow;
class SButton;
class FTabManager;
class FValidatorBase;
class SExpandableArea;
class SIntegrationWidget;

class SubmitToolWidget final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SubmitToolWidget) {}
		SLATE_ATTRIBUTE(TSharedPtr<SDockTab>, ParentTab)
		SLATE_ARGUMENT(FModelInterface*, ModelInterface)
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SubmitToolWidget() override;
	
	FReply SubmitClicked();
	FReply ValidateClicked();
private:
	void OnCLDescriptionUpdated();
	void HandleApplicationActivationStateChanged(bool bActive);
	TWeakPtr<SDockTab> ParentTab;
	FModelInterface* ModelInterface;

	/**
	 * @brief Shows a dialog if the user wants to delete their shelf
	 * @return The button that is pressed
	 */
	EDialogFactoryResult ShowDeleteShelveDialog() const;
	FText GetMainButtonText() const;
	FText GetValidateButtonText() const;
	FText GetValidateButtonTooltip() const;

	TSharedPtr<FTabManager> LogTabManager;
	TSharedPtr<SButton> ValidateBtn;
	TSharedRef<SHorizontalBox> BuildButtonRow();
	TSharedRef<SWidget> BuildOutputLogWidget();
	TSharedRef<SExpandableArea> BuildFilesInCLWidget();
	TSharedPtr<SDockTab> ValidatorLogTab;
	TSharedPtr<SDockTab> PresubmitLogTab;
	TSharedPtr<SDockTab> SummaryLogDockTab;
	TSharedPtr<SMultiLineEditableTextBox> DescriptionBox;
	TSharedPtr<SIntegrationWidget> IntegrationWidget;

	SSplitter::FSlot* P4SectionSlot;
	SSplitter::FSlot* ValidatorSectionSlot;
	SSplitter::FSlot* LogSectionSlot;

	FDelegateHandle OnValidatorFinishedHandle;
	FDelegateHandle OnValidationUpdateHandle;
	FDelegateHandle OnCLDescriptionUpdatedHandle;
	void OnSingleValidatorFinished(const FValidatorBase& InValidator);
	void OnValidationUpdated(bool bValid);

#if PLATFORM_WINDOWS
	FReply CopyAllLogsClicked();
#endif
};