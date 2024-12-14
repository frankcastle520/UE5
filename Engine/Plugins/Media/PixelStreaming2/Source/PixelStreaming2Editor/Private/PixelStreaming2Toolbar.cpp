// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreaming2Toolbar.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "LevelEditor.h"
#include "ToolMenus.h"
#include "PixelStreaming2Commands.h"
#include "PixelStreaming2Style.h"
#include "Framework/SlateDelegates.h"
#include "ToolMenuContext.h"
#include "IPixelStreaming2Module.h"
#include "IPixelStreaming2Streamer.h"
#include "Editor/EditorEngine.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SWindow.h"
#include "EditorViewportClient.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SViewport.h"
#include "Widgets/Layout/SSpacer.h"
#include "Styling/AppStyle.h"
#include "PixelStreaming2EditorModule.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Types/SlateEnums.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "PixelStreaming2Servers.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include <SocketSubsystem.h>
#include <IPAddress.h>
#include "SlateFwd.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "PixelStreaming2PluginSettings.h"

#include "CoderUtils.h"
#include "Video/Encoders/Configs/VideoEncoderConfigAV1.h"
#include "Video/Encoders/Configs/VideoEncoderConfigH264.h"

#define LOCTEXT_NAMESPACE "PixelStreaming2Editor"

DECLARE_LOG_CATEGORY_EXTERN(LogPixelStreaming2Toolbar, Log, All);
DEFINE_LOG_CATEGORY(LogPixelStreaming2Toolbar);

void SetCodec(EVideoCodec Codec)
{
	UPixelStreaming2PluginSettings::CVarEncoderCodec.AsVariable()->Set(*UE::PixelStreaming2::GetCVarStringFromEnum(Codec));
}

EVideoCodec GetCodec()
{
	return UE::PixelStreaming2::GetEnumFromCVar<EVideoCodec>(UPixelStreaming2PluginSettings::CVarEncoderCodec);
}

void SetUseRemoteSignallingServer(bool UseRemoteSignallingServer)
{
	UPixelStreaming2PluginSettings::CVarEditorUseRemoteSignallingServer.AsVariable()->Set(UseRemoteSignallingServer);
}

bool GetUseRemoteSignallingServer()
{
	return UPixelStreaming2PluginSettings::CVarEditorUseRemoteSignallingServer.GetValueOnAnyThread();
}

