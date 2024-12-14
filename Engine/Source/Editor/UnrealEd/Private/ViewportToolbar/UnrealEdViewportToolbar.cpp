// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewportToolbar/UnrealEdViewportToolbar.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "DebugViewModeHelpers.h"
#include "Editor/EditorPerformanceSettings.h"
#include "EditorViewportClient.h"
#include "EditorViewportCommands.h"
#include "Framework/Commands/GenericCommands.h"
#include "GPUSkinCache.h"
#include "GPUSkinCacheVisualizationMenuCommands.h"
#include "IPreviewProfileController.h"
#include "LevelEditorActions.h"
#include "RayTracingDebugVisualizationMenuCommands.h"
#include "SEditorViewport.h"
#include "ShowFlagMenuCommands.h"
#include "Settings/EditorProjectSettings.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "Styling/SlateIconFinder.h"
#include "Templates/SharedPointer.h"
#include "ToolMenu.h"
#include "ToolMenuEntry.h"
#include "ToolMenuSection.h"
#include "ToolMenus.h"
#include "ViewportToolbar/UnrealEdViewportToolbarContext.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SToolTip.h"

#define LOCTEXT_NAMESPACE "UnrealEdViewportToolbar"

namespace UE::UnrealEd::Private
{
int32 CVarToolMenusViewportToolbarsValue = 0;

FToolUIAction DisabledAction()
{
	static FToolUIAction Action;
	Action.CanExecuteAction = FToolMenuCanExecuteAction::CreateLambda(
		[](const FToolMenuContext&)
		{
			return false;
		}
	);

	return Action;
}

// TODO: Maybe export CreateSurfaceSnapOffsetEntry function, so that it can be used elsewhere, e.g. STransformViewportToolbar.cpp
FToolMenuEntry CreateSurfaceSnapOffsetEntry()
{
	FText Label = LOCTEXT("SurfaceOffsetLabel", "Surface Offset");
	FText Tooltip = LOCTEXT("SurfaceOffsetTooltip", "The amount of offset to apply when snapping to surfaces");

	const FMargin WidgetsMargin(2.0f, 0.0f, 3.0f, 0.0f);

	FToolMenuEntry SurfaceOffset = FToolMenuEntry::InitMenuEntry(
		"SurfaceOffset",
		FUIAction(
			FExecuteAction(),
			FCanExecuteAction::CreateLambda(
				[]()
				{
					return GetDefault<ULevelEditorViewportSettings>()->SnapToSurface.bEnabled;
				}
			)
		),
		// clang-format off
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		.Padding(WidgetsMargin)
		.AutoWidth()
		[
			SNew(STextBlock)
			.Text(Label)
		]
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.Padding(WidgetsMargin)
		.AutoWidth()
		[
			SNew(SBox)
			.Padding(WidgetsMargin)
			.MinDesiredWidth(100.0f)
			[
				// Min/Max/Slider values taken from STransformViewportToolbar.cpp
				SNew(SNumericEntryBox<float>)
				.ToolTipText(Tooltip)
				.MinValue(0.0f)
				.MaxValue(static_cast<float>(HALF_WORLD_MAX))
				.MaxSliderValue(1000.0f)
				.AllowSpin(true)
				.MaxFractionalDigits(2)
				.Font(FAppStyle::GetFontStyle(TEXT("MenuItem.Font")))
				.OnValueChanged_Lambda([](float InNewValue)
				{
					ULevelEditorViewportSettings* Settings =
						GetMutableDefault<ULevelEditorViewportSettings>();
					Settings->SnapToSurface.SnapOffsetExtent = InNewValue;
				})
				.Value_Lambda([]()
				{
					return GetDefault<ULevelEditorViewportSettings>()->SnapToSurface.SnapOffsetExtent;
				})
			]
		]
		// clang-format on
	);

	return SurfaceOffset;
}

FToolMenuEntry CreateSurfaceSnapCheckboxMenu()
{
	FNewToolMenuDelegate MakeMenuDelegate = FNewToolMenuDelegate::CreateLambda(
		[](UToolMenu* Submenu)
		{
			FToolMenuSection& SurfaceSnappingSection =
				Submenu->FindOrAddSection("SurfaceSnapping", LOCTEXT("SurfaceSnappingLabel", "Surface Snapping"));

			// Add "Rotate to surface normal" checkbox.
			{
				FToolMenuEntry RotateToSurfaceNormalSnapping =
					FToolMenuEntry::InitMenuEntry(FEditorViewportCommands::Get().RotateToSurfaceNormal);
				SurfaceSnappingSection.AddEntry(RotateToSurfaceNormalSnapping);
			}

			// Add "Surface offset" widget.
			{
				SurfaceSnappingSection.AddEntry(CreateSurfaceSnapOffsetEntry());
			}
		}
	);

	FToolMenuEntry Entry = UnrealEd::CreateCheckboxSubmenu(
		"SurfaceSnapping",
		LOCTEXT("SurfaceSnapLabel", "Surface"),
		FEditorViewportCommands::Get().SurfaceSnapping->MakeTooltip()->GetTextTooltip(),
		FToolMenuExecuteAction::CreateLambda(
			[](const FToolMenuContext& InContext)
			{
				ULevelEditorViewportSettings* Settings = GetMutableDefault<ULevelEditorViewportSettings>();
				Settings->SnapToSurface.bEnabled = !Settings->SnapToSurface.bEnabled;
			}
		),
		FToolMenuCanExecuteAction(),
		FToolMenuGetActionCheckState::CreateLambda(
			[](const FToolMenuContext& InContext)
			{
				return GetDefault<ULevelEditorViewportSettings>()->SnapToSurface.bEnabled ? ECheckBoxState::Checked
																						  : ECheckBoxState::Unchecked;
			}
		),
		MakeMenuDelegate
	);

	Entry.ToolBarData.LabelOverride = TAttribute<FText>::CreateLambda(
		[]()
		{
			const ULevelEditorViewportSettings* const Settings = GetMutableDefault<ULevelEditorViewportSettings>();
			return FText::AsNumber(Settings->SnapToSurface.SnapOffsetExtent);
		}
	);
	Entry.Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.ToggleSurfaceSnapping");

	return Entry;
}

FToolMenuEntry CreateActorSnapDistanceEntry()
{
	const FText Label = LOCTEXT("ActorSnapDistanceLabel", "Snap Distance");
	const FText Tooltip = LOCTEXT("ActorSnapDistanceTooltip", "The amount of offset to apply when snapping to surfaces");

	const FMargin WidgetsMargin(2.0f, 0.0f, 3.0f, 0.0f);

	FToolMenuEntry SnapDistance = FToolMenuEntry::InitMenuEntry(
		"ActorSnapDistance",
		FUIAction(
			FExecuteAction(),
			FCanExecuteAction::CreateLambda(
				[]()
				{
					return !!GetDefault<ULevelEditorViewportSettings>()->bEnableActorSnap;
				}
			)
		),
		// clang-format off
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.Padding(WidgetsMargin)
		.AutoWidth()
		[
			SNew(STextBlock)
			.Text(Label)
		]
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.Padding(WidgetsMargin)
		.AutoWidth()
		[
			SNew(SBox).Padding(WidgetsMargin).MinDesiredWidth(100.0f)
			[
				// TODO: Check how to improve performance for this widget OnValueChanged.
				// Same functionality in LevelEditorToolBar.cpp seems to have better performance
				SNew(SNumericEntryBox<float>)
				.ToolTipText(Tooltip)
				.MinValue(0.0f)
				.MaxValue(1.0f)
				.MaxSliderValue(1.0f)
				.AllowSpin(true)
				.MaxFractionalDigits(1)
				.Font(FAppStyle::GetFontStyle(TEXT("MenuItem.Font")))
				.OnValueChanged_Static(&FLevelEditorActionCallbacks::SetActorSnapSetting)
				.Value_Lambda([]()
				{
					return FLevelEditorActionCallbacks::GetActorSnapSetting();
				})
			]
		]
		// clang-format on
	);

	return SnapDistance;
}

FToolMenuEntry CreateActorSnapCheckboxMenu()
{
	FNewToolMenuDelegate MakeMenuDelegate = FNewToolMenuDelegate::CreateLambda(
		[](UToolMenu* Submenu)
		{
			// Add "Actor snapping" widget.
			{
				FToolMenuSection& ActorSnappingSection =
					Submenu->FindOrAddSection("ActorSnapping", LOCTEXT("ActorSnappingLabel", "Actor Snapping"));
				ActorSnappingSection.AddEntry(CreateActorSnapDistanceEntry());
			}
		}
	);

	FToolUIAction CheckboxMenuAction;
	{
		CheckboxMenuAction.ExecuteAction = FToolMenuExecuteAction::CreateLambda(
			[](const FToolMenuContext& InContext)
			{
				if (ULevelEditorViewportSettings* Settings = GetMutableDefault<ULevelEditorViewportSettings>())
				{
					Settings->bEnableActorSnap = !Settings->bEnableActorSnap;
				}
			}
		);
		CheckboxMenuAction.GetActionCheckState = FToolMenuGetActionCheckState::CreateLambda(
			[](const FToolMenuContext& InContext)
			{
				return GetDefault<ULevelEditorViewportSettings>()->bEnableActorSnap ? ECheckBoxState::Checked
																					: ECheckBoxState::Unchecked;
			}
		);
	}

	FToolMenuEntry ActorSnapping = FToolMenuEntry::InitSubMenu(
		"ActorSnapping",
		LOCTEXT("ActorSnapLabel", "Actor"),
		FLevelEditorCommands::Get().EnableActorSnap->MakeTooltip()->GetTextTooltip(),
		MakeMenuDelegate,
		CheckboxMenuAction,
		EUserInterfaceActionType::ToggleButton
	);

	return ActorSnapping;
}

FToolMenuEntry CreateLocationSnapCheckboxMenu()
{
	const FName LocationSnapName = "LocationSnap";
	const FText LocationSnapLabel = LOCTEXT("LocationSnapLabel", "Location");

	FToolMenuEntry Entry;

	if (!GEditor)
	{
		Entry = FToolMenuEntry::InitMenuEntry(
			LocationSnapName, LocationSnapLabel, FText(), FSlateIcon(), UnrealEd::Private::DisabledAction()
		);
	}
	else
	{

		FNewToolMenuDelegate MakeMenuDelegate = FNewToolMenuDelegate::CreateLambda(
			[LocationSnapName](UToolMenu* InToolMenu)
			{
				UnrealEd::FLocationGridCheckboxListExecuteActionDelegate ExecuteDelegate =
					UnrealEd::FLocationGridCheckboxListExecuteActionDelegate::CreateUObject(
						GEditor, &UEditorEngine::SetGridSize
					);

				UnrealEd::FLocationGridCheckboxListIsCheckedDelegate IsCheckedDelegate =
					UnrealEd::FLocationGridCheckboxListIsCheckedDelegate::CreateLambda(
						[](int CurrGridSizeIndex)
						{
							const ULevelEditorViewportSettings* ViewportSettings =
								GetDefault<ULevelEditorViewportSettings>();
							return ViewportSettings->CurrentPosGridSize == CurrGridSizeIndex;
						}
					);

				const ULevelEditorViewportSettings* ViewportSettings = GetDefault<ULevelEditorViewportSettings>();
				TArray<float> GridSizes = ViewportSettings->bUsePowerOf2SnapSize ? ViewportSettings->Pow2GridSizes
																				 : ViewportSettings->DecimalGridSizes;

				InToolMenu->AddMenuEntry(
					LocationSnapName,
					FToolMenuEntry::InitWidget(
						LocationSnapName,
						UnrealEd::CreateLocationGridSnapMenu(
							ExecuteDelegate,
							IsCheckedDelegate,
							GridSizes,
							TAttribute<bool>::CreateLambda(
								[]()
								{
									return FLevelEditorActionCallbacks::LocationGridSnap_IsChecked();
								}
							)
						),
						FText()
					)
				);
			}
		);

		Entry = UnrealEd::CreateCheckboxSubmenu(
			"GridSnapping",
			LocationSnapLabel,
			FEditorViewportCommands::Get().SurfaceSnapping->MakeTooltip()->GetTextTooltip(),
			FToolMenuExecuteAction::CreateLambda(
				[](const FToolMenuContext& InContext)
				{
					FLevelEditorActionCallbacks::LocationGridSnap_Clicked();
				}
			),
			FToolMenuCanExecuteAction(),
			FToolMenuGetActionCheckState::CreateLambda(
				[](const FToolMenuContext& InContext)
				{
					return FLevelEditorActionCallbacks::LocationGridSnap_IsChecked() ? ECheckBoxState::Checked
																					 : ECheckBoxState::Unchecked;
				}
			),
			MakeMenuDelegate
		);
	}

	Entry.ToolBarData.LabelOverride = TAttribute<FText>::Create(&UE::UnrealEd::GetLocationGridLabel);
	Entry.Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.LocationGridSnap");

	return Entry;
}

FToolMenuEntry CreateRotationSnapCheckboxMenu()
{
	const FName RotationSnapName = "RotationSnap";
	const FText RotationSnapLabel = LOCTEXT("RotationSnapLabel", "Rotation");

	if (!GEditor)
	{
		return FToolMenuEntry::InitMenuEntry(
			RotationSnapName, RotationSnapLabel, FText(), FSlateIcon(), UnrealEd::Private::DisabledAction()
		);
	}

	const FNewToolMenuDelegate MakeMenuDelegate = FNewToolMenuDelegate::CreateLambda(
		[RotationSnapName](UToolMenu* InToolMenu)
		{
			const UnrealEd::FRotationGridCheckboxListExecuteActionDelegate ExecuteDelegate =
				UnrealEd::FRotationGridCheckboxListExecuteActionDelegate::CreateUObject(
					GEditor, &UEditorEngine::SetRotGridSize
				);

			const UnrealEd::FRotationGridCheckboxListIsCheckedDelegate IsCheckedDelegate =
				UnrealEd::FRotationGridCheckboxListIsCheckedDelegate::CreateLambda(
					[](int CurrGridAngleIndex, ERotationGridMode InGridMode)
					{
						const ULevelEditorViewportSettings* ViewportSettings = GetDefault<ULevelEditorViewportSettings>();
						return ViewportSettings->CurrentRotGridSize == CurrGridAngleIndex
							&& ViewportSettings->CurrentRotGridMode == InGridMode;
					}
				);

			const TAttribute<bool> IsEnabledDelegate =
				TAttribute<bool>::Create(&FLevelEditorActionCallbacks::RotationGridSnap_IsChecked);

			InToolMenu->AddMenuEntry(
				RotationSnapName,
				FToolMenuEntry::InitWidget(
					RotationSnapName,
					UnrealEd::CreateRotationGridSnapMenu(ExecuteDelegate, IsCheckedDelegate, IsEnabledDelegate),
					FText()
				)
			);
		}
	);

	FToolMenuEntry Entry = UnrealEd::CreateCheckboxSubmenu(
		"RotationSnapping",
		RotationSnapLabel,
		FEditorViewportCommands::Get().SurfaceSnapping->MakeTooltip()->GetTextTooltip(),
		FToolMenuExecuteAction::CreateLambda(
			[](const FToolMenuContext& InContext)
			{
				FLevelEditorActionCallbacks::RotationGridSnap_Clicked();
			}
		),
		FToolMenuCanExecuteAction(),
		FToolMenuGetActionCheckState::CreateLambda(
			[](const FToolMenuContext& InContext)
			{
				return FLevelEditorActionCallbacks::RotationGridSnap_IsChecked() ? ECheckBoxState::Checked
																				 : ECheckBoxState::Unchecked;
			}
		),
		MakeMenuDelegate
	);

	Entry.ToolBarData.LabelOverride = TAttribute<FText>::Create(&UE::UnrealEd::GetRotationGridLabel);
	Entry.Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.RotationGridSnap");

	return Entry;
}

FToolMenuEntry CreateScaleSnapCheckboxMenu()
{
	const FName ScaleSnapName = "ScaleSnap";
	const FText ScaleSnapLabel = LOCTEXT("ScaleSnapLabel", "Scale");

	if (!GEditor)
	{
		return FToolMenuEntry::InitMenuEntry(
			ScaleSnapName, ScaleSnapLabel, FText(), FSlateIcon(), UnrealEd::Private::DisabledAction()
		);
	}

	FNewToolMenuDelegate MakeMenuDelegate = FNewToolMenuDelegate::CreateLambda(
		[ScaleSnapName](UToolMenu* InToolMenu)
		{
			FCanExecuteAction CanExecuteScaleSnapping = FCanExecuteAction::CreateLambda(
				[]()
				{
					return FLevelEditorActionCallbacks::ScaleGridSnap_IsChecked();
				}
			);

			const ULevelEditorViewportSettings* ViewportSettings = GetDefault<ULevelEditorViewportSettings>();
			TArray<float> GridSizes = ViewportSettings->ScalingGridSizes;

			UnrealEd::FScaleGridCheckboxListExecuteActionDelegate ExecuteDelegate =
				UnrealEd::FScaleGridCheckboxListExecuteActionDelegate::CreateUObject(GEditor, &UEditorEngine::SetScaleGridSize);

			UnrealEd::FScaleGridCheckboxListIsCheckedDelegate IsCheckedDelegate =
				UnrealEd::FScaleGridCheckboxListIsCheckedDelegate::CreateLambda(
					[](int CurrGridSizeIndex)
					{
						const ULevelEditorViewportSettings* ViewportSettings = GetDefault<ULevelEditorViewportSettings>();
						return ViewportSettings->CurrentScalingGridSize == CurrGridSizeIndex;
					}
				);

			InToolMenu->AddMenuEntry(
				ScaleSnapName,
				FToolMenuEntry::InitWidget(
					ScaleSnapName,
					UnrealEd::CreateScaleGridSnapMenu(
						ExecuteDelegate,
						IsCheckedDelegate,
						GridSizes,
						TAttribute<bool>::CreateLambda(
							[]()
							{
								return FLevelEditorActionCallbacks::ScaleGridSnap_IsChecked();
							}
						),
						{},
						true,
						FUIAction(
							FExecuteAction::CreateLambda(
								[]()
								{
									ULevelEditorViewportSettings* Settings =
										GetMutableDefault<ULevelEditorViewportSettings>();
									Settings->PreserveNonUniformScale = !Settings->PreserveNonUniformScale;
								}
							),
							CanExecuteScaleSnapping,
							FIsActionChecked::CreateLambda(
								[]()
								{
									return GetDefault<ULevelEditorViewportSettings>()->PreserveNonUniformScale;
								}
							)
						)
					),
					FText()
				)
			);
		}
	);

	FToolMenuEntry Entry = UnrealEd::CreateCheckboxSubmenu(
		"ScaleSnapping",
		ScaleSnapLabel,
		FEditorViewportCommands::Get().SurfaceSnapping->MakeTooltip()->GetTextTooltip(),
		FToolMenuExecuteAction::CreateLambda(
			[](const FToolMenuContext& InContext)
			{
				FLevelEditorActionCallbacks::ScaleGridSnap_Clicked();
			}
		),
		FToolMenuCanExecuteAction(),
		FToolMenuGetActionCheckState::CreateLambda(
			[](const FToolMenuContext& InContext)
			{
				return FLevelEditorActionCallbacks::ScaleGridSnap_IsChecked() ? ECheckBoxState::Checked
																			  : ECheckBoxState::Unchecked;
			}
		),
		MakeMenuDelegate
	);

	Entry.ToolBarData.LabelOverride = TAttribute<FText>::Create(&UE::UnrealEd::GetScaleGridLabel);
	Entry.Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.ScaleGridSnap");

	return Entry;
}

bool IsViewModeSupported(EViewModeIndex InViewModeIndex)
{
	switch (InViewModeIndex)
	{
	case VMI_PrimitiveDistanceAccuracy:
	case VMI_MaterialTextureScaleAccuracy:
	case VMI_RequiredTextureResolution:
		return false;
	default:
		return true;
	}
}

}

