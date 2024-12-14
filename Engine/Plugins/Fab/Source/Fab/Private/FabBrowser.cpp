// Copyright Epic Games, Inc. All Rights Reserved.

#include "FabBrowser.h"

#include "ContentBrowserModule.h"
#include "FabBrowserApi.h"
#include "FabLog.h"
#include "FabSettings.h"
#include "FabSettingsWindow.h"
#include "IWebBrowserPopupFeatures.h"
#include "IWebBrowserWindow.h"
#include "JsonObjectConverter.h"
#include "LevelEditor.h"
#include "ToolMenu.h"
#include "ToolMenuEntry.h"
#include "ToolMenuSection.h"
#include "ToolMenus.h"
#include "WebBrowserModule.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxExtender.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"

#include "Interfaces/IMainFrameModule.h"
#include "Interfaces/IPluginManager.h"

#include "Math/Vector2D.h"

#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"

#include "Styling/CoreStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

#include "UObject/GCObject.h"

#include "Utilities/FabLocalAssets.h"

#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "Fab"

TSharedPtr<SWebBrowser> FFabBrowser::WebBrowserInstance = nullptr;

TObjectPtr<UFabBrowserApi> FFabBrowser::JavascriptApi = nullptr;

TSharedPtr<SDockTab> FFabBrowser::DockTab                     = nullptr;
TUniquePtr<FSlateStyleSet> FFabBrowser::SlateStyleSet         = nullptr;
TSharedPtr<IWebBrowserWindow> FFabBrowser::WebBrowserWindow   = nullptr;
TObjectPtr<const UFabSettings> FFabBrowser::FabPluginSettings = nullptr;

const FName FFabBrowser::TabId = TEXT("FabTab");

const FText FFabBrowser::FabLabel           = LOCTEXT("Fab.Label", "Fab");
const FText FFabBrowser::FabTooltip         = LOCTEXT("Fab.Tooltip", "Get content from Fab");
const FName FFabBrowser::FabMenuIconName    = TEXT("Fab.MenuIcon");
const FName FFabBrowser::FabAssetIconName   = TEXT("Fab.AssetIcon");
const FName FFabBrowser::FabToolbarIconName = TEXT("Fab.ToolbarIcon");

void FFabBrowser::Init()
{
	RegisterSlateStyle();
	RegisterNomadTab();
	SetupEntryPoints();
	ExtendContextMenuInContentBrowser();
}

void FFabBrowser::ExtendContextMenuInContentBrowser()
{
	FContentBrowserModule& ContentBrowserModule                       = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<FContentBrowserMenuExtender_SelectedAssets>& MenuExtenders = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
	FAssetViewExtraStateGenerator StateGenerator(
		FOnGenerateAssetViewExtraStateIndicators::CreateStatic(&FFabBrowser::OnFabAssetIconGenerate),
		FOnGenerateAssetViewExtraStateIndicators()
	);
	ContentBrowserModule.AddAssetViewExtraStateGenerator(StateGenerator);
	MenuExtenders.Add(FContentBrowserMenuExtender_SelectedAssets::CreateStatic(&FFabBrowser::OnExtendContentBrowserAssetSelectionMenu));
}

void FFabBrowser::RegisterSlateStyle()
{
	SlateStyleSet = MakeUnique<FSlateStyleSet>(TEXT("FabStyle"));
	SlateStyleSet->SetContentRoot(IPluginManager::Get().FindPlugin(TEXT("Fab"))->GetBaseDir() / TEXT("Resources"));

	const FString IconPath = SlateStyleSet->RootToContentDir(TEXT("FabLogo.svg"));
	const FString AlternateIconPath = SlateStyleSet->RootToContentDir(TEXT("FabLogoAlternate.svg"));
	SlateStyleSet->Set(FabMenuIconName, new FSlateVectorImageBrush(IconPath, CoreStyleConstants::Icon16x16));
	SlateStyleSet->Set(FabAssetIconName, new FSlateVectorImageBrush(AlternateIconPath, CoreStyleConstants::Icon20x20));
	SlateStyleSet->Set(FabToolbarIconName, new FSlateVectorImageBrush(IconPath, CoreStyleConstants::Icon20x20));

	FSlateStyleRegistry::RegisterSlateStyle(*SlateStyleSet);

	FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
}

void FFabBrowser::SetupEntryPoints()
{
	const FUIAction InvokeTabAction = FUIAction(
		FExecuteAction::CreateLambda(
			[LevelEditorModuleName = FName("LevelEditor")]()
			{
				FModuleManager::GetModuleChecked<FLevelEditorModule>(LevelEditorModuleName).GetLevelEditorTabManager()->TryInvokeTab(TabId);
			}
		),
		FCanExecuteAction()
	);

	FToolMenuEntry& ToolMenuEntry = UToolMenus::Get()->ExtendMenu("ContentBrowser.Toolbar")->FindOrAddSection("New").AddEntry(
		FToolMenuEntry::InitToolBarButton(
			"OpenFabWindow",
			InvokeTabAction,
			FabLabel,
			FabTooltip,
			FSlateIcon(SlateStyleSet->GetStyleSetName(), FabToolbarIconName),
			EUserInterfaceActionType::Button
		)
	);
	ToolMenuEntry.StyleNameOverride = FName("CalloutToolbar");

	UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu("MainFrame.MainMenu.Window");

	FToolMenuSection* ContentSectionPtr = WindowMenu->FindSection("GetContent");
	if (!ContentSectionPtr)
	{
		ContentSectionPtr = &WindowMenu->AddSection("GetContent", NSLOCTEXT("MainAppMenu", "GetContentHeader", "Get Content"));
	}

	ContentSectionPtr->AddMenuEntry(
		TEXT("OpenFabTab"),
		LOCTEXT("OpenFabTab_Label", "Fab"),
		LOCTEXT("OpenFabTab_Desc", "Opens the Fab Plugin."),
		FSlateIcon(SlateStyleSet->GetStyleSetName(), FabMenuIconName),
		InvokeTabAction
	);

	// Add a Fab entry to the Content Browser's Add popup menu
	UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.AddNewContextMenu"))->AddSection(TEXT("ContentBrowserGetContent"), LOCTEXT("GetContentText", "Get Content")).AddEntry(
		FToolMenuEntry::InitMenuEntry(TEXT("OpenFabWindow"), FabLabel, FabTooltip, FSlateIcon(SlateStyleSet->GetStyleSetName(), FabMenuIconName), InvokeTabAction)
	);
}

// Extend the context menu to view listings in Fab
TSharedRef<FExtender> FFabBrowser::OnExtendContentBrowserAssetSelectionMenu(const TArray<FAssetData>& SelectedAssets)
{
	TSharedRef<FExtender> Extender = MakeShared<FExtender>();

	if (SelectedAssets.Num() != 1)
	{
		return Extender;
	}
	const FAssetData AssetData = SelectedAssets[0];
	const FString ObjectPath   = AssetData.GetObjectPathString();
	FString FabListingId;
	UFabLocalAssets::GetListingID(ObjectPath, FabListingId);

	if (FabListingId.IsEmpty())
	{
		return Extender;
	}

	Extender->AddMenuExtension(
		"CommonAssetActions",
		EExtensionHook::After,
		nullptr,
		FMenuExtensionDelegate::CreateLambda(
			[FabListingId](FMenuBuilder& MenuBuilder)
			{
				MenuBuilder.AddMenuEntry(
					FText::FromString("View in Fab"),
					FText::FromString("View the asset in Fab plugin"),
					FSlateIcon(SlateStyleSet->GetStyleSetName(), FabMenuIconName),
					FUIAction(
						FExecuteAction::CreateLambda(
							[FabListingId]()
							{
								FFabBrowser::OpenURL(GetUrl() / "listings" / FabListingId);
							}
						)
					)
				);
			}
		)
	);
	return Extender;
}

