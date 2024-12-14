// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLevelViewportToolBar.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SceneView.h"
#include "Subsystems/PanelExtensionSubsystem.h"
#include "ToolMenus.h"
#include "LevelEditorMenuContext.h"
#include "ViewportToolBarContext.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Styling/AppStyle.h"
#include "Camera/CameraActor.h"
#include "Misc/ConfigCacheIni.h"
#include "GameFramework/ActorPrimitiveColorHandler.h"
#include "GameFramework/WorldSettings.h"
#include "EngineUtils.h"
#include "LevelEditor.h"
#include "STransformViewportToolbar.h"
#include "EditorShowFlags.h"
#include "LevelViewportActions.h"
#include "LevelEditorViewport.h"
#include "Layers/LayersSubsystem.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "IDeviceProfileServicesModule.h"
#include "EditorViewportCommands.h"
#include "SEditorViewportToolBarMenu.h"
#include "SEditorViewportToolBarButton.h"
#include "SEditorViewportViewMenu.h"
#include "Stats/StatsData.h"
#include "BufferVisualizationData.h"
#include "NaniteVisualizationData.h"
#include "LumenVisualizationData.h"
#include "SubstrateVisualizationData.h"
#include "GroomVisualizationData.h"
#include "VirtualShadowMapVisualizationData.h"
#include "FoliageType.h"
#include "ShowFlagMenuCommands.h"
#include "Bookmarks/BookmarkUI.h"
#include "Scalability.h"
#include "SScalabilitySettings.h"
#include "Editor/EditorPerformanceSettings.h"
#include "SEditorViewportViewMenuContext.h"
#include "Bookmarks/IBookmarkTypeTools.h"
#include "ToolMenu.h"
#include "WorldPartition/IWorldPartitionEditorModule.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartition.h"
#include "SLevelViewport.h"
#include "SortHelper.h"
#include "Interfaces/IMainFrameModule.h"
#include "SCommonEditorViewportToolbarBase.h"
#include "SActionableMessageViewportWidget.h"
#include "Engine/SceneCapture.h"
#include "ViewportToolbar/LevelEditorViewportToolbarSections.h"
#include "ViewportToolbar/LevelViewportContext.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"

#define LOCTEXT_NAMESPACE "LevelViewportToolBar"

/** Override the view menu, just so we can specify the level viewport as active when the button is clicked */
class SLevelEditorViewportViewMenu : public SEditorViewportViewMenu
{
public:

	void Construct(const FArguments& InArgs, TSharedRef<SEditorViewport> InViewport, TSharedRef<class SViewportToolBar> InParentToolBar)
	{
		SEditorViewportViewMenu::Construct(InArgs, InViewport, InParentToolBar);
		MenuName = FName("LevelEditor.LevelViewportToolBar.View");
	}

	virtual void RegisterMenus() const override
	{
		SEditorViewportViewMenu::RegisterMenus();

		// Use a static bool to track whether or not this menu is registered. Bool instead of checking the registered
		// state with ToolMenus because we want the new viewport toolbar to be able to create this menu without breaking
		// this code. Static because this code can be called multiple times using different instances of this class.
		static bool bDidRegisterMenu = false;
		if (!bDidRegisterMenu)
		{
			bDidRegisterMenu = true;

			// Don't warn here to avoid warnings if the new viewport toolbar already has created an empty version
			// of this menu.
			UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(
				"LevelEditor.LevelViewportToolBar.View", "UnrealEd.ViewportToolbar.View", EMultiBoxType::Menu, false
			);
			Menu->AddDynamicSection("LevelSection", FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu) {
				if (UEditorViewportViewMenuContext* Context = InMenu->FindContext<UEditorViewportViewMenuContext>())
				{
					TSharedPtr<SLevelViewportToolBar> LevelViewportToolBar = StaticCastSharedPtr<SLevelViewportToolBar>(
						Context->EditorViewportViewMenu.Pin()->GetParentToolBar().Pin());
					LevelViewportToolBar->FillViewMenu(InMenu);
				}
			}));
		}
	}

	virtual TSharedRef<SWidget> GenerateViewMenuContent() const override
	{
		SLevelViewport* LevelViewport = static_cast<SLevelViewport*>(Viewport.Pin().Get());
		LevelViewport->OnFloatingButtonClicked();
		
		return SEditorViewportViewMenu::GenerateViewMenuContent();
	}
};