namespace UE::EditorPixelStreaming2
{
	FPixelStreaming2Toolbar::FPixelStreaming2Toolbar()
	{
		FPixelStreaming2Commands::Register();

		PluginCommands = MakeShared<FUICommandList>();

		PluginCommands->MapAction(
			FPixelStreaming2Commands::Get().ExternalSignalling,
			FExecuteAction::CreateLambda([]() {
				SetUseRemoteSignallingServer(!GetUseRemoteSignallingServer());
				IPixelStreaming2EditorModule::Get().StopSignalling();
			}),
			FCanExecuteAction::CreateLambda([] {
				TSharedPtr<UE::PixelStreaming2Servers::IServer> SignallingServer = IPixelStreaming2EditorModule::Get().GetSignallingServer();
				if (!SignallingServer.IsValid() || !SignallingServer->HasLaunched())
				{
					return true;
				}
				return false;
			}),
			FIsActionChecked::CreateLambda([]() {
				return GetUseRemoteSignallingServer();
			}));

		PluginCommands->MapAction(
			FPixelStreaming2Commands::Get().StreamLevelEditor,
			FExecuteAction::CreateLambda([]() {
				IPixelStreaming2EditorModule::Get().StartStreaming(EPixelStreaming2EditorStreamTypes::LevelEditorViewport);
			}),
			FCanExecuteAction::CreateLambda([] {
				if (TSharedPtr<IPixelStreaming2Streamer> Streamer = IPixelStreaming2Module::Get().FindStreamer("Editor"))
				{
					return !Streamer->IsStreaming();
				}
				return false;
			}));

		PluginCommands->MapAction(
			FPixelStreaming2Commands::Get().StreamEditor,
			FExecuteAction::CreateLambda([]() {
				IPixelStreaming2EditorModule::Get().StartStreaming(EPixelStreaming2EditorStreamTypes::Editor);
			}),
			FCanExecuteAction::CreateLambda([] {
				if (TSharedPtr<IPixelStreaming2Streamer> Streamer = IPixelStreaming2Module::Get().FindStreamer("Editor"))
				{
					return !Streamer->IsStreaming();
				}
				return false;
			}));

		PluginCommands->MapAction(
			FPixelStreaming2Commands::Get().StartSignalling,
			FExecuteAction::CreateLambda([]() {
				IPixelStreaming2EditorModule::Get().StartSignalling();
			}),
			FCanExecuteAction::CreateLambda([] {
				TSharedPtr<UE::PixelStreaming2Servers::IServer> SignallingServer = IPixelStreaming2EditorModule::Get().GetSignallingServer();
				if (!SignallingServer.IsValid() || !SignallingServer->HasLaunched())
				{
					return true;
				}
				return false;
			}));

		PluginCommands->MapAction(
			FPixelStreaming2Commands::Get().StopSignalling,
			FExecuteAction::CreateLambda([]() {
				IPixelStreaming2EditorModule::Get().StopSignalling();
			}),
			FCanExecuteAction::CreateLambda([] {
				TSharedPtr<UE::PixelStreaming2Servers::IServer> SignallingServer = IPixelStreaming2EditorModule::Get().GetSignallingServer();
				if (SignallingServer.IsValid() && SignallingServer->HasLaunched())
				{
					return true;
				}
				return false;
			}));

		PluginCommands->MapAction(
			FPixelStreaming2Commands::Get().VP8,
			FExecuteAction::CreateLambda([]() {
				SetCodec(EVideoCodec::VP8);
			}),
			FCanExecuteAction::CreateLambda([] {
				bool bCanChangeCodec = true;
				IPixelStreaming2Module::Get().ForEachStreamer([&bCanChangeCodec](TSharedPtr<IPixelStreaming2Streamer> Streamer) {
					bCanChangeCodec &= !Streamer->IsStreaming();
				});

				return bCanChangeCodec;
			}),
			FIsActionChecked::CreateLambda([]() {
				return GetCodec() == EVideoCodec::VP8;
			}));

		PluginCommands->MapAction(
			FPixelStreaming2Commands::Get().VP9,
			FExecuteAction::CreateLambda([]() {
				SetCodec(EVideoCodec::VP9);
			}),
			FCanExecuteAction::CreateLambda([] {
				bool bCanChangeCodec = true;
				IPixelStreaming2Module::Get().ForEachStreamer([&bCanChangeCodec](TSharedPtr<IPixelStreaming2Streamer> Streamer) {
					bCanChangeCodec &= !Streamer->IsStreaming();
				});

				return bCanChangeCodec;
			}),
			FIsActionChecked::CreateLambda([]() {
				return GetCodec() == EVideoCodec::VP9;
			}));

		PluginCommands->MapAction(
			FPixelStreaming2Commands::Get().H264,
			FExecuteAction::CreateLambda([]() {
				SetCodec(EVideoCodec::H264);
			}),
			FCanExecuteAction::CreateLambda([] {
				bool bCanChangeCodec = UE::PixelStreaming2::IsEncoderSupported<FVideoEncoderConfigH264>();
				IPixelStreaming2Module::Get().ForEachStreamer([&bCanChangeCodec](TSharedPtr<IPixelStreaming2Streamer> Streamer) {
					bCanChangeCodec &= !Streamer->IsStreaming();
				});

				return bCanChangeCodec;
			}),
			FIsActionChecked::CreateLambda([]() {
				return GetCodec() == EVideoCodec::H264;
			}));

		PluginCommands->MapAction(
			FPixelStreaming2Commands::Get().AV1,
			FExecuteAction::CreateLambda([]() {
				SetCodec(EVideoCodec::AV1);
			}),
			FCanExecuteAction::CreateLambda([] {
				bool bCanChangeCodec = UE::PixelStreaming2::IsEncoderSupported<FVideoEncoderConfigAV1>();
				IPixelStreaming2Module::Get().ForEachStreamer([&bCanChangeCodec](TSharedPtr<IPixelStreaming2Streamer> Streamer) {
					bCanChangeCodec &= !Streamer->IsStreaming();
				});

				return bCanChangeCodec;
			}),
			FIsActionChecked::CreateLambda([]() {
				return GetCodec() == EVideoCodec::AV1;
			}));

		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FPixelStreaming2Toolbar::RegisterMenus));
	}

	FPixelStreaming2Toolbar::~FPixelStreaming2Toolbar()
	{
		FPixelStreaming2Commands::Unregister();
	}

	void FPixelStreaming2Toolbar::RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		{
			UToolMenu* CustomToolBar = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User");
			{
				FToolMenuSection& Section = CustomToolBar->AddSection("PixelStreaming2");
				Section.AddSeparator("PixelStreaming2Seperator");
				{
					// Settings dropdown
					FToolMenuEntry SettingsEntry = FToolMenuEntry::InitComboButton(
						"PixelStreaming2Menus",
						FUIAction(),
						FOnGetContent::CreateLambda(
							[&]() {
								FMenuBuilder MenuBuilder(true, PluginCommands);

								// Use external signalling server option
								MenuBuilder.BeginSection("Signalling Server Location", LOCTEXT("PixelStreaming2SSLocation", "Signalling Server Location"));
								MenuBuilder.AddMenuEntry(FPixelStreaming2Commands::Get().ExternalSignalling);
								MenuBuilder.EndSection();

								if (!GetUseRemoteSignallingServer())
								{
									// Embedded Signalling Server Config (streamer port & http port)
									RegisterEmbeddedSignallingServerConfig(MenuBuilder);

									// Signalling Server Viewer URLs
									TSharedPtr<UE::PixelStreaming2Servers::IServer> SignallingServer = IPixelStreaming2EditorModule::Get().GetSignallingServer();
									if (SignallingServer.IsValid() && SignallingServer->HasLaunched())
									{
										RegisterSignallingServerURLs(MenuBuilder);
									}
								}
								else
								{
									// Remote Signalling Server Config (URL)
									RegisterRemoteSignallingServerConfig(MenuBuilder);
								}

								// Pixel Streaming streamer controls
								RegisterStreamerControls(MenuBuilder);

								// Codec Config
								RegisterCodecConfig(MenuBuilder);

								return MenuBuilder.MakeWidget();
							}),
						LOCTEXT("PixelStreaming2Menu", "Pixel Streaming"),
						LOCTEXT("PixelStreaming2MenuTooltip", "Configure Pixel Streaming"),
						FSlateIcon(FPixelStreaming2Style::GetStyleSetName(), "PixelStreaming2.Icon"),
						false,
						"PixelStreaming2Menu");
					SettingsEntry.StyleNameOverride = "CalloutToolbar";
					SettingsEntry.SetCommandList(PluginCommands);
					Section.AddEntry(SettingsEntry);
				}
			}
		}
	}

	void FPixelStreaming2Toolbar::RegisterEmbeddedSignallingServerConfig(FMenuBuilder& MenuBuilder)
	{
		MenuBuilder.BeginSection("Signalling Server Options", LOCTEXT("PixelStreaming2EmbeddedSSOptions", "Embedded Signalling Server Options"));

		TSharedPtr<UE::PixelStreaming2Servers::IServer> SignallingServer = IPixelStreaming2EditorModule::Get().GetSignallingServer();
		if (!SignallingServer.IsValid() || !SignallingServer->HasLaunched())
		{
			TSharedRef<SWidget> StreamerPortInputBlock = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
					  .AutoWidth()
					  .VAlign(VAlign_Center)
					  .Padding(FMargin(36.0f, 3.0f, 8.0f, 3.0f))
						  [SNew(STextBlock)
								  .Text(FText::FromString(TEXT("Streamer Port: ")))
								  .ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f)))]
				+ SHorizontalBox::Slot()
					  .AutoWidth()
						  [SNew(SNumericEntryBox<int32>)
								  .MinValue(1)
								  .Value_Lambda([]() {
									  return IPixelStreaming2EditorModule::Get().GetStreamerPort();
								  })
								  .OnValueChanged_Lambda([](int32 InStreamerPort) {
									  IPixelStreaming2EditorModule::Get().SetStreamerPort(InStreamerPort);
								  })
								  .OnValueCommitted_Lambda([](int32 InStreamerPort, ETextCommit::Type InCommitType) {
									  IPixelStreaming2EditorModule::Get().SetStreamerPort(InStreamerPort);
								  })];
			MenuBuilder.AddWidget(StreamerPortInputBlock, FText(), true);
			TSharedRef<SWidget> ViewerPortInputBlock = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
					  .AutoWidth()
					  .VAlign(VAlign_Center)
					  .Padding(FMargin(36.0f, 3.0f, 8.0f, 3.0f))
						  [SNew(STextBlock)
								  .Text(FText::FromString(TEXT("Viewer Port: ")))
								  .ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f)))]
				+ SHorizontalBox::Slot()
					  .AutoWidth()
						  [SNew(SNumericEntryBox<int32>)
								  .MinValue(1)
								  .Value_Lambda([]() {
									  return IPixelStreaming2EditorModule::Get().GetViewerPort();
								  })
								  .OnValueChanged_Lambda([](int32 InViewerPort) {
									  IPixelStreaming2EditorModule::Get().SetViewerPort(InViewerPort);
								  })
								  .OnValueCommitted_Lambda([](int32 InViewerPort, ETextCommit::Type InCommitType) {
									  IPixelStreaming2EditorModule::Get().SetViewerPort(InViewerPort);
								  })];
			MenuBuilder.AddWidget(ViewerPortInputBlock, FText(), true);
			MenuBuilder.AddMenuEntry(FPixelStreaming2Commands::Get().StartSignalling);
		}
		else
		{
			MenuBuilder.AddMenuEntry(FPixelStreaming2Commands::Get().StopSignalling);
		}

		MenuBuilder.EndSection();
	}

	void FPixelStreaming2Toolbar::RegisterRemoteSignallingServerConfig(FMenuBuilder& MenuBuilder)
	{
		TSharedPtr<UE::PixelStreaming2Servers::IServer> SignallingServer = IPixelStreaming2EditorModule::Get().GetSignallingServer();
		MenuBuilder.BeginSection("Remote Signalling Server Options", LOCTEXT("PixelStreaming2RemoteSSOptions", "Remote Signalling Server Options"));
		{
			TSharedRef<SWidget> URLInputBlock = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
					  .AutoWidth()
					  .VAlign(VAlign_Center)
					  .Padding(FMargin(36.0f, 3.0f, 8.0f, 3.0f))
						  [SNew(STextBlock)
								  .Text(FText::FromString(TEXT("Remote Signalling Server URL")))
								  .ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f)))]
				+ SHorizontalBox::Slot()
					  .AutoWidth()
						  [SNew(SEditableTextBox)
								  .Text_Lambda([]() {
									  TSharedPtr<IPixelStreaming2Streamer> Streamer = IPixelStreaming2Module::Get().FindStreamer("Editor");
									  return FText::FromString(Streamer->GetSignallingServerURL());
								  })
								  .OnTextChanged_Lambda([](const FText& InText) {
									  IPixelStreaming2Module::Get().ForEachStreamer([InText](TSharedPtr<IPixelStreaming2Streamer> Streamer) {
										  Streamer->SetSignallingServerURL(InText.ToString());
									  });
								  })
								  .OnTextCommitted_Lambda([](const FText& InText, ETextCommit::Type InTextCommit) {
									  IPixelStreaming2Module::Get().ForEachStreamer([InText](TSharedPtr<IPixelStreaming2Streamer> Streamer) {
										  Streamer->SetSignallingServerURL(InText.ToString());
									  });
								  })
								  .IsEnabled_Lambda([]() {
									  bool bCanChangeURL = true;
									  IPixelStreaming2Module::Get().ForEachStreamer([&bCanChangeURL](TSharedPtr<IPixelStreaming2Streamer> Streamer) {
										  bCanChangeURL &= !Streamer->IsStreaming();
									  });
									  return bCanChangeURL;
								  })];
			MenuBuilder.AddWidget(URLInputBlock, FText(), true);
		}
		MenuBuilder.EndSection();
	}

	void FPixelStreaming2Toolbar::RegisterSignallingServerURLs(FMenuBuilder& MenuBuilder)
	{
		MenuBuilder.BeginSection("Signalling Server URLs", LOCTEXT("PixelStreaming2SignallingURLs", "Signalling Server URLs"));
		{
			MenuBuilder.AddWidget(SNew(SBox)
									  .Padding(FMargin(16.0f, 3.0f))
										  [SNew(STextBlock)
												  .ColorAndOpacity(FSlateColor::UseSubduedForeground())
												  .Text(LOCTEXT("SignallingTip", "The Signalling Server is running and may be accessed via the following URLs (network settings permitting):"))
												  .WrapTextAt(400)],
				FText());

			MenuBuilder.AddWidget(SNew(SBox)
									  .Padding(FMargin(32.0f, 3.0f))
										  [SNew(STextBlock)
												  .ColorAndOpacity(FSlateColor::UseSubduedForeground())
												  .Text(FText::FromString(FString::Printf(TEXT("127.0.0.1:%d"), IPixelStreaming2EditorModule::Get().GetViewerPort())))],
				FText());

			TArray<TSharedPtr<FInternetAddr>> AdapterAddresses;
			if (ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLocalAdapterAddresses(AdapterAddresses))
			{
				for (TSharedPtr<FInternetAddr> AdapterAddress : AdapterAddresses)
				{
					MenuBuilder.AddWidget(SNew(SBox)
											  .Padding(FMargin(32.0f, 3.0f))
												  [SNew(STextBlock)
														  .ColorAndOpacity(FSlateColor::UseSubduedForeground())
														  .Text(FText::FromString(FString::Printf(TEXT("%s:%d"), *AdapterAddress->ToString(false), IPixelStreaming2EditorModule::Get().GetViewerPort())))],
						FText());
				}
			}
		}
		MenuBuilder.EndSection();
	}

	void FPixelStreaming2Toolbar::RegisterStreamerControls(FMenuBuilder& MenuBuilder)
	{
		IPixelStreaming2Module::Get().ForEachStreamer([&](TSharedPtr<IPixelStreaming2Streamer> Streamer) {
			FString StreamerId = Streamer->GetId();
			MenuBuilder.BeginSection(FName(*StreamerId), FText::FromString(FString::Printf(TEXT("Streamer - %s"), *StreamerId)));
			{

				if (Streamer->IsStreaming())
				{
					FString VideoProducer = TEXT("nothing (no video input)");
					if (TSharedPtr<IPixelStreaming2VideoProducer> Video = Streamer->GetVideoProducer().Pin())
					{
						VideoProducer = Video->ToString();
					}

					MenuBuilder.AddWidget(SNew(SBox)
											  .Padding(FMargin(16.0f, 3.0f))
												  [SNew(STextBlock)
														  .ColorAndOpacity(FSlateColor::UseSubduedForeground())
														  .Text(FText::FromString(FString::Printf(TEXT("Streaming %s"), *VideoProducer)))
														  .WrapTextAt(400)],
						FText());

					MenuBuilder.AddMenuEntry(
						LOCTEXT("PixelStreaming2_StopStreaming", "Stop Streaming"),
						LOCTEXT("PixelStreaming2_StopStreamingToolTip", "Stop this streamer"),
						FSlateIcon(),
						FExecuteAction::CreateLambda([Streamer]() {
							Streamer->StopStreaming();
						}));
				}
				else
				{
					if (Streamer->GetId() == "Editor")
					{
						MenuBuilder.AddMenuEntry(FPixelStreaming2Commands::Get().StreamLevelEditor);
						MenuBuilder.AddMenuEntry(FPixelStreaming2Commands::Get().StreamEditor);
					}
					else
					{
						MenuBuilder.AddMenuEntry(
							LOCTEXT("PixelStreaming2_StartStreaming", "Start Streaming"),
							LOCTEXT("PixelStreaming2_StartStreamingToolTip", "Start this streamer"),
							FSlateIcon(),
							FExecuteAction::CreateLambda([Streamer]() {
								Streamer->StartStreaming();
							}));
					}
				}
			}
			MenuBuilder.EndSection();
		});
	}

	void FPixelStreaming2Toolbar::RegisterCodecConfig(FMenuBuilder& MenuBuilder)
	{
		MenuBuilder.BeginSection("Codec", LOCTEXT("PixelStreaming2CodecSettings", "Codec"));
		{
			MenuBuilder.AddMenuEntry(FPixelStreaming2Commands::Get().H264);
			MenuBuilder.AddMenuEntry(FPixelStreaming2Commands::Get().AV1);
			MenuBuilder.AddMenuEntry(FPixelStreaming2Commands::Get().VP8);
			MenuBuilder.AddMenuEntry(FPixelStreaming2Commands::Get().VP9);
		}
		MenuBuilder.EndSection();
	}

	TSharedRef<SWidget> FPixelStreaming2Toolbar::GeneratePixelStreaming2MenuContent(TSharedPtr<FUICommandList> InCommandList)
	{
		FToolMenuContext MenuContext(InCommandList);
		return UToolMenus::Get()->GenerateWidget("LevelEditor.LevelEditorToolBar.AddQuickMenu", MenuContext);
	}
} // namespace UE::EditorPixelStreaming2

#undef LOCTEXT_NAMESPACE