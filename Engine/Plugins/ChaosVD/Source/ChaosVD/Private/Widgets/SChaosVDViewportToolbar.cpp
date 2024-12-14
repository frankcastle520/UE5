// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SChaosVDViewportToolbar.h"

#include "ChaosVDCommands.h"

#include "ChaosVDPlaybackViewportClient.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "SCommonEditorViewportToolbarBase.h"
#include "SEditorViewport.h"
#include "ShowFlagFilter.h"
#include "ShowFlagMenuCommands.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/SChaosVDEditorViewportViewMenu.h"
#include "Widgets/SChaosVDPlaybackViewport.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "ChaosVisualDebugger"

namespace Chaos::VisualDebugger::Utils
{
	template <typename TNumericValue>
	TSharedRef<SWidget> GenerateSpinBoxMenuEntryWidget(const FText& ToolTipText, TNumericValue MinValue, TNumericValue MaxValue, typename SSpinBox<TNumericValue>::FOnValueChanged&& ValueChangedDelegate, const TAttribute<TNumericValue>&& ValueAttribute, const TAttribute<bool>&& EnabledAttribute)
	{
		return SNew(SBox)
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
						SNew(SSpinBox<TNumericValue>)
						.Style(&FAppStyle::Get(), "Menu.SpinBox")
						.ToolTipText(ToolTipText)
						.MinValue(MinValue)
						.MaxValue(MaxValue)
						.Font(FAppStyle::GetFontStyle(TEXT("MenuItem.Font")))
						.Value(ValueAttribute)
						.OnValueChanged(ValueChangedDelegate)
						.IsEnabled(EnabledAttribute)
					]
				]
			];
	}

}

void SChaosVDViewportToolbar::Construct(const FArguments& InArgs, TSharedPtr<ICommonEditorViewportToolbarInfoProvider> InInfoProvider)
{
	SCommonEditorViewportToolbarBase::Construct(SCommonEditorViewportToolbarBase::FArguments(), InInfoProvider);
}

TSharedRef<SEditorViewportViewMenu> SChaosVDViewportToolbar::MakeViewMenu()
{
	TSharedRef<SEditorViewport> ViewportRef = GetInfoProvider().GetViewportWidget();
	return SNew(SChaosVDEditorViewportViewMenu, ViewportRef, SharedThis(this));
}

void SChaosVDViewportToolbar::ExtendOptionsMenu(FMenuBuilder& OptionsMenuBuilder) const
{
	constexpr bool bInShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder CVDOptionsMenuBuilder(bInShouldCloseWindowAfterMenuSelection, GetInfoProvider().GetViewportWidget()->GetCommandList());
	
	CVDOptionsMenuBuilder.BeginSection("CVDViewportViewportOptions", LOCTEXT("ViewportOptionsMenuHeader", "Viewport Options"));
	{
		CVDOptionsMenuBuilder.AddSubMenu(
			LOCTEXT("FrameRateOptionsMenuLabel", "Playback Framerate"),
			LOCTEXT("FrameRateOptionsMenuToolTip", "Options that control how CVD plays a recording."),
			FNewMenuDelegate::CreateSP(this, &SChaosVDViewportToolbar::PopulateFrameRateSubmenu),
			false,
			FSlateIcon(FAppStyle::Get().GetStyleSetName(), TEXT("EditorViewport.ToggleFPS")
		));

		CVDOptionsMenuBuilder.AddSubMenu(
		LOCTEXT("ObjectTrackingMenuLabel", "Object Tracking"),
		LOCTEXT("ObjectTrackingMenuToolTip", "Options that control how objects are tracked in the scene by the camera."),
		FNewMenuDelegate::CreateSP(this, &SChaosVDViewportToolbar::PopulateAutoTrackingSubMenu),
		false,
		FSlateIcon(FAppStyle::Get().GetStyleSetName(), TEXT("AnimViewportMenu.CameraFollow.Small"))
		);

		CVDOptionsMenuBuilder.AddSeparator();

		CVDOptionsMenuBuilder.AddWidget(GenerateFOVMenu(), LOCTEXT("FOVAngle", "Field of View (H)"));
		CVDOptionsMenuBuilder.AddWidget(GenerateFarViewPlaneMenu(), LOCTEXT("FarViewPlane", "Far View Plane"));

		CVDOptionsMenuBuilder.AddSeparator();

		CVDOptionsMenuBuilder.AddMenuEntry(FChaosVDCommands::Get().AllowTranslucentSelection, NAME_None);
		
	}
	CVDOptionsMenuBuilder.EndSection();

	CVDOptionsMenuBuilder.BeginSection("CVDViewportViewportUtils", LOCTEXT("ViewportUtilMenuHeader", "Utils"));
	{
		CVDOptionsMenuBuilder.AddWidget(GenerateGoToLocationWidget(), LOCTEXT("GoToLocation", "Go to Location"));
	}
	CVDOptionsMenuBuilder.EndSection();

	OptionsMenuBuilder = CVDOptionsMenuBuilder;
}