void SLevelViewportToolBar::Construct( const FArguments& InArgs )
{
	Viewport = InArgs._Viewport;
	TSharedRef<SLevelViewport> ViewportRef = Viewport.Pin().ToSharedRef();

	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));

	UViewportToolBarContext* ExtensionContextObject = NewObject<UViewportToolBarContext>();
	ExtensionContextObject->ViewportToolBar = SharedThis(this);
	ExtensionContextObject->Viewport = Viewport;

	const FMargin ToolbarSlotPadding(4.0f, 1.0f);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("EditorViewportToolBar.Background"))
		.Visibility(EVisibility::SelfHitTestInvisible)
		[
			SNew( SVerticalBox )
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(ToolbarSlotPadding)
			[
				SNew( SHorizontalBox )
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(ToolbarSlotPadding)
				[
					SNew( SEditorViewportToolbarMenu )
					.ParentToolBar( SharedThis( this ) )
					.Visibility(Viewport.Pin().Get(), &SLevelViewport::GetToolbarVisibility)
					.Image("EditorViewportToolBar.OptionsDropdown")
					.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("EditorViewportToolBar.MenuDropdown")))
					.OnGetMenuContent( this, &SLevelViewportToolBar::GenerateOptionsMenu )
				]
				+ SHorizontalBox::Slot()
				[
					SNew( SHorizontalBox )
					.Visibility(Viewport.Pin().Get(), &SLevelViewport::GetFullToolbarVisibility)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding( ToolbarSlotPadding )
					[
						SNew( SEditorViewportToolbarMenu )
						.ParentToolBar( SharedThis( this ) )
						.Label( this, &SLevelViewportToolBar::GetCameraMenuLabel )
						.LabelIcon( this, &SLevelViewportToolBar::GetCameraMenuLabelIcon )
						.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("EditorViewportToolBar.CameraMenu")))
						.OnGetMenuContent( this, &SLevelViewportToolBar::GenerateCameraMenu ) 
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding( ToolbarSlotPadding )
					[
						SNew( SLevelEditorViewportViewMenu, ViewportRef, SharedThis(this) )
						.MenuExtenders(UE::LevelEditor::GetViewModesLegacyExtenders())
						.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("ViewMenuButton")))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding( ToolbarSlotPadding )
					[
						SNew( SEditorViewportToolbarMenu )
						.Label( LOCTEXT("ShowMenuTitle", "Show") )
						.ParentToolBar( SharedThis( this ) )
						.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("EditorViewportToolBar.ShowMenu")))
						.OnGetMenuContent( this, &SLevelViewportToolBar::GenerateShowMenu ) 
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding( ToolbarSlotPadding )
					[
						SNew( SEditorViewportToolbarMenu )
						.Label( this, &SLevelViewportToolBar::GetViewModeOptionsMenuLabel )
						.ParentToolBar( SharedThis( this ) )
						.Visibility( this, &SLevelViewportToolBar::GetViewModeOptionsVisibility )
						.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("EditorViewportToolBar.ViewModeOptions")))
						.OnGetMenuContent( this, &SLevelViewportToolBar::GenerateViewModeOptionsMenu ) 
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding( ToolbarSlotPadding )
					[
						SNew( SEditorViewportToolbarMenu )
						.ParentToolBar( SharedThis( this ) )
						.Label( this, &SLevelViewportToolBar::GetDevicePreviewMenuLabel )
						.LabelIcon( this, &SLevelViewportToolBar::GetDevicePreviewMenuLabelIcon )
						.OnGetMenuContent( this, &SLevelViewportToolBar::GenerateDevicePreviewMenu )
						//@todo rendering: mobile preview in view port is not functional yet - remove this once it is.
						.Visibility(EVisibility::Collapsed)
					]
					+ SHorizontalBox::Slot()
					.Padding(ToolbarSlotPadding)
					.AutoWidth()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Fill)
					[
						SNew(SExtensionPanel)
						.ExtensionPanelID("LevelViewportToolBar.LeftExtension")
						.ExtensionContext(ExtensionContextObject)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(ToolbarSlotPadding)
					[
						// Button to show that realtime is off
						SNew(SEditorViewportToolBarButton)	
						.ButtonType(EUserInterfaceActionType::Button)
						.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("EditorViewportToolBar.WarningButton"))
						.OnClicked(this, &SLevelViewportToolBar::OnRealtimeWarningClicked)
						.Visibility(this, &SLevelViewportToolBar::GetRealtimeWarningVisibility)
						.ToolTipText(LOCTEXT("RealtimeOff_ToolTip", "This viewport is not updating in realtime.  Click to turn on realtime mode."))
						.Content()
						[
							SNew(STextBlock)
							.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"))
							.Text(LOCTEXT("RealtimeOff", "Realtime Off"))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(ToolbarSlotPadding)
					[
						// Button to show scalability warnings
						SNew(SEditorViewportToolbarMenu)
						.ParentToolBar(SharedThis(this))
						.Label(this, &SLevelViewportToolBar::GetScalabilityWarningLabel)
						.MenuStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("EditorViewportToolBar.WarningButton"))
						.OnGetMenuContent(this, &SLevelViewportToolBar::GetScalabilityWarningMenuContent)
						.Visibility(this, &SLevelViewportToolBar::GetScalabilityWarningVisibility)
						.ToolTipText(LOCTEXT("ScalabilityWarning_ToolTip", "Non-default scalability settings could be affecting what is shown in this viewport.\nFor example you may experience lower visual quality, reduced particle counts, and other artifacts that don't match what the scene would look like when running outside of the editor. Click to make changes."))
					]
					+ SHorizontalBox::Slot()
					.Padding(ToolbarSlotPadding)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Fill)
					[
						SNew(SExtensionPanel)
						.ExtensionPanelID("LevelViewportToolBar.MiddleExtension")
						.ExtensionContext(ExtensionContextObject)
					]
					+ SHorizontalBox::Slot()
					.Padding(ToolbarSlotPadding)
					.AutoWidth()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Fill)
					[
						SNew(SExtensionPanel)
						.ExtensionPanelID("LevelViewportToolBar.RightExtension")
						.ExtensionContext(ExtensionContextObject)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.MaxWidth(TAttribute<float>::CreateSP(this, &SLevelViewportToolBar::GetTransformToolbarWidth))
					.Padding(ToolbarSlotPadding)
					.HAlign(HAlign_Right)
					[
						SAssignNew(TransformToolbar, STransformViewportToolBar)
						.Viewport(ViewportRef)
						.CommandList(ViewportRef->GetCommandList())
						.Extenders(LevelEditorModule.GetToolBarExtensibilityManager()->GetAllExtenders())
						.Visibility(ViewportRef, &SLevelViewport::GetTransformToolbarVisibility)
					]
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Right)
					.AutoWidth()
					.Padding(ToolbarSlotPadding)
					[
						//The Maximize/Minimize button is only displayed when not in Immersive mode.
						SNew(SEditorViewportToolBarButton)
						.ButtonType(EUserInterfaceActionType::ToggleButton)
						.CheckBoxStyle(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("EditorViewportToolBar.MaximizeRestoreButton"))
						.IsChecked(ViewportRef, &SLevelViewport::IsMaximized)
						.OnClicked(ViewportRef, &SLevelViewport::OnToggleMaximize)
						.Visibility(ViewportRef, &SLevelViewport::GetMaximizeToggleVisibility)
						.Image("EditorViewportToolBar.Maximize")
						.ToolTipText(LOCTEXT("Maximize_ToolTip", "Maximizes or restores this viewport"))
					]
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Right)
					.AutoWidth()
					.Padding(ToolbarSlotPadding)
					[
						//The Restore from Immersive' button is only displayed when the editor is in Immersive mode.
						SNew(SEditorViewportToolBarButton)
						.ButtonType(EUserInterfaceActionType::Button)
						.OnClicked(ViewportRef, &SLevelViewport::OnToggleMaximize)
						.Visibility(ViewportRef, &SLevelViewport::GetCloseImmersiveButtonVisibility)
						.Image("EditorViewportToolBar.RestoreFromImmersive.Normal")
						.ToolTipText(LOCTEXT("RestoreFromImmersive_ToolTip", "Restore from Immersive"))
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 8.f, 10.f, 0.f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("ActionableMessage.Border"))
				[
					SAssignNew(ActionableMessageViewportWidget, SActionableMessageViewportWidget)
					.Visibility_Lambda([this]()
					{
						return ActionableMessageViewportWidget->GetVisibility();
					})
				]
			]
		]
	];
	
	SViewportToolBar::Construct(SViewportToolBar::FArguments());
}

