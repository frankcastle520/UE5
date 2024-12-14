// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCommonEditorViewportToolbarBase.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/SToolTip.h"
#include "Styling/AppStyle.h"

#include "STransformViewportToolbar.h"
#include "SEditorViewport.h"
#include "EditorViewportCommands.h"
#include "SEditorViewportToolBarMenu.h"
#include "SEditorViewportToolBarButton.h"
#include "SEditorViewportViewMenu.h"
#include "Editor/EditorPerformanceSettings.h"
#include "Settings/EditorProjectSettings.h"
#include "Scalability.h"
#include "SceneView.h"
#include "SScalabilitySettings.h"
#include "SAssetEditorViewport.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "ShowFlagMenuCommands.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"

#define LOCTEXT_NAMESPACE "SCommonEditorViewportToolbarBase"

//////////////////////////////////////////////////////////////////////////
// SPreviewSceneProfileSelector

void SPreviewSceneProfileSelector::Construct(const FArguments& InArgs)
{
	PreviewProfileController = InArgs._PreviewProfileController;

	// clang-format off
	TSharedRef<SHorizontalBox> ButtonContent = 
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		.AutoWidth()
		.Padding(4.0f, 0.0f, 4.f, 0.0f)
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush("AssetEditor.PreviewSceneSettings"))
			.ColorAndOpacity(FSlateColor::UseForeground())
		]
		+SHorizontalBox::Slot()
		.Padding(0.0f, 0.0f, 4.f, 0.0f)
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Margin(FMargin(0))
			.Text_Lambda(
				[this]() -> FText
				{
					return FText::FromString(PreviewProfileController->GetActiveProfile());
				}
			)
		];
	// clang-format on

	// clang-format off
	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(AssetViewerProfileComboButton, SComboButton)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("EditorViewportToolBar.Button"))
			.ContentPadding(FMargin(0))
			.HasDownArrow(false)
			.OnGetMenuContent(this, &SPreviewSceneProfileSelector::BuildComboMenu)
			.ButtonContent()
			[
				ButtonContent
			]
		]
	];
	// clang-format on
}

TSharedRef<SWidget> SPreviewSceneProfileSelector::BuildComboMenu()
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	TSharedPtr<const FUICommandList> CommandList = nullptr;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, CommandList);

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("PreviewSceneProfilesSectionLabel", "Preview Scene Profiles"));

	int32 UnusedActiveIndex;
	const FName UnusedExtensionHook = NAME_None;
	const TArray<FString> PreviewProfiles = PreviewProfileController->GetPreviewProfiles(UnusedActiveIndex);
	for (const FString& ProfileName : PreviewProfiles)
	{
		MenuBuilder.AddMenuEntry(
			FText::FromString(ProfileName),
			FText(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[this, WeakController = PreviewProfileController.ToWeakPtr(), ProfileName]()
					{
						if (TSharedPtr<IPreviewProfileController> PinnedController = WeakController.Pin())
						{
							PinnedController->SetActiveProfile(ProfileName);
						}
					}
				),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda(
					[WeakController = PreviewProfileController.ToWeakPtr(), ProfileName]()
					{
						if (TSharedPtr<IPreviewProfileController> PinnedController = WeakController.Pin())
						{
							return ProfileName == PinnedController->GetActiveProfile();
						}

						return false;
					}
				)
			),
			UnusedExtensionHook,
			EUserInterfaceActionType::RadioButton
		);
	}

	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

//////////////////////////////////////////////////////////////////////////
// SCommonEditorViewportToolbarBase