TSharedRef<SWidget> SChaosVDViewportToolbar::GenerateGoToLocationWidget() const
{
	return SNew(SBox)
			.HAlign(HAlign_Right)
			[
				SNew(SBox)
				.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
				.WidthOverride(100.0f)
				[
					SNew (SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("Menu.WidgetBorder"))
					.Padding(FMargin(1.0f))
					[
						SNew(SEditableText)
						.ToolTipText(LOCTEXT("GoToLocationTooltip", "Location to teleport to."))
						.Font(FAppStyle::GetFontStyle(TEXT("MenuItem.Font")))
						.OnTextCommitted(FOnTextCommitted::CreateSP(this, &SChaosVDViewportToolbar::HandleGoToLocationCommited))
					]
				]
			];
}

TSharedRef<SWidget> SChaosVDViewportToolbar::GenerateFrameRateOverrideValueWidget() const
{
	TAttribute<int32> ValueAttribute;
	ValueAttribute.Bind(TAttribute<int32>::FGetter::CreateSP(this, &SChaosVDViewportToolbar::OnGetFrameRateOverrideValue));
	
	TAttribute<bool> EnabledAttribute;
	EnabledAttribute.Bind(TAttribute<bool>::FGetter::CreateSP(this, &SChaosVDViewportToolbar::IsUsingFrameRateOverride));

	SSpinBox<int32>::FOnValueChanged ValueChangedDelegate = SSpinBox<int32>::FOnValueChanged::CreateSP(this, &SChaosVDViewportToolbar::OnFrameRateOverrideValueChanged);

	constexpr int32 MinValue = 1;
	constexpr int32 MaxValue = 1000;

	return Chaos::VisualDebugger::Utils::GenerateSpinBoxMenuEntryWidget(LOCTEXT("FramerateOverrideTooltip", "Target framerate we should play the loaded recording at"), MinValue, MaxValue, MoveTemp(ValueChangedDelegate), MoveTemp(ValueAttribute), MoveTemp(EnabledAttribute));
}

TSharedRef<SWidget> SChaosVDViewportToolbar::GenerateTrackingDistanceValueWidget() const
{
	TAttribute<float> ValueAttribute;
	ValueAttribute.Bind(TAttribute<float>::FGetter::CreateSP(this, &SChaosVDViewportToolbar::OnGetTrackingDistanceValue));
	
	TAttribute<bool> EnabledAttribute;
	EnabledAttribute.Bind(TAttribute<bool>::FGetter::CreateSP(this, &SChaosVDViewportToolbar::IsAutoTrackingEnabled));

	SSpinBox<float>::FOnValueChanged ValueChangedDelegate = SSpinBox<float>::FOnValueChanged::CreateSP(this, &SChaosVDViewportToolbar::OnTrackingDistanceValueChanged);

	constexpr float MinValue = 1.0f;
	constexpr float MaxValue = 100000.0f;

	return Chaos::VisualDebugger::Utils::GenerateSpinBoxMenuEntryWidget(LOCTEXT("TrackingDistanceTooltip", "Distance from which we want to track the selected object"), MinValue, MaxValue, MoveTemp(ValueChangedDelegate), MoveTemp(ValueAttribute), MoveTemp(EnabledAttribute));
}

int32 SChaosVDViewportToolbar::OnGetFrameRateOverrideValue() const
{
	TSharedRef<SChaosVDPlaybackViewport> ViewportRef = StaticCastSharedRef<SChaosVDPlaybackViewport>(GetInfoProvider().GetViewportWidget());
	return ViewportRef->GetCurrentTargetFrameRateOverride();
}

void SChaosVDViewportToolbar::OnFrameRateOverrideValueChanged(int32 NewFrameRate) const
{
	TSharedRef<SChaosVDPlaybackViewport> ViewportRef = StaticCastSharedRef<SChaosVDPlaybackViewport>(GetInfoProvider().GetViewportWidget());
	return ViewportRef->SetCurrentTargetFrameRateOverride(NewFrameRate);
}

bool SChaosVDViewportToolbar::IsUsingFrameRateOverride() const
{
	TSharedRef<SChaosVDPlaybackViewport> ViewportRef = StaticCastSharedRef<SChaosVDPlaybackViewport>(GetInfoProvider().GetViewportWidget());
	return ViewportRef->IsUsingFrameRateOverride();
}

float SChaosVDViewportToolbar::OnGetTrackingDistanceValue() const
{
	const TSharedPtr<FChaosVDPlaybackViewportClient> CVDViewportClient = StaticCastSharedPtr<FChaosVDPlaybackViewportClient>(GetInfoProvider().GetViewportWidget()->GetViewportClient());

	return CVDViewportClient ? CVDViewportClient->GetAutoTrackingViewDistance() : -1.0f;
}