bool SLevelViewportToolBar::IsViewModeSupported(EViewModeIndex ViewModeIndex) const
{
	return true;
}

FLevelEditorViewportClient* SLevelViewportToolBar::GetLevelViewportClient() const
{
	if (Viewport.IsValid())
	{
		return &Viewport.Pin()->GetLevelViewportClient();
	}

	return nullptr;
}

FText SLevelViewportToolBar::GetCameraMenuLabel() const
{
	TSharedPtr< SLevelViewport > PinnedViewport( Viewport.Pin() );
	if( PinnedViewport.IsValid() )
	{
		return UE::UnrealEd::GetCameraSubmenuLabelFromViewportType(PinnedViewport->GetLevelViewportClient().ViewportType);
	}

	return LOCTEXT("CameraMenuTitle_Default", "Camera");
}

const FSlateBrush* SLevelViewportToolBar::GetCameraMenuLabelIcon() const
{
	TSharedPtr< SLevelViewport > PinnedViewport( Viewport.Pin() );
	if( PinnedViewport.IsValid() )
	{
		return GetCameraMenuLabelIconFromViewportType(PinnedViewport->GetLevelViewportClient().ViewportType );
	}

	return FStyleDefaults::GetNoBrush();
}

FText SLevelViewportToolBar::GetDevicePreviewMenuLabel() const
{
	FText Label = LOCTEXT("DevicePreviewMenuTitle_Default", "Preview");

	TSharedPtr< SLevelViewport > PinnedViewport( Viewport.Pin() );
	if( PinnedViewport.IsValid() )
	{
		if ( PinnedViewport->GetDeviceProfileString() != "Default" )
		{
			Label = FText::FromString( PinnedViewport->GetDeviceProfileString() );
		}
	}

	return Label;
}

const FSlateBrush* SLevelViewportToolBar::GetDevicePreviewMenuLabelIcon() const
{
	TSharedRef<SLevelViewport> ViewportRef = Viewport.Pin().ToSharedRef();
	FString DeviceProfileName = ViewportRef->GetDeviceProfileString();

	FName PlatformIcon = NAME_None;
	if( !DeviceProfileName.IsEmpty() && DeviceProfileName != "Default" )
	{
		static FName DeviceProfileServices( "DeviceProfileServices" );

		IDeviceProfileServicesModule& ScreenDeviceProfileUIServices = FModuleManager::LoadModuleChecked<IDeviceProfileServicesModule>(TEXT( "DeviceProfileServices"));
		IDeviceProfileServicesUIManagerPtr UIManager = ScreenDeviceProfileUIServices.GetProfileServicesManager();

		PlatformIcon = UIManager->GetDeviceIconName( DeviceProfileName );

		return FAppStyle::GetOptionalBrush( PlatformIcon );
	}

	return nullptr;
}

bool SLevelViewportToolBar::IsCurrentLevelViewport() const
{
	TSharedPtr< SLevelViewport > PinnedViewport( Viewport.Pin() );
	if( PinnedViewport.IsValid() )
	{
		if( &( PinnedViewport->GetLevelViewportClient() ) == GCurrentLevelEditingViewportClient )
		{
			return true;
		}
	}
	return false;
}

bool SLevelViewportToolBar::IsPerspectiveViewport() const
{
	TSharedPtr< SLevelViewport > PinnedViewport( Viewport.Pin() );
	if( PinnedViewport.IsValid() )
	{
		if(  PinnedViewport->GetLevelViewportClient().IsPerspective() )
		{
			return true;
		}
	}
	return false;
}

TSharedRef<SWidget> SLevelViewportToolBar::GenerateOptionsMenu() 
{
	static const FName MenuName("LevelEditor.LevelViewportToolBar.Options");
	if (!UToolMenus::Get()->IsMenuRegistered(MenuName))
	{
		UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(MenuName);
		Menu->AddDynamicSection("DynamicSection", FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
		{
			ULevelViewportToolBarContext* Context = InMenu->FindContext<ULevelViewportToolBarContext>();
			Context->LevelViewportToolBarWidget.Pin()->FillOptionsMenu(InMenu);
		}));
	}

	Viewport.Pin()->OnFloatingButtonClicked();

	const FLevelViewportCommands& LevelViewportActions = FLevelViewportCommands::Get();
	TSharedRef<FUICommandList> CommandList = Viewport.Pin()->GetCommandList().ToSharedRef();

	// Get all menu extenders for this context menu from the level editor module
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>( TEXT("LevelEditor") );
	TSharedPtr<FExtender> MenuExtender = LevelEditorModule.AssembleExtenders(CommandList, LevelEditorModule.GetAllLevelViewportOptionsMenuExtenders());

	FToolMenuContext MenuContext(CommandList, MenuExtender);
	{
		{
			ULevelViewportToolBarContext* ToolbarContextObject = NewObject<ULevelViewportToolBarContext>();
			ToolbarContextObject->LevelViewportToolBarWidget = SharedThis(this);
			MenuContext.AddObject(ToolbarContextObject);
		}

		{
			ULevelViewportContext* const LevelContextObject = NewObject<ULevelViewportContext>();
			LevelContextObject->LevelViewport = Viewport;
			MenuContext.AddObject(LevelContextObject);
		}
	}

	return UToolMenus::Get()->GenerateWidget(MenuName, MenuContext);
}