void SCommonEditorViewportToolbarBase::Construct(const FArguments& InArgs, TSharedPtr<class ICommonEditorViewportToolbarInfoProvider> InInfoProvider)
{
	InfoProviderPtr = InInfoProvider;

	TSharedRef<SEditorViewport> ViewportRef = GetInfoProvider().GetViewportWidget();
	TSharedPtr<SHorizontalBox> MainBoxPtr;

	const FMargin ToolbarSlotPadding(4.0f, 1.0f);
	const FMargin ToolbarButtonPadding(4.0f, 0.0f);

	ChildSlot
	[
		SNew( SBorder )
		.BorderImage(FAppStyle::Get().GetBrush("EditorViewportToolBar.Background"))
		.Cursor(EMouseCursor::Default)
		[
			SAssignNew( MainBoxPtr, SHorizontalBox )
		]
	];

	// Options menu
	MainBoxPtr->AddSlot()
		.AutoWidth()
		.Padding(ToolbarSlotPadding)
		[
			SNew(SEditorViewportToolbarMenu)
			.ParentToolBar(SharedThis(this))
			.Cursor(EMouseCursor::Default)
			.Image("EditorViewportToolBar.OptionsDropdown")
			.OnGetMenuContent(this, &SCommonEditorViewportToolbarBase::GenerateOptionsMenu)
		];

	// Camera mode menu
	MainBoxPtr->AddSlot()
		.AutoWidth()
		.Padding(ToolbarSlotPadding)
		[
			SNew(SEditorViewportToolbarMenu)
			.ParentToolBar(SharedThis(this))
			.Cursor(EMouseCursor::Default)
			.Label(this, &SCommonEditorViewportToolbarBase::GetCameraMenuLabel)
			.OnGetMenuContent(this, &SCommonEditorViewportToolbarBase::GenerateCameraMenu)
		];

	// View menu
	MainBoxPtr->AddSlot()
		.AutoWidth()
		.Padding(ToolbarSlotPadding)
		[
			MakeViewMenu()
		];

	// Show menu
	MainBoxPtr->AddSlot()
		.AutoWidth()
		.Padding(ToolbarSlotPadding)
		[
			SNew(SEditorViewportToolbarMenu)
			.Label(LOCTEXT("ShowMenuTitle", "Show"))
			.Cursor(EMouseCursor::Default)
			.ParentToolBar(SharedThis(this))
			.OnGetMenuContent(this, &SCommonEditorViewportToolbarBase::GenerateShowMenu)
		];

	// Profile menu (Controls the Preview Scene Settings)
	if (InArgs._PreviewProfileController)
	{
		MainBoxPtr->AddSlot()
			.AutoWidth()
			.Padding(ToolbarSlotPadding)
			[
				SNew(SPreviewSceneProfileSelector).PreviewProfileController(InArgs._PreviewProfileController)
			];
	}

	// Realtime button
	if (InArgs._AddRealtimeButton)
	{
		MainBoxPtr->AddSlot()
			.AutoWidth()
			.Padding(ToolbarSlotPadding)
			[
				SNew(SEditorViewportToolBarButton)
				.Cursor(EMouseCursor::Default)
				.ButtonType(EUserInterfaceActionType::Button)
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("EditorViewportToolBar.WarningButton"))
				.OnClicked(this, &SCommonEditorViewportToolbarBase::OnRealtimeWarningClicked)
				.Visibility(this, &SCommonEditorViewportToolbarBase::GetRealtimeWarningVisibility)
				.ToolTipText(LOCTEXT("RealtimeOff_ToolTip", "This viewport is not updating in realtime.  Click to turn on realtime mode."))
				.Content()
				[
					SNew(STextBlock)
					.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"))
					.Text(LOCTEXT("RealtimeOff", "Realtime Off"))
				]
			];
	}

	MainBoxPtr->AddSlot()
		.AutoWidth()
		.Padding(ToolbarSlotPadding)
		[
			SNew(SEditorViewportToolbarMenu)
			.Label(LOCTEXT("ViewParamMenuTitle", "View Mode Options"))
			.Cursor(EMouseCursor::Default)
			.ParentToolBar(SharedThis(this))
			.Visibility(this, &SCommonEditorViewportToolbarBase::GetViewModeOptionsVisibility)
			.OnGetMenuContent(this, &SCommonEditorViewportToolbarBase::GenerateViewModeOptionsMenu)
		];

	MainBoxPtr->AddSlot()
		.AutoWidth()
		.Padding(ToolbarSlotPadding)
		[
			// Button to show scalability warnings
			SNew(SEditorViewportToolbarMenu)
			.ParentToolBar(SharedThis(this))
			.Label(this, &SCommonEditorViewportToolbarBase::GetScalabilityWarningLabel)
			.MenuStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("EditorViewportToolBar.WarningButton"))
			.OnGetMenuContent(this, &SCommonEditorViewportToolbarBase::GetScalabilityWarningMenuContent)
			.Visibility(this, &SCommonEditorViewportToolbarBase::GetScalabilityWarningVisibility)
			.ToolTipText(LOCTEXT("ScalabilityWarning_ToolTip", "Non-default scalability settings could be affecting what is shown in this viewport.\nFor example you may experience lower visual quality, reduced particle counts, and other artifacts that don't match what the scene would look like when running outside of the editor. Click to make changes."))
		];

	// Add optional toolbar slots to be added by child classes inherited from this common viewport toolbar
	ExtendLeftAlignedToolbarSlots(MainBoxPtr, SharedThis(this));

	// Transform toolbar
	MainBoxPtr->AddSlot()
		.Padding(ToolbarSlotPadding)
		.HAlign(HAlign_Right)
		[
			SNew(STransformViewportToolBar)
			.Viewport(ViewportRef)
			.CommandList(ViewportRef->GetCommandList())
			.Extenders(GetInfoProvider().GetExtenders())
			.Visibility(ViewportRef, &SEditorViewport::GetTransformToolbarVisibility)
		];

	SViewportToolBar::Construct(SViewportToolBar::FArguments());
}