static FAutoConsoleVariableRef CVarToolMenusViewportToolbars(
	TEXT("ToolMenusViewportToolbars"),
	UE::UnrealEd::Private::CVarToolMenusViewportToolbarsValue,
	TEXT("Control whether the new ToolMenus-based viewport toolbars are enabled across the editor. Set to 0 (default) "
		 "to show only the old viewport toolbars. Set to 1 for side-by-side mode where both the old and new viewport "
		 "toolbars are shown. Set to 2 to show only the new viewport toolbars."),
	ECVF_Default
);

namespace UE::UnrealEd
{

bool ShowOldViewportToolbars()
{
	return Private::CVarToolMenusViewportToolbarsValue <= 1;
}

bool ShowNewViewportToolbars()
{
	return Private::CVarToolMenusViewportToolbarsValue >= 1;
}

FSlateIcon GetIconFromCoordSystem(ECoordSystem InCoordSystem)
{
	if (InCoordSystem == COORD_World)
	{
		static FName WorldIcon("EditorViewport.RelativeCoordinateSystem_World");
		return FSlateIcon(FAppStyle::GetAppStyleSetName(), WorldIcon);
	}
	else if (InCoordSystem == COORD_Parent)
	{
		static const FName ParentIcon("Icons.ConstraintManager.ParentHierarchy");
		return FSlateIcon(FAppStyle::GetAppStyleSetName(), ParentIcon);
	}
	else
	{
		static FName LocalIcon("Icons.Transform");
		return FSlateIcon(FAppStyle::GetAppStyleSetName(), LocalIcon);
	}
}

FToolMenuEntry CreateViewportToolbarTransformsSection()
{
	FToolMenuEntry Entry = FToolMenuEntry::InitSubMenu(
		"Transform",
		LOCTEXT("TransformsSubmenuLabel", "Transform"),
		LOCTEXT("TransformsSubmenuTooltip", "Viewport-related transforms tools"),
		FNewToolMenuDelegate::CreateLambda(
			[](UToolMenu* Submenu) -> void
			{
				{
					FToolMenuSection& TransformToolsSection =
						Submenu->FindOrAddSection("TransformTools", LOCTEXT("TransformToolsLabel", "Transform Tools"));

					FToolMenuEntry SelectMode = FToolMenuEntry::InitMenuEntry(FEditorViewportCommands::Get().SelectMode);
					SelectMode.UserInterfaceActionType = EUserInterfaceActionType::RadioButton;
					SelectMode.SetShowInToolbarTopLevel(true);
					TransformToolsSection.AddEntry(SelectMode);

					FToolMenuEntry TranslateMode =
						FToolMenuEntry::InitMenuEntry(FEditorViewportCommands::Get().TranslateMode);
					TranslateMode.UserInterfaceActionType = EUserInterfaceActionType::RadioButton;
					TranslateMode.SetShowInToolbarTopLevel(true);
					TransformToolsSection.AddEntry(TranslateMode);

					FToolMenuEntry RotateMode = FToolMenuEntry::InitMenuEntry(FEditorViewportCommands::Get().RotateMode);
					RotateMode.UserInterfaceActionType = EUserInterfaceActionType::RadioButton;
					RotateMode.SetShowInToolbarTopLevel(true);
					TransformToolsSection.AddEntry(RotateMode);

					FToolMenuEntry ScaleMode = FToolMenuEntry::InitMenuEntry(FEditorViewportCommands::Get().ScaleMode);
					ScaleMode.UserInterfaceActionType = EUserInterfaceActionType::RadioButton;
					ScaleMode.SetShowInToolbarTopLevel(true);
					TransformToolsSection.AddEntry(ScaleMode);

					// Build a submenu for selecting the coordinate system to use.
					{
						TransformToolsSection.AddSeparator("CoordinateSystemSeparator");

						FToolMenuEntry& CoordianteSystemSubmenu = TransformToolsSection.AddSubMenu(
							"CoordinateSystem",
							LOCTEXT("CoordinateSystemLabel", "Coordinate System"),
							LOCTEXT("CoordinateSystemTooltip", "Select between coordinate systems"),
							FNewToolMenuDelegate::CreateLambda(
								[](UToolMenu* InSubmenu)
								{
									FToolMenuSection& UnnamedSection = InSubmenu->FindOrAddSection(NAME_None);

									FToolMenuEntry WorldCoords = FToolMenuEntry::InitMenuEntry(
										FEditorViewportCommands::Get().RelativeCoordinateSystem_World
									);
									WorldCoords.UserInterfaceActionType = EUserInterfaceActionType::RadioButton;
									UnnamedSection.AddEntry(WorldCoords);

									FToolMenuEntry LocalCoords = FToolMenuEntry::InitMenuEntry(
										FEditorViewportCommands::Get().RelativeCoordinateSystem_Local
									);
									LocalCoords.UserInterfaceActionType = EUserInterfaceActionType::RadioButton;
									UnnamedSection.AddEntry(LocalCoords);
								}
							)
						);

						// Set the icon based on the current coordinate system and fall back to the Local icon.
						{
							TWeakPtr<SEditorViewport> WeakViewport;
							if (UUnrealEdViewportToolbarContext* const Context =
									Submenu->FindContext<UUnrealEdViewportToolbarContext>())
							{
								WeakViewport = Context->Viewport;
							}

							CoordianteSystemSubmenu.Icon = TAttribute<FSlateIcon>::CreateLambda(
								[WeakViewport]() -> FSlateIcon
								{
									ECoordSystem CoordSystem = ECoordSystem::COORD_Local;
									if (const TSharedPtr<SEditorViewport> EditorViewport = WeakViewport.Pin())
									{
										CoordSystem = EditorViewport->GetViewportClient()->GetWidgetCoordSystemSpace();
									}
									return GetIconFromCoordSystem(CoordSystem);
								}
							);
						}
						CoordianteSystemSubmenu.ToolBarData.LabelOverride = FText();
						CoordianteSystemSubmenu.SetShowInToolbarTopLevel(true);
					}
				}

				{
					FToolMenuSection& GizmoSection = Submenu->FindOrAddSection("Gizmo", LOCTEXT("GizmoLabel", "Gizmo"));

					GizmoSection.AddMenuEntry(
						FLevelEditorCommands::Get().ShowTransformWidget,
						LOCTEXT("ShowTransformGizmoLabel", "Show Transform Gizmo")
					);

					TSharedRef<SWidget> GizmoScaleWidget =
						// clang-format off
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(0.9f)
						[
							SNew(SSpinBox<int32>)
								.MinValue(-10)
								.MaxValue(150)
								.ToolTipText_Lambda(
									[]() -> FText
									{
										return FText::AsNumber(
											GetDefault<ULevelEditorViewportSettings>()->TransformWidgetSizeAdjustment
										);
									}
								)
								.Value_Lambda(
									[]() -> float
									{
										return GetDefault<ULevelEditorViewportSettings>()->TransformWidgetSizeAdjustment;
									}
								)
								.OnValueChanged_Lambda(
									[](float InValue)
									{
										ULevelEditorViewportSettings* ViewportSettings =
											GetMutableDefault<ULevelEditorViewportSettings>();
										ViewportSettings->TransformWidgetSizeAdjustment = InValue;
										ViewportSettings->PostEditChange();
									}
								)
						]
						+ SHorizontalBox::Slot()
						.FillWidth(0.1f);
					// clang-format on
					GizmoSection.AddEntry(FToolMenuEntry::InitWidget(
						"GizmoScale", GizmoScaleWidget, LOCTEXT("GizmoScaleLabel", "Gizmo Scale")
					));
				}
			}
		)
	);

	Entry.ToolBarData.LabelOverride = FText();
	Entry.Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.SelectMode");
	return Entry;
}

FToolMenuEntry CreateViewportToolbarSelectSection()
{
	FToolMenuEntry Entry = FToolMenuEntry::InitSubMenu(
		"Select",
		LOCTEXT("SelectonSubmenuLabel", "Select"),
		LOCTEXT("SelectionSubmenuTooltip", "Viewport-related selection tools"),
		FNewToolMenuDelegate::CreateLambda(
			[](UToolMenu* Submenu) -> void
			{
				{
					FToolMenuSection& UnnamedSection = Submenu->FindOrAddSection(NAME_None);

					UnnamedSection.AddMenuEntry(
						FGenericCommands::Get().SelectAll,
						FGenericCommands::Get().SelectAll->GetLabel(),
						FGenericCommands::Get().SelectAll->MakeTooltip()->GetTextTooltip(),
						FSlateIconFinder::FindIcon("FoliageEditMode.SelectAll")
					);

					UnnamedSection.AddMenuEntry(
						FLevelEditorCommands::Get().SelectNone,
						FLevelEditorCommands::Get().SelectNone->GetLabel(),
						FLevelEditorCommands::Get().SelectNone->MakeTooltip()->GetTextTooltip(),
						FSlateIconFinder::FindIcon("Cross")
					);

					UnnamedSection.AddMenuEntry(
						FLevelEditorCommands::Get().InvertSelection,
						FLevelEditorCommands::Get().InvertSelection->GetLabel(),
						FLevelEditorCommands::Get().InvertSelection->MakeTooltip()->GetTextTooltip(),
						FSlateIconFinder::FindIcon("FoliageEditMode.DeselectAll")
					);

					// Hierarchy based selection
					{
						UnnamedSection.AddSubMenu(
							"Hierarchy",
							LOCTEXT("HierarchyLabel", "Hierarchy"),
							LOCTEXT("HierarchyTooltip", "Hierarchy selection tools"),
							FNewToolMenuDelegate::CreateLambda(
								[](UToolMenu* HierarchyMenu)
								{
									FToolMenuSection& HierarchySection = HierarchyMenu->FindOrAddSection(
										"SelectAllHierarchy", LOCTEXT("SelectAllHierarchyLabel", "Hierarchy")
									);

									HierarchySection.AddMenuEntry(
										FLevelEditorCommands::Get().SelectImmediateChildren,
										LOCTEXT("HierarchySelectImmediateChildrenLabel", "Immediate Children")
									);

									HierarchySection.AddMenuEntry(
										FLevelEditorCommands::Get().SelectAllDescendants,
										LOCTEXT("HierarchySelectAllDescendantsLabel", "All Descendants")
									);
								}
							),
							false,
							FSlateIconFinder::FindIcon("BTEditor.SwitchToBehaviorTreeMode")
						);
					}

					UnnamedSection.AddSeparator("Advanced");

					UnnamedSection.AddMenuEntry(
						FLevelEditorCommands::Get().SelectAllActorsOfSameClass,
						LOCTEXT("AdvancedSelectAllActorsOfSameClassLabel", "All of Same Class"),
						FLevelEditorCommands::Get().SelectAllActorsOfSameClass->MakeTooltip()->GetTextTooltip(),
						FSlateIconFinder::FindIcon("PlacementBrowser.Icons.All")
					);
				}

				{
					FToolMenuSection& ByTypeSection =
						Submenu->FindOrAddSection("ByTypeSection", LOCTEXT("ByTypeSectionLabel", "By Type"));

					ByTypeSection.AddSubMenu(
						"BSP",
						LOCTEXT("BspLabel", "BSP"),
						LOCTEXT("BspTooltip", "BSP-related tools"),
						FNewToolMenuDelegate::CreateLambda(
							[](UToolMenu* BspMenu)
							{
								FToolMenuSection& SelectAllSection = BspMenu->FindOrAddSection(
									"SelectAllBSP", LOCTEXT("SelectAllBSPLabel", "Select All BSP")
								);

								SelectAllSection.AddMenuEntry(
									FLevelEditorCommands::Get().SelectAllAddditiveBrushes,
									LOCTEXT("BSPSelectAllAdditiveBrushesLabel", "Addditive Brushes")
								);

								SelectAllSection.AddMenuEntry(
									FLevelEditorCommands::Get().SelectAllSubtractiveBrushes,
									LOCTEXT("BSPSelectAllSubtractiveBrushesLabel", "Subtractive Brushes")
								);

								SelectAllSection.AddMenuEntry(
									FLevelEditorCommands::Get().SelectAllSurfaces,
									LOCTEXT("BSPSelectAllAllSurfacesLabel", "Surfaces")
								);
							}
						),
						false,
						FSlateIconFinder::FindIcon("ShowFlagsMenu.BSP")
					);

					ByTypeSection.AddSubMenu(
						"Emitters",
						LOCTEXT("EmittersLabel", "Emitters"),
						LOCTEXT("EmittersTooltip", "Emitters-related tools"),
						FNewToolMenuDelegate::CreateLambda(
							[](UToolMenu* EmittersMenu)
							{
								FToolMenuSection& SelectAllSection = EmittersMenu->FindOrAddSection(
									"SelectAllEmitters", LOCTEXT("SelectAllEmittersLabel", "Select All Emitters")
								);

								SelectAllSection.AddMenuEntry(
									FLevelEditorCommands::Get().SelectMatchingEmitter,
									LOCTEXT("EmittersSelectMatchingEmitterLabel", "Matching Emitters")
								);
							}
						),
						false,
						FSlateIconFinder::FindIcon("ClassIcon.Emitter")
					);

					ByTypeSection.AddSubMenu(
						"GeometryCollections",
						LOCTEXT("GeometryCollectionsLabel", "Geometry Collections"),
						LOCTEXT("GeometryCollectionsTooltip", "GeometryCollections-related tools"),
						FNewToolMenuDelegate::CreateLambda(
							[](UToolMenu* GeometryCollectionsMenu)
							{
								// This one will be filled by extensions from GeometryCollectionEditorPlugin
								// Hook is "SelectGeometryCollections"
								FToolMenuSection& SelectAllSection = GeometryCollectionsMenu->FindOrAddSection(
									"SelectGeometryCollections",
									LOCTEXT("SelectGeometryCollectionsLabel", "Geometry Collections")
								);
							}
						),
						false,
						FSlateIconFinder::FindIcon("ClassIcon.GeometryCollection")
					);

					ByTypeSection.AddSubMenu(
						"HLOD",
						LOCTEXT("HLODLabel", "HLOD"),
						LOCTEXT("HLODTooltip", "HLOD-related tools"),
						FNewToolMenuDelegate::CreateLambda(
							[](UToolMenu* HLODMenu)
							{
								FToolMenuSection& SelectAllSection = HLODMenu->FindOrAddSection(
									"SelectAllHLOD", LOCTEXT("SelectAllHLODLabel", "Select All HLOD")
								);

								SelectAllSection.AddMenuEntry(
									FLevelEditorCommands::Get().SelectOwningHierarchicalLODCluster,
									LOCTEXT("HLODSelectOwningHierarchicalLODClusterLabel", "Owning HLOD Cluster")
								);
							}
						),
						false,
						FSlateIconFinder::FindIcon("WorldPartition.ShowHLODActors")
					);

					ByTypeSection.AddSubMenu(
						"Lights",
						LOCTEXT("LightsLabel", "Lights"),
						LOCTEXT("LightsTooltip", "Lights-related tools"),
						FNewToolMenuDelegate::CreateLambda(
							[](UToolMenu* LightsMenu)
							{
								FToolMenuSection& SelectAllSection = LightsMenu->FindOrAddSection(
									"SelectAllLights", LOCTEXT("SelectAllLightsLabel", "Select All Lights")
								);

								SelectAllSection.AddMenuEntry(
									FLevelEditorCommands::Get().SelectAllLights,
									LOCTEXT("LightsSelectAllLightsLabel", "All Lights")
								);

								SelectAllSection.AddMenuEntry(
									FLevelEditorCommands::Get().SelectRelevantLights,
									LOCTEXT("LightsSelectRelevantLightsLabel", "Relevant Lights")
								);

								SelectAllSection.AddMenuEntry(
									FLevelEditorCommands::Get().SelectStationaryLightsExceedingOverlap,
									LOCTEXT("LightsSelectStationaryLightsExceedingOverlapLabel", "Stationary Lights Exceeding Overlap")
								);
							}
						),
						false,
						FSlateIconFinder::FindIcon("PlacementBrowser.Icons.Lights")
					);

					ByTypeSection.AddSubMenu(
						"Material",
						LOCTEXT("MaterialLabel", "Material"),
						LOCTEXT("MaterialTooltip", "Material-related tools"),
						FNewToolMenuDelegate::CreateLambda(
							[](UToolMenu* MaterialMenu)
							{
								FToolMenuSection& SelectAllSection = MaterialMenu->FindOrAddSection(
									"SelectAllMaterial", LOCTEXT("SelectAllMaterialLabel", "Select All Material")
								);

								SelectAllSection.AddMenuEntry(
									FLevelEditorCommands::Get().SelectAllWithSameMaterial,
									LOCTEXT("MaterialSelectAllWithSameMaterialLabel", "With Same Material")
								);
							}
						),
						false,
						FSlateIconFinder::FindIcon("ClassIcon.Material")
					);

					ByTypeSection.AddSubMenu(
						"SkeletalMeshes",
						LOCTEXT("SkeletalMeshesLabel", "Skeletal Meshes"),
						LOCTEXT("SkeletalMeshesTooltip", "SkeletalMeshes-related tools"),
						FNewToolMenuDelegate::CreateLambda(
							[](UToolMenu* SkeletalMeshesMenu)
							{
								FToolMenuSection& SelectAllSection = SkeletalMeshesMenu->FindOrAddSection(
									"SelectAllSkeletalMeshes",
									LOCTEXT("SelectAllSkeletalMeshesLabel", "Select All SkeletalMeshes")
								);

								SelectAllSection.AddMenuEntry(
									FLevelEditorCommands::Get().SelectSkeletalMeshesOfSameClass,
									LOCTEXT(
										"SkeletalMeshesSelectSkeletalMeshesOfSameClassLabel",
										"Using Selected Skeletal Meshes (Selected Actor Types)"
									)
								);

								SelectAllSection.AddMenuEntry(
									FLevelEditorCommands::Get().SelectSkeletalMeshesAllClasses,
									LOCTEXT(
										"SkeletalMeshesSelectSkeletalMeshesAllClassesLabel",
										"Using Selected Skeletal Meshes (All Actor Types)"
									)
								);
							}
						),
						false,
						FSlateIconFinder::FindIcon("SkeletonTree.Bone")
					);

					ByTypeSection.AddSubMenu(
						"StaticMeshes",
						LOCTEXT("StaticMeshesLabel", "Static Meshes"),
						LOCTEXT("StaticMeshesTooltip", "StaticMeshes-related tools"),
						FNewToolMenuDelegate::CreateLambda(
							[](UToolMenu* StaticMeshesMenu)
							{
								FToolMenuSection& SelectAllSection = StaticMeshesMenu->FindOrAddSection(
									"SelectAllStaticMeshes", LOCTEXT("SelectAllStaticMeshesLabel", "Select All StaticMeshes")
								);

								SelectAllSection.AddMenuEntry(
									FLevelEditorCommands::Get().SelectStaticMeshesOfSameClass,
									LOCTEXT("StaticMeshesSelectStaticMeshesOfSameClassLabel", "Matching Selected Class")
								);

								SelectAllSection.AddMenuEntry(
									FLevelEditorCommands::Get().SelectStaticMeshesAllClasses,
									LOCTEXT("StaticMeshesSelectStaticMeshesAllClassesLabel", "Matching All Classes")
								);
							}
						),
						false,
						FSlateIconFinder::FindIcon("ShowFlagsMenu.StaticMeshes")
					);
				}

				{
					FToolMenuSection& OptionsSection =
						Submenu->FindOrAddSection("Options", LOCTEXT("OptionsLabel", "Options"));

					OptionsSection.AddMenuEntry(
						FLevelEditorCommands::Get().AllowTranslucentSelection,
						LOCTEXT("OptionsAllowTranslucentSelectionLabel", "Translucent Objects")
					);

					OptionsSection.AddMenuEntry(
						FLevelEditorCommands::Get().AllowGroupSelection,
						LOCTEXT("OptionsAllowGroupSelectionLabel", "Select Groups")
					);

					OptionsSection.AddMenuEntry(
						FLevelEditorCommands::Get().StrictBoxSelect,
						LOCTEXT("OptionsStrictBoxSelectLabel", "Strict Marquee Selection")
					);

					OptionsSection.AddMenuEntry(
						FLevelEditorCommands::Get().TransparentBoxSelect,
						LOCTEXT("OptionsTransparentBoxSelectLabel", "Marquee Select Occluded")
					);
				}
			}
		)
	);

	Entry.Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.SelectMode");
	return Entry;
}

FToolMenuEntry CreateViewportToolbarSnappingSubmenu()
{
	FToolMenuEntry Entry = FToolMenuEntry::InitSubMenu(
		"Snapping",
		LOCTEXT("SnappingSubmenuLabel", "Snapping"),
		LOCTEXT("SnappingSubmenuTooltip", "Viewport-related snapping settings"),
		FNewToolMenuDelegate::CreateLambda(
			[](UToolMenu* Submenu) -> void
			{
				FToolMenuSection& SnappingSection =
					Submenu->FindOrAddSection("Snapping", LOCTEXT("SnappingLabel", "Snapping"));

				SnappingSection.AddEntry(Private::CreateSurfaceSnapCheckboxMenu()).SetShowInToolbarTopLevel(true);
				SnappingSection.AddEntry(Private::CreateLocationSnapCheckboxMenu()).SetShowInToolbarTopLevel(true);
				SnappingSection.AddEntry(Private::CreateRotationSnapCheckboxMenu()).SetShowInToolbarTopLevel(true);
				SnappingSection.AddEntry(Private::CreateScaleSnapCheckboxMenu()).SetShowInToolbarTopLevel(true);
				SnappingSection.AddEntry(Private::CreateActorSnapCheckboxMenu());

				FToolMenuEntry SocketSnapping =
					FToolMenuEntry::InitMenuEntry(FLevelEditorCommands::Get().ToggleSocketSnapping);
				SocketSnapping.UserInterfaceActionType = EUserInterfaceActionType::ToggleButton;
				SocketSnapping.Label = LOCTEXT("SocketSnapLabel", "Socket");
				SnappingSection.AddEntry(SocketSnapping);

				FToolMenuEntry VertexSnapping = FToolMenuEntry::InitMenuEntry(FLevelEditorCommands::Get().EnableVertexSnap);
				VertexSnapping.UserInterfaceActionType = EUserInterfaceActionType::ToggleButton;
				VertexSnapping.Label = LOCTEXT("VertexSnapLabel", "Vertex");
				SnappingSection.AddEntry(VertexSnapping);
			}
		)
	);

	Entry.ToolBarData.LabelOverride = FText();
	Entry.Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Snap");
	return Entry;
}

FText GetViewModesSubmenuLabel(TWeakPtr<SEditorViewport> InViewport)
{
	FText Label = LOCTEXT("ViewMenuTitle_Default", "View");
	if (TSharedPtr<SEditorViewport> PinnedViewport = InViewport.Pin())
	{
		const TSharedPtr<FEditorViewportClient> ViewportClient = PinnedViewport->GetViewportClient();
		check(ViewportClient.IsValid());
		const EViewModeIndex ViewMode = ViewportClient->GetViewMode();
		// If VMI_VisualizeBuffer, return its subcategory name
		if (ViewMode == VMI_VisualizeBuffer)
		{
			Label = ViewportClient->GetCurrentBufferVisualizationModeDisplayName();
		}
		else if (ViewMode == VMI_VisualizeNanite)
		{
			Label = ViewportClient->GetCurrentNaniteVisualizationModeDisplayName();
		}
		else if (ViewMode == VMI_VisualizeLumen)
		{
			Label = ViewportClient->GetCurrentLumenVisualizationModeDisplayName();
		}
		else if (ViewMode == VMI_VisualizeSubstrate)
		{
			Label = ViewportClient->GetCurrentSubstrateVisualizationModeDisplayName();
		}
		else if (ViewMode == VMI_VisualizeGroom)
		{
			Label = ViewportClient->GetCurrentGroomVisualizationModeDisplayName();
		}
		else if (ViewMode == VMI_VisualizeVirtualShadowMap)
		{
			Label = ViewportClient->GetCurrentVirtualShadowMapVisualizationModeDisplayName();
		}
		else if (ViewMode == VMI_VisualizeActorColoration)
		{
			Label = ViewportClient->GetCurrentActorColorationVisualizationModeDisplayName();
		}
		else if (ViewMode == VMI_VisualizeGPUSkinCache)
		{
			Label = ViewportClient->GetCurrentGPUSkinCacheVisualizationModeDisplayName();
		}
		// For any other category, return its own name
		else
		{
			Label = UViewModeUtils::GetViewModeDisplayName(ViewMode);
		}
	}

	return Label;
}

void PopulateViewModesMenu(UToolMenu* InMenu)
{
	UUnrealEdViewportToolbarContext* const Context = InMenu->FindContext<UUnrealEdViewportToolbarContext>();
	if (!Context)
	{
		return;
	}

	const TSharedPtr<SEditorViewport> EditorViewport = Context->Viewport.Pin();

	if (!EditorViewport)
	{
		return;
	}

	const FEditorViewportCommands& BaseViewportActions = FEditorViewportCommands::Get();

	IsViewModeSupportedDelegate IsViewModeSupported = Context->IsViewModeSupported;

	struct ViewModesSubmenu
	{
		static void AddModeIfSupported(
			const IsViewModeSupportedDelegate& InIsViewModeSupported,
			FToolMenuSection& InMenuSection,
			const TSharedPtr<FUICommandInfo>& InModeCommandInfo,
			EViewModeIndex InViewModeIndex,
			const TAttribute<FText>& InToolTipOverride = TAttribute<FText>(),
			const TAttribute<FSlateIcon>& InIconOverride = TAttribute<FSlateIcon>()
		)
		{
			if (!InIsViewModeSupported.IsBound() || InIsViewModeSupported.Execute(InViewModeIndex))
			{
				InMenuSection.AddMenuEntry(
					InModeCommandInfo, UViewModeUtils::GetViewModeDisplayName(InViewModeIndex), InToolTipOverride, InIconOverride
				);
			}
		}

		static bool IsMenuSectionAvailable(const UUnrealEdViewportToolbarContext* InContext, EHidableViewModeMenuSections InMenuSection)
		{
			if (!InContext->DoesViewModeMenuShowSection.IsBound())
			{
				return true;
			}

			return InContext->DoesViewModeMenuShowSection.Execute(InMenuSection);
		}
	};

	// View modes
	{
		FToolMenuSection& Section = InMenu->AddSection("ViewMode", LOCTEXT("ViewModeHeader", "View Mode"));
		{
			ViewModesSubmenu::AddModeIfSupported(IsViewModeSupported, Section, BaseViewportActions.LitMode, VMI_Lit);
			ViewModesSubmenu::AddModeIfSupported(IsViewModeSupported, Section, BaseViewportActions.UnlitMode, VMI_Unlit);
			ViewModesSubmenu::AddModeIfSupported(
				IsViewModeSupported, Section, BaseViewportActions.WireframeMode, VMI_BrushWireframe
			);
			ViewModesSubmenu::AddModeIfSupported(
				IsViewModeSupported, Section, BaseViewportActions.LitWireframeMode, VMI_Lit_Wireframe
			);
			ViewModesSubmenu::AddModeIfSupported(
				IsViewModeSupported, Section, BaseViewportActions.DetailLightingMode, VMI_Lit_DetailLighting
			);
			ViewModesSubmenu::AddModeIfSupported(
				IsViewModeSupported, Section, BaseViewportActions.LightingOnlyMode, VMI_LightingOnly
			);
			ViewModesSubmenu::AddModeIfSupported(
				IsViewModeSupported, Section, BaseViewportActions.ReflectionOverrideMode, VMI_ReflectionOverride
			);
			ViewModesSubmenu::AddModeIfSupported(
				IsViewModeSupported, Section, BaseViewportActions.CollisionPawn, VMI_CollisionPawn
			);
			ViewModesSubmenu::AddModeIfSupported(
				IsViewModeSupported, Section, BaseViewportActions.CollisionVisibility, VMI_CollisionVisibility
			);
		}

		if (IsRayTracingEnabled())
		{
			static auto PathTracingCvar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.PathTracing"));
			const bool bPathTracingSupported = FDataDrivenShaderPlatformInfo::GetSupportsPathTracing(GMaxRHIShaderPlatform);
			const bool bPathTracingEnabled = PathTracingCvar && PathTracingCvar->GetValueOnAnyThread() != 0;
			if (bPathTracingSupported && bPathTracingEnabled)
			{
				ViewModesSubmenu::AddModeIfSupported(
					IsViewModeSupported, Section, BaseViewportActions.PathTracingMode, VMI_PathTracing
				);
			}
		}

		// Optimization
		{
			struct Local
			{
				static void BuildOptimizationMenu(UToolMenu* Menu, IsViewModeSupportedDelegate IsViewModeSupported)
				{
					const FEditorViewportCommands& BaseViewportActions = FEditorViewportCommands::Get();

					UWorld* World = GWorld;
					const ERHIFeatureLevel::Type FeatureLevel = (IsInGameThread() && World)
																  ? (ERHIFeatureLevel::Type)World->GetFeatureLevel()
																  : GMaxRHIFeatureLevel;

					{
						FToolMenuSection& Section = Menu->AddSection(
							"OptimizationViewmodes", LOCTEXT("OptimizationSubMenuHeader", "Optimization Viewmodes")
						);
						if (FeatureLevel >= ERHIFeatureLevel::SM5)
						{
							ViewModesSubmenu::AddModeIfSupported(
								IsViewModeSupported, Section, BaseViewportActions.LightComplexityMode, VMI_LightComplexity
							);

							if (IsStaticLightingAllowed())
							{
								ViewModesSubmenu::AddModeIfSupported(
									IsViewModeSupported, Section, BaseViewportActions.LightmapDensityMode, VMI_LightmapDensity
								);
							}

							ViewModesSubmenu::AddModeIfSupported(
								IsViewModeSupported, Section, BaseViewportActions.StationaryLightOverlapMode, VMI_StationaryLightOverlap
							);
						}

						ViewModesSubmenu::AddModeIfSupported(
							IsViewModeSupported, Section, BaseViewportActions.ShaderComplexityMode, VMI_ShaderComplexity
						);

						if (AllowDebugViewShaderMode(
								DVSM_ShaderComplexityContainedQuadOverhead, GMaxRHIShaderPlatform, FeatureLevel
							))
						{
							ViewModesSubmenu::AddModeIfSupported(
								IsViewModeSupported,
								Section,
								BaseViewportActions.ShaderComplexityWithQuadOverdrawMode,
								VMI_ShaderComplexityWithQuadOverdraw
							);
						}

						if (AllowDebugViewShaderMode(DVSM_QuadComplexity, GMaxRHIShaderPlatform, FeatureLevel))
						{
							ViewModesSubmenu::AddModeIfSupported(
								IsViewModeSupported, Section, BaseViewportActions.QuadOverdrawMode, VMI_QuadOverdraw
							);
						}

						if (AllowDebugViewShaderMode(DVSM_LWCComplexity, GMaxRHIShaderPlatform, FeatureLevel))
						{
							ViewModesSubmenu::AddModeIfSupported(
								IsViewModeSupported,
								Section,
								BaseViewportActions.VisualizeLWCComplexity,
								VMI_LWCComplexity,
								TAttribute<FText>(),
								FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.LWCComplexityMode")
							);
						}
					}

					{
						FToolMenuSection& Section = Menu->AddSection(
							"TextureStreaming", LOCTEXT("TextureStreamingHeader", "Texture Streaming Accuracy")
						);

						if (AllowDebugViewShaderMode(DVSM_PrimitiveDistanceAccuracy, GMaxRHIShaderPlatform, FeatureLevel))
						{
							ViewModesSubmenu::AddModeIfSupported(
								IsViewModeSupported, Section, BaseViewportActions.TexStreamAccPrimitiveDistanceMode, VMI_PrimitiveDistanceAccuracy
							);
						}

						if (AllowDebugViewShaderMode(DVSM_MeshUVDensityAccuracy, GMaxRHIShaderPlatform, FeatureLevel))
						{
							ViewModesSubmenu::AddModeIfSupported(
								IsViewModeSupported, Section, BaseViewportActions.TexStreamAccMeshUVDensityMode, VMI_MeshUVDensityAccuracy
							);
						}

						// TexCoordScale accuracy viewmode requires shaders that are only built in the
						// TextureStreamingBuild, which requires the new metrics to be enabled.
						if (AllowDebugViewShaderMode(DVSM_MaterialTextureScaleAccuracy, GMaxRHIShaderPlatform, FeatureLevel)
							&& CVarStreamingUseNewMetrics.GetValueOnAnyThread() != 0)
						{
							ViewModesSubmenu::AddModeIfSupported(
								IsViewModeSupported,
								Section,
								BaseViewportActions.TexStreamAccMaterialTextureScaleMode,
								VMI_MaterialTextureScaleAccuracy
							);
						}

						if (AllowDebugViewShaderMode(DVSM_RequiredTextureResolution, GMaxRHIShaderPlatform, FeatureLevel))
						{
							ViewModesSubmenu::AddModeIfSupported(
								IsViewModeSupported, Section, BaseViewportActions.RequiredTextureResolutionMode, VMI_RequiredTextureResolution
							);
						}

						if (AllowDebugViewShaderMode(DVSM_RequiredTextureResolution, GMaxRHIShaderPlatform, FeatureLevel))
						{
							ViewModesSubmenu::AddModeIfSupported(
								IsViewModeSupported, Section, BaseViewportActions.VirtualTexturePendingMipsMode, VMI_VirtualTexturePendingMips
							);
						}
					}
				}

				static bool ViewModesShouldShowOptimizationEntries(const IsViewModeSupportedDelegate& InIsViewModeSupported)
				{
					if (!InIsViewModeSupported.IsBound())
					{
						return true;
					}

					return InIsViewModeSupported.Execute(VMI_LightComplexity)
						|| InIsViewModeSupported.Execute(VMI_LightmapDensity)
						|| InIsViewModeSupported.Execute(VMI_StationaryLightOverlap)
						|| InIsViewModeSupported.Execute(VMI_ShaderComplexity)
						|| InIsViewModeSupported.Execute(VMI_ShaderComplexityWithQuadOverdraw)
						|| InIsViewModeSupported.Execute(VMI_QuadOverdraw)
						|| InIsViewModeSupported.Execute(VMI_PrimitiveDistanceAccuracy)
						|| InIsViewModeSupported.Execute(VMI_MeshUVDensityAccuracy)
						|| InIsViewModeSupported.Execute(VMI_MaterialTextureScaleAccuracy)
						|| InIsViewModeSupported.Execute(VMI_RequiredTextureResolution)
						|| InIsViewModeSupported.Execute(VMI_VirtualTexturePendingMips);
				}
			};

			if (Local::ViewModesShouldShowOptimizationEntries(IsViewModeSupported))
			{
				Section.AddSubMenu(
					"OptimizationSubMenu",
					LOCTEXT("OptimizationSubMenu", "Optimization Viewmodes"),
					LOCTEXT("Optimization_ToolTip", "Select optimization visualizer"),
					FNewToolMenuDelegate::CreateStatic(&Local::BuildOptimizationMenu, IsViewModeSupported),
					FUIAction(
						FExecuteAction(),
						FCanExecuteAction(),
						FIsActionChecked::CreateLambda(
							[Viewport = EditorViewport.ToWeakPtr()]()
							{
								const TSharedRef<SEditorViewport> ViewportRef = Viewport.Pin().ToSharedRef();
								const TSharedPtr<FEditorViewportClient> ViewportClient = ViewportRef->GetViewportClient();
								check(ViewportClient.IsValid());
								const EViewModeIndex ViewMode = ViewportClient->GetViewMode();
								return (
									// Texture Streaming Accuracy
									ViewMode == VMI_LightComplexity || ViewMode == VMI_LightmapDensity
									|| ViewMode == VMI_StationaryLightOverlap || ViewMode == VMI_ShaderComplexity
									|| ViewMode == VMI_ShaderComplexityWithQuadOverdraw
									|| ViewMode == VMI_QuadOverdraw
									// Texture Streaming Accuracy
									|| ViewMode == VMI_PrimitiveDistanceAccuracy || ViewMode == VMI_MeshUVDensityAccuracy
									|| ViewMode == VMI_MaterialTextureScaleAccuracy
									|| ViewMode == VMI_RequiredTextureResolution || ViewMode == VMI_VirtualTexturePendingMips
								);
							}
						)
					),
					EUserInterfaceActionType::RadioButton,
					/* bInOpenSubMenuOnClick = */ false,
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.QuadOverdrawMode")
				);
			}
		}

		if (IsRayTracingEnabled()
			&& ViewModesSubmenu::IsMenuSectionAvailable(Context, EHidableViewModeMenuSections::RayTracingDebug))
		{
			struct Local
			{
				static void BuildRayTracingDebugMenu(FMenuBuilder& Menu) //, TWeakPtr<SViewportToolBar> InParentToolBar)
				{
					const FRayTracingDebugVisualizationMenuCommands& RtDebugCommands =
						FRayTracingDebugVisualizationMenuCommands::Get();
					RtDebugCommands.BuildVisualisationSubMenu(Menu);
				}
			};

			Section.AddSubMenu(
				"RayTracingDebugSubMenu",
				LOCTEXT("RayTracingDebugSubMenu", "Ray Tracing Debug"),
				LOCTEXT("RayTracing_ToolTip", "Select ray tracing buffer visualization view modes"),
				FNewMenuDelegate::CreateStatic(&Local::BuildRayTracingDebugMenu), //, ParentToolBar)
				false,
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.RayTracingDebugMode")
			);
		}

		{
			struct Local
			{
				static void BuildLODMenu(UToolMenu* Menu, IsViewModeSupportedDelegate IsViewModeSupported)
				{
					{
						FToolMenuSection& Section = Menu->AddSection(
							"LevelViewportLODColoration", LOCTEXT("LODModesHeader", "Level of Detail Coloration")
						);

						ViewModesSubmenu::AddModeIfSupported(
							IsViewModeSupported, Section, FEditorViewportCommands::Get().LODColorationMode, VMI_LODColoration
						);

						ViewModesSubmenu::AddModeIfSupported(
							IsViewModeSupported, Section, FEditorViewportCommands::Get().HLODColorationMode, VMI_HLODColoration
						);
					}
				}
			};

			if (!IsViewModeSupported.IsBound()
				|| (IsViewModeSupported.Execute(VMI_LODColoration) || IsViewModeSupported.Execute(VMI_HLODColoration)))
			{
				Section.AddSubMenu(
					"VisualizeGroupedLOD",
					LOCTEXT("VisualizeGroupedLODDisplayName", "Level of Detail Coloration"),
					LOCTEXT("GroupedLODMenu_ToolTip", "Select a mode for LOD Coloration"),
					FNewToolMenuDelegate::CreateStatic(&Local::BuildLODMenu, IsViewModeSupported),
					FUIAction(
						FExecuteAction(),
						FCanExecuteAction(),
						FIsActionChecked::CreateLambda(
							[WeakViewport = EditorViewport.ToWeakPtr()]()
							{
								const TSharedRef<SEditorViewport> ViewportRef = WeakViewport.Pin().ToSharedRef();
								const TSharedPtr<FEditorViewportClient> ViewportClient = ViewportRef->GetViewportClient();
								check(ViewportClient.IsValid());
								const EViewModeIndex ViewMode = ViewportClient->GetViewMode();
								return (ViewMode == VMI_LODColoration || ViewMode == VMI_HLODColoration);
							}
						)
					),
					EUserInterfaceActionType::RadioButton,
					/* bInOpenSubMenuOnClick = */ false,
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.GroupLODColorationMode")
				);
			}
		}

		if (GEnableGPUSkinCache
			&& ViewModesSubmenu::IsMenuSectionAvailable(Context, EHidableViewModeMenuSections::GPUSkinCache))
		{
			Section.AddSubMenu(
				"VisualizeGPUSkinCacheViewMode",
				LOCTEXT("VisualizeGPUSkinCacheViewModeDisplayName", "GPU Skin Cache"),
				LOCTEXT("GPUSkinCacheVisualizationMenu_ToolTip", "Select a mode for GPU Skin Cache visualization."),
				FNewMenuDelegate::CreateStatic(&FGPUSkinCacheVisualizationMenuCommands::BuildVisualisationSubMenu),
				FUIAction(
					FExecuteAction(),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda(
						[WeakViewport = EditorViewport.ToWeakPtr()]()
						{
							const TSharedRef<SEditorViewport> ViewportRef = WeakViewport.Pin().ToSharedRef();
							const TSharedPtr<FEditorViewportClient> ViewportClient = ViewportRef->GetViewportClient();
							check(ViewportClient.IsValid());
							return ViewportClient->IsViewModeEnabled(VMI_VisualizeGPUSkinCache);
						}
					)
				),
				EUserInterfaceActionType::RadioButton,
				/* bInOpenSubMenuOnClick = */ false,
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.VisualizeGPUSkinCacheMode")
			);
		}
	}

	// Auto Exposure
	if (ViewModesSubmenu::IsMenuSectionAvailable(Context, EHidableViewModeMenuSections::Exposure))
	{
		const FEditorViewportCommands& BaseViewportCommands = FEditorViewportCommands::Get();

		TSharedRef<SWidget> FixedEV100Menu = EditorViewport->BuildFixedEV100Menu();
		TSharedPtr<FEditorViewportClient> EditorViewportClient = EditorViewport->GetViewportClient();
		const bool bIsLevelEditor = EditorViewportClient.IsValid() && EditorViewportClient->IsLevelEditorClient();

		FToolMenuSection& Section = InMenu->AddSection("Exposure", LOCTEXT("ExposureHeader", "Exposure"));
		Section.AddMenuEntry(
			bIsLevelEditor ? BaseViewportCommands.ToggleInGameExposure : BaseViewportCommands.ToggleAutoExposure
		);
		Section.AddEntry(FToolMenuEntry::InitWidget("FixedEV100", FixedEV100Menu, LOCTEXT("FixedEV100", "EV100")));
	}

	// TODO: would be nice to make this appear/disappear based on current mode
	// Wireframe Opacity
	if (!IsViewModeSupported.IsBound()
		|| (IsViewModeSupported.Execute(VMI_Wireframe) || IsViewModeSupported.Execute(VMI_BrushWireframe)
			|| IsViewModeSupported.Execute(VMI_Lit_Wireframe)))
	{
		TSharedRef<SWidget> WireOpacityMenu = EditorViewport->BuildWireframeMenu();
		FToolMenuSection& Section = InMenu->AddSection("Wireframe", LOCTEXT("WireframeHeader", "Wireframe"));
		Section.AddEntry(
			FToolMenuEntry::InitWidget("WireframeOpacity", WireOpacityMenu, LOCTEXT("WireframeOpacity", "Opacity"))
		);
	}
}

UUnrealEdViewportToolbarContext* CreateViewportToolbarDefaultContext(const TWeakPtr<SEditorViewport>& InViewport)
{
	UUnrealEdViewportToolbarContext* const ContextObject = NewObject<UUnrealEdViewportToolbarContext>();
	ContextObject->Viewport = InViewport;

	// Hook up our toolbar's filter for supported view modes.
	ContextObject->IsViewModeSupported =
		UE::UnrealEd::IsViewModeSupportedDelegate::CreateStatic(&UE::UnrealEd::Private::IsViewModeSupported);

	return ContextObject;
}

FToolMenuEntry CreateViewportToolbarViewModesSubmenu()
{
	// This has to be a dynamic entry for the ViewModes submenu's label to be able to access the context.
	return FToolMenuEntry::InitDynamicEntry(
		"DynamicViewModes",
		FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InDynamicSection) -> void
			{
				// Base the label on the current view mode.
				TAttribute<FText> LabelAttribute = UE::UnrealEd::GetViewModesSubmenuLabel(nullptr);
				if (UUnrealEdViewportToolbarContext* const Context =
						InDynamicSection.FindContext<UUnrealEdViewportToolbarContext>())
				{
					LabelAttribute = TAttribute<FText>::CreateLambda(
						[WeakViewport = Context->Viewport]()
						{
							return UE::UnrealEd::GetViewModesSubmenuLabel(WeakViewport);
						}
					);
				}

				InDynamicSection.AddSubMenu(
					"ViewModes",
					LabelAttribute,
					LOCTEXT("ViewModesSubmenuTooltip", "View mode settings for the current viewport."),
					FNewToolMenuDelegate::CreateLambda(
						[](UToolMenu* Submenu) -> void
						{
							PopulateViewModesMenu(Submenu);
						}
					)
				);
			}
		)
	);
}

TSharedRef<SWidget> BuildRotationGridCheckBoxList(
	FName InExtentionHook,
	const FText& InHeading,
	const TArray<float>& InGridSizes,
	ERotationGridMode InGridMode,
	const FRotationGridCheckboxListExecuteActionDelegate& InExecuteAction,
	const FRotationGridCheckboxListIsCheckedDelegate& InIsActionChecked,
	const TSharedPtr<FUICommandList>& InCommandList
)
{
	constexpr bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder RotationGridMenuBuilder(bShouldCloseWindowAfterMenuSelection, InCommandList);

	RotationGridMenuBuilder.BeginSection(InExtentionHook, InHeading);
	for (int32 CurrGridAngleIndex = 0; CurrGridAngleIndex < InGridSizes.Num(); ++CurrGridAngleIndex)
	{
		const float CurrGridAngle = InGridSizes[CurrGridAngleIndex];

		FText MenuName =
			FText::Format(LOCTEXT("RotationGridAngle", "{0}\u00b0"), FText::AsNumber(CurrGridAngle)); /*degree symbol*/
		FText ToolTipText = FText::Format(
			LOCTEXT("RotationGridAngle_ToolTip", "Sets rotation grid angle to {0}"), MenuName
		); /*degree symbol*/

		RotationGridMenuBuilder.AddMenuEntry(
			MenuName,
			ToolTipText,
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[CurrGridAngleIndex, InGridMode, InExecuteAction]()
					{
						InExecuteAction.Execute(CurrGridAngleIndex, InGridMode);
					}
				),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda(
					[CurrGridAngleIndex, InGridMode, InIsActionChecked]()
					{
						return InIsActionChecked.Execute(CurrGridAngleIndex, InGridMode);
					}
				)
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);
	}
	RotationGridMenuBuilder.EndSection();

	return RotationGridMenuBuilder.MakeWidget();
}