void SLevelViewportToolBar::FillOptionsMenu(UToolMenu* Menu)
{
	const FLevelViewportCommands& LevelViewportActions = FLevelViewportCommands::Get();
	const bool bIsPerspective = Viewport.Pin()->GetLevelViewportClient().IsPerspective();

	{
		{
			FToolMenuSection& Section = Menu->AddSection("LevelViewportViewportOptions", LOCTEXT("OptionsMenuHeader", "Viewport Options"));
			Section.AddMenuEntry(FEditorViewportCommands::Get().ToggleRealTime);

			// Add an option to disable the temporary override if there is one
			{
				FUIAction Action;
				Action.ExecuteAction.BindSP(this, &SLevelViewportToolBar::OnDisableRealtimeOverride);
				Action.IsActionVisibleDelegate.BindSP(this, &SLevelViewportToolBar::IsRealtimeOverrideToggleVisible);

				TAttribute<FText> Tooltip(this, &SLevelViewportToolBar::GetRealtimeOverrideTooltip);

				Section.AddMenuEntry(
					"DisableRealtimeOverride",
					LOCTEXT("DisableRealtimeOverride", "Disable Realtime Override"),
					Tooltip,
					FSlateIcon(),
					Action);

				Section.AddSeparator("DisableRealtimeOverrideSeparator");
					
			}

			Section.AddMenuEntry(FEditorViewportCommands::Get().ToggleFPS);

#if STATS
			{
				Section.AddMenuEntry(FEditorViewportCommands::Get().ToggleStats);

				Section.AddEntry(UE::LevelEditor::CreateShowStatsSubmenu());
			}
#endif

			Section.AddMenuEntry(LevelViewportActions.ToggleViewportToolbar);

			if( bIsPerspective )
			{
				Section.AddEntry(UE::LevelEditor::CreateFOVMenu(Viewport));
				Section.AddEntry(UE::LevelEditor::CreateFarViewPlaneMenu(Viewport));
			}

			FEditorViewportClient& ViewportClient = Viewport.Pin()->GetLevelViewportClient();
			Section.AddSubMenu(
				"ScreenPercentageSubMenu",
				LOCTEXT("ScreenPercentageSubMenu", "Screen Percentage"),
				LOCTEXT("ScreenPercentageSubMenu_ToolTip", "Customize the viewport's screen percentage"),
				FNewMenuDelegate::CreateStatic(&SCommonEditorViewportToolbarBase::ConstructScreenPercentageMenu, &ViewportClient));
		}

		{
			FToolMenuSection& Section = Menu->AddSection("LevelViewportViewportOptions2");

			if( bIsPerspective )
			{
				// Cinematic preview only applies to perspective
				Section.AddMenuEntry( LevelViewportActions.ToggleCinematicPreview );
			}

			Section.AddMenuEntry( LevelViewportActions.ToggleGameView );
			Section.AddMenuEntry( LevelViewportActions.ToggleImmersive );
		}


		{
			FToolMenuSection& Section = Menu->AddSection("LevelViewportBookmarks");
			if( bIsPerspective )
			{
				// Bookmarks only work in perspective viewports so only show the menu option if this toolbar is in one

				Section.AddSubMenu(
					"Bookmark",
					LOCTEXT("BookmarkSubMenu", "Bookmarks"),
					LOCTEXT("BookmarkSubMenu_ToolTip", "Viewport location bookmarking"),
					FNewToolMenuDelegate::CreateStatic(&UE::LevelEditor::CreateBookmarksMenu),
					false,
					FSlateIcon(FAppStyle::Get().GetStyleSetName(), "EditorViewport.SubMenu.Bookmarks")
				);

				Section.AddSubMenu(
					"Camera",
					LOCTEXT("CameraSubMeun", "Create Camera Here"),
					LOCTEXT("CameraSubMenu_ToolTip", "Select a camera type to create at current viewport's location"),
					FNewToolMenuDelegate::CreateStatic(&UE::LevelEditor::CreateCameraSpawnMenu),
					false,
					FSlateIcon(FAppStyle::Get().GetStyleSetName(), "EditorViewport.SubMenu.CreateCamera")
				);
			}

			Section.AddMenuEntry(LevelViewportActions.HighResScreenshot);
		}


		{
			FToolMenuSection& Section = Menu->AddSection("LevelViewportLayouts");
			Section.AddSubMenu("Configs", LOCTEXT("ConfigsSubMenu", "Layouts"), FText::GetEmpty(),
				FNewToolMenuDelegate::CreateLambda(
					[](UToolMenu* InMenu)
					{
						UE::LevelEditor::GenerateViewportLayoutsMenu(InMenu);
				}),
				false, FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Layout"));
		}

		{
			FToolMenuSection& Section = Menu->AddSection("LevelViewportSettings");
			Section.AddMenuEntry(LevelViewportActions.AdvancedSettings);
		}
	}
}


TSharedRef<SWidget> SLevelViewportToolBar::GenerateDevicePreviewMenu() const
{
	static const FName MenuName("LevelEditor.LevelViewportToolBar.DevicePreview");
	if (!UToolMenus::Get()->IsMenuRegistered(MenuName))
	{
		UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(MenuName);
		Menu->AddDynamicSection("DynamicSection", FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
		{
			ULevelViewportToolBarContext* Context = InMenu->FindContext<ULevelViewportToolBarContext>();
			Context->LevelViewportToolBarWidget.Pin()->FillDevicePreviewMenu(InMenu);
		}));
	}

	ULevelViewportToolBarContext* ContextObject = NewObject<ULevelViewportToolBarContext>();
	ContextObject->LevelViewportToolBarWidget = ConstCastSharedRef<SLevelViewportToolBar>(SharedThis(this));

	FToolMenuContext MenuContext(Viewport.Pin()->GetCommandList(), TSharedPtr<FExtender>(), ContextObject);
	return UToolMenus::Get()->GenerateWidget(MenuName, MenuContext);
}

