// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleEditorToolbar.h"

#include "Commands/DMXControlConsoleEditorCommands.h"
#include "DMXControlConsole.h"
#include "DMXControlConsoleData.h"
#include "DMXControlConsoleEditorData.h"
#include "DMXControlConsoleEditorSelection.h"
#include "DMXEditorStyle.h"
#include "Filters/SCustomTextFilterDialog.h"
#include "Filters/SFilterSearchBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Layouts/DMXControlConsoleEditorGlobalLayoutBase.h"
#include "Layouts/DMXControlConsoleEditorLayouts.h"
#include "Models/DMXControlConsoleEditorModel.h"
#include "Models/Filter/DMXControlConsoleGlobalFilterModel.h"
#include "ScopedTransaction.h"
#include "Style/DMXControlConsoleEditorStyle.h"
#include "Toolkits/DMXControlConsoleEditorToolkit.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SDMXControlConsoleEditorCueStackComboBox.h"
#include "Widgets/SDMXControlConsoleEditorPortSelector.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleEditorToolbar"

namespace UE::DMX::Private
{
	FDMXControlConsoleEditorToolbar::FDMXControlConsoleEditorToolbar(TSharedPtr<FDMXControlConsoleEditorToolkit> InToolkit)
		: WeakToolkit(InToolkit)
	{}

	void FDMXControlConsoleEditorToolbar::BuildToolbar(TSharedPtr<FExtender> Extender)
	{
		TSharedPtr<FDMXControlConsoleEditorToolkit> Toolkit = WeakToolkit.Pin();
		check(Toolkit.IsValid());

		// Generate Port Selector widget
		PortSelector = SNew(SDMXControlConsoleEditorPortSelector)
			.OnPortsSelected(this, &FDMXControlConsoleEditorToolbar::OnSelectedPortsChanged);

		OnSelectedPortsChanged();

		Extender->AddToolBarExtension
		(
			"PlayMenu",
			EExtensionHook::After,
			Toolkit->GetToolkitCommands(),
			FToolBarExtensionDelegate::CreateSP(this, &FDMXControlConsoleEditorToolbar::BuildToolbarCallback)
		);
	}

	void FDMXControlConsoleEditorToolbar::BuildToolbarCallback(FToolBarBuilder& ToolbarBuilder)
	{
		const TSharedPtr<FDMXControlConsoleEditorToolkit> Toolkit = WeakToolkit.Pin();
		if (!Toolkit.IsValid())
		{
			return;
		}

		const auto GenerateButtonContentLambda = [](const FSlateColor& ImageColor, const FSlateBrush* ImageBrush, const FText& ButtonText)
			{
				return
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.Padding(2.f)
					.AutoWidth()
					[
						SNew(SImage)
						.ColorAndOpacity(ImageColor)
						.Image(ImageBrush)
					]

					+ SHorizontalBox::Slot()
					.Padding(8.f, 2.f, 2.f, 2.f)
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(ButtonText)
					];
			};

		// The play section is added via tools menus in the toolkit that owns the toolbar

		ToolbarBuilder.BeginSection("Clear");
		{
			const TSharedRef<SComboButton> ClearComboButton =
				SNew(SComboButton)
				.ContentPadding(0.f)
				.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
				.OnGetMenuContent(this, &FDMXControlConsoleEditorToolbar::GenerateClearMenuWidget)
				.HasDownArrow(true)
				.ButtonContent()
				[
					GenerateButtonContentLambda
					(
						FSlateColor::UseForeground(),
						FAppStyle::GetBrush("Icons.Delete"),
						LOCTEXT("ClearToolbarButtonText", "Clear")
					)
				];

			ToolbarBuilder.AddWidget(ClearComboButton);
		}
		ToolbarBuilder.EndSection();