FText GetRotationGridLabel()
{
	return FText::Format(
		LOCTEXT("GridRotation - Number - DegreeSymbol", "{0}\u00b0"), FText::AsNumber(GEditor->GetRotGridSize().Pitch)
	);
}

TSharedRef<SWidget> CreateRotationGridSnapMenu(
	const FRotationGridCheckboxListExecuteActionDelegate& InExecuteDelegate,
	const FRotationGridCheckboxListIsCheckedDelegate& InIsCheckedDelegate,
	const TAttribute<bool>& InIsEnabledDelegate,
	const TSharedPtr<FUICommandList>& InCommandList
)
{
	const ULevelEditorViewportSettings* ViewportSettings = GetDefault<ULevelEditorViewportSettings>();

	// clang-format off
	return SNew(SUniformGridPanel)
		.IsEnabled(InIsEnabledDelegate)
		+ SUniformGridPanel::Slot(0, 0)
		[
			UnrealEd::BuildRotationGridCheckBoxList("Common",LOCTEXT("RotationCommonText", "Rotation Increment")
				, ViewportSettings->CommonRotGridSizes, GridMode_Common,
				InExecuteDelegate,
				InIsCheckedDelegate,
				InCommandList
			)
		]
		+ SUniformGridPanel::Slot(1, 0)
		[
			UnrealEd::BuildRotationGridCheckBoxList("Div360",LOCTEXT("RotationDivisions360DegreesText", "Divisions of 360\u00b0")
				, ViewportSettings->DivisionsOf360RotGridSizes, GridMode_DivisionsOf360,
				InExecuteDelegate,
				InIsCheckedDelegate,
				InCommandList
			)
		];
	// clang-format on
}

FText GetLocationGridLabel()
{
	return FText::AsNumber(GEditor->GetGridSize());
}

TSharedRef<SWidget> CreateLocationGridSnapMenu(
	const FLocationGridCheckboxListExecuteActionDelegate& InExecuteDelegate,
	const FLocationGridCheckboxListIsCheckedDelegate& InIsCheckedDelegate,
	const TArray<float>& InGridSizes,
	const TAttribute<bool>& InIsEnabledDelegate,
	const TSharedPtr<FUICommandList>& InCommandList
)
{
	constexpr bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder LocationGridMenuBuilder(bShouldCloseWindowAfterMenuSelection, InCommandList);

	LocationGridMenuBuilder.BeginSection("Snap", LOCTEXT("LocationSnapText", "Snap Sizes"));
	for (int32 CurrGridSizeIndex = 0; CurrGridSizeIndex < InGridSizes.Num(); ++CurrGridSizeIndex)
	{
		const float CurGridSize = InGridSizes[CurrGridSizeIndex];

		LocationGridMenuBuilder.AddMenuEntry(
			FText::AsNumber(CurGridSize),
			FText::Format(LOCTEXT("LocationGridSize_ToolTip", "Sets grid size to {0}"), FText::AsNumber(CurGridSize)),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[CurrGridSizeIndex, InExecuteDelegate]()
					{
						InExecuteDelegate.Execute(CurrGridSizeIndex);
					}
				),
				FCanExecuteAction::CreateLambda(
					[InIsEnabledDelegate]()
					{
						return InIsEnabledDelegate.Get();
					}
				),
				FIsActionChecked::CreateLambda(
					[CurrGridSizeIndex, InIsCheckedDelegate]()
					{
						return InIsCheckedDelegate.Execute(CurrGridSizeIndex);
					}
				)
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);
	}
	LocationGridMenuBuilder.EndSection();

	return LocationGridMenuBuilder.MakeWidget();
}

FText GetScaleGridLabel()
{
	FNumberFormattingOptions NumberFormattingOptions;
	NumberFormattingOptions.MaximumFractionalDigits = 5;

	const float CurGridAmount = GEditor->GetScaleGridSize();
	return (GEditor->UsePercentageBasedScaling()) ? FText::AsPercent(CurGridAmount / 100.0f, &NumberFormattingOptions)
												  : FText::AsNumber(CurGridAmount, &NumberFormattingOptions);
}

TSharedRef<SWidget> CreateScaleGridSnapMenu(
	const FScaleGridCheckboxListExecuteActionDelegate& InExecuteDelegate,
	const FScaleGridCheckboxListIsCheckedDelegate& InIsCheckedDelegate,
	const TArray<float>& InGridSizes,
	const TAttribute<bool>& InIsEnabledDelegate,
	const TSharedPtr<FUICommandList>& InCommandList,
	const TAttribute<bool>& ShowPreserveNonUniformScaleOption,
	const FUIAction& PreserveNonUniformScaleUIAction
)
{
	FNumberFormattingOptions NumberFormattingOptions;
	NumberFormattingOptions.MaximumFractionalDigits = 5;

	constexpr bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder ScaleGridMenuBuilder(bShouldCloseWindowAfterMenuSelection, InCommandList);

	ScaleGridMenuBuilder.BeginSection("ScaleSnapOptions", LOCTEXT("ScaleSnapOptions", "Scale Snap"));

	for (int32 CurrGridAmountIndex = 0; CurrGridAmountIndex < InGridSizes.Num(); ++CurrGridAmountIndex)
	{
		const float CurGridAmount = InGridSizes[CurrGridAmountIndex];

		FText MenuText;
		FText ToolTipText;

		if (GEditor->UsePercentageBasedScaling())
		{
			MenuText = FText::AsPercent(CurGridAmount / 100.0f, &NumberFormattingOptions);
			ToolTipText = FText::Format(LOCTEXT("ScaleGridAmountOld_ToolTip", "Snaps scale values to {0}"), MenuText);
		}
		else
		{
			MenuText = FText::AsNumber(CurGridAmount, &NumberFormattingOptions);
			ToolTipText =
				FText::Format(LOCTEXT("ScaleGridAmount_ToolTip", "Snaps scale values to increments of {0}"), MenuText);
		}

		ScaleGridMenuBuilder.AddMenuEntry(
			MenuText,
			ToolTipText,
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda(
					[CurrGridAmountIndex, InExecuteDelegate]()
					{
						InExecuteDelegate.Execute(CurrGridAmountIndex);
					}
				),
				FCanExecuteAction::CreateLambda(
					[InIsEnabledDelegate]()
					{
						return InIsEnabledDelegate.Get();
					}
				),
				FIsActionChecked::CreateLambda(
					[CurrGridAmountIndex, InIsCheckedDelegate]()
					{
						return InIsCheckedDelegate.Execute(CurrGridAmountIndex);
					}
				)
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);
	}
	ScaleGridMenuBuilder.EndSection();

	if (!GEditor->UsePercentageBasedScaling() && ShowPreserveNonUniformScaleOption.Get())
	{
		ScaleGridMenuBuilder.BeginSection("ScaleGeneralOptions", LOCTEXT("ScaleOptions", "Scaling Options"));

		ScaleGridMenuBuilder.AddMenuEntry(
			LOCTEXT("ScaleGridPreserveNonUniformScale", "Preserve Non-Uniform Scale"),
			LOCTEXT(
				"ScaleGridPreserveNonUniformScale_ToolTip", "When this option is checked, scaling objects that have a non-uniform scale will preserve the ratios between each axis, snapping the axis with the largest value."
			),
			FSlateIcon(),
			PreserveNonUniformScaleUIAction,
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);

		ScaleGridMenuBuilder.EndSection();
	}

	return ScaleGridMenuBuilder.MakeWidget();
}

FToolMenuEntry CreateCheckboxSubmenu(
	const FName InName,
	const TAttribute<FText>& InLabel,
	const TAttribute<FText>& InToolTip,
	const FToolMenuExecuteAction& InCheckboxExecuteAction,
	const FToolMenuCanExecuteAction& InCheckboxCanExecuteAction,
	const FToolMenuGetActionCheckState& InCheckboxActionCheckState,
	const FNewToolMenuChoice& InMakeMenu
)
{
	FToolUIAction CheckboxMenuAction;
	{
		CheckboxMenuAction.ExecuteAction = InCheckboxExecuteAction;
		CheckboxMenuAction.CanExecuteAction = InCheckboxCanExecuteAction;
		CheckboxMenuAction.GetActionCheckState = InCheckboxActionCheckState;
	}

	FToolMenuEntry CheckBoxSubmenu = FToolMenuEntry::InitSubMenu(
		InName, InLabel, InToolTip, InMakeMenu, CheckboxMenuAction, EUserInterfaceActionType::ToggleButton
	);

	return CheckBoxSubmenu;
}

FToolMenuEntry CreateNumericEntry(
	const FName InName,
	const FText& InLabel,
	const FText& InTooltip,
	const FCanExecuteAction& InCanExecuteAction,
	const FNumericEntryExecuteActionDelegate& InOnValueChanged,
	const TAttribute<float>& InGetValue,
	float InMinValue,
	float InMaxValue,
	int32 InMaxFractionalDigits
)
{
	const FMargin WidgetsMargin(2.0f, 0.0f, 3.0f, 0.0f);

	FToolMenuEntry NumericEntry = FToolMenuEntry::InitMenuEntry(
		InName,
		FUIAction(FExecuteAction(), InCanExecuteAction),
		// clang-format off
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		.Padding(WidgetsMargin)
		.AutoWidth()
		[
			SNew(STextBlock)
			.Text(InLabel)
		]
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Right)
		.Padding(FMargin(6.0f, 0))
		.FillContentWidth(1.0)
		[
			SNew(SBox)
			.Padding(WidgetsMargin)
			.MinDesiredWidth(80.0f)
			[
				SNew(SNumericEntryBox<float>)
				.ToolTipText(InTooltip)
				.MinValue(InMinValue)
				.MaxValue(InMaxValue)
				.MaxSliderValue(InMaxValue)
				.AllowSpin(true)
				.MaxFractionalDigits(InMaxFractionalDigits)
				.Font(FAppStyle::GetFontStyle(TEXT("MenuItem.Font")))
				.OnValueChanged_Lambda([InOnValueChanged](float InValue){ InOnValueChanged.Execute(InValue); })
				.Value_Lambda([InGetValue](){ return InGetValue.Get(); })
			]
		]
		// clang-format on
	);

	return NumericEntry;
}

TSharedRef<SWidget> CreateCameraMenuWidget(const TSharedRef<SEditorViewport>& InViewport)
{
	constexpr bool bInShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder CameraMenuBuilder(bInShouldCloseWindowAfterMenuSelection, InViewport->GetCommandList());

	// Camera types
	CameraMenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().Perspective);

	CameraMenuBuilder.BeginSection("LevelViewportCameraType_Ortho", LOCTEXT("CameraTypeHeader_Ortho", "Orthographic"));
	CameraMenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().Top);
	CameraMenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().Bottom);
	CameraMenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().Left);
	CameraMenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().Right);
	CameraMenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().Front);
	CameraMenuBuilder.AddMenuEntry(FEditorViewportCommands::Get().Back);
	CameraMenuBuilder.EndSection();

	return CameraMenuBuilder.MakeWidget();
}

TSharedRef<SWidget> CreateFOVMenuWidget(const TSharedRef<SEditorViewport>& InViewport)
{
	constexpr float FOVMin = 5.0f;
	constexpr float FOVMax = 170.0f;

	TWeakPtr<FEditorViewportClient> ViewportClientWeak = InViewport->GetViewportClient();

	return
		// clang-format off
		SNew(SBox)
		.HAlign(HAlign_Right)
		[
			SNew(SBox)
			.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
			.WidthOverride(100.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("Menu.WidgetBorder"))
				.Padding(FMargin(1.0f))
				[
					SNew(SSpinBox<float>)
					.Style(&FAppStyle::Get(), "Menu.SpinBox")
					.Font( FAppStyle::GetFontStyle(TEXT("MenuItem.Font")))
					.MinValue(FOVMin)
					.MaxValue(FOVMax)
					.Value_Lambda([ViewportClientWeak]()
					{
						if (TSharedPtr<FEditorViewportClient> ViewportClient = ViewportClientWeak.Pin())
						{
							return ViewportClient->ViewFOV;
						}
						return 90.0f;
					})
					.OnValueChanged_Lambda([ViewportClientWeak](float InNewValue)
					{
						if (TSharedPtr<FEditorViewportClient> ViewportClient = ViewportClientWeak.Pin())
						{
							ViewportClient->FOVAngle = InNewValue;
							ViewportClient->ViewFOV = InNewValue;
							ViewportClient->Invalidate();
						}
					})
				]
			]
		];
	// clang-format on
}

TSharedRef<SWidget> CreateFarViewPlaneMenuWidget(const TSharedRef<SEditorViewport>& InViewport)
{
	TWeakPtr<FEditorViewportClient> ViewportClientWeak = InViewport->GetViewportClient();

	return
		// clang-format off
		SNew(SBox)
		.HAlign(HAlign_Right)
		[
			SNew(SBox)
			.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
			.WidthOverride(100.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("Menu.WidgetBorder"))
				.Padding(FMargin(1.0f))
				[
					SNew(SSpinBox<float>)
					.Style(&FAppStyle::Get(), "Menu.SpinBox")
					.ToolTipText(LOCTEXT("FarViewPlaneTooltip", "Distance to use as the far view plane, or zero to enable an infinite far view plane"))
					.MinValue(0.0f)
					.MaxValue(100000.0f)
					.Font(FAppStyle::GetFontStyle(TEXT("MenuItem.Font")))
					.Value_Lambda([ViewportClientWeak]()
					{
						if (TSharedPtr<FEditorViewportClient> ViewportClient = ViewportClientWeak.Pin())
						{
							return ViewportClient->GetFarClipPlaneOverride();
						}

						return 100000.0f;
					})
					.OnValueChanged_Lambda([ViewportClientWeak](float InNewValue)
					{
						if (TSharedPtr<FEditorViewportClient> ViewportClient = ViewportClientWeak.Pin())
						{
							ViewportClient->OverrideFarClipPlane(InNewValue);
							ViewportClient->Invalidate();
						}
					})
				]
			]
		];
	// clang-format on
}

FText GetCameraSpeedLabel(const TWeakPtr<SEditorViewport>& WeakViewport)
{
	if (const TSharedPtr<SEditorViewport> Viewport = WeakViewport.Pin())
	{
		if (Viewport->GetViewportClient())
		{
			const float CameraSpeed = Viewport->GetViewportClient()->GetCameraSpeed();
			FNumberFormattingOptions FormattingOptions = FNumberFormattingOptions::DefaultNoGrouping();
			FormattingOptions.MaximumFractionalDigits = CameraSpeed > 1 ? 1 : 3;
			return FText::AsNumber(CameraSpeed, &FormattingOptions);
		}
	}

	return FText();
}

FText GetCameraSubmenuLabelFromViewportType(const ELevelViewportType ViewportType)
{
	FText Label = LOCTEXT("CameraMenuTitle_Default", "Camera");
	switch (ViewportType)
	{
	case LVT_Perspective:
		Label = LOCTEXT("CameraMenuTitle_Perspective", "Perspective");
		break;

	case LVT_OrthoXY:
		Label = LOCTEXT("CameraMenuTitle_Top", "Top");
		break;

	case LVT_OrthoNegativeXZ:
		Label = LOCTEXT("CameraMenuTitle_Left", "Left");
		break;

	case LVT_OrthoNegativeYZ:
		Label = LOCTEXT("CameraMenuTitle_Front", "Front");
		break;

	case LVT_OrthoNegativeXY:
		Label = LOCTEXT("CameraMenuTitle_Bottom", "Bottom");
		break;

	case LVT_OrthoXZ:
		Label = LOCTEXT("CameraMenuTitle_Right", "Right");
		break;

	case LVT_OrthoYZ:
		Label = LOCTEXT("CameraMenuTitle_Back", "Back");
		break;
	case LVT_OrthoFreelook:
		break;
	}

	return Label;
}

FName GetCameraSubmenuIconFNameFromViewportType(const ELevelViewportType ViewportType)
{
	static FName PerspectiveIcon("EditorViewport.Perspective");
	static FName TopIcon("EditorViewport.Top");
	static FName LeftIcon("EditorViewport.Left");
	static FName FrontIcon("EditorViewport.Front");
	static FName BottomIcon("EditorViewport.Bottom");
	static FName RightIcon("EditorViewport.Right");
	static FName BackIcon("EditorViewport.Back");

	FName Icon = NAME_None;

	switch (ViewportType)
	{
	case LVT_Perspective:
		Icon = PerspectiveIcon;
		break;

	case LVT_OrthoXY:
		Icon = TopIcon;
		break;

	case LVT_OrthoNegativeXZ:
		Icon = LeftIcon;
		break;

	case LVT_OrthoNegativeYZ:
		Icon = FrontIcon;
		break;

	case LVT_OrthoNegativeXY:
		Icon = BottomIcon;
		break;

	case LVT_OrthoXZ:
		Icon = RightIcon;
		break;

	case LVT_OrthoYZ:
		Icon = BackIcon;
		break;
	case LVT_OrthoFreelook:
		break;
	}

	return Icon;
}

FToolMenuEntry CreateViewportToolbarCameraSubmenu()
{
	return FToolMenuEntry::InitDynamicEntry(
		"DynamicCameraOptions",
		FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InDynamicSection) -> void
			{
				TWeakPtr<SEditorViewport> WeakViewport;
				if (UUnrealEdViewportToolbarContext* const EditorViewportContext =
						InDynamicSection.FindContext<UUnrealEdViewportToolbarContext>())
				{
					WeakViewport = EditorViewportContext->Viewport;
				}

				const TAttribute<FText> Label = TAttribute<FText>::CreateLambda(
					[WeakViewport]()
					{
						if (TSharedPtr<SEditorViewport> Viewport = WeakViewport.Pin())
						{
							return UE::UnrealEd::GetCameraSubmenuLabelFromViewportType(Viewport->GetViewportClient()->ViewportType
							);
						}
						return LOCTEXT("CameraSubmenuLabel", "Camera");
					}
				);

				const TAttribute<FSlateIcon> Icon = TAttribute<FSlateIcon>::CreateLambda(
					[WeakViewport]()
					{
						if (TSharedPtr<SEditorViewport> Viewport = WeakViewport.Pin())
						{
							const FName IconFName = UE::UnrealEd::GetCameraSubmenuIconFNameFromViewportType(
								Viewport->GetViewportClient()->ViewportType
							);
							return FSlateIcon(FAppStyle::GetAppStyleSetName(), IconFName);
						}
						return FSlateIcon();
					}
				);

				InDynamicSection.AddSubMenu(
					"CameraOptions",
					Label,
					LOCTEXT("CameraSubmenuTooltip", "Camera options"),
					FNewToolMenuDelegate::CreateLambda(
						[](UToolMenu* Submenu) -> void
						{
							PopulateCameraMenu(Submenu);
						}
					),
					false,
					Icon
				);
			}
		)
	);
}

