// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLiveLinkTimecode.h"

#include "UObject/NameTypes.h"

#include "EditorFontGlyphs.h"
#include "Features/IModularFeatures.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Internationalization/Text.h"
#include "Misc/App.h"
#include "UI/Widgets/SLiveLinkTimecode.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/STimecode.h"

#include "Clients/LiveLinkHubProvider.h"
#include "LiveLinkClient.h"
#include "LiveLinkHub.h"
#include "LiveLinkHubCommands.h"
#include "LiveLinkHubMessages.h"
#include "LiveLinkHubModule.h"
#include "Session/LiveLinkHubSessionManager.h"

#define LOCTEXT_NAMESPACE "LiveLinkHub"

namespace UE::LiveLinkTimecode::Private
{
	static FName EnableTimecodeSourceId = TEXT("EnableTimeCodeSource");

	/** We only support a preset list of timecode values + named subjects. */
	static FName System23976fps = TEXT("SystemTime23976fps");
	static FName System24fps = TEXT("SystemTime24fps");
	static FName System25fps = TEXT("SystemTime25fps");
	static FName System2997fps = TEXT("SystemTime2997fps");
	static FName System30fps = TEXT("SystemTime30fps");
	static FName System48fps = TEXT("SystemTime48fps");
	static FName System50fps = TEXT("SystemTime50fps");
	static FName System5994fps = TEXT("SystemTime5994fps");
	static FName System60fps = TEXT("SystemTime60fps");

	struct FTimecodeMenuItem
	{
		FFrameRate Rate;
		FText Label;
		FText ToolTip;
	};
	static const TMap<FName, FTimecodeMenuItem> StaticTimecodeMenu = []() {
		TMap<FName, FTimecodeMenuItem> Definition = {
			{System23976fps, {
				FFrameRate(24000,1001),
				LOCTEXT("LiveLinkHubTimecodeSource23976fps", "System Time (23.976 ND fps)"),
				LOCTEXT("LiveLinkHubTimecodeSource23976fps_Tooltip", "Use a 23.976 Non-drop FPS time code based on system time.")
			}},
			{System24fps, {
				FFrameRate(24,1),
				LOCTEXT("LiveLinkHubTimecodeSource24fps", "System Time (24 fps)"),
				LOCTEXT("LiveLinkHubTimecodeSource24fps_Tooltip", "Use a 24 FPS time code based on system time.")
			}},
			{System25fps, {
				FFrameRate(25,1),
				LOCTEXT("LiveLinkHubTimecodeSource25fps", "System Time (25 fps)"),
				LOCTEXT("LiveLinkHubTimecodeSource25fps_Tooltip", "Use a 25 FPS time code based on system time.")
			}},
			{System2997fps, {
				FFrameRate(30000,1001),
				LOCTEXT("LiveLinkHubTimecodeSource2997fps", "System Time (29.97 ND fps)"),
				LOCTEXT("LiveLinkHubTimecodeSource29976fps_Tooltip", "Use a 29.97 Non-drop FPS time code based on system time.")
			}},
			{System30fps, {
				FFrameRate(30,1),
				LOCTEXT("LiveLinkHubTimecodeSource30fps", "System Time (30 fps)"),
				LOCTEXT("LiveLinkHubTimecodeSource30fps_Tooltip", "Use a 30 FPS time code based on system time.")
			}},
			{System48fps, {
				FFrameRate(48,1),
				LOCTEXT("LiveLinkHubTimecodeSource48fps", "System Time (48 fps)"),
				LOCTEXT("LiveLinkHubTimecodeSource48fps_Tooltip", "Use a 48 FPS time code based on system time.")
			}},
			{System50fps, {
				FFrameRate(50,1),
				LOCTEXT("LiveLinkHubTimecodeSource50fps", "System Time (50 fps)"),
				LOCTEXT("LiveLinkHubTimecodeSource50fps_Tooltip", "Use a 50 FPS time code based on system time.")
			}},
			{System5994fps, {
				FFrameRate(60000, 1001),
				LOCTEXT("LiveLinkHubTimecodeSource5994fps", "System Time (59.94 ND fps)"),
				LOCTEXT("LiveLinkHubTimecodeSource5994fps_Tooltip", "Use a 59.94 Non-drp FPS time code based on system time.")
			}},
			{System60fps, {
				FFrameRate(60, 1),
				LOCTEXT("LiveLinkHubTimecodeSource60fps", "System Time (60 fps)"),
				LOCTEXT("LiveLinkHubTimecodeSource60fps_Tooltip", "Use a 60 FPS time code based on system time.")
			}}
		};
		return Definition;
	}();
}

FSlateColor SLiveLinkTimecode::GetTimecodeStatusColor() const
{
	return bIsTimecodeSource ? FSlateColor(FColor::Green) : FSlateColor(FColor::Yellow);
}

FText SLiveLinkTimecode::GetTimecodeTooltip() const
{
	if (bIsTimecodeSource)
	{
		return LOCTEXT("LiveLinkTimeCode_IsConnected", "Sending timecode data to connected editors.");
	}
	return LOCTEXT("LiveLinkTimeCode_NotConnected", "No timecode data shared with connected editors.");
}

void SLiveLinkTimecode::OnEnableTimecodeToggled()
{
	bIsTimecodeSource = !bIsTimecodeSource;
	SendUpdatedTimecodeToEditor();
}

void SLiveLinkTimecode::SendUpdatedTimecodeToEditor()
{
	FLiveLinkHubTimecodeSettings Settings;
	using namespace UE::LiveLinkTimecode::Private;

	if (!bIsTimecodeSource)
	{
		Settings.Source = ELiveLinkHubTimecodeSource::NotDefined;
	}

	auto GetSystemTimeSource = [this]() -> TOptional<FLiveLinkHubTimecodeSettings>
	{
		for (const TPair<FName,FTimecodeMenuItem>& Source : StaticTimecodeMenu)
		{
			if (ActiveTimecodeSource == Source.Key)
			{
				FLiveLinkHubTimecodeSettings Settings;
				Settings.Source = ELiveLinkHubTimecodeSource::SystemTimeEditor;
				Settings.DesiredFrameRate = Source.Value.Rate;
				return Settings;
			}
		}
		return {};
	};

	if (TOptional<FLiveLinkHubTimecodeSettings> SystemTimeSource = GetSystemTimeSource())
	{
		Settings = MoveTemp(*SystemTimeSource);
	}
	else
	{
		Settings.Source = ELiveLinkHubTimecodeSource::UseSubjectName;
	}
	Settings.SubjectName = ActiveTimecodeSource;

	// Apply the timecode settings to the local instance too.
	Settings.AssignTimecodeSettingsAsProviderToEngine();

	const TSharedPtr<FLiveLinkHub> LiveLinkHub = FModuleManager::Get().GetModuleChecked<FLiveLinkHubModule>("LiveLinkHub").GetLiveLinkHub();
	if (LiveLinkHub.IsValid())
	{
		TSharedPtr<FLiveLinkHubProvider> Provider = LiveLinkHub->GetLiveLinkProvider();
		if (Provider.IsValid())
		{
			Provider->SetTimecodeSettings(MoveTemp(Settings));
		}
	}
}

void SLiveLinkTimecode::UpdateTimecodeFromSettings()
{
	FLiveLinkHubTimecodeSettings Settings;
	using namespace UE::LiveLinkTimecode::Private;

	const TSharedPtr<FLiveLinkHub> LiveLinkHub = FModuleManager::Get().GetModuleChecked<FLiveLinkHubModule>("LiveLinkHub").GetLiveLinkHub();
	if (LiveLinkHub.IsValid())
	{
		TSharedPtr<FLiveLinkHubProvider> Provider = LiveLinkHub->GetLiveLinkProvider();
		if (Provider.IsValid())
		{
			Settings = Provider->GetTimecodeSettings();
		}
	}

	bIsTimecodeSource = Settings.Source != ELiveLinkHubTimecodeSource::NotDefined;
	ActiveTimecodeSource = Settings.SubjectName.IsNone() ? System24fps : Settings.SubjectName;
}

void SLiveLinkTimecode::SetTimecodeSource(const FName SourceId)
{
	if (SourceId == ActiveTimecodeSource)
	{
		return;
	}

	ActiveTimecodeSource = SourceId;
	SendUpdatedTimecodeToEditor();
}

TSharedRef<SWidget> SLiveLinkTimecode::MakeMenu()
{
	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("LiveLinkHubTimecodeSource", "Enable Timecode Source"),
		LOCTEXT("LiveLinkHubTimecodeSource_Tooltip", "Make this Live Link Hub a time code source for connected editors."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &SLiveLinkTimecode::OnEnableTimecodeToggled),
			FCanExecuteAction::CreateLambda([] { return true; }),
			FIsActionChecked::CreateLambda([this] { return bIsTimecodeSource; })),
		NAME_None,
		EUserInterfaceActionType::ToggleButton);


	MenuBuilder.BeginSection("LiveLinkHub.Timecode.TimecodeProvider", LOCTEXT("TimecodeProviderSection", "Timecode Provider"));

	auto GenerateUIAction = [this](const FName Id) -> FUIAction
	{
		// Generate a UI Action
		return FUIAction(
			FExecuteAction::CreateSP(this, &SLiveLinkTimecode::SetTimecodeSource, Id),
			FCanExecuteAction::CreateLambda([] { return true; }),
			FIsActionChecked::CreateLambda([this, Id] { return Id == ActiveTimecodeSource; }));
	};
	using namespace UE::LiveLinkTimecode::Private;
	for (const TPair<FName,FTimecodeMenuItem>& Source : StaticTimecodeMenu)
	{
		MenuBuilder.AddMenuEntry(
			Source.Value.Label,
			Source.Value.ToolTip,
			FSlateIcon(),
			GenerateUIAction(Source.Key),
			NAME_None,
			EUserInterfaceActionType::Check);
	}

	TArray<FName> Subjects;
	WorkingClient->GetSubjectNames(Subjects);
	for (FName Subject : Subjects)
	{
		FTextBuilder SubjectText;
		SubjectText.AppendLineFormat(LOCTEXT("LiveLinkHubTimecodeSourceSubject_Tooltip", "{0}'s timecode"), FText::FromName(Subject));
		MenuBuilder.AddMenuEntry(
			FText::FromName(Subject),
			SubjectText.ToText(),
			FSlateIcon(),
			GenerateUIAction(Subject),
			NAME_None,
			EUserInterfaceActionType::Check);
	}
	MenuBuilder.EndSection();
	return MenuBuilder.MakeWidget();
}

void SLiveLinkTimecode::Construct(const FArguments& InArgs)
{
	WorkingClient = (FLiveLinkClient*)&IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
	
	const FLiveLinkHubModule& LiveLinkHubModule = FModuleManager::Get().GetModuleChecked<FLiveLinkHubModule>("LiveLinkHub");
	TSharedPtr<ILiveLinkHubSessionManager> SessionManager = LiveLinkHubModule.GetSessionManager();
	if (SessionManager.IsValid())
	{
		SessionManager->OnActiveSessionChanged().AddLambda([this](const TSharedRef<ILiveLinkHubSession>&)
		{
			// Update the UI when a config is loaded.
			UpdateTimecodeFromSettings();
		});
	}

	UpdateTimecodeFromSettings();
	
	check(WorkingClient);
	ChildSlot
	[
		SNew(SComboButton)
		.ContentPadding(FMargin(4.0f, 0.0f))
		.MenuPlacement(MenuPlacement_AboveAnchor)
		.OnGetMenuContent(this, &SLiveLinkTimecode::MakeMenu)
		.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
		.HasDownArrow(true)
		.ToolTipText(this, &SLiveLinkTimecode::GetTimecodeTooltip)
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 3, 0)
			[
				SNew(STextBlock)
					.Font(FAppStyle::Get().GetFontStyle("FontAwesome.8"))
					.ColorAndOpacity(this, &SLiveLinkTimecode::GetTimecodeStatusColor)
					.Text(FEditorFontGlyphs::Circle)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2, 0, 10, 0)
			[
				SNew(STimecode)
				.DisplayLabel(false)
				.TimecodeFont(FCoreStyle::Get().GetFontStyle(TEXT("NormalText")))
				.Timecode(MakeAttributeLambda([]
				{
					return FApp::GetTimecode();
				}))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2, 0, 10, 0)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::Get().GetFontStyle(TEXT("NormalText")))
				.Text(MakeAttributeLambda([] {return FApp::GetTimecodeFrameRate().ToPrettyText();}))
			]
    	]
	];
}

#undef LOCTEXT_NAMESPACE