		ToolbarBuilder.BeginSection("Modes");
		{
			// Control Mode
			const TSharedRef<SComboButton> ControlModeComboButton =
				SNew(SComboButton)
				.ContentPadding(0.f)
				.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
				.OnGetMenuContent(this, &FDMXControlConsoleEditorToolbar::GenerateControlModeMenuWidget)
				.HasDownArrow(true)
				.ButtonContent()
				[
					GenerateButtonContentLambda
					(
						FSlateColor::UseForeground(),
						FDMXControlConsoleEditorStyle::Get().GetBrush("DMXControlConsole.ControlMode"),
						LOCTEXT("ControlModeToolbarButtonText", "Control")
					)
				];

			ToolbarBuilder.AddWidget(ControlModeComboButton);

			// View Mode
			const TSharedRef<SComboButton> ViewModeComboButton =
				SNew(SComboButton)
				.ContentPadding(0.f)
				.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
				.OnGetMenuContent(this, &FDMXControlConsoleEditorToolbar::GenerateViewModeMenuWidget)
				.HasDownArrow(true)
				.ButtonContent()
				[
					GenerateButtonContentLambda
					(
						FSlateColor::UseForeground(),
						FAppStyle::Get().GetBrush("Icons.Layout"),
						LOCTEXT("ViewModeToolbarButtonText", "View")
					)
				];

			ToolbarBuilder.AddWidget(ViewModeComboButton);

			// Sorting mode
			const TSharedRef<SComboButton> LayoutModeComboButton =
				SNew(SComboButton)
				.ContentPadding(0.f)
				.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
				.OnGetMenuContent(this, &FDMXControlConsoleEditorToolbar::GenerateLayoutModeMenuWidget)
				.HasDownArrow(true)
				.ButtonContent()
				[
					GenerateButtonContentLambda
					(
						FSlateColor::UseForeground(),
						FAppStyle::Get().GetBrush("EditorViewport.LocationGridSnap"),
						LOCTEXT("LayoutModeToolbarButtonText", "Layout")
					)
				];

			ToolbarBuilder.AddWidget(LayoutModeComboButton);
		}
		ToolbarBuilder.EndSection();

		ToolbarBuilder.BeginSection("Selection");
		{
			const TSharedRef<SComboButton> SelectionComboButton =
				SNew(SComboButton)
				.ContentPadding(0.f)
				.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
				.OnGetMenuContent(this, &FDMXControlConsoleEditorToolbar::GenerateSelectionMenuWidget)
				.HasDownArrow(true)
				.ButtonContent()
				[
					GenerateButtonContentLambda
					(
						FSlateColor::UseForeground(),
						FAppStyle::GetBrush("LevelEditor.Tabs.Viewports"),
						LOCTEXT("SelectionToolbarButtonText", "Selection")
					)
				];

			ToolbarBuilder.AddWidget(SelectionComboButton);
		}
		ToolbarBuilder.EndSection();

		ToolbarBuilder.BeginSection("Search");
		{
			const TSharedRef<SWidget> SearchBarWidget =
				SNew(SHorizontalBox)

				// SearchBox section
				+ SHorizontalBox::Slot()
				.Padding(2.f)
				[
					SNew(SBox)
					.VAlign(VAlign_Center)
					.WidthOverride(200.f)
					[	
						SAssignNew(GlobalFilterSearchBox, SFilterSearchBox)
						.DelayChangeNotificationsWhileTyping(true)
						.ShowSearchHistory(true)
						.OnTextChanged(this, &FDMXControlConsoleEditorToolbar::OnSearchTextChanged)
						.OnSaveSearchClicked(this, &FDMXControlConsoleEditorToolbar::OnSaveSearchButtonClicked)
						.HintText(LOCTEXT("SearchBarHintText", "Search"))
						.ToolTipText(LOCTEXT("SearchBarTooltip", "Searches for Fader Name, Attributes, Fixture ID, Universe or Patch. Examples:\n\n* FaderName\n* Dimmer\n* Pan, Tilt\n* 1\n* 1.\n* 1.1\n* Universe 1\n* Uni 1-3\n* Uni 1, 3\n* Uni 1, 4-5'."))
					]
				]

				// Autoselection CheckBox section
				+ SHorizontalBox::Slot()
				.Padding(4.f, 0.f)
				.AutoWidth()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SCheckBox)
						.IsChecked(this, &FDMXControlConsoleEditorToolbar::IsFilteredElementsAutoSelectChecked)
						.OnCheckStateChanged(this, &FDMXControlConsoleEditorToolbar::OnFilteredElementsAutoSelectStateChanged)
						.ToolTipText(LOCTEXT("SearchBarCheckBoxToolTipText", "Checked if filtered elements must be automatically selected."))
					]
					+ SHorizontalBox::Slot()
					.Padding(4.f, 0.f, 2.f, 0.f)
					.VAlign(VAlign_Center)
					.AutoWidth()
					[
						SNew(STextBlock)
						.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
						.Text(LOCTEXT("SearchBarAutoselectText", "Auto-Select Searched Elements"))
						.ToolTipText(LOCTEXT("SearchBarAutoselectToolTipText", "Checked if filtered elements must be automatically selected."))
					]
				];

			RestoreGlobalFilter();

			ToolbarBuilder.AddWidget(SearchBarWidget);
		}
		ToolbarBuilder.EndSection();

		ToolbarBuilder.BeginSection("EditorMode");
		{
			const TSharedRef<SWidget> ShowCompactEditorButton = 
				SNew(SButton)
				.OnClicked(this, &FDMXControlConsoleEditorToolbar::OnShowCompactEditorButtonClicked)
				[
					SNew(SBorder)
					.VAlign(VAlign_Center)
					.BorderImage(FAppStyle::GetBrush("NoBorder"))
					[
						SNew(STextBlock)
						.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
						.Text(LOCTEXT("ShowCompactWindowLabel", "Show Compact Editor"))
						.ToolTipText(LOCTEXT("ShowCompactWindowTooltip", "Creates a compact window that can be docked in the level editor."))	
					]
				];

			ToolbarBuilder.AddWidget(ShowCompactEditorButton);
		}
		ToolbarBuilder.EndSection();

		ToolbarBuilder.BeginSection("CueStack");
		{
			const TSharedRef<SWidget> CueStackComboBoxWidget =
				SNew(SDMXControlConsoleEditorCueStackComboBox, Toolkit->GetControlConsoleCueStackModel());

			ToolbarBuilder.AddWidget(CueStackComboBoxWidget, NAME_None, true, HAlign_Right);
		}
		ToolbarBuilder.EndSection();
	}

	TSharedRef<SWidget> FDMXControlConsoleEditorToolbar::GenerateClearMenuWidget()
	{
		const TSharedPtr<FDMXControlConsoleEditorToolkit> Toolkit = WeakToolkit.Pin();
		UDMXControlConsoleEditorData* EditorData = Toolkit.IsValid() ? Toolkit->GetControlConsoleEditorData() : nullptr;
		if (!ensureMsgf(EditorData, TEXT("Invalid control console editor data, can't generate control console toolbar correctly.")))
		{
			return SNullWidget::NullWidget;
		}

		constexpr bool bShouldCloseWindowAfterClosing = false;
		FMenuBuilder MenuBuilder(bShouldCloseWindowAfterClosing, Toolkit->GetToolkitCommands());

		MenuBuilder.BeginSection("Options", LOCTEXT("ClearMenuOptionsCategory", "Options"));
		{
			MenuBuilder.AddMenuEntry
			(
				FDMXControlConsoleEditorCommands::Get().ClearAll,
				NAME_None,
				TAttribute<FText>(),
				TAttribute<FText>(),
				FSlateIcon(FDMXControlConsoleEditorStyle::Get().GetStyleSetName(), "DMXControlConsole.Clear")
			);

			MenuBuilder.AddMenuEntry
			(
				FDMXControlConsoleEditorCommands::Get().ResetToDefault,
				NAME_None,
				TAttribute<FText>(),
				TAttribute<FText>(),
				FSlateIcon(FDMXControlConsoleEditorStyle::Get().GetStyleSetName(), "DMXControlConsole.ResetToDefault")
			);

			MenuBuilder.AddMenuEntry
			(
				FDMXControlConsoleEditorCommands::Get().ResetToZero,
				NAME_None,
				TAttribute<FText>(),
				TAttribute<FText>(),
				FSlateIcon(FDMXControlConsoleEditorStyle::Get().GetStyleSetName(), "DMXControlConsole.ResetToZero")
			);

			MenuBuilder.AddSeparator();

			MenuBuilder.AddMenuEntry
			(
				FDMXControlConsoleEditorCommands::Get().Reload,
				NAME_None,
				TAttribute<FText>(),
				TAttribute<FText>(),
				FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Refresh")
			);
		}
		MenuBuilder.EndSection();

		return MenuBuilder.MakeWidget();
	}

	TSharedRef<SWidget> FDMXControlConsoleEditorToolbar::GenerateControlModeMenuWidget()
	{
		const TSharedPtr<FDMXControlConsoleEditorToolkit> Toolkit = WeakToolkit.Pin();
		UDMXControlConsoleEditorData* EditorData = Toolkit.IsValid() ? Toolkit->GetControlConsoleEditorData() : nullptr;
		if (!ensureMsgf(EditorData, TEXT("Invalid control console editor data, can't generate control console toolbar correctly.")))
		{
			return SNullWidget::NullWidget;
		}

		constexpr bool bShouldCloseWindowAfterClosing = false;
		FMenuBuilder MenuBuilder(bShouldCloseWindowAfterClosing, nullptr);

		MenuBuilder.BeginSection("Faders", LOCTEXT("FadersControlModeCategory", "Faders"));
		{
			const auto AddControlModeMenuEntryLambda = [&MenuBuilder, EditorData](const FText& Label, const FText& ToolTip, EDMXControlConsoleEditorControlMode ControlMode)
				{
					MenuBuilder.AddMenuEntry
					(
						Label,
						ToolTip,
						FSlateIcon(),
						FUIAction
						(
							FExecuteAction::CreateUObject(EditorData, &UDMXControlConsoleEditorData::SetControlMode, ControlMode),
							FCanExecuteAction(),
							FIsActionChecked::CreateLambda([EditorData, ControlMode]() { return IsValid(EditorData) ? EditorData->GetControlMode() == ControlMode : false; })
						),
						NAME_None,
						EUserInterfaceActionType::RadioButton
					);
				};

			// Add a button to select relative control mode
			AddControlModeMenuEntryLambda
			(
				LOCTEXT("RelativeControlModeRadioButtonLabel", "Relative"),
				LOCTEXT("RelativeControlModeRadioButton_ToolTip", "Values of all selected Faders are increased/decreased by the same percentage."),
				EDMXControlConsoleEditorControlMode::Relative
			);

			// Add a button to select absolute control mode
			AddControlModeMenuEntryLambda
			(
				LOCTEXT("AbsoluteControlModeRadioButtonLabel", "Absolute"),
				LOCTEXT("AbsoluteControlModeRadioButton_ToolTip", "Values of all selected Faders are set to the same percentage."),
				EDMXControlConsoleEditorControlMode::Absolute
			);

			MenuBuilder.AddSeparator();

			const auto AddValueTypeMenuEntryLambda = [&MenuBuilder, EditorData](const FText& Label, const FText& ToolTip, EDMXControlConsoleEditorValueType ValueType)
				{
					MenuBuilder.AddMenuEntry
					(
						Label,
						ToolTip,
						FSlateIcon(),
						FUIAction
						(
							FExecuteAction::CreateUObject(EditorData, &UDMXControlConsoleEditorData::SetValueType, ValueType),
							FCanExecuteAction(),
							FIsActionChecked::CreateLambda([EditorData, ValueType]() { return IsValid(EditorData) ? EditorData->GetValueType() == ValueType : false; })
						),
						NAME_None,
						EUserInterfaceActionType::RadioButton
					);
				};

			// Add a button to select dmx value type
			AddValueTypeMenuEntryLambda
			(
				LOCTEXT("DMXValueTypeRadioButtonLabel", "DMX"),
				LOCTEXT("DMXValueTypeRadioButton_ToolTip", "Values are displayed as 8bit multiples."),
				EDMXControlConsoleEditorValueType::DMX
			);

			// Add a button to select normalized value type
			AddValueTypeMenuEntryLambda
			(
				LOCTEXT("NormalizedValueTypeRadioButtonLabel", "Normalized"),
				LOCTEXT("NormalizedValueTypeRadioButton_ToolTip", "Values are displayed in a 0 to 1 range."),
				EDMXControlConsoleEditorValueType::Normalized
			);

			// Add a button to select physical value type
			AddValueTypeMenuEntryLambda
			(
				LOCTEXT("PhysicalValueTypeRadioButtonLabel", "Physical"),
				LOCTEXT("PhysicalValueTypeRadioButton_ToolTip", "Values are displayed according to specified physical unit."),
				EDMXControlConsoleEditorValueType::Physical
			);

			MenuBuilder.AddSeparator();

			// Port Selector widget menu entry
			const TSharedRef<SWidget> PortSelectorWidget =
				SNew(SBox)
				.Padding(4.f, 0.f)
				[
					PortSelector.ToSharedRef()
				];

			MenuBuilder.AddWidget(PortSelectorWidget, FText::GetEmpty());
		}
		MenuBuilder.EndSection();

		return MenuBuilder.MakeWidget();
	}

	TSharedRef<SWidget> FDMXControlConsoleEditorToolbar::GenerateViewModeMenuWidget()
	{
		const TSharedPtr<FDMXControlConsoleEditorToolkit> Toolkit = WeakToolkit.Pin();
		UDMXControlConsoleEditorData* EditorData = Toolkit.IsValid() ? Toolkit->GetControlConsoleEditorData() : nullptr;
		if (!ensureMsgf(EditorData, TEXT("Invalid control console editor data, can't generate control console toolbar correctly.")))
		{
			return SNullWidget::NullWidget;
		}

		constexpr bool bShouldCloseWindowAfterClosing = false;
		FMenuBuilder MenuBuilder(bShouldCloseWindowAfterClosing, nullptr);

		MenuBuilder.BeginSection("Fader Groups", LOCTEXT("FaderGroupsViewModeCategory", "Fader Groups"));
		{
			const auto AddMenuEntryLambda = [&MenuBuilder, this](const FText& Label, EDMXControlConsoleEditorViewMode ViewMode)
				{
					MenuBuilder.AddMenuEntry
					(
						Label,
						FText::GetEmpty(),
						FSlateIcon(),
						FUIAction
						(
							FExecuteAction::CreateSP(this, &FDMXControlConsoleEditorToolbar::OnFaderGroupsViewModeSelected, ViewMode)
						),
						NAME_None,
						EUserInterfaceActionType::Button
					);
				};

			// Add buttons to select the view mode for fader groups
			AddMenuEntryLambda(LOCTEXT("FaderGroupsViewModeCollapseAllButtonLabel", "Collapse All"), EDMXControlConsoleEditorViewMode::Collapsed);
			AddMenuEntryLambda(LOCTEXT("FaderGroupsViewModeExpandAllButtonLabel", "Expand All"), EDMXControlConsoleEditorViewMode::Expanded);
		}
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection("Faders", LOCTEXT("FadersViewModeCategory", "Faders"));
		{
			const auto AddMenuEntryLambda = [&MenuBuilder, EditorData, this](const FText& Label, EDMXControlConsoleEditorViewMode ViewMode)
				{
					MenuBuilder.AddMenuEntry
					(
						Label,
						FText::GetEmpty(),
						FSlateIcon(),
						FUIAction
						(
							FExecuteAction::CreateSP(this, &FDMXControlConsoleEditorToolbar::OnFadersViewModeSelected, ViewMode),
							FCanExecuteAction(),
							FIsActionChecked::CreateLambda([EditorData, ViewMode]() { return IsValid(EditorData) ? EditorData->GetFadersViewMode() == ViewMode : false; })
						),
						NAME_None,
						EUserInterfaceActionType::RadioButton
					);
				};

			// Add buttons to select the view mode for faders
			AddMenuEntryLambda(LOCTEXT("FadersViewModeCollapsedRadioButtonLabel", "Basic"), EDMXControlConsoleEditorViewMode::Collapsed);
			AddMenuEntryLambda(LOCTEXT("FadersViewModeExpandedRadioButtonLabel", "Advanced"), EDMXControlConsoleEditorViewMode::Expanded);
		}
		MenuBuilder.EndSection();

		return MenuBuilder.MakeWidget();
	}

	TSharedRef<SWidget> FDMXControlConsoleEditorToolbar::GenerateSelectionMenuWidget()
	{
		const TSharedPtr<FDMXControlConsoleEditorToolkit> Toolkit = WeakToolkit.Pin();
		UDMXControlConsoleEditorData* EditorData = Toolkit.IsValid() ? Toolkit->GetControlConsoleEditorData() : nullptr;
		if (!ensureMsgf(EditorData, TEXT("Invalid control console editor data, can't generate control console toolbar correctly.")))
		{
			return SNullWidget::NullWidget;
		}

		constexpr bool bShouldCloseWindowAfterClosing = true;
		FMenuBuilder MenuBuilder(bShouldCloseWindowAfterClosing, nullptr);

		MenuBuilder.BeginSection("Fader Groups", LOCTEXT("FaderGroupsSelectionCategory", "Fader Groups"));
		{
			// Selection buttons menu entries
			const auto AddMenuEntryLambda = [&MenuBuilder, this](const FText& Label, bool bOnlyVisible = false)
				{
					MenuBuilder.AddMenuEntry
					(
						Label,
						FText::GetEmpty(),
						FSlateIcon(),
						FUIAction
						(
							FExecuteAction::CreateSP(this, &FDMXControlConsoleEditorToolbar::OnSelectAll, bOnlyVisible)
						),
						NAME_None,
						EUserInterfaceActionType::Button
					);
				};

			// Add buttons for handling selection actions
			AddMenuEntryLambda(LOCTEXT("EditorViewSelectAllButtonLabel", "Select All"));
			AddMenuEntryLambda(LOCTEXT("EditorViewSelectOnlyFilteredLabel", "Select Only Filtered"), true);

			// Selection toggle button menu entry
			MenuBuilder.AddMenuEntry
			(
				LOCTEXT("EditorViewAutoSelectLabel", "Auto-Select"),
				LOCTEXT("EditorViewAutoSelectToolTip", "Checked if activated Fader Groups must be automatically selected."),
				FSlateIcon(),
				FUIAction
				(
					FExecuteAction::CreateUObject(EditorData, &UDMXControlConsoleEditorData::ToggleAutoSelectActivePatches),
					FCanExecuteAction(),
					FIsActionChecked::CreateUObject(EditorData, &UDMXControlConsoleEditorData::GetAutoSelectActivePatches)
				),
				NAME_None,
				EUserInterfaceActionType::ToggleButton
			);
		}
		MenuBuilder.EndSection();

		return MenuBuilder.MakeWidget();
	}

	TSharedRef<SWidget> FDMXControlConsoleEditorToolbar::GenerateLayoutModeMenuWidget()
	{
		constexpr bool bShouldCloseWindowAfterClosing = false;
		FMenuBuilder MenuBuilder(bShouldCloseWindowAfterClosing, nullptr);

		MenuBuilder.BeginSection("Global", LOCTEXT("SortingModeCategory", "Global"));
		{
			const auto AddMenuEntryLambda = [&MenuBuilder, this](const FText& Label, EDMXControlConsoleLayoutMode LayoutMode)
				{
					MenuBuilder.AddMenuEntry
					(
						Label,
						FText::GetEmpty(),
						FSlateIcon(),
						FUIAction
						(
							FExecuteAction::CreateSP(this, &FDMXControlConsoleEditorToolbar::OnLayoutModeSelected, LayoutMode),
							FCanExecuteAction(),
							FIsActionChecked::CreateSP(this, &FDMXControlConsoleEditorToolbar::IsCurrentLayoutMode, LayoutMode)
						),
						NAME_None,
						EUserInterfaceActionType::RadioButton
					);
				};

			// Add buttons to select the layout mode
			AddMenuEntryLambda(LOCTEXT("HorizontalSortingModeRadioButtonLabel", "Horizontal"), EDMXControlConsoleLayoutMode::Horizontal);
			AddMenuEntryLambda(LOCTEXT("VerticalSortingModeRadioButtonLabel", "Vertical"), EDMXControlConsoleLayoutMode::Vertical);
			AddMenuEntryLambda(LOCTEXT("GridSortingModeRadioButtonLabel", "Grid"), EDMXControlConsoleLayoutMode::Grid);
		}
		MenuBuilder.EndSection();

		return MenuBuilder.MakeWidget();
	}

	void FDMXControlConsoleEditorToolbar::RestoreGlobalFilter()
	{
		const UDMXControlConsoleData* ControlConsoleData = WeakToolkit.IsValid() ? WeakToolkit.Pin()->GetControlConsoleData() : nullptr;
		if (ControlConsoleData && GlobalFilterSearchBox.IsValid())
		{
			const FString& FilterString = ControlConsoleData->FilterString;
			const FText FilterText = FText::FromString(FilterString);
			GlobalFilterSearchBox->SetText(FilterText);
		}
	}

	void FDMXControlConsoleEditorToolbar::OnSearchTextChanged(const FText& SearchText)
	{
		const TSharedPtr<FDMXControlConsoleEditorToolkit> Toolkit = WeakToolkit.Pin();
		if (!Toolkit.IsValid())
		{
			return;
		}

		UDMXControlConsoleEditorModel* EditorModel = Toolkit->GetControlConsoleEditorModel();
		const UDMXControlConsoleEditorData* EditorData = Toolkit->GetControlConsoleEditorData();
		if (!EditorModel || !EditorData)
		{
			return;
		}

		const TSharedRef<FDMXControlConsoleGlobalFilterModel> GlobalFilterModel = EditorModel->GetGlobalFilterModel();
		const FString SearchString = SearchText.ToString();
		GlobalFilterModel->SetGlobalFilter(SearchString);

		if (EditorData->GetAutoSelectFilteredElements() &&
			!SearchString.IsEmpty())
		{
			const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = EditorModel->GetSelectionHandler();
			constexpr bool bNotifySelection = false;
			SelectionHandler->ClearSelection(bNotifySelection);

			constexpr bool bSelectOnlyFiltered = true;
			SelectionHandler->SelectAll(bSelectOnlyFiltered);
		}
	}

	void FDMXControlConsoleEditorToolbar::OnSaveSearchButtonClicked(const FText& InSearchText)
	{
		/** If we already have a window, delete it */
		if (WeakCustomTextFilterWindow.IsValid())
		{
			WeakCustomTextFilterWindow.Pin()->RequestDestroyWindow();
		}

		const FText WindowTitle = LOCTEXT("CreateCustomTextFilterWindow", "Create Custom Filter");

		const TSharedRef<SWindow> NewTextFilterWindow = SNew(SWindow)
			.Title(WindowTitle)
			.HasCloseButton(true)
			.SupportsMaximize(false)
			.SupportsMinimize(false)
			.SizingRule(ESizingRule::FixedSize)
			.ClientSize(FVector2D(724, 183));

		FCustomTextFilterData TextFilterData;
		TextFilterData.FilterString = InSearchText;

		const TSharedRef<SCustomTextFilterDialog> CustomTextFilterDialog =
			SNew(SCustomTextFilterDialog)
			.FilterData(TextFilterData)
			.InEditMode(false)
			.OnCreateFilter(this, &FDMXControlConsoleEditorToolbar::OnCreateCustomTextFilter)
			.OnCancelClicked(this, &FDMXControlConsoleEditorToolbar::OnCancelCustomFilterWindowClicked);

		NewTextFilterWindow->SetContent(CustomTextFilterDialog);
		FSlateApplication::Get().AddWindow(NewTextFilterWindow);

		WeakCustomTextFilterWindow = NewTextFilterWindow;
	}

	void FDMXControlConsoleEditorToolbar::OnCreateCustomTextFilter(const FCustomTextFilterData& InFilterData, bool bApplyFilter)
	{
		const TSharedPtr<SWindow> CustomTextFilterWindow = WeakCustomTextFilterWindow.Pin();
		if (!CustomTextFilterWindow.IsValid())
		{
			return;
		}

		UDMXControlConsoleEditorData* EditorData = WeakToolkit.IsValid() ? WeakToolkit.Pin()->GetControlConsoleEditorData() : nullptr;
		if (EditorData)
		{
			EditorData->AddUserFilter(InFilterData.FilterLabel.ToString(), InFilterData.FilterString.ToString(), InFilterData.FilterColor, bApplyFilter);
		}

		CustomTextFilterWindow->RequestDestroyWindow();
	}

	void FDMXControlConsoleEditorToolbar::OnCancelCustomFilterWindowClicked()
	{
		if (WeakCustomTextFilterWindow.IsValid())
		{
			WeakCustomTextFilterWindow.Pin()->RequestDestroyWindow();
		}
	}

	void FDMXControlConsoleEditorToolbar::OnSelectedPortsChanged()
	{
		if (!WeakToolkit.IsValid() || !PortSelector.IsValid())
		{
			return;
		}

		UDMXControlConsoleData* ControlConsoleData = WeakToolkit.Pin()->GetControlConsoleData();
		if (!ControlConsoleData)
		{
			return;
		}

		const TArray<FDMXOutputPortSharedRef> SelectedOutputPorts = PortSelector->GetSelectedOutputPorts();
		ControlConsoleData->UpdateOutputPorts(SelectedOutputPorts);
	}

	ECheckBoxState FDMXControlConsoleEditorToolbar::IsFilteredElementsAutoSelectChecked() const
	{
		const UDMXControlConsoleEditorData* EditorData = WeakToolkit.IsValid() ? WeakToolkit.Pin()->GetControlConsoleEditorData() : nullptr;
		if (EditorData)
		{
			return EditorData->GetAutoSelectFilteredElements() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}

		return ECheckBoxState::Undetermined;
	}

	void FDMXControlConsoleEditorToolbar::OnFilteredElementsAutoSelectStateChanged(ECheckBoxState CheckBoxState)
	{
		UDMXControlConsoleEditorData* EditorData = WeakToolkit.IsValid() ? WeakToolkit.Pin()->GetControlConsoleEditorData() : nullptr;
		if (EditorData)
		{
			EditorData->ToggleAutoSelectFilteredElements();
		}
	}

	void FDMXControlConsoleEditorToolbar::OnFaderGroupsViewModeSelected(const EDMXControlConsoleEditorViewMode ViewMode) const
	{
		UDMXControlConsoleEditorData* EditorData = WeakToolkit.IsValid() ? WeakToolkit.Pin()->GetControlConsoleEditorData() : nullptr;
		if (EditorData)
		{
			EditorData->SetFaderGroupsViewMode(ViewMode);
		}
	}

	void FDMXControlConsoleEditorToolbar::OnFadersViewModeSelected(const EDMXControlConsoleEditorViewMode ViewMode) const
	{
		UDMXControlConsoleEditorData* EditorData = WeakToolkit.IsValid() ? WeakToolkit.Pin()->GetControlConsoleEditorData() : nullptr;
		if (EditorData)
		{
			EditorData->SetFadersViewMode(ViewMode);
		}
	}

	void FDMXControlConsoleEditorToolbar::OnLayoutModeSelected(const EDMXControlConsoleLayoutMode LayoutMode) const
	{
		if (!WeakToolkit.IsValid())
		{
			return;
		}

		const UDMXControlConsoleEditorLayouts* ControlConsoleLayouts = WeakToolkit.Pin()->GetControlConsoleLayouts();
		if (!ControlConsoleLayouts)
		{
			return;
		}

		if (UDMXControlConsoleEditorGlobalLayoutBase* CurrentLayout = ControlConsoleLayouts->GetActiveLayout())
		{
			const FScopedTransaction LayouModeSelectedTransaction(LOCTEXT("LayouModeSelectedTransaction", "Change Layout Mode"));
			CurrentLayout->PreEditChange(UDMXControlConsoleEditorGlobalLayoutBase::StaticClass()->FindPropertyByName(UDMXControlConsoleEditorGlobalLayoutBase::GetLayoutModePropertyName()));
			CurrentLayout->SetLayoutMode(LayoutMode);
			CurrentLayout->PostEditChange();
		}
	}

	bool FDMXControlConsoleEditorToolbar::IsCurrentLayoutMode(const EDMXControlConsoleLayoutMode LayoutMode) const
	{
		if (!WeakToolkit.IsValid())
		{
			return false;
		}

		const UDMXControlConsoleEditorLayouts* ControlConsoleLayouts = WeakToolkit.Pin()->GetControlConsoleLayouts();
		if (!ControlConsoleLayouts)
		{
			return false;
		}

		const UDMXControlConsoleEditorGlobalLayoutBase* CurrentLayout = ControlConsoleLayouts->GetActiveLayout();
		return CurrentLayout && CurrentLayout->GetLayoutMode() == LayoutMode;
	}

	void FDMXControlConsoleEditorToolbar::OnSelectAll(bool bOnlyMatchingFilter) const
	{
		UDMXControlConsoleEditorModel* EditorModel = WeakToolkit.IsValid() ? WeakToolkit.Pin()->GetControlConsoleEditorModel() : nullptr;
		if (!EditorModel)
		{
			return;
		}

		const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = EditorModel->GetSelectionHandler();
		SelectionHandler->SelectAll(bOnlyMatchingFilter);
	}

	void FDMXControlConsoleEditorToolbar::OnClearAll()
	{
		if (WeakToolkit.IsValid())
		{
			WeakToolkit.Pin()->ClearAll();
		}
	}

	FReply FDMXControlConsoleEditorToolbar::OnShowCompactEditorButtonClicked()
	{
		const TSharedPtr<FDMXControlConsoleEditorToolkit> Toolkit = WeakToolkit.Pin();
		UDMXControlConsole* ControlConsole = Toolkit.IsValid() ? Toolkit->GetControlConsole() : nullptr;
		if (ControlConsole)
		{
			Toolkit->ShowCompactEditor();
		}

		return FReply::Handled();
	}
}

#undef LOCTEXT_NAMESPACE