FToolMenuEntry CreateViewportToolbarAssetViewerProfileSubmenu(const TSharedPtr<IPreviewProfileController>& InPreviewProfileController
)
{
	TWeakPtr<IPreviewProfileController> PreviewProfileControllerWeak = InPreviewProfileController;

	return FToolMenuEntry::InitSubMenu(
		"AssetViewerProfile",
		TAttribute<FText>::CreateLambda(
			[PreviewProfileControllerWeak]()
			{
				if (TSharedPtr<IPreviewProfileController> PreviewProfileController = PreviewProfileControllerWeak.Pin())
				{
					return FText::FromString(PreviewProfileController->GetActiveProfile());
				}

				return LOCTEXT("AssetViewerDefaultProfileLabel", "Profile");
			}
		),
		LOCTEXT("AssetViewerProfileSelectionSectionTooltip", "Select the Preview Scene Profile for this viewport."),
		FNewToolMenuDelegate::CreateLambda(
			[PreviewProfileControllerWeak](UToolMenu* Submenu) -> void
			{
				TSharedPtr<IPreviewProfileController> PreviewProfileController = PreviewProfileControllerWeak.Pin();

				if (!PreviewProfileController)
				{
					return;
				}

				FToolMenuSection& UnnamedSection = Submenu->FindOrAddSection(NAME_None);

				constexpr bool bInShouldCloseWindowAfterMenuSelection = true;
				FMenuBuilder PreviewProfilesSelectionMenuBuilder(bInShouldCloseWindowAfterMenuSelection, nullptr);
				PreviewProfilesSelectionMenuBuilder.BeginSection(
					"AssetViewerProfileSelectionSection",
					LOCTEXT("AssetViewerProfileSelectionSectionLabel", "Preview Scene Profiles")
				);

				int32 CurrProfileIndex = 0;
				const TArray<FString>& PreviewProfiles = PreviewProfileController->GetPreviewProfiles(CurrProfileIndex);

				for (int32 ProfileIndex = 0; ProfileIndex < PreviewProfiles.Num(); ProfileIndex++)
				{
					const FString& ProfileName = PreviewProfiles[ProfileIndex];
					PreviewProfilesSelectionMenuBuilder.AddMenuEntry(
						FText::FromString(ProfileName),
						FText(),
						FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateLambda(
								[PreviewProfileControllerWeak, ProfileIndex, PreviewProfiles]()
								{
									if (TSharedPtr<IPreviewProfileController> PreviewProfileController =
											PreviewProfileControllerWeak.Pin())
									{
										PreviewProfileController->SetActiveProfile(PreviewProfiles[ProfileIndex]);
									}
								}
							),
							FCanExecuteAction(),
							FIsActionChecked::CreateLambda(
								[PreviewProfileControllerWeak, ProfileIndex]()
								{
									if (TSharedPtr<IPreviewProfileController> PreviewProfileController =
											PreviewProfileControllerWeak.Pin())
									{
										int32 CurrentlySelectedProfileIndex;
										PreviewProfileController->GetPreviewProfiles(CurrentlySelectedProfileIndex);

										return ProfileIndex == CurrentlySelectedProfileIndex;
									}

									return false;
								}
							)
						),
						NAME_None,
						EUserInterfaceActionType::RadioButton
					);
				}

				PreviewProfilesSelectionMenuBuilder.EndSection();

				UnnamedSection.AddEntry(FToolMenuEntry::InitWidget(
					"AssetViewerProfile", PreviewProfilesSelectionMenuBuilder.MakeWidget(), FText(), true
				));
			}
		)
	);
}

void PopulateCameraMenu(UToolMenu* InMenu)
{
	UUnrealEdViewportToolbarContext* const EditorViewportContext = InMenu->FindContext<UUnrealEdViewportToolbarContext>();
	if (!EditorViewportContext)
	{
		return;
	}

	const TSharedPtr<SEditorViewport> EditorViewport = EditorViewportContext->Viewport.Pin();
	if (!EditorViewport)
	{
		return;
	}

	FToolMenuSection& PerspectiveCameraSection =
		InMenu->FindOrAddSection("LevelViewportCameraType_Perspective");
	PerspectiveCameraSection.AddMenuEntry(FEditorViewportCommands::Get().Perspective);

	FToolMenuSection& OrthographicCameraSection =
		InMenu->FindOrAddSection("LevelViewportCameraType_Ortho", LOCTEXT("CameraTypeHeader_Ortho", "Orthographic"));
	OrthographicCameraSection.AddMenuEntry(FEditorViewportCommands::Get().Top);
	OrthographicCameraSection.AddMenuEntry(FEditorViewportCommands::Get().Bottom);
	OrthographicCameraSection.AddMenuEntry(FEditorViewportCommands::Get().Left);
	OrthographicCameraSection.AddMenuEntry(FEditorViewportCommands::Get().Right);
	OrthographicCameraSection.AddMenuEntry(FEditorViewportCommands::Get().Front);
	OrthographicCameraSection.AddMenuEntry(FEditorViewportCommands::Get().Back);
}

void ExtendCameraSubmenu(FName InCameraOptionsSubmenuName)
{
	UToolMenu* const Submenu = UToolMenus::Get()->ExtendMenu(InCameraOptionsSubmenuName);

	Submenu->AddDynamicSection(
		"EditorCameraExtensionDynamicSection",
		FNewToolMenuDelegate::CreateLambda(
			[](UToolMenu* InDynamicMenu)
			{
				UUnrealEdViewportToolbarContext* const EditorViewportContext =
					InDynamicMenu->FindContext<UUnrealEdViewportToolbarContext>();
				if (!EditorViewportContext)
				{
					return;
				}

				const TSharedPtr<SEditorViewport> EditorViewport = EditorViewportContext->Viewport.Pin();
				if (!EditorViewport)
				{
					return;
				}

				FToolMenuInsert InsertPosition("LevelViewportCameraType_Ortho", EToolMenuInsertType::After);

				FToolMenuSection& UnnamedSection =
					InDynamicMenu->FindOrAddSection(NAME_None, FText(), InsertPosition);
				UnnamedSection.AddSeparator("CameraSubmenuSeparator");

				UnnamedSection.AddEntry(FToolMenuEntry::InitWidget(
					"CameraFOV",
					UE::UnrealEd::CreateFOVMenuWidget(EditorViewport.ToSharedRef()),
					LOCTEXT("CameraSubmenu_FieldOfViewLabel", "Field of View"),
					true
				));

				UnnamedSection.AddEntry(FToolMenuEntry::InitWidget(
					"CameraFarViewPlane",
					UE::UnrealEd::CreateFarViewPlaneMenuWidget(EditorViewport.ToSharedRef()),
					LOCTEXT("CameraSubmenu_FarViewPlaneLabel", "Far View Plane"),
					true
				));
			}
		)
	);
}

static FFormatNamedArguments GetScreenPercentageFormatArguments(const FEditorViewportClient& ViewportClient)
{
	const UEditorPerformanceProjectSettings* EditorProjectSettings = GetDefault<UEditorPerformanceProjectSettings>();
	const UEditorPerformanceSettings* EditorUserSettings = GetDefault<UEditorPerformanceSettings>();
	const FEngineShowFlags& EngineShowFlags = ViewportClient.EngineShowFlags;

	const EViewStatusForScreenPercentage ViewportRenderingMode = ViewportClient.GetViewStatusForScreenPercentage();
	const bool bViewModeSupportsScreenPercentage = ViewportClient.SupportsPreviewResolutionFraction();
	const bool bIsPreviewScreenPercentage = ViewportClient.IsPreviewingScreenPercentage();

	float DefaultScreenPercentage = FMath::Clamp(
										ViewportClient.GetDefaultPrimaryResolutionFractionTarget(),
										ISceneViewFamilyScreenPercentage::kMinTSRResolutionFraction,
										ISceneViewFamilyScreenPercentage::kMaxTSRResolutionFraction
									)
								  * 100.0f;
	float PreviewScreenPercentage = float(ViewportClient.GetPreviewScreenPercentage());
	float FinalScreenPercentage = bIsPreviewScreenPercentage ? PreviewScreenPercentage : DefaultScreenPercentage;

	FFormatNamedArguments FormatArguments;
	FormatArguments.Add(TEXT("ViewportMode"), UEnum::GetDisplayValueAsText(ViewportRenderingMode));

	EScreenPercentageMode ProjectSetting = EScreenPercentageMode::Manual;
	EEditorUserScreenPercentageModeOverride UserPreference = EEditorUserScreenPercentageModeOverride::ProjectDefault;
	IConsoleVariable* CVarDefaultScreenPercentage = nullptr;
	if (ViewportRenderingMode == EViewStatusForScreenPercentage::PathTracer)
	{
		ProjectSetting = EditorProjectSettings->PathTracerScreenPercentageMode;
		UserPreference = EditorUserSettings->PathTracerScreenPercentageMode;
		CVarDefaultScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Editor.Viewport.ScreenPercentageMode.PathTracer"));
	}
	else if (ViewportRenderingMode == EViewStatusForScreenPercentage::VR)
	{
		ProjectSetting = EditorProjectSettings->VRScreenPercentageMode;
		UserPreference = EditorUserSettings->VRScreenPercentageMode;
		CVarDefaultScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Editor.Viewport.ScreenPercentageMode.VR"));
	}
	else if (ViewportRenderingMode == EViewStatusForScreenPercentage::Mobile)
	{
		ProjectSetting = EditorProjectSettings->MobileScreenPercentageMode;
		UserPreference = EditorUserSettings->MobileScreenPercentageMode;
		CVarDefaultScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Editor.Viewport.ScreenPercentageMode.Mobile"));
	}
	else if (ViewportRenderingMode == EViewStatusForScreenPercentage::Desktop)
	{
		ProjectSetting = EditorProjectSettings->RealtimeScreenPercentageMode;
		UserPreference = EditorUserSettings->RealtimeScreenPercentageMode;
		CVarDefaultScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Editor.Viewport.ScreenPercentageMode.RealTime"));
	}
	else if (ViewportRenderingMode == EViewStatusForScreenPercentage::NonRealtime)
	{
		ProjectSetting = EditorProjectSettings->NonRealtimeScreenPercentageMode;
		UserPreference = EditorUserSettings->NonRealtimeScreenPercentageMode;
		CVarDefaultScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Editor.Viewport.ScreenPercentageMode.NonRealTime"));
	}
	else
	{
		unimplemented();
	}

	EScreenPercentageMode FinalScreenPercentageMode = EScreenPercentageMode::Manual;
	if (!bViewModeSupportsScreenPercentage)
	{
		FormatArguments.Add(
			TEXT("SettingSource"), LOCTEXT("ScreenPercentage_SettingSource_UnsupportedByViewMode", "Unsupported by View mode")
		);
		FinalScreenPercentageMode = EScreenPercentageMode::Manual;
		FinalScreenPercentage = 100;
	}
	else if (bIsPreviewScreenPercentage)
	{
		FormatArguments.Add(
			TEXT("SettingSource"), LOCTEXT("ScreenPercentage_SettingSource_ViewportOverride", "Viewport Override")
		);
		FinalScreenPercentageMode = EScreenPercentageMode::Manual;
	}
	else if ((CVarDefaultScreenPercentage->GetFlags() & ECVF_SetByMask) > ECVF_SetByProjectSetting)
	{
		FormatArguments.Add(TEXT("SettingSource"), LOCTEXT("ScreenPercentage_SettingSource_Cvar", "Console Variable"));
		FinalScreenPercentageMode = EScreenPercentageMode(CVarDefaultScreenPercentage->GetInt());
	}
	else if (UserPreference == EEditorUserScreenPercentageModeOverride::ProjectDefault)
	{
		FormatArguments.Add(
			TEXT("SettingSource"), LOCTEXT("ScreenPercentage_SettingSource_ProjectSettigns", "Project Settings")
		);
		FinalScreenPercentageMode = ProjectSetting;
	}
	else
	{
		FormatArguments.Add(
			TEXT("SettingSource"), LOCTEXT("ScreenPercentage_SettingSource_EditorPreferences", "Editor Preferences")
		);
		if (UserPreference == EEditorUserScreenPercentageModeOverride::BasedOnDPIScale)
		{
			FinalScreenPercentageMode = EScreenPercentageMode::BasedOnDPIScale;
		}
		else if (UserPreference == EEditorUserScreenPercentageModeOverride::BasedOnDisplayResolution)
		{
			FinalScreenPercentageMode = EScreenPercentageMode::BasedOnDisplayResolution;
		}
		else
		{
			FinalScreenPercentageMode = EScreenPercentageMode::Manual;
		}
	}

	if (FinalScreenPercentageMode == EScreenPercentageMode::BasedOnDPIScale)
	{
		FormatArguments.Add(TEXT("Setting"), LOCTEXT("ScreenPercentage_Setting_BasedOnDPIScale", "Based on OS's DPI scale"));
	}
	else if (FinalScreenPercentageMode == EScreenPercentageMode::BasedOnDisplayResolution)
	{
		FormatArguments.Add(
			TEXT("Setting"), LOCTEXT("ScreenPercentage_Setting_BasedOnDisplayResolution", "Based on display resolution")
		);
	}
	else
	{
		FormatArguments.Add(TEXT("Setting"), LOCTEXT("ScreenPercentage_Setting_Manual", "Manual"));
	}

	FormatArguments.Add(
		TEXT("CurrentScreenPercentage"),
		FText::FromString(FString::Printf(TEXT("%3.1f"), FMath::RoundToFloat(FinalScreenPercentage * 10.0f) / 10.0f))
	);

	{
		float FinalResolutionFraction = (FinalScreenPercentage / 100.0f);
		FIntPoint DisplayResolution = ViewportClient.Viewport->GetSizeXY();
		FIntPoint RenderingResolution;
		RenderingResolution.X = FMath::CeilToInt(DisplayResolution.X * FinalResolutionFraction);
		RenderingResolution.Y = FMath::CeilToInt(DisplayResolution.Y * FinalResolutionFraction);

		FormatArguments.Add(
			TEXT("ResolutionFromTo"),
			FText::FromString(FString::Printf(
				TEXT("%dx%d -> %dx%d"),
				RenderingResolution.X,
				RenderingResolution.Y,
				DisplayResolution.X,
				DisplayResolution.Y
			))
		);
	}

	return FormatArguments;
}

static const FMargin ScreenPercentageMenuCommonPadding(26.0f, 3.0f);

TSharedRef<SWidget> CreateCurrentPercentageWidget(FEditorViewportClient& InViewportClient)
{
	// clang-format off
	return SNew(SBox)
		.Padding(ScreenPercentageMenuCommonPadding)
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text_Lambda([&InViewportClient]()
			{
				FFormatNamedArguments FormatArguments = GetScreenPercentageFormatArguments(InViewportClient);
				return FText::Format(LOCTEXT("ScreenPercentageCurrent_Display", "Current Screen Percentage: {CurrentScreenPercentage}"), FormatArguments);
			})
			.ToolTip(SNew(SToolTip).Text(LOCTEXT("ScreenPercentageCurrent_ToolTip", "Current Screen Percentage the viewport is rendered with. The primary screen percentage can either be a spatial or temporal upscaler based of your anti-aliasing settings.")))
		];
	// clang-format on
}