void SLevelViewportToolBar::FillDevicePreviewMenu(UToolMenu* Menu) const
{
	IDeviceProfileServicesModule& ScreenDeviceProfileUIServices = FModuleManager::LoadModuleChecked<IDeviceProfileServicesModule>(TEXT( "DeviceProfileServices"));
	IDeviceProfileServicesUIManagerPtr UIManager = ScreenDeviceProfileUIServices.GetProfileServicesManager();

	TSharedRef<SLevelViewport> ViewportRef = Viewport.Pin().ToSharedRef();

	// Default menu - clear all settings
	{
		FToolMenuSection& Section = Menu->AddSection("DevicePreview", LOCTEXT("DevicePreviewMenuTitle", "Device Preview"));
		FUIAction Action( FExecuteAction::CreateSP( const_cast<SLevelViewportToolBar*>(this), &SLevelViewportToolBar::SetLevelProfile, FString( "Default" ) ),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP( ViewportRef, &SLevelViewport::IsDeviceProfileStringSet, FString( "Default" ) ) );
		Section.AddMenuEntry("DevicePreviewMenuClear", LOCTEXT("DevicePreviewMenuClear", "Off"), FText::GetEmpty(), FSlateIcon(), Action, EUserInterfaceActionType::Button);
		}

	// Recent Device Profiles	
	{
	FToolMenuSection& Section = Menu->AddSection("Recent", LOCTEXT("RecentMenuHeading", "Recent"));

	const FString INISection = "SelectedProfile";
	const FString INIKeyBase = "ProfileItem";
	const int32 MaxItems = 4; // Move this into a config file
	FString CurItem;
	for( int32 ItemIdx = 0 ; ItemIdx < MaxItems; ++ItemIdx )
	{
		// Build the menu from the contents of the game ini
		//@todo This should probably be using GConfig->GetText [10/21/2013 justin.sargent]
		if ( GConfig->GetString( *INISection, *FString::Printf( TEXT("%s%d"), *INIKeyBase, ItemIdx ), CurItem, GEditorPerProjectIni ) )
		{
			const FName PlatformIcon = UIManager->GetDeviceIconName( CurItem );

			FUIAction Action( FExecuteAction::CreateSP( const_cast<SLevelViewportToolBar*>(this), &SLevelViewportToolBar::SetLevelProfile, CurItem ),
				FCanExecuteAction(),
				FIsActionChecked::CreateSP( ViewportRef, &SLevelViewport::IsDeviceProfileStringSet, CurItem ) );
			Section.AddMenuEntry(NAME_None, FText::FromString(CurItem), FText(), FSlateIcon(FAppStyle::GetAppStyleSetName(), PlatformIcon), Action, EUserInterfaceActionType::Button );
		}
	}
	}

	// Device List
	{
	FToolMenuSection& Section = Menu->AddSection("Devices", LOCTEXT("DevicesMenuHeading", "Devices"));

	const TArray<TSharedPtr<FString> > PlatformList = UIManager->GetPlatformList();
	for ( int32 Index = 0; Index < PlatformList.Num(); Index++ )
	{
		TArray< UDeviceProfile* > DeviceProfiles;
		UIManager->GetProfilesByType( DeviceProfiles, *PlatformList[Index] );
		if ( DeviceProfiles.Num() > 0 )
		{
			const FString PlatformNameStr = DeviceProfiles[0]->DeviceType;
			const FName PlatformIcon =  UIManager->GetPlatformIconName( PlatformNameStr );
			Section.AddSubMenu(
				NAME_None,
				FText::FromString( PlatformNameStr ),
				FText::GetEmpty(),
				FNewToolMenuDelegate::CreateRaw( const_cast<SLevelViewportToolBar*>(this), &SLevelViewportToolBar::MakeDevicePreviewSubMenu, DeviceProfiles ),
				false,
				FSlateIcon(FAppStyle::GetAppStyleSetName(), PlatformIcon)
				);
		}
	}
	}
}

void SLevelViewportToolBar::MakeDevicePreviewSubMenu(UToolMenu* Menu, TArray< class UDeviceProfile* > InProfiles)
{
	TSharedRef<SLevelViewport> ViewportRef = Viewport.Pin().ToSharedRef();
	FToolMenuSection& Section = Menu->AddSection("Section");

	for ( int32 Index = 0; Index < InProfiles.Num(); Index++ )
	{
		FUIAction Action( FExecuteAction::CreateSP( this, &SLevelViewportToolBar::SetLevelProfile, InProfiles[ Index ]->GetName() ),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP( ViewportRef, &SLevelViewport::IsDeviceProfileStringSet, InProfiles[ Index ]->GetName() ) );

		Section.AddMenuEntry(NAME_None, FText::FromString( InProfiles[ Index ]->GetName() ), FText(), FSlateIcon(), Action, EUserInterfaceActionType::RadioButton );
	}
}

void SLevelViewportToolBar::SetLevelProfile( FString DeviceProfileName )
{
	TSharedRef<SLevelViewport> ViewportRef = Viewport.Pin().ToSharedRef();
	ViewportRef->SetDeviceProfileString( DeviceProfileName );

	IDeviceProfileServicesModule& ScreenDeviceProfileUIServices = FModuleManager::LoadModuleChecked<IDeviceProfileServicesModule>(TEXT( "DeviceProfileServices"));
	IDeviceProfileServicesUIManagerPtr UIManager = ScreenDeviceProfileUIServices.GetProfileServicesManager();
	UIManager->SetProfile( DeviceProfileName );
}