TSharedRef<SWidget> FFabBrowser::OnFabAssetIconGenerate(const FAssetData& AssetData)
{
	const FSlateBrush* FabImage = nullptr;

	const FString ObjectPath = AssetData.GetObjectPathString();
	FString FabListingId;
	UFabLocalAssets::GetListingID(ObjectPath, FabListingId);

	if (!FabListingId.IsEmpty())
	{
		FabImage = SlateStyleSet->GetBrush(FabAssetIconName);
	}

	return SNew(SBox)
		.Padding(4.0f, 4.0f, 0.0f, 0.0f)
		.IsEnabled(FabImage != nullptr)
		[
			SNew(SImage)
			.Image(FabImage)
			.ToolTipText(FText::FromString("Imported from FAB"))
		];
}

void FFabBrowser::RegisterNomadTab()
{
	auto RegisterSpawner = [](TSharedPtr<ILevelEditor>)
	{
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabId, FOnSpawnTab::CreateStatic(OpenTab)).SetAutoGenerateMenuEntry(false).SetDisplayName(FabLabel).
		                          SetTooltipTextAttribute(FabTooltip).SetIcon(FSlateIcon(SlateStyleSet->GetStyleSetName(), FabMenuIconName));
	};

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	if (LevelEditorModule.GetLevelEditorInstance().IsValid())
	{
		RegisterSpawner(LevelEditorModule.GetLevelEditorInstance().Pin());
	}
	else
	{
		LevelEditorModule.OnLevelEditorCreated().AddLambda(RegisterSpawner);
	}
}

FString FFabBrowser::GetUrl()
{
	const FString ProdUrl = TEXT("https://www.fab.com/plugins/ue5");
	if (FabPluginSettings == nullptr)
	{
		return ProdUrl;
	}

	switch (FabPluginSettings->Environment)
	{
		case EFabEnvironment::Prod:
		{
			return ProdUrl;
		}
		case EFabEnvironment::Gamedev:
		{
			return TEXT("https://fab.cceb.dev.use1a.on.epicgames.com/plugins/ue5");
		}
		case EFabEnvironment::Test:
		{
			return TEXT("https://fab.daec.live.use1a.on.epicgames.com/plugins/ue5");
		}
		case EFabEnvironment::CustomUrl:
		{
			return FabPluginSettings->CustomUrl;
		}
		default:
		{
			return ProdUrl;
		}
	}
}

TSharedRef<SDockTab> FFabBrowser::OpenTab(const FSpawnTabArgs&)
{
	LogEvent(
		{
			"Open Tab",
			"Plugin",
			"Click"
		}
	);

	FabPluginSettings = GetDefault<UFabSettings>();

	JavascriptApi = NewObject<UFabBrowserApi>();
	JavascriptApi->AddToRoot(); //Don't garbage collect

	IWebBrowserModule& WebBrowserModule = IWebBrowserModule::Get();
	if (!IWebBrowserModule::IsAvailable() || !WebBrowserModule.IsWebModuleAvailable())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Failed to load the plugin. Please enable Web WebBrowserWindow in the plugin manager to use Emporium."));
		return SNew(SDockTab).TabRole(ETabRole::NomadTab);
	}

	FCreateBrowserWindowSettings WindowSettings;

	FString PluginPath = IPluginManager::Get().FindPlugin(TEXT("Fab"))->GetBaseDir();
	FString IndexUrl   = FPaths::ConvertRelativePathToFull(FPaths::Combine(PluginPath, TEXT("ThirdParty"), TEXT("index.html")));
	FString FinalUrl   = FPaths::Combine(TEXT("file:///"), IndexUrl);
	WindowSettings.InitialURL       = FinalUrl;
	WindowSettings.BrowserFrameRate = 60;

	IWebBrowserSingleton* WebBrowserSingleton = WebBrowserModule.GetSingleton();
	WebBrowserSingleton->SetDevToolsShortcutEnabled(true);

	WebBrowserWindow = WebBrowserSingleton->CreateBrowserWindow(WindowSettings);
	WebBrowserWindow->OnUnhandledKeyUp().BindLambda([](const FKeyEvent&) { return true; });
	WebBrowserWindow->OnUnhandledKeyDown().BindLambda([](const FKeyEvent&) { return true; });

	if (FabPluginSettings->bEnableDebugOptions)
	{
		WebBrowserWindow.Get()->OnCreateWindow().BindLambda(
			[](const TWeakPtr<IWebBrowserWindow>& NewBrowserWindow, const TWeakPtr<IWebBrowserPopupFeatures>& PopupFeatures)
			{
				const TSharedRef<SWindow> DialogMainWindow = SNew(SWindow)
					.ClientSize(FVector2D(700, 700))
					.SupportsMaximize(true)
					.SupportsMinimize(true)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
						[
							SNew(SWebBrowser, NewBrowserWindow.Pin())
						]
					];
				FSlateApplication::Get().AddWindow(DialogMainWindow);
				return true;
			}
		);
	}

	bool bShowAddressBar = FabPluginSettings->Environment == EFabEnvironment::CustomUrl;

	SAssignNew(WebBrowserInstance, SWebBrowser, WebBrowserWindow).ShowAddressBar(bShowAddressBar).ShowControls(bShowAddressBar);

	WebBrowserInstance->BindUObject(TEXT("fab"), Cast<UObject>(JavascriptApi), true);
	WebBrowserWindow->Reload();

	SAssignNew(DockTab, SDockTab).TabRole(ETabRole::NomadTab).OnTabClosed_Lambda(
		[](TSharedRef<class SDockTab> InParentTab)
		{
			WebBrowserInstance->UnbindUObject(TEXT("fab"), Cast<UObject>(JavascriptApi), true);
			WebBrowserInstance.Reset();
			WebBrowserWindow.Reset();
			DockTab.Reset();
		}
	)
	[
		WebBrowserInstance.ToSharedRef()
	];

	return DockTab.ToSharedRef();
}

void FFabBrowser::ExecuteJavascript(const FString& InSrcScript)
{
	if (WebBrowserInstance == nullptr)
	{
		return;
	}

	WebBrowserInstance->ExecuteJavascript(InSrcScript);
}

void FFabBrowser::Shutdown()
{
	WebBrowserInstance.Reset();
	WebBrowserWindow.Reset();
	DockTab.Reset();
	FSlateStyleRegistry::UnRegisterSlateStyle(*SlateStyleSet);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
}

void FFabBrowser::LoggedIn(const FString& InAccessToken)
{
	ExecuteJavascript(FString::Printf(TEXT("window.ue.fab.onLoginSuccessful('%s');"), *InAccessToken));
}

void FFabBrowser::GetSignedUrl(const FString& AssetId, const int32 Tier)
{
	ExecuteJavascript(FString::Printf(TEXT("window.ue.fab.getSignedUrl('%s', %d)"), *AssetId, Tier));
}

void FFabBrowser::LogEvent(const FFabAnalyticsPayload& Payload)
{
	FString JSONPayload;
	FJsonObjectConverter::UStructToJsonObjectString(Payload, JSONPayload, 0, 0);
	FAB_LOG("%s", *JSONPayload);

	// ExecuteJavascript(FString::Printf(TEXT("window.ue.fab.onDownloaded()")));
}

void FFabBrowser::ShowSettings()
{
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("FabSettingsLabel", "Fab Settings"))
		.ClientSize(FVector2D(600.f, 300.f))
		.SizingRule(ESizingRule::UserSized);

	TSharedPtr<SFabSettingsWindow> SettingsWindow;
	Window->SetContent(SAssignNew(SettingsWindow, SFabSettingsWindow).WidgetWindow(Window));
	TSharedPtr<SWindow> ParentWindow;
	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		const IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
		ParentWindow                      = MainFrame.GetParentWindow();
	}

	FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);
}

void FFabBrowser::OpenURL(const FString& InURL)
{
	FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor").GetLevelEditorTabManager()->TryInvokeTab(TabId);
	if (WebBrowserWindow.Get()->GetUrl() != InURL)
	{
		WebBrowserWindow.Get()->LoadURL(InURL);
	}
}

#undef LOCTEXT_NAMESPACE