void SChaosVDViewportToolbar::OnTrackingDistanceValueChanged(float NewTrackingDistance) const
{
	if (const TSharedPtr<FChaosVDPlaybackViewportClient> CVDViewportClient = StaticCastSharedPtr<FChaosVDPlaybackViewportClient>(GetInfoProvider().GetViewportWidget()->GetViewportClient()))
	{
		CVDViewportClient->SetAutoTrackingViewDistance(NewTrackingDistance);
	}
}

bool SChaosVDViewportToolbar::IsAutoTrackingEnabled() const
{
	const TSharedPtr<FChaosVDPlaybackViewportClient> CVDViewportClient = StaticCastSharedPtr<FChaosVDPlaybackViewportClient>(GetInfoProvider().GetViewportWidget()->GetViewportClient());

	return CVDViewportClient ? CVDViewportClient->IsAutoTrackingSelectedObject() : false;
}

void SChaosVDViewportToolbar::PopulateFrameRateSubmenu(FMenuBuilder& MenuBuilder) const
{
	MenuBuilder.AddMenuEntry(FChaosVDCommands::Get().OverridePlaybackFrameRate, NAME_None);
	MenuBuilder.AddWidget(GenerateFrameRateOverrideValueWidget(), LOCTEXT("FrameRateOverride", "Target Framerate"));
}

void SChaosVDViewportToolbar::PopulateAutoTrackingSubMenu(FMenuBuilder& MenuBuilder) const
{
	MenuBuilder.AddMenuEntry(FChaosVDCommands::Get().ToggleFollowSelectedObject, NAME_None);
	MenuBuilder.AddWidget(GenerateTrackingDistanceValueWidget(), LOCTEXT("TrackingDistance", "Follow Distance"));
}

TSharedRef<SWidget> SChaosVDViewportToolbar::GenerateShowMenu() const
{	
	GetInfoProvider().OnFloatingButtonClicked();

	if (!UToolMenus::Get()->IsMenuRegistered(ShowMenuName))
	{
		if (UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(ShowMenuName))
		{
			FToolMenuSection& Section = Menu->AddSection("ChaosVDViewportToolbarBase.Show.CommonViewportFlags", LOCTEXT("ToolbarCommonViewportFlags", "Common Show Flags"));

			FNewToolMenuDelegate CustomShowMenuBuilder = FNewToolMenuDelegate::CreateLambda([](UToolMenu* Menu)
															   {
																	// Only include the flags that might be helpful.
																	static const FShowFlagFilter ShowFlagFilter = FShowFlagFilter(FShowFlagFilter::ExcludeAllFlagsByDefault)
																		.IncludeFlag(FEngineShowFlags::SF_AntiAliasing)
																		.IncludeFlag(FEngineShowFlags::SF_Grid)
																		.IncludeFlag(FEngineShowFlags::SF_Translucency)
																		.IncludeFlag(FEngineShowFlags::SF_MeshEdges)
																		.IncludeFlag(FEngineShowFlags::SF_HitProxies)
																		.IncludeFlag(FEngineShowFlags::SF_Fog)
																		.IncludeFlag(FEngineShowFlags::SF_Pivot);

																	FShowFlagMenuCommands::Get().BuildShowFlagsMenu(Menu, ShowFlagFilter);
															   });

			constexpr bool bOpenSubMenuOnClick = false;
			Section.AddSubMenu(TEXT("CommonViewportFlags"), LOCTEXT("CommonShowFlagsMenuLabel", "Common Show Flags"), LOCTEXT("CommonShowFlagsMenuToolTip", "Set of flags to enable/disable specific viewport features"), CustomShowMenuBuilder,
							   bOpenSubMenuOnClick, FSlateIcon(FAppStyle::Get().GetStyleSetName(), TEXT("Icons.Toolbar.Settings")));
		}
	}

	FToolMenuContext NewMenuContext;
	UCommonViewportToolbarBaseMenuContext* ContextObject = NewObject<UCommonViewportToolbarBaseMenuContext>();
	ContextObject->ToolbarWidget = SharedThis(this);
	NewMenuContext.AddObject(ContextObject);

	if (TSharedPtr<SEditorViewport> ViewportWidget = GetInfoProvider().GetViewportWidget())
	{
		NewMenuContext.AppendCommandList(GetInfoProvider().GetViewportWidget()->GetCommandList());
	}

	return UToolMenus::Get()->GenerateWidget(ShowMenuName, NewMenuContext);
}

void SChaosVDViewportToolbar::HandleGoToLocationCommited(const FText& InLocationAsText, ETextCommit::Type Type) const
{
	if (Type != ETextCommit::OnEnter)
	{
		return;
	}

	FVector Location;
	Location.InitFromString(InLocationAsText.ToString());

	TSharedRef<SChaosVDPlaybackViewport> ViewportRef = StaticCastSharedRef<SChaosVDPlaybackViewport>(GetInfoProvider().GetViewportWidget());

	ViewportRef->GoToLocation(Location);
	
}
#undef LOCTEXT_NAMESPACE