void SLevelViewportToolBar::GeneratePlacedCameraMenuEntries(FToolMenuSection& Section, TArray<AActor*> LookThroughActors) const
{
	FSlateIcon CameraIcon( FAppStyle::GetAppStyleSetName(), "ClassIcon.CameraComponent" );

	// Sort the cameras to make the ordering predictable for users.
	LookThroughActors.StableSort([](const AActor& Left, const AActor& Right)
	{
		// Do "natural sorting" via SceneOutliner::FNumericStringWrapper to make more sense to humans (also matches the Scene Outliner). This sorts "Camera2" before "Camera10" which a normal lexicographical sort wouldn't.
		SceneOutliner::FNumericStringWrapper LeftWrapper(FString(Left.GetActorLabel()));
		SceneOutliner::FNumericStringWrapper RightWrapper(FString(Right.GetActorLabel()));

		return LeftWrapper < RightWrapper;
	});

	for( AActor* LookThroughActor : LookThroughActors)
	{
		// Needed for the delegate hookup to work below
		AActor* GenericActor = LookThroughActor;

		FText ActorDisplayName = FText::FromString(LookThroughActor->GetActorLabel());
		FUIAction LookThroughCameraAction(
			FExecuteAction::CreateSP(Viewport.Pin().ToSharedRef(), &SLevelViewport::OnActorLockToggleFromMenu, GenericActor),
			FCanExecuteAction(),
			FIsActionChecked::CreateSP(Viewport.Pin().ToSharedRef(), &SLevelViewport::IsActorLocked, MakeWeakObjectPtr(GenericActor))
			);

		Section.AddMenuEntry( NAME_None, ActorDisplayName, FText::Format(LOCTEXT("LookThroughCameraActor_ToolTip", "Look through and pilot {0}"), ActorDisplayName), CameraIcon, LookThroughCameraAction, EUserInterfaceActionType::RadioButton );
	}
}

void SLevelViewportToolBar::GeneratePlacedCameraMenuEntries(UToolMenu* Menu, TArray<AActor*> LookThroughActors) const
{
	FToolMenuSection& Section = Menu->AddSection("Section");
	GeneratePlacedCameraMenuEntries(Section, LookThroughActors);
}

void SLevelViewportToolBar::GenerateViewportTypeMenu(FToolMenuSection& Section) const
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	LevelEditorModule.IterateViewportTypes([&](FName ViewportTypeName, const FViewportTypeDefinition& InDefinition)
	{
		if (InDefinition.ActivationCommand.IsValid())
		{
			Section.AddMenuEntry(*FString::Printf(TEXT("ViewportType_%s"), *ViewportTypeName.ToString()), InDefinition.ActivationCommand);
		}
	});
}

void SLevelViewportToolBar::GenerateViewportTypeMenu(UToolMenu* Menu) const
{
	FToolMenuSection& Section = Menu->AddSection("Section");
	GenerateViewportTypeMenu(Section);
}

void SLevelViewportToolBar::GenerateCameraSpawnMenu(UToolMenu* Menu) const
{
	FToolMenuSection& Section = Menu->AddSection("Section");
	const FLevelViewportCommands& Actions = FLevelViewportCommands::Get();

	for (TSharedPtr<FUICommandInfo> Camera : Actions.CreateCameras)
	{
		Section.AddMenuEntry(NAME_None, Camera);
	}
}

TSharedRef<SWidget> SLevelViewportToolBar::GenerateCameraMenu() const
{
	static const FName MenuName("LevelEditor.LevelViewportToolBar.Camera");
	if (!UToolMenus::Get()->IsMenuRegistered(MenuName))
	{
		UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(MenuName);
		Menu->AddDynamicSection("DynamicSection", FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
		{
			ULevelViewportToolBarContext* Context = InMenu->FindContext<ULevelViewportToolBarContext>();
			Context->LevelViewportToolBarWidget.Pin()->FillCameraMenu(InMenu);
		}));
	}

	Viewport.Pin()->OnFloatingButtonClicked();

	ULevelViewportToolBarContext* ContextObject = NewObject<ULevelViewportToolBarContext>();
	ContextObject->LevelViewportToolBarWidget = ConstCastSharedRef<SLevelViewportToolBar>(SharedThis(this));

	FToolMenuContext MenuContext(Viewport.Pin()->GetCommandList(), TSharedPtr<FExtender>(), ContextObject);
	return UToolMenus::Get()->GenerateWidget(MenuName, MenuContext);
}

void SLevelViewportToolBar::FillCameraMenu(UToolMenu* Menu) const
{
	// Camera types
	{
		FToolMenuSection& Section = Menu->AddSection("CameraTypes");
		Section.AddMenuEntry(FEditorViewportCommands::Get().Perspective);
	}

	{
		FToolMenuSection& Section = Menu->AddSection("LevelViewportCameraType_Ortho", LOCTEXT("CameraTypeHeader_Ortho", "Orthographic"));
		Section.AddMenuEntry(FEditorViewportCommands::Get().Top);
		Section.AddMenuEntry(FEditorViewportCommands::Get().Bottom);
		Section.AddMenuEntry(FEditorViewportCommands::Get().Left);
		Section.AddMenuEntry(FEditorViewportCommands::Get().Right);
		Section.AddMenuEntry(FEditorViewportCommands::Get().Front);
		Section.AddMenuEntry(FEditorViewportCommands::Get().Back);
	}

	TArray<AActor*> LookThroughActors;

	for( TActorIterator<ACameraActor> It(GetWorld().Get()); It; ++It )
	{
		LookThroughActors.Add( Cast<AActor>(*It) );
	}

	for (TActorIterator<ASceneCapture> It(GetWorld().Get()); It; ++It)
	{
		LookThroughActors.Add(Cast<AActor>(*It));
	}

	FText CameraActorsHeading = LOCTEXT("CameraActorsHeading", "Placed Cameras and Scene Capture Actors");

	// Don't add too many cameras to the top level menu or else it becomes too large
	const uint32 MaxCamerasInTopLevelMenu = 10;
	if(LookThroughActors.Num() > MaxCamerasInTopLevelMenu )
	{
		FToolMenuSection& Section = Menu->AddSection("CameraActors");
		Section.AddSubMenu("CameraActors", CameraActorsHeading, LOCTEXT("LookThroughPlacedCameras_ToolTip", "Look through and pilot placed cameras"), FNewToolMenuDelegate::CreateSP(this, &SLevelViewportToolBar::GeneratePlacedCameraMenuEntries, LookThroughActors ) );
	}
	else
	{
		FToolMenuSection& Section = Menu->AddSection("CameraActors", CameraActorsHeading);
		GeneratePlacedCameraMenuEntries(Section, LookThroughActors);
	}

	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));

		int32 NumCustomViewportTypes = 0;
		LevelEditorModule.IterateViewportTypes([&](FName, const FViewportTypeDefinition&){ ++NumCustomViewportTypes; });

		FText ViewportTypesHeading = LOCTEXT("ViewportTypes", "Viewport Type");
		const uint32 MaxViewportTypesInTopLevelMenu = 4;
		if( NumCustomViewportTypes > MaxViewportTypesInTopLevelMenu )
		{
			FToolMenuSection& Section = Menu->AddSection("ViewportTypes");
			Section.AddSubMenu("ViewportTypes", ViewportTypesHeading, FText(), FNewToolMenuDelegate::CreateSP(this, &SLevelViewportToolBar::GenerateViewportTypeMenu) );
		}
		else
		{
			FToolMenuSection& Section = Menu->AddSection("ViewportTypes", ViewportTypesHeading);
			GenerateViewportTypeMenu(Section);
		}
	}
}