void SCommonEditorViewportToolbarBase::ConstructScreenPercentageMenu(FMenuBuilder& MenuBuilder, FEditorViewportClient* InViewportClient)
{
	FEditorViewportClient& ViewportClient = *InViewportClient;

	FMargin CommonPadding(26.0f, 3.0f);

	const int32 PreviewScreenPercentageMin = ISceneViewFamilyScreenPercentage::kMinTSRResolutionFraction * 100.0f;
	const int32 PreviewScreenPercentageMax = ISceneViewFamilyScreenPercentage::kMaxTSRResolutionFraction * 100.0f;

	const FEditorViewportCommands& BaseViewportCommands = FEditorViewportCommands::Get();

	MenuBuilder.BeginSection("Summary", LOCTEXT("Summary", "Summary"));
	{
		MenuBuilder.AddWidget(
			UE::UnrealEd::CreateCurrentPercentageWidget(ViewportClient),
			FText::GetEmpty()
		);

		MenuBuilder.AddWidget(
			UE::UnrealEd::CreateResolutionsWidget(ViewportClient),
			FText::GetEmpty()
		);
		MenuBuilder.AddWidget(
			UE::UnrealEd::CreateActiveViewportWidget(ViewportClient),
			FText::GetEmpty()
		);
		MenuBuilder.AddWidget(
			UE::UnrealEd::CreateSetFromWidget(ViewportClient),
			FText::GetEmpty()
		);
		MenuBuilder.AddWidget(
			UE::UnrealEd::CreateCurrentScreenPercentageSettingWidget(ViewportClient),
			FText::GetEmpty()
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("ScreenPercentage", LOCTEXT("ScreenPercentage_ViewportOverride", "Viewport Override"));
	{
		MenuBuilder.AddMenuEntry(BaseViewportCommands.ToggleOverrideViewportScreenPercentage);
		MenuBuilder.AddWidget(
			UE::UnrealEd::CreateCurrentScreenPercentageWidget(ViewportClient),
			LOCTEXT("ScreenPercentage", "Screen Percentage")
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("ScreenPercentageSettings", LOCTEXT("ScreenPercentage_ViewportSettings", "Viewport Settings"));
	{
		MenuBuilder.AddMenuEntry(BaseViewportCommands.OpenEditorPerformanceProjectSettings,
			/* InExtensionHook = */ NAME_None,
			/* InLabelOverride = */ TAttribute<FText>(),
			/* InToolTipOverride = */ TAttribute<FText>(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "ProjectSettings.TabIcon"));
		MenuBuilder.AddMenuEntry(BaseViewportCommands.OpenEditorPerformanceEditorPreferences,
			/* InExtensionHook = */ NAME_None,
			/* InLabelOverride = */ TAttribute<FText>(),
			/* InToolTipOverride = */ TAttribute<FText>(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorPreferences.TabIcon"));
	}
	MenuBuilder.EndSection();
}

FText SCommonEditorViewportToolbarBase::GetCameraMenuLabel() const
{
	return UE::UnrealEd::GetCameraSubmenuLabelFromViewportType(GetViewportClient().GetViewportType());
}


EVisibility SCommonEditorViewportToolbarBase::GetViewModeOptionsVisibility() const
{
	const FEditorViewportClient& ViewClient = GetViewportClient();
	if (ViewClient.GetViewMode() == VMI_MeshUVDensityAccuracy || ViewClient.GetViewMode() == VMI_MaterialTextureScaleAccuracy || ViewClient.GetViewMode() == VMI_RequiredTextureResolution)
	{
		return EVisibility::SelfHitTestInvisible;
	}
	else
	{
		return EVisibility::Collapsed;
	}
}

TSharedRef<SWidget> SCommonEditorViewportToolbarBase::GenerateViewModeOptionsMenu() const
{
	GetInfoProvider().OnFloatingButtonClicked();
	TSharedRef<SEditorViewport> ViewportRef = GetInfoProvider().GetViewportWidget();
	FEditorViewportClient& ViewClient = GetViewportClient();
	const UWorld* World = ViewClient.GetWorld();
	return BuildViewModeOptionsMenu(ViewportRef->GetCommandList(), ViewClient.GetViewMode(), World ? World->GetFeatureLevel() : GMaxRHIFeatureLevel, ViewClient.GetViewModeParamNameMap());
}


TSharedRef<SWidget> SCommonEditorViewportToolbarBase::GenerateOptionsMenu() const
{
	GetInfoProvider().OnFloatingButtonClicked();
	TSharedRef<SEditorViewport> ViewportRef = GetInfoProvider().GetViewportWidget();

	const bool bIsPerspective = GetViewportClient().GetViewportType() == LVT_Perspective;
	
	const bool bInShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder OptionsMenuBuilder(bInShouldCloseWindowAfterMenuSelection, ViewportRef->GetCommandList());
	{
		OptionsMenuBuilder.BeginSection("LevelViewportViewportOptions", LOCTEXT("OptionsMenuHeader", "Viewport Options") );
		{
			OptionsMenuBuilder.AddMenuEntry( FEditorViewportCommands::Get().ToggleRealTime );
			OptionsMenuBuilder.AddMenuEntry( FEditorViewportCommands::Get().ToggleStats );
			OptionsMenuBuilder.AddMenuEntry( FEditorViewportCommands::Get().ToggleFPS );

			if (bIsPerspective)
			{
				OptionsMenuBuilder.AddWidget( UE::UnrealEd::CreateFOVMenuWidget(ViewportRef), LOCTEXT("FOVAngle", "Field of View (H)") );
				OptionsMenuBuilder.AddWidget( UE::UnrealEd::CreateFarViewPlaneMenuWidget(ViewportRef), LOCTEXT("FarViewPlane", "Far View Plane") );
			}

			OptionsMenuBuilder.AddSubMenu(
				LOCTEXT("ScreenPercentageSubMenu", "Screen Percentage"),
				LOCTEXT("ScreenPercentageSubMenu_ToolTip", "Customize the viewport's screen percentage"),
				FNewMenuDelegate::CreateStatic(&SCommonEditorViewportToolbarBase::ConstructScreenPercentageMenu, &GetViewportClient()));
		}
		OptionsMenuBuilder.EndSection();

 		TSharedPtr<SAssetEditorViewport> AssetEditorViewportPtr = StaticCastSharedRef<SAssetEditorViewport>(ViewportRef);
 		if (AssetEditorViewportPtr.IsValid())
		{
			OptionsMenuBuilder.BeginSection("EditorViewportLayouts");
			{
				OptionsMenuBuilder.AddSubMenu(
					LOCTEXT("ConfigsSubMenu", "Layouts"),
					FText::GetEmpty(),
					FNewMenuDelegate::CreateSP(AssetEditorViewportPtr.Get(), &SAssetEditorViewport::GenerateLayoutMenu));
			}
			OptionsMenuBuilder.EndSection();
		}

		ExtendOptionsMenu(OptionsMenuBuilder);
	}

	return OptionsMenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SCommonEditorViewportToolbarBase::GenerateCameraMenu() const
{
	GetInfoProvider().OnFloatingButtonClicked();
	TSharedRef<SEditorViewport> ViewportRef = GetInfoProvider().GetViewportWidget();

	return UE::UnrealEd::CreateCameraMenuWidget(ViewportRef);
}

TSharedRef<SWidget> SCommonEditorViewportToolbarBase::GenerateShowMenu() const
{
	GetInfoProvider().OnFloatingButtonClicked();

	static const FName MenuName("ViewportToolbarBase.Show");
	if (!UToolMenus::Get()->IsMenuRegistered(MenuName))
	{
		UToolMenu* ShowMenu = UToolMenus::Get()->RegisterMenu(MenuName);
		ShowMenu->AddDynamicSection("Flags", FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
		{
			if (UCommonViewportToolbarBaseMenuContext* ContextObject = InMenu->FindContext<UCommonViewportToolbarBaseMenuContext>())
			{
				if (TSharedPtr<const SCommonEditorViewportToolbarBase> ToolbarWidgetPin = ContextObject->ToolbarWidget.Pin())
				{
					ToolbarWidgetPin->FillShowFlagsMenu(InMenu);
				}
			}
		}));
	}

	FToolMenuContext NewMenuContext;
	UCommonViewportToolbarBaseMenuContext* ContextObject = NewObject<UCommonViewportToolbarBaseMenuContext>();
	ContextObject->ToolbarWidget = SharedThis(this);
	NewMenuContext.AddObject(ContextObject);
	if (TSharedPtr<SEditorViewport> ViewportWidget = GetInfoProvider().GetViewportWidget())
	{
		NewMenuContext.AppendCommandList(GetInfoProvider().GetViewportWidget()->GetCommandList());
	}
	return UToolMenus::Get()->GenerateWidget(MenuName, NewMenuContext);
}

void SCommonEditorViewportToolbarBase::FillShowFlagsMenu(UToolMenu* InMenu) const
{
	FShowFlagMenuCommands::Get().BuildShowFlagsMenu(InMenu);
}

TSharedRef<SWidget> SCommonEditorViewportToolbarBase::GenerateFOVMenu() const
{
	const float FOVMin = 5.f;
	const float FOVMax = 170.f;

	return
		SNew( SBox )
		.HAlign( HAlign_Right )
		[
			SNew( SBox )
			.Padding( FMargin(4.0f, 0.0f, 0.0f, 0.0f) )
			.WidthOverride( 100.0f )
			[
				SNew ( SBorder )
				.BorderImage(FAppStyle::Get().GetBrush("Menu.WidgetBorder"))
				.Padding(FMargin(1.0f))
				[
					SNew(SSpinBox<float>)
					.Style(&FAppStyle::Get(), "Menu.SpinBox")
					.Font( FAppStyle::GetFontStyle( TEXT( "MenuItem.Font" ) ) )
					.MinValue(FOVMin)
					.MaxValue(FOVMax)
					.Value(this, &SCommonEditorViewportToolbarBase::OnGetFOVValue)
					.OnValueChanged(this, &SCommonEditorViewportToolbarBase::OnFOVValueChanged)
				]
			]
		];
}

float SCommonEditorViewportToolbarBase::OnGetFOVValue() const
{
	return GetViewportClient().ViewFOV;
}

void SCommonEditorViewportToolbarBase::OnFOVValueChanged(float NewValue) const
{
	FEditorViewportClient& ViewportClient = GetViewportClient();
	ViewportClient.FOVAngle = NewValue;
	ViewportClient.ViewFOV = NewValue;
	ViewportClient.Invalidate();
}

TSharedRef<SWidget> SCommonEditorViewportToolbarBase::GenerateFarViewPlaneMenu() const
{
	return
		SNew(SBox)
		.HAlign(HAlign_Right)
		[
			SNew(SBox)
			.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
			.WidthOverride(100.0f)
			[
				SNew ( SBorder )
				.BorderImage(FAppStyle::Get().GetBrush("Menu.WidgetBorder"))
				.Padding(FMargin(1.0f))
				[
					SNew(SSpinBox<float>)
					.Style(&FAppStyle::Get(), "Menu.SpinBox")
					.ToolTipText(LOCTEXT("FarViewPlaneTooltip", "Distance to use as the far view plane, or zero to enable an infinite far view plane"))
					.MinValue(0.0f)
					.MaxValue(100000.0f)
					.Font(FAppStyle::GetFontStyle(TEXT("MenuItem.Font")))
					.Value(this, &SCommonEditorViewportToolbarBase::OnGetFarViewPlaneValue)
					.OnValueChanged(const_cast<SCommonEditorViewportToolbarBase*>(this), &SCommonEditorViewportToolbarBase::OnFarViewPlaneValueChanged)
				]
			]
		];
}

float SCommonEditorViewportToolbarBase::OnGetFarViewPlaneValue() const
{
	return GetViewportClient().GetFarClipPlaneOverride();
}

void SCommonEditorViewportToolbarBase::OnFarViewPlaneValueChanged(float NewValue)
{
	FEditorViewportClient& ViewportClient = GetViewportClient();
	ViewportClient.OverrideFarClipPlane(NewValue);
	ViewportClient.Invalidate();
}

FReply SCommonEditorViewportToolbarBase::OnRealtimeWarningClicked()
{
	FEditorViewportClient& ViewportClient = GetViewportClient();
	ViewportClient.SetRealtime(true);

	return FReply::Handled();
}

EVisibility SCommonEditorViewportToolbarBase::GetRealtimeWarningVisibility() const
{
	FEditorViewportClient& ViewportClient = GetViewportClient();
	// If the viewport is not realtime and there is no override then realtime is off
	return !ViewportClient.IsRealtime() && !ViewportClient.IsRealtimeOverrideSet() ? EVisibility::Visible : EVisibility::Collapsed;
}

TSharedPtr<FExtender> SCommonEditorViewportToolbarBase::GetCombinedExtenderList(TSharedRef<FExtender> MenuExtender) const
{
	TSharedPtr<FExtender> HostEditorExtenders = GetInfoProvider().GetExtenders();

	TArray<TSharedPtr<FExtender>> Extenders;
	Extenders.Reserve(2);
	Extenders.Add(HostEditorExtenders);
	Extenders.Add(MenuExtender);

	return FExtender::Combine(Extenders);
}

TSharedPtr<FExtender> SCommonEditorViewportToolbarBase::GetViewMenuExtender() const
{
	TSharedRef<FExtender> ViewModeExtender(new FExtender());
	ViewModeExtender->AddMenuExtension(
		TEXT("ViewMode"),
		EExtensionHook::After,
		GetInfoProvider().GetViewportWidget()->GetCommandList(),
		FMenuExtensionDelegate::CreateSP(const_cast<SCommonEditorViewportToolbarBase*>(this), &SCommonEditorViewportToolbarBase::CreateViewMenuExtensions));

	return GetCombinedExtenderList(ViewModeExtender);
}

void SCommonEditorViewportToolbarBase::CreateViewMenuExtensions(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("LevelViewportDeferredRendering", LOCTEXT("DeferredRenderingHeader", "Deferred Rendering") );
	MenuBuilder.EndSection();

//FINDME
// 	MenuBuilder.BeginSection("LevelViewportLandscape", LOCTEXT("LandscapeHeader", "Landscape") );
// 	{
// 		MenuBuilder.AddSubMenu(LOCTEXT("LandscapeLODDisplayName", "LOD"), LOCTEXT("LandscapeLODMenu_ToolTip", "Override Landscape LOD in this viewport"), FNewMenuDelegate::CreateStatic(&Local::BuildLandscapeLODMenu, this), /*Default*/false, FSlateIcon());
// 	}
// 	MenuBuilder.EndSection();
}

ICommonEditorViewportToolbarInfoProvider& SCommonEditorViewportToolbarBase::GetInfoProvider() const
{
	return *InfoProviderPtr.Pin().Get();
}

FEditorViewportClient& SCommonEditorViewportToolbarBase::GetViewportClient() const
{
	return *GetInfoProvider().GetViewportWidget()->GetViewportClient().Get();
}

TSharedRef<SEditorViewportViewMenu> SCommonEditorViewportToolbarBase::MakeViewMenu()
{
	TSharedRef<SEditorViewport> ViewportRef = GetInfoProvider().GetViewportWidget();

	return SNew(SEditorViewportViewMenu, ViewportRef, SharedThis(this))
		.Cursor(EMouseCursor::Default)
		.MenuExtenders(GetViewMenuExtender());
}

FText SCommonEditorViewportToolbarBase::GetScalabilityWarningLabel() const
{
	const int32 QualityLevel = Scalability::GetQualityLevels().GetMinQualityLevel();
	if (QualityLevel >= 0)
	{
		return FText::Format(LOCTEXT("ScalabilityWarning", "Scalability: {0}"), Scalability::GetScalabilityNameFromQualityLevel(QualityLevel));
	}

	return FText::GetEmpty();
}

EVisibility SCommonEditorViewportToolbarBase::GetScalabilityWarningVisibility() const
{
	//This method returns magic numbers. 3 means epic
	return GetDefault<UEditorPerformanceSettings>()->bEnableScalabilityWarningIndicator && GetShowScalabilityMenu() && Scalability::GetQualityLevels().GetMinQualityLevel() != 3 ? EVisibility::Visible : EVisibility::Collapsed;
}

TSharedRef<SWidget> SCommonEditorViewportToolbarBase::GetScalabilityWarningMenuContent() const
{
	return
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Menu.Background"))
		[
			SNew(SScalabilitySettings)
		];
}

#undef LOCTEXT_NAMESPACE