TSharedRef<SWidget> CreateResolutionsWidget(FEditorViewportClient& InViewportClient)
{
	// clang-format off
	return SNew(SBox)
	.Padding(ScreenPercentageMenuCommonPadding)
	[
		SNew(STextBlock)
		.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		.Text_Lambda([&InViewportClient]()
		{
			FFormatNamedArguments FormatArguments = GetScreenPercentageFormatArguments(InViewportClient);
			return FText::Format(LOCTEXT("ScreenPercentageResolutions", "Resolution: {ResolutionFromTo}"), FormatArguments);
		})
	];
	// clang-format on
}

TSharedRef<SWidget> CreateActiveViewportWidget(FEditorViewportClient& InViewPortClient)
{
	// clang-format off
	return SNew(SBox)
		.Padding(ScreenPercentageMenuCommonPadding)
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text_Lambda([&InViewPortClient]()
			{
				FFormatNamedArguments FormatArguments = GetScreenPercentageFormatArguments(InViewPortClient);
				return FText::Format(LOCTEXT("ScreenPercentageActiveViewport", "Active Viewport: {ViewportMode}"), FormatArguments);
			})
		];
	// clang-format on
}

TSharedRef<SWidget> CreateSetFromWidget(FEditorViewportClient& InViewPortClient)
{
	// clang-format off
	return SNew(SBox)
		.Padding(ScreenPercentageMenuCommonPadding)
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text_Lambda([&InViewPortClient]()
			{
				FFormatNamedArguments FormatArguments = GetScreenPercentageFormatArguments(InViewPortClient);
				return FText::Format(LOCTEXT("ScreenPercentageSetFrom", "Set From: {SettingSource}"), FormatArguments);
			})
		];
	// clang-format on
}

TSharedRef<SWidget> CreateCurrentScreenPercentageSettingWidget(FEditorViewportClient& InViewPortClient)
{
	// clang-format off
	return SNew(SBox)
		.Padding(ScreenPercentageMenuCommonPadding)
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text_Lambda([&InViewPortClient]()
			{
				FFormatNamedArguments FormatArguments = GetScreenPercentageFormatArguments(InViewPortClient);
				return FText::Format(LOCTEXT("ScreenPercentageSetting", "Setting: {Setting}"), FormatArguments);
			})
		];
	// clang-format on
}

TSharedRef<SWidget> CreateCurrentScreenPercentageWidget(FEditorViewportClient& InViewPortClient)
{
	constexpr int32 PreviewScreenPercentageMin = ISceneViewFamilyScreenPercentage::kMinTSRResolutionFraction * 100.0f;
	constexpr int32 PreviewScreenPercentageMax = ISceneViewFamilyScreenPercentage::kMaxTSRResolutionFraction * 100.0f;

	// clang-format off
	return SNew(SBox)
		.HAlign(HAlign_Right)
		.IsEnabled_Lambda([&InViewPortClient]()
		{
			return InViewPortClient.IsPreviewingScreenPercentage() && InViewPortClient.SupportsPreviewResolutionFraction();
		})
		[
			SNew(SBox)
			.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
			.WidthOverride(100.0f)
			[
				SNew(SBorder)
				.Padding(FMargin(1.0f))
				[
					SNew(SSpinBox<int32>)
					.Style(&FAppStyle::Get(), "Menu.SpinBox")
					.Font(FAppStyle::GetFontStyle(TEXT("MenuItem.Font")))
					.MinSliderValue(PreviewScreenPercentageMin)
					.MaxSliderValue(PreviewScreenPercentageMax)
					.Value_Lambda([&InViewPortClient]()
					{
						return InViewPortClient.GetPreviewScreenPercentage();
					})
					.OnValueChanged_Lambda([&InViewPortClient](int32 NewValue)
					{
						InViewPortClient.SetPreviewScreenPercentage(NewValue);
						InViewPortClient.Invalidate();
					})
				]
			]
		];
	// clang-format on
}

static void ConstructScreenPercentageMenu(UToolMenu* InMenu)
{
	UUnrealEdViewportToolbarContext* const LevelViewportContext = InMenu->FindContext<UUnrealEdViewportToolbarContext>();
	if (!LevelViewportContext)
	{
		return;
	}

	const TSharedPtr<SEditorViewport> LevelViewport = LevelViewportContext->Viewport.Pin();
	if (!LevelViewport)
	{
		return;
	}

	FEditorViewportClient& ViewportClient = *LevelViewport->GetViewportClient();

	const FEditorViewportCommands& BaseViewportCommands = FEditorViewportCommands::Get();

	// Summary
	{
		FToolMenuSection& SummarySection = InMenu->FindOrAddSection("Summary", LOCTEXT("Summary", "Summary"));
		SummarySection.AddEntry(FToolMenuEntry::InitWidget(
			"ScreenPercentageCurrent", CreateCurrentPercentageWidget(ViewportClient), FText::GetEmpty()
		));

		SummarySection.AddEntry(FToolMenuEntry::InitWidget(
			"ScreenPercentageResolutions", CreateResolutionsWidget(ViewportClient), FText::GetEmpty()
		));

		SummarySection.AddEntry(FToolMenuEntry::InitWidget(
			"ScreenPercentageActiveViewport", CreateActiveViewportWidget(ViewportClient), FText::GetEmpty()
		));

		SummarySection.AddEntry(
			FToolMenuEntry::InitWidget("ScreenPercentageSetFrom", CreateSetFromWidget(ViewportClient), FText::GetEmpty())
		);

		SummarySection.AddEntry(FToolMenuEntry::InitWidget(
			"ScreenPercentageSetting", CreateCurrentScreenPercentageSettingWidget(ViewportClient), FText::GetEmpty()
		));
	}

	// Screen Percentage
	{
		FToolMenuSection& ScreenPercentageSection =
			InMenu->FindOrAddSection("ScreenPercentage", LOCTEXT("ScreenPercentage_ViewportOverride", "Viewport Override"));

		ScreenPercentageSection.AddMenuEntry(BaseViewportCommands.ToggleOverrideViewportScreenPercentage);

		ScreenPercentageSection.AddEntry(FToolMenuEntry::InitWidget(
			"PreviewScreenPercentage",
			CreateCurrentScreenPercentageWidget(ViewportClient),
			LOCTEXT("ScreenPercentage", "Screen Percentage")
		));
	}

	// Screen Percentage Settings
	{
		FToolMenuSection& ScreenPercentageSettingsSection = InMenu->FindOrAddSection(
			"ScreenPercentageSettings", LOCTEXT("ScreenPercentage_ViewportSettings", "Viewport Settings")
		);

		ScreenPercentageSettingsSection.AddMenuEntry(
			BaseViewportCommands.OpenEditorPerformanceProjectSettings,
			/* InLabelOverride = */ TAttribute<FText>(),
			/* InToolTipOverride = */ TAttribute<FText>(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "ProjectSettings.TabIcon")
		);

		ScreenPercentageSettingsSection.AddMenuEntry(
			BaseViewportCommands.OpenEditorPerformanceEditorPreferences,
			/* InLabelOverride = */ TAttribute<FText>(),
			/* InToolTipOverride = */ TAttribute<FText>(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorPreferences.TabIcon")
		);
	}
}

bool ShouldShowViewportRealtimeWarning(const FEditorViewportClient& ViewportClient)
{
	return !ViewportClient.IsRealtime() && !ViewportClient.IsRealtimeOverrideSet() && ViewportClient.IsPerspective();
}

FToolMenuEntry CreatePerformanceAndScalabilitySubmenu()
{
	return FToolMenuEntry::InitSubMenu(
		"PerformanceAndScalability",
		LOCTEXT("PerformanceAndScalabilitySubmenuLabel", "Performance and Scalability"),
		LOCTEXT("PerformanceAndScalabilitySubmenuTooltip", "Performance and scalability tools tied to this viewport."),
		FNewToolMenuDelegate::CreateLambda(
			[](UToolMenu* Submenu) -> void
			{
				FToolMenuSection& UnnamedSection = Submenu->FindOrAddSection(NAME_None);

				UnnamedSection.AddEntry(CreateToggleRealtimeEntry());

				UnnamedSection.AddSubMenu(
					"ScreenPercentage",
					LOCTEXT("ScreenPercentageSubMenu", "Screen Percentage"),
					LOCTEXT("ScreenPercentageSubMenu_ToolTip", "Customize the viewport's screen percentage"),
					FNewToolMenuDelegate::CreateStatic(&ConstructScreenPercentageMenu)
				);
			}
		)
	);
}

FToolMenuEntry CreateDefaultShowSubmenu()
{
	return FToolMenuEntry::InitSubMenu(
		"Show",
		LOCTEXT("ShowSubmenuLabel", "Show"),
		LOCTEXT("ShowSubmenuTooltip", "Show flags related to the current viewport"),
		FNewToolMenuDelegate::CreateLambda(
			[](UToolMenu* InMenu) -> void
			{
				AddDefaultShowFlags(InMenu);
			}
		)
	);
}

void AddDefaultShowFlags(UToolMenu* InMenu)
{
	{
		FToolMenuSection& CommonShowFlagsSection =
			InMenu->FindOrAddSection("CommonShowFlags", LOCTEXT("CommonShowFlagsLabel", "Common Show Flags"));

		FShowFlagMenuCommands::Get().PopulateCommonShowFlagsSection(CommonShowFlagsSection);
	}

	{
		FToolMenuSection& AllShowFlagsSection =
			InMenu->FindOrAddSection("AllShowFlags", LOCTEXT("AllShowFlagsLabel", "All Show Flags"));

		FShowFlagMenuCommands::Get().PopulateAllShowFlagsSection(AllShowFlagsSection);
	}
}

FToolMenuEntry CreateToggleRealtimeEntry()
{
	return FToolMenuEntry::InitDynamicEntry(
		"ToggleRealtimeDynamicSection",
		FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InnerSection) -> void
			{
				UUnrealEdViewportToolbarContext* const EditorViewportContext =
					InnerSection.FindContext<UUnrealEdViewportToolbarContext>();
				if (!EditorViewportContext)
				{
					return;
				}

				TWeakPtr<SEditorViewport> EditorViewportWeak;
				FToolUIAction RealtimeToggleAction;
				if (EditorViewportContext)
				{
					EditorViewportWeak = EditorViewportContext->Viewport;

					RealtimeToggleAction.ExecuteAction = FToolMenuExecuteAction::CreateLambda(
						[EditorViewportWeak](const FToolMenuContext& Context) -> void
						{
							if (TSharedPtr<SEditorViewport> EditorViewport = EditorViewportWeak.Pin())
							{
								EditorViewport->OnToggleRealtime();
								// Calling UToolMenu::RefreshAllWidgets here is cheating. We do it because the menu
								// entry's TAttribute<FSlateIcon> is only called once when the menu is opened (because
								// FBaseMenuBuilder::AddMenuEntry takes an FSlateIcon and not a TAttribute<FSlateIcon>).
								// So when we refresh all widgets here, we force the open menu to close and hide the
								// fact that the icon wouldn't have updated if the menu stayed open.
								UToolMenus::Get()->RefreshAllWidgets();
							}
						}
					);

					RealtimeToggleAction.GetActionCheckState = FToolMenuGetActionCheckState::CreateLambda(
						[EditorViewportWeak](const FToolMenuContext& Context) -> ECheckBoxState
						{
							if (TSharedPtr<SEditorViewport> EditorViewport = EditorViewportWeak.Pin())
							{
								return EditorViewport->IsRealtime() ? ECheckBoxState::Checked
																	: ECheckBoxState::Unchecked;
							}
							return ECheckBoxState::Undetermined;
						}
					);
				}

				TAttribute<FText> Tooltip;
				{
					const FText NonRealtimeTooltip = LOCTEXT(
						"ToggleRealtimeTooltip_WarnRealtimeOff",
						"This viewport is not updating in realtime.  Click to turn on realtime mode."
					);
					const FText RealtimeTooltip =
						LOCTEXT("ToggleRealtimeTooltip", "Toggle realtime rendering of the viewport");

					// If we can find a context with a viewport, use that to adjust the tooltip
					// based on the viewport's realtime status.
					if (EditorViewportContext)
					{
						Tooltip = TAttribute<FText>::CreateLambda(
							[EditorViewportWeak, NonRealtimeTooltip, RealtimeTooltip]() -> FText
							{
								bool bDisplayTopLevel = false;
								if (const TSharedPtr<SEditorViewport> EditorViewport = EditorViewportWeak.Pin())
								{
									bDisplayTopLevel = !EditorViewport->IsRealtime();
								}

								return bDisplayTopLevel ? NonRealtimeTooltip : RealtimeTooltip;
							}
						);
					}
					else
					{
						Tooltip = RealtimeTooltip;
					}
				}

				const TAttribute<FSlateIcon> Icon = TAttribute<FSlateIcon>::CreateLambda(
					[EditorViewportWeak]() -> FSlateIcon
					{
						bool bIsViewportRealtime = true;
						if (const TSharedPtr<SEditorViewport> EditorViewport = EditorViewportWeak.Pin())
						{
							bIsViewportRealtime = EditorViewport->IsRealtime();
						}

						return bIsViewportRealtime
								 ? FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.ToggleRealTime")
								 : FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.ToggleRealTimeWarning");
					}
				);

				FToolMenuEntry ToggleRealtime = FToolMenuEntry::InitMenuEntry(
					"ToggleRealtime",
					LOCTEXT("ToggleRealtimeLabel", "Realtime Viewport"),
					Tooltip,
					Icon,
					RealtimeToggleAction,
					EUserInterfaceActionType::ToggleButton
				);

				// If we can find a context with a viewport, bind the top-level status of the
				// realtime button to the viewport's realtime state where we show the realtime
				// toggle in the top-level if the viewport is NOT realtime.
				if (EditorViewportContext)
				{
					ToggleRealtime.SetShowInToolbarTopLevel(TAttribute<bool>::CreateLambda(
						[WeakViewport = EditorViewportContext->Viewport]() -> bool
						{
							if (const TSharedPtr<SEditorViewport> EditorViewport = WeakViewport.Pin())
							{
								return ShouldShowViewportRealtimeWarning(*EditorViewport->GetViewportClient());
							}
							return false;
						}
					));
				}

				InnerSection.AddEntry(ToggleRealtime);
			}
		)
	);
}

} // namespace UE::UnrealEd

#undef LOCTEXT_NAMESPACE