TSharedRef<SWidget> SLevelViewportToolBar::GenerateShowMenu() const
{
	static const FName MenuName("LevelEditor.LevelViewportToolbar.Show");

	// Use a static bool to track whether or not this menu is registered. Bool instead of checking the registered state
	// with ToolMenus because we want the new viewport toolbar to be able to create this menu without breaking this
	// code. Static because this code can be called multiple times using different instances of this class.
	static bool bDidRegisterMenu = false;
	if (!bDidRegisterMenu)
	{
		bDidRegisterMenu = true;

		// Don't warn here to avoid warnings if the new viewport toolbar already has created an empty version
		// of this menu.
		UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(MenuName, NAME_None, EMultiBoxType::Menu, false);
		Menu->AddDynamicSection(
			"LevelDynamicSection",
			FNewToolMenuDelegate::CreateLambda(
				[](UToolMenu* InMenu)
				{
					if (ULevelViewportToolBarContext* Context = InMenu->FindContext<ULevelViewportToolBarContext>())
					{
						Context->LevelViewportToolBarWidget.Pin()->FillShowMenu(InMenu);
					}
				}
			)
		);
	}

	Viewport.Pin()->OnFloatingButtonClicked();

	TSharedRef<FUICommandList> CommandList = Viewport.Pin()->GetCommandList().ToSharedRef();

	// Get all menu extenders for this context menu from the level editor module
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	TSharedPtr<FExtender> MenuExtender = LevelEditorModule.AssembleExtenders(CommandList, LevelEditorModule.GetAllLevelViewportShowMenuExtenders());

	FToolMenuContext MenuContext;
	{
		MenuContext.AppendCommandList(CommandList);
		MenuContext.AddExtender(MenuExtender);

		{
			ULevelViewportToolBarContext* ContextObject = NewObject<ULevelViewportToolBarContext>();
			ContextObject->LevelViewportToolBarWidget = ConstCastSharedRef<SLevelViewportToolBar>(SharedThis(this));
			MenuContext.AddObject(ContextObject);
		}

		{
			ULevelViewportContext* const ContextObject = NewObject<ULevelViewportContext>();
			ContextObject->LevelViewport = Viewport;
			MenuContext.AddObject(ContextObject);
		}
	}

	return UToolMenus::Get()->GenerateWidget(MenuName, MenuContext);
}

void SLevelViewportToolBar::FillShowMenu(UToolMenu* Menu) const
{
	const FLevelViewportCommands& Actions = FLevelViewportCommands::Get();
	{
		{
			FToolMenuSection& Section = Menu->AddSection("UseDefaultShowFlags");
			Section.AddMenuEntry(Actions.UseDefaultShowFlags);
		}

		FShowFlagMenuCommands::Get().BuildShowFlagsMenu(Menu);

		FText ShowAllLabel = LOCTEXT("ShowAllLabel", "Show All");
		FText HideAllLabel = LOCTEXT("HideAllLabel", "Hide All");

		FLevelEditorViewportClient& ViewClient = Viewport.Pin()->GetLevelViewportClient();
		const UWorld* World = ViewClient.GetWorld();

		{
			FToolMenuSection& Section = Menu->AddSection("LevelViewportEditorShow", LOCTEXT("EditorShowHeader", "Editor"));

			Section.AddEntry(UE::LevelEditor::CreateShowVolumesSubmenu());
			Section.AddEntry(UE::LevelEditor::CreateShowLayersSubmenu());
			Section.AddEntry(UE::LevelEditor::CreateShowSpritesSubmenu());
			Section.AddEntry(UE::LevelEditor::CreateShowFoliageSubmenu());

			// Show 'HLODs' sub-menu is dynamically generated when the user enters 'show' menu
			if (World->IsPartitionedWorld())
			{
				Section.AddEntry(UE::LevelEditor::CreateShowHLODsSubmenu());
			}
		}
	}
}

EVisibility SLevelViewportToolBar::GetViewModeOptionsVisibility() const
{
	const FLevelEditorViewportClient& ViewClient = Viewport.Pin()->GetLevelViewportClient();
	if (ViewClient.GetViewMode() == VMI_MeshUVDensityAccuracy || ViewClient.GetViewMode() == VMI_MaterialTextureScaleAccuracy || ViewClient.GetViewMode() == VMI_RequiredTextureResolution)
	{
		return EVisibility::SelfHitTestInvisible;
	}
	else
	{
		return EVisibility::Collapsed;
	}
}

FText SLevelViewportToolBar::GetViewModeOptionsMenuLabel() const
{
	Viewport.Pin()->OnFloatingButtonClicked();
	const FLevelEditorViewportClient& ViewClient = Viewport.Pin()->GetLevelViewportClient();
	return ::GetViewModeOptionsMenuLabel(ViewClient.GetViewMode());
}


TSharedRef<SWidget> SLevelViewportToolBar::GenerateViewModeOptionsMenu() const
{
	Viewport.Pin()->OnFloatingButtonClicked();
	FLevelEditorViewportClient& ViewClient = Viewport.Pin()->GetLevelViewportClient();
	const UWorld* World = ViewClient.GetWorld();
	return BuildViewModeOptionsMenu(Viewport.Pin()->GetCommandList(), ViewClient.GetViewMode(), World ? World->GetFeatureLevel() : GMaxRHIFeatureLevel, ViewClient.GetViewModeParamNameMap());
}

double SLevelViewportToolBar::OnGetHLODInEditorMaxDrawDistanceValue() const
{
	IWorldPartitionEditorModule* WorldPartitionEditorModule = FModuleManager::GetModulePtr<IWorldPartitionEditorModule>("WorldPartitionEditor");
	return WorldPartitionEditorModule ? WorldPartitionEditorModule->GetHLODInEditorMaxDrawDistance() : 0;
}

void SLevelViewportToolBar::OnHLODInEditorMaxDrawDistanceValueChanged(double NewValue) const
{
	IWorldPartitionEditorModule* WorldPartitionEditorModule = FModuleManager::GetModulePtr<IWorldPartitionEditorModule>("WorldPartitionEditor");
	if (WorldPartitionEditorModule)
	{
		WorldPartitionEditorModule->SetHLODInEditorMaxDrawDistance(NewValue);
		GEditor->RedrawLevelEditingViewports(true);
	}
}

TWeakObjectPtr<UWorld> SLevelViewportToolBar::GetWorld() const
{
	if (Viewport.IsValid())
	{
		return Viewport.Pin()->GetWorld();
	}
	return NULL;
}

void SLevelViewportToolBar::FillViewMenu(UToolMenu* Menu)
{
	if (!Menu)
	{
		return;
	}

	ULevelViewportContext* const ContextObject = NewObject<ULevelViewportContext>();
	ContextObject->LevelViewport = Viewport;
	Menu->Context.AddObject(ContextObject);

	UE::LevelEditor::PopulateViewModesMenu(Menu);
}


void SLevelViewportToolBar::OnDisableRealtimeOverride()
{
	if (TSharedPtr<SLevelViewport> ViewportPinned = Viewport.Pin())
	{
		ViewportPinned->GetLevelViewportClient().PopRealtimeOverride();
	}
}

bool SLevelViewportToolBar::IsRealtimeOverrideToggleVisible() const
{
	if (TSharedPtr<SLevelViewport> ViewportPinned = Viewport.Pin())
	{
		return ViewportPinned->GetLevelViewportClient().IsRealtimeOverrideSet();
	}

	return false;
}

FText SLevelViewportToolBar::GetRealtimeOverrideTooltip() const
{
	if (TSharedPtr<SLevelViewport> ViewportPinned = Viewport.Pin())
	{
		return FText::Format(LOCTEXT("DisableRealtimeOverrideToolTip", "Realtime is currently overridden by \"{0}\".  Click to remove that override"), ViewportPinned->GetLevelViewportClient().GetRealtimeOverrideMessage());
	}

	return FText::GetEmpty();
}

float SLevelViewportToolBar::GetTransformToolbarWidth() const
{
	if (TransformToolbar)
	{
		const float TransformToolbarWidth = TransformToolbar->GetDesiredSize().X;
		if (TransformToolbar_CachedMaxWidth == 0.0f)
		{
			TransformToolbar_CachedMaxWidth = TransformToolbarWidth;
		}

		const float ToolbarWidthMinusPreviousTransformToolbar = GetDesiredSize().X - TransformToolbar_CachedMaxWidth;
		const float ToolbarWidthEstimate = ToolbarWidthMinusPreviousTransformToolbar + TransformToolbarWidth;

		float DpiScale = 1.0f;
		{
			IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
			const TSharedPtr<SWindow>& MainFrameParentWindow = MainFrameModule.GetParentWindow();
			if (MainFrameParentWindow.IsValid())
			{
				DpiScale = MainFrameParentWindow->GetDPIScaleFactor();
			}
		}

		const float ViewportToolBarWidth = static_cast<float>(GetCachedGeometry().GetLocalSize().X);
		const float OverflowWidth = ToolbarWidthEstimate * DpiScale - ViewportToolBarWidth;
		if (OverflowWidth > 0.0f)
		{
			// There isn't enough space in the viewport to show the toolbar!
			// Try and shrink the transform toolbar (which has an overflow area) to make things fit
			TransformToolbar_CachedMaxWidth = FMath::Max(FMath::Min(4.0f, TransformToolbarWidth), TransformToolbarWidth - OverflowWidth / DpiScale);
		}
		else
		{
			TransformToolbar_CachedMaxWidth = TransformToolbarWidth;
		}
		
		return TransformToolbar_CachedMaxWidth;
	}

	return 0.0f;
}

FReply SLevelViewportToolBar::OnRealtimeWarningClicked()
{
	FLevelEditorViewportClient& ViewportClient = Viewport.Pin()->GetLevelViewportClient();
	ViewportClient.SetRealtime(true);

	return FReply::Handled();
}

EVisibility SLevelViewportToolBar::GetRealtimeWarningVisibility() const
{
	const FEditorViewportClient& ViewportClient = Viewport.Pin()->GetLevelViewportClient();
	const bool bWarn = UE::UnrealEd::ShouldShowViewportRealtimeWarning(ViewportClient);
	return bWarn ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SLevelViewportToolBar::GetScalabilityWarningLabel() const
{
	const int32 QualityLevel = Scalability::GetQualityLevels().GetMinQualityLevel();
	if (QualityLevel >= 0)
	{
		return FText::Format(LOCTEXT("ScalabilityWarning", "Scalability: {0}"), Scalability::GetScalabilityNameFromQualityLevel(QualityLevel));
	}
	
	return FText::GetEmpty();
}

EVisibility SLevelViewportToolBar::GetScalabilityWarningVisibility() const
{
	//This method returns magic numbers. 3 means epic
	return GetDefault<UEditorPerformanceSettings>()->bEnableScalabilityWarningIndicator && Scalability::GetQualityLevels().GetMinQualityLevel() != 3 ? EVisibility::Visible : EVisibility::Collapsed;
}

TSharedRef<SWidget> SLevelViewportToolBar::GetScalabilityWarningMenuContent() const
{
	return
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Menu.Background"))
		[
			SNew(SScalabilitySettings)
		];
}

FLevelEditorViewportClient* ULevelViewportToolBarContext::GetLevelViewportClient()
{
	if (LevelViewportToolBarWidget.IsValid())
	{
		return LevelViewportToolBarWidget.Pin()->GetLevelViewportClient();
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE
