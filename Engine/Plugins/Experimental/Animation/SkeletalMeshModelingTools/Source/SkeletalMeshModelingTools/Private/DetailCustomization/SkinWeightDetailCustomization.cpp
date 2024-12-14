// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkinWeightDetailCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "ModelingToolsEditorModeStyle.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "SkeletalMesh/SkinWeightsPaintTool.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "SkinWeightToolSettingsEditor"

// layout constants
float FSkinWeightDetailCustomization::WeightSliderWidths = 150.0f;
float FSkinWeightDetailCustomization::WeightEditingLabelsPercent = 0.40f;
float FSkinWeightDetailCustomization::WeightEditVerticalPadding = 4.0f;
float FSkinWeightDetailCustomization::WeightEditHorizontalPadding = 2.0f;

static FName ColumnName_Bone = "Bone";
static FName ColumnName_Weight = "Weight";
static FName ColumnName_Prune = "Prune";

FSkinWeightDetailCustomization::~FSkinWeightDetailCustomization()
{
	if (Tool.IsValid())
	{
		Tool->OnSelectionChanged.RemoveAll(this);
	}

	Tool.Reset();
	ToolSettings.Reset();
}

void FSkinWeightDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	CurrentDetailBuilder = &DetailBuilder;
	
	TArray<TWeakObjectPtr<UObject>> DetailObjects;
	DetailBuilder.GetObjectsBeingCustomized(DetailObjects);

	// should be impossible to get multiple settings objects for a single tool
	ensure(DetailObjects.Num()==1);
	ToolSettings = Cast<USkinWeightsPaintToolProperties>(DetailObjects[0]);
	Tool = ToolSettings->WeightTool;
	Tool->OnSelectionChanged.AddSP(this, &FSkinWeightDetailCustomization::OnSelectionChanged);
	
	// custom display of falloff mode as segmented toggle buttons
	IDetailCategoryBuilder& EditModeCategory = DetailBuilder.EditCategory("Weight Editing Mode", FText::GetEmpty(), ECategoryPriority::Important);

	// add segmented control toggle for editing modes ("Brush" or "Selection")
	EditModeCategory.AddCustomRow(LOCTEXT("EditModeCategory", "Weight Editing Mode"), false)
	.WholeRowContent()
	[
		SNew(SBox)
		.Padding(2.0f)
		[
			SNew(SSegmentedControl<EWeightEditMode>)
			.ToolTipText(LOCTEXT("EditingModeTooltip",
					"Brush: edit weights by painting on mesh.\n"
					"Mesh: select vertices/edges/faces to edit weights directly.\n"
					"Bones: select and manipulate bones to preview deformations.\n"))
			.Value_Lambda([this]()
			{
				return ToolSettings->EditingMode;
			})
			.OnValueChanged_Lambda([this](EWeightEditMode Mode)
			{
				ToolSettings->EditingMode = Mode;
				ToolSettings->WeightTool->ToggleEditingMode();
				if (CurrentDetailBuilder)
				{
					CurrentDetailBuilder->ForceRefreshDetails();
				}
			})
			+SSegmentedControl<EWeightEditMode>::Slot(EWeightEditMode::Brush)
			.Text(LOCTEXT("BrushEditMode", "Brush"))
			+ SSegmentedControl<EWeightEditMode>::Slot(EWeightEditMode::Mesh)
			.Text(LOCTEXT("MeshEditMode", "Mesh"))
			+ SSegmentedControl<EWeightEditMode>::Slot(EWeightEditMode::Bones)
			.Text(LOCTEXT("BoneEditMode", "Bones"))
		]
	];

	// BRUSH editing mode UI
	if (ToolSettings->EditingMode == EWeightEditMode::Brush)
	{
		AddBrushUI(DetailBuilder);
	}

	// MESH editing mode UI
	if (ToolSettings->EditingMode == EWeightEditMode::Mesh)
	{
		AddSelectionUI(DetailBuilder);
	}
	
	// COLOR MODE category
	IDetailCategoryBuilder& MeshDisplayCategory = DetailBuilder.EditCategory("MeshDisplay", FText::GetEmpty(), ECategoryPriority::Important);
	MeshDisplayCategory.InitiallyCollapsed(false);
	MeshDisplayCategory.AddCustomRow(LOCTEXT("ColorModeCategory", "Color Mode"), false)
	.WholeRowContent()
	[
		SNew(SBox)
		.Padding(2.0f)
		.HAlign(HAlign_Center)
		[
			SNew(SSegmentedControl<EWeightColorMode>)
			.ToolTipText(LOCTEXT("WeightColorTooltip",
					"Adjust the weight display in the viewport.\n\n"
					"Greyscale: Displays weights on the current bone by blending from black (0) to white (1).\n"
					"Ramp: Displays weights on the current bone. Weights at 0 and 1 use the min and max colors. Weights inbetween 0 and 1 use the ramp colors.\n"
					"Multi Color: Displays weights on ALL bones using the color of the bones.\n"
					"Full Material: Displays normal mesh materials with textures.\n"))
			.Value_Lambda([this]()
			{
				return ToolSettings->ColorMode;
			})
			.OnValueChanged_Lambda([this](EWeightColorMode Mode)
			{
				ToolSettings->SetColorMode(Mode);
			})
			+ SSegmentedControl<EWeightColorMode>::Slot(EWeightColorMode::Greyscale)
			.Text(LOCTEXT("GreyscaleMode", "Greyscale"))
			+SSegmentedControl<EWeightColorMode>::Slot(EWeightColorMode::Ramp)
			.Text(LOCTEXT("RampMode", "Ramp"))
			+SSegmentedControl<EWeightColorMode>::Slot(EWeightColorMode::BoneColors)
			.Text(LOCTEXT("BoneColorsMode", "Bone Colors"))
			+SSegmentedControl<EWeightColorMode>::Slot(EWeightColorMode::FullMaterial)
			.Text(LOCTEXT("MaterialMode", "Full Material"))
		]
	];

	AddTransferUI(DetailBuilder);

	// Edit SkinWeightLayer category 
	IDetailCategoryBuilder& SkinWeightLayerCategory = DetailBuilder.EditCategory("SkinWeightLayer", FText::GetEmpty(), ECategoryPriority::Important);
	SkinWeightLayerCategory.InitiallyCollapsed(true);
	
	// hide skin weight tool properties that were customized
	TArray<FName> PropertiesToHide;
	PropertiesToHide.Add(GET_MEMBER_NAME_CHECKED(USkinWeightsPaintToolProperties, BrushMode));
	
	PropertiesToHide.Add(GET_MEMBER_NAME_CHECKED(USkinWeightsPaintToolProperties, EditingMode));
	PropertiesToHide.Add(GET_MEMBER_NAME_CHECKED(USkinWeightsPaintToolProperties, ColorMode));
	PropertiesToHide.Add(GET_MEMBER_NAME_CHECKED(USkinWeightsPaintToolProperties, MeshSelectMode));
	PropertiesToHide.Add(GET_MEMBER_NAME_CHECKED(USkinWeightsPaintToolProperties, SourceSkeletalMesh));
	PropertiesToHide.Add(GET_MEMBER_NAME_CHECKED(USkinWeightsPaintToolProperties, SourceLOD));
	PropertiesToHide.Add(GET_MEMBER_NAME_CHECKED(USkinWeightsPaintToolProperties, SourceSkinWeightProfile));
	for (const FName PropertyToHide : PropertiesToHide)
	{
		TSharedRef<IPropertyHandle> Property = DetailBuilder.GetProperty(PropertyToHide);
		DetailBuilder.HideProperty(Property);
	}
	// hide base class properties
	PropertiesToHide.Reset();
	PropertiesToHide.Add(GET_MEMBER_NAME_CHECKED(UBrushBaseProperties, bSpecifyRadius));
	PropertiesToHide.Add(GET_MEMBER_NAME_CHECKED(UBrushBaseProperties, BrushSize));
	PropertiesToHide.Add(GET_MEMBER_NAME_CHECKED(UBrushBaseProperties, BrushStrength));
	PropertiesToHide.Add(GET_MEMBER_NAME_CHECKED(UBrushBaseProperties, BrushFalloffAmount));
	PropertiesToHide.Add(GET_MEMBER_NAME_CHECKED(UBrushBaseProperties, BrushRadius));
	for (const FName PropertyToHide : PropertiesToHide)
	{
		TSharedRef<IPropertyHandle> Property = DetailBuilder.GetProperty(PropertyToHide, UBrushBaseProperties::StaticClass());
		DetailBuilder.HideProperty(Property);
	}
}

void FSkinWeightDetailCustomization::AddBrushUI(IDetailLayoutBuilder& DetailBuilder) const
{
	// custom display of falloff mode as segmented toggle buttons
	IDetailCategoryBuilder& BrushCategory = DetailBuilder.EditCategory("Brush", FText::GetEmpty(), ECategoryPriority::Important);

	// add segmented control toggle for brush behavior modes ("Add", "Replace", etc..)
	BrushCategory.AddCustomRow(LOCTEXT("BrushModeCategory", "Brush Mode"), false)
	.WholeRowContent()
	[
		SNew(SBox)
		.Padding(2.0f)
		[
			SNew(SSegmentedControl<EWeightEditOperation>)
			.ToolTipText(LOCTEXT("BrushTooltip",
				"Select the operation to apply when using the brush.\n"
				"Add: applies the current weight plus the Strength value to the new weight.\n"
				"Replace: applies the current weight minus the Strength value to the new weight.\n"
				"Multiply: applies the current weight multiplied by the Strength value to the new weight.\n"
				"Relax: applies the average of the connected (by edge) vertex weights to the new vertex weight, blended by the Strength."))
			.Value_Lambda([this]()
			{
				return ToolSettings->BrushMode;
			})
			.OnValueChanged_Lambda([this](EWeightEditOperation Mode)
			{
				ToolSettings->SetBrushMode(Mode);
			})
			+SSegmentedControl<EWeightEditOperation>::Slot(EWeightEditOperation::Add)
			.Text(LOCTEXT("BrushAddMode", "Add"))
			+ SSegmentedControl<EWeightEditOperation>::Slot(EWeightEditOperation::Replace)
			.Text(LOCTEXT("BrushReplaceMode", "Replace"))
			+ SSegmentedControl<EWeightEditOperation>::Slot(EWeightEditOperation::Multiply)
			.Text(LOCTEXT("BrushMultiplyMode", "Multiply"))
			+ SSegmentedControl<EWeightEditOperation>::Slot(EWeightEditOperation::Relax)
			.Text(LOCTEXT("BrushRelaxMode", "Relax"))
		]
	];

	// add segmented control toggle for brush falloff modes ("Surface" or "Volume")
	BrushCategory.AddCustomRow(LOCTEXT("BrushFalloffModeCategory", "Brush Falloff Mode"), false)
	.WholeRowContent()
	[
		SNew(SBox)
		.Padding(2.0f)
		[
			SNew(SSegmentedControl<EWeightBrushFalloffMode>)
			.ToolTipText(LOCTEXT("BrushFalloffModeTooltip",
					"Surface: falloff is based on the distance along the surface from the brush center to nearby connected vertices.\n"
					"Volume: falloff is based on the straight-line distance from the brush center to surrounding vertices.\n"))
			.Value_Lambda([this]()
			{
				return ToolSettings->GetBrushConfig().FalloffMode;
			})
			.OnValueChanged_Lambda([this](EWeightBrushFalloffMode Mode)
			{
				ToolSettings->SetFalloffMode(Mode);
			})
			+SSegmentedControl<EWeightBrushFalloffMode>::Slot(EWeightBrushFalloffMode::Surface)
			.Text(LOCTEXT("SurfaceMode", "Surface"))
			+ SSegmentedControl<EWeightBrushFalloffMode>::Slot(EWeightBrushFalloffMode::Volume)
			.Text(LOCTEXT("VolumeMode", "Volume"))
		]
	];

	BrushCategory.AddCustomRow(LOCTEXT("BrushSizeCategory", "Brush Radius"), false)
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("BrushRadiusLabel", "Radius"))
		.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
		.ToolTipText(LOCTEXT("BrushRadiusTooltip", "The radius of the brush in scene units."))
	]
	.ValueContent()
	[
		SNew(SSpinBox<float>)
		.MinValue(0.01f)
		.MaxSliderValue(20.f)
		.Value(10.0f)
		.SupportDynamicSliderMaxValue(true)
		.Value_Lambda([this]()
		{
			return ToolSettings->GetBrushConfig().Radius;
		})
		.OnValueChanged_Lambda([this](float NewValue)
		{
			ToolSettings->BrushRadius = NewValue;
			ToolSettings->GetBrushConfig().Radius = NewValue;
			FPropertyChangedEvent PropertyChangedEvent(UBrushBaseProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UBrushBaseProperties, BrushRadius)));
			ToolSettings->PostEditChangeProperty(PropertyChangedEvent);
		})
		.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type CommitType)
		{
			ToolSettings->SaveConfig();
		})
	];

	BrushCategory.AddCustomRow(LOCTEXT("BrushStrengthCategory", "Brush Strength"), false)
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("BrushStrengthLabel", "Strength"))
		.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
		.ToolTipText(LOCTEXT("BrushStrengthTooltip", "The strength of the effect on the weights. Exact effect depends on the Brush mode."))
	]
	.ValueContent()
	[
		SNew(SSpinBox<float>)
		.MinValue(0.f)
		.MaxValue(2.0f)
		.MaxSliderValue(1.f)
		.Value(1.0f)
		.SupportDynamicSliderMaxValue(true)
		.Value_Lambda([this]()
		{
			return ToolSettings->GetBrushConfig().Strength;
		})
		.OnValueChanged_Lambda([this](float NewValue)
		{
			ToolSettings->BrushStrength = NewValue;
			ToolSettings->GetBrushConfig().Strength = NewValue;
			FPropertyChangedEvent PropertyChangedEvent(UBrushBaseProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UBrushBaseProperties, BrushStrength)));
			ToolSettings->PostEditChangeProperty(PropertyChangedEvent);
		})
		.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type CommitType)
		{
			ToolSettings->SaveConfig();
		})
	];

	BrushCategory.AddCustomRow(LOCTEXT("BrushFalloffCategory", "Brush Falloff"), false)
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("BrushFalloffLabel", "Falloff"))
		.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
		.ToolTipText(LOCTEXT("BrushFalloffTooltip", "At 0, the brush has no falloff. At 1 it has exponential falloff."))
	]
	.ValueContent()
	[
		SNew(SSpinBox<float>)
		.MinValue(0.f)
		.MaxValue(1.f)
		.Value_Lambda([this]()
		{
			return ToolSettings->GetBrushConfig().Falloff;
		})
		.OnValueChanged_Lambda([this](float NewValue)
		{
			ToolSettings->BrushFalloffAmount = NewValue;
			ToolSettings->GetBrushConfig().Falloff = NewValue;
			FPropertyChangedEvent PropertyChangedEvent(UBrushBaseProperties::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UBrushBaseProperties, BrushFalloffAmount)));
			ToolSettings->PostEditChangeProperty(PropertyChangedEvent);
		})
		.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type CommitType)
		{
			ToolSettings->SaveConfig();
		})
	];
}

void FSkinWeightDetailCustomization::AddSelectionUI(IDetailLayoutBuilder& DetailBuilder) const
{
	// custom display of selection editing tools
	IDetailCategoryBuilder& EditSelectionCategory = DetailBuilder.EditCategory("Edit Selection", FText::GetEmpty(), ECategoryPriority::Important);
	EditSelectionCategory.InitiallyCollapsed(true);

	// create a toolbar for the selection filter
	FSlimHorizontalToolBarBuilder ToolbarBuilder(MakeShared<FUICommandList>(), FMultiBoxCustomization::None);
	ToolbarBuilder.SetStyle(FModelingToolsEditorModeStyle::Get().Get(), "PolyEd.SelectionToolbar");
	ToolbarBuilder.SetLabelVisibility(EVisibility::Collapsed);

	ToolbarBuilder.BeginSection("SelectionFilter");
	ToolbarBuilder.BeginBlockGroup();
	
	auto AddToggleButtonForBool = [&ToolbarBuilder, this](EComponentSelectionMode Mode, const FText& Label, const FText& Tooltip, const FName IconName)
	{
		ToolbarBuilder.AddToolBarButton(FUIAction(
		FExecuteAction::CreateLambda([this, Mode]()
		{
			ToolSettings->SetComponentMode(Mode);
		}),
		FCanExecuteAction::CreateLambda([this]()
		{
			return ToolSettings->EditingMode == EWeightEditMode::Mesh;
		}),
		FIsActionChecked::CreateLambda([this, Mode]()
		{
			return ToolSettings->ComponentSelectionMode == Mode;
		})),
		NAME_None,	// Extension hook
		Label,		// Label
		Tooltip,	// Tooltip
		FSlateIcon(FModelingToolsEditorModeStyle::Get()->GetStyleSetName(), IconName),
		EUserInterfaceActionType::ToggleButton);
	};

	AddToggleButtonForBool(
		EComponentSelectionMode::Vertices,
		LOCTEXT("VerticesLabel", "Vertices"),
		LOCTEXT("VerticesTooltip", "Select mesh vertices."),
		"PolyEd.SelectCorners");
	AddToggleButtonForBool(
		EComponentSelectionMode::Edges,
		LOCTEXT("EdgesLabel", "Edges"),
		LOCTEXT("EdgesTooltip", "Select mesh edges."),
		"PolyEd.SelectEdges");
	AddToggleButtonForBool(
		EComponentSelectionMode::Faces,
		LOCTEXT("FacesLabel", "Faces"),
		LOCTEXT("FacesTooltip", "Select mesh faces."),
		"PolyEd.SelectFaces");

	ToolbarBuilder.EndBlockGroup();
	ToolbarBuilder.EndSection();

	// edit selection category
	EditSelectionCategory.AddCustomRow(LOCTEXT("EditSelectionRow", "Edit Selection"), false)
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			// ISOLATE SELECTION
			+SHorizontalBox::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(2.0f, WeightEditVerticalPadding)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				[
					ToolbarBuilder.MakeWidget()
				]

				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
					.HAlign(HAlign_Center)
					.ToolTipText(LOCTEXT("IsolateSelectedTooltip",
							"Shows only the selected faces in the viewport.\n"
							"Weight editing operations will not affect hidden vertices.\n"
							"NOTE: This only works on the target (main) mesh."))
					.IsEnabled_Lambda([this]()
					{
						// isolated selection only available on main mesh (for now)
						const bool bHasSelection = Tool->GetMainMeshSelector()->IsAnyComponentSelected();
						const bool bAlreadyIsolatingSelection = Tool->GetSelectionIsolator()->IsSelectionIsolated();
						return bHasSelection ||  bAlreadyIsolatingSelection;
					})
					.IsChecked_Lambda([this]()
					{
						const bool bAlreadyIsolatingSelection = Tool->GetSelectionIsolator()->IsSelectionIsolated();
						return bAlreadyIsolatingSelection ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this](ECheckBoxState InCheckBoxState)
					{
						if (InCheckBoxState == ECheckBoxState::Checked)
						{
							Tool->GetSelectionIsolator()->IsolateSelectionAsTransaction();
						}
						else
						{
							Tool->GetSelectionIsolator()->UnIsolateSelectionAsTransaction();
						}
					})
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							if (Tool->GetSelectionIsolator()->IsSelectionIsolated())
							{
								return LOCTEXT("ShowAllButtonLabel", "Show Full Mesh");
							}
								
							return LOCTEXT("IsolateButtonLabel", "Isolate Selected");
						})
					]
				]
			]
		]

		// GROW / SHRINK / FLOOD
		+SVerticalBox::Slot()
		[
			SNew(SHorizontalBox)
		
			+SHorizontalBox::Slot()
			.Padding(2.f, WeightEditVerticalPadding)
			[
				SNew(SButton)
				.IsEnabled_Lambda([this]{ return Tool->GetActiveMeshSelector()->IsAnyComponentSelected(); })
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Text(LOCTEXT("GrowSelectionButtonLabel", "Grow"))
				.ToolTipText(LOCTEXT("GrowSelectionTooltip",
						"Grow the current selection by adding connected neighbors to current selection.\n"))
				.OnClicked_Lambda([this]()
				{
					Tool->GetActiveMeshSelector()->GrowSelection();
					return FReply::Handled();
				})
			]

			+SHorizontalBox::Slot()
			.Padding(2.f, WeightEditVerticalPadding)
			[
				SNew(SButton)
				.IsEnabled_Lambda([this]{ return Tool->GetActiveMeshSelector()->IsAnyComponentSelected(); })
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Text(LOCTEXT("ShrinkSelectionButtonLabel", "Shrink"))
				.ToolTipText(LOCTEXT("ShrinkSelectionTooltip",
						"Shrink the current selection by removing components on the border of the current selection.\n"))
				.OnClicked_Lambda([this]()
				{
					Tool->GetActiveMeshSelector()->ShrinkSelection();
					return FReply::Handled();
				})
			]

			+SHorizontalBox::Slot()
			.Padding(2.f, WeightEditVerticalPadding)
			[
				SNew(SButton)
				.IsEnabled_Lambda([this]{ return Tool->GetActiveMeshSelector()->IsAnyComponentSelected(); })
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Text(LOCTEXT("FloodSelectionButtonLabel", "Flood"))
				.ToolTipText(LOCTEXT("FloodSelectionTooltip",
						"Flood the current selection by adding all connected components to the current selection.\n"))
				.OnClicked_Lambda([this]()
				{
					Tool->GetActiveMeshSelector()->FloodSelection();
					return FReply::Handled();
				})
			]
		]

		// SELECT AFFECTED VERTICES
		+SVerticalBox::Slot()
		[
			SNew(SHorizontalBox)
		
			+SHorizontalBox::Slot()
			.Padding(2.f, WeightEditVerticalPadding)
			[
				SNew(SButton)
				.IsEnabled_Lambda([this]
				{
					// only allow selecting affected vertices on the target/main mesh
					return ToolSettings->MeshSelectMode == EMeshTransferOption::Target;
				})
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Text(LOCTEXT("AffectedSelectionButtonLabel", "Affected"))
				.ToolTipText(LOCTEXT("AffectedSelectionTooltip",
						"Select vertices that are affected by the currently selected bone(s).\n"
						"Holding Shift or Ctrl will add or subtract affected vertices from the current selection."))
				.OnClicked_Lambda([this]()
				{
					Tool->SelectAffected();
					return FReply::Handled();
				})
			]

			+SHorizontalBox::Slot()
			.Padding(2.f, WeightEditVerticalPadding)
			[
				SNew(SButton)
				.IsEnabled_Lambda([this]{ return Tool->GetActiveMeshSelector()->IsAnyComponentSelected(); })
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Text(LOCTEXT("BorderSelectionButtonLabel", "Convert to Border"))
				.ToolTipText(LOCTEXT("BorderSelectionTooltip", "Select vertices on the border of the current selection."))
				.OnClicked_Lambda([this]()
				{
					Tool->GetActiveMeshSelector()->SelectBorder();
					return FReply::Handled();
				})
			]
		]
	];
	
	IDetailCategoryBuilder& EditWeightsCategory = DetailBuilder.EditCategory("Edit Weights", FText::GetEmpty(), ECategoryPriority::Important);
	EditWeightsCategory.InitiallyCollapsed(true);

	EditWeightsCategory.AddCustomRow(LOCTEXT("SelectMessageRow", "Select Vertices"), false)
	.WholeRowContent()
	.VAlign(VAlign_Center)
	[
		SNew(STextBlock)
		.Visibility_Lambda([this]()
		{
			return !Tool->HasActiveSelectionOnMainMesh() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		.Text_Lambda([this]()
		{
			return LOCTEXT("NothingSelectedLabel", "Select vertices on target mesh to edit weights...");
		})
	];

	// FLOOD WEIGHTS SLIDER category
	ToolSettings->DirectEditState.Reset();
	EditWeightsCategory.AddCustomRow(LOCTEXT("FloodWeightsRow", "Flood Weights Slider"), false)
	.WholeRowContent()
	[
		SNew(SHorizontalBox)
		.IsEnabled_Lambda([this]{ return Tool->HasActiveSelectionOnMainMesh(); })

		+SHorizontalBox::Slot()
		[
			SNew(SSegmentedControl<EWeightEditOperation>)
			.ToolTipText(LOCTEXT("InteractiveEditModeTooltip",
				"Add: applies the current weight plus the flood value to the new weight.\n"
				"Multiply: applies the current weight multiplied by the flood value to the new weight.\n"
				"This operation applies interactively while dragging the slider. It operates on the currently selected vertices."))
			.Value_Lambda([this]()
			{
				return ToolSettings->DirectEditState.EditMode;
			})
			.OnValueChanged_Lambda([this](EWeightEditOperation Mode)
			{
				ToolSettings->DirectEditState.EditMode = Mode;
				ToolSettings->DirectEditState.Reset();
			})
			+SSegmentedControl<EWeightEditOperation>::Slot(EWeightEditOperation::Add)
			.Text(LOCTEXT("BrushAddMode", "Add"))
			+ SSegmentedControl<EWeightEditOperation>::Slot(EWeightEditOperation::Multiply)
			.Text(LOCTEXT("BrushMultiplyMode", "Multiply"))
		]
		
		+SHorizontalBox::Slot()
		.FillWidth(1)
		[
			SNew(SVerticalBox)

			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SSpinBox<float>)
				.Visibility_Lambda([this]()
				{
					const bool bIsVisible = ToolSettings->DirectEditState.EditMode != EWeightEditOperation::Relax;
					return bIsVisible ? EVisibility::Visible : EVisibility::Collapsed;
				})
				.MinSliderValue_Lambda([this]()
				{
					return ToolSettings->DirectEditState.GetModeMinValue();
				})
				.MaxSliderValue_Lambda([this]()
				{
					return ToolSettings->DirectEditState.GetModeMaxValue();
				})
				.MinValue_Lambda([this]()
				{
					return ToolSettings->DirectEditState.GetModeMinValue();
				})
				.MaxValue_Lambda([this]()
				{
					return ToolSettings->DirectEditState.GetModeMaxValue();
				})
				.Value_Lambda([this]()
				{
					return ToolSettings->DirectEditState.CurrentValue;
				})
				.OnValueChanged_Lambda([this](float NewValue)
				{
					ToolSettings->DirectEditState.CurrentValue = NewValue;
					
					if (ToolSettings->DirectEditState.bInTransaction)
					{
						
						float Value = NewValue;
						if (ToolSettings->DirectEditState.EditMode == EWeightEditOperation::Add)
						{
							Value = NewValue - ToolSettings->DirectEditState.StartValue;
						}
						
						constexpr bool bShouldTransact = false;
						
						Tool->EditWeightsOnVertices(
							Tool->GetCurrentBoneIndex(),
							Value,
							0, // iterations
							ToolSettings->DirectEditState.EditMode,
							Tool->GetMainMeshSelector()->GetSelectedVertices(),
							bShouldTransact);
					}
				})
				.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type CommitType)
				{
					if (!ToolSettings->DirectEditState.bInTransaction)
					{
						constexpr bool bShouldTransact = true;
						Tool->EditWeightsOnVertices(
							Tool->GetCurrentBoneIndex(),
							NewValue,
							0, // iterations
							ToolSettings->DirectEditState.EditMode,
							Tool->GetMainMeshSelector()->GetSelectedVertices(),
							bShouldTransact);
					}
					ToolSettings->DirectEditState.bInTransaction = false;
				})
				.OnBeginSliderMovement_Lambda([this]()
				{
					ToolSettings->DirectEditState.StartValue = ToolSettings->DirectEditState.CurrentValue;
					ToolSettings->DirectEditState.bInTransaction = true;
					Tool->BeginChange();
				})
				.OnEndSliderMovement_Lambda([this](float)
				{
					const FText TransactionLabel = LOCTEXT("FloodWeightChange", "Flood weights on vertices.");
					Tool->EndChange(TransactionLabel);
					ToolSettings->DirectEditState.bInTransaction = false;

					// reset multiply slider
					if (ToolSettings->DirectEditState.EditMode == EWeightEditOperation::Multiply)
					{
						// multiplying operation is always relative to 1.0
						ToolSettings->DirectEditState.CurrentValue = 1.0f;
						ToolSettings->DirectEditState.StartValue = 1.0f;
					}

					// reset add slider
					if (ToolSettings->DirectEditState.EditMode == EWeightEditOperation::Add)
					{
						// add operation is always relative to 0.0
						ToolSettings->DirectEditState.CurrentValue = 0.0f;
						ToolSettings->DirectEditState.StartValue = 0.0f;
					}
				})
				.ToolTipText(LOCTEXT("FloodWeightsToolTip", "Drag the slider to interactively adjust weights on the selected vertices."))
			]	
		]
	];

	// ADD WEIGHTS category
	EditWeightsCategory.AddCustomRow(LOCTEXT("AddWeightsRow", "Add"), false)
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		.IsEnabled_Lambda([this]{ return Tool->HasActiveSelectionOnMainMesh(); })

		+ SVerticalBox::Slot()
		.Padding(WeightEditHorizontalPadding, WeightEditVerticalPadding)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			[
				SNew(SBox)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("AddWeightsButtonLabel", "Add"))
					.ToolTipText(LOCTEXT("AddButtonTooltip",
					"Add: applies the current weight plus the flood value to the new weight.\n"
					"This operation applies to the currently selected vertices."))
					.OnClicked_Lambda([this]()
					{
						constexpr bool bShouldTransact = true;
						Tool->EditWeightsOnVertices(
							Tool->GetCurrentBoneIndex(),
							ToolSettings->AddStrength,
							0, // iterations 
							EWeightEditOperation::Add,
							Tool->GetMainMeshSelector()->GetSelectedVertices(),
							bShouldTransact);
						return FReply::Handled();
					})
				]
			]

			+SHorizontalBox::Slot()
			[
				SNew(SSpinBox<float>)
				.MinValue(0)
				.MaxValue(1)
				.ToolTipText(LOCTEXT("AddWeightsSliderToolTip", "Adjust the value to Add to the selected vertices."))
				.Value_Lambda([this]()
				{
					return ToolSettings->AddStrength;
				})
				.OnValueChanged_Lambda([this](float NewValue)
				{
					ToolSettings->AddStrength = NewValue;
				})
				.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type CommitType)
				{
					ToolSettings->SaveConfig();
				})
			]
		]
	];

	// REPLACE WEIGHTS category
	EditWeightsCategory.AddCustomRow(LOCTEXT("ReplaceWeightsRow", "Replace"), false)
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		.IsEnabled_Lambda([this]{ return Tool->HasActiveSelectionOnMainMesh(); })

		+ SVerticalBox::Slot()
		.Padding(WeightEditHorizontalPadding, WeightEditVerticalPadding)
		[
			SNew(SHorizontalBox)

			+SHorizontalBox::Slot()
			[
				SNew(SBox)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("ReplaceWeightsButtonLabel", "Replace"))
					.ToolTipText(LOCTEXT("ReplaceButtonTooltip",
						"Replace: the weight of selected vertices is replaced by the specified value.\n"
						"This operation applies to the currently selected vertices."))
					.OnClicked_Lambda([this]()
					{
						constexpr bool bShouldTransact = true;
						Tool->EditWeightsOnVertices(
							Tool->GetCurrentBoneIndex(),
							ToolSettings->ReplaceValue,
							0, // iterations
							EWeightEditOperation::Replace,
							Tool->GetMainMeshSelector()->GetSelectedVertices(),
							bShouldTransact);
						return FReply::Handled();
					})
				]
			]

			+SHorizontalBox::Slot()
			[
				SNew(SSpinBox<float>)
				.MinValue(0.f)
				.MaxValue(1.f)
				.ToolTipText(LOCTEXT("ReplaceWeightsSliderToolTip", "Adjust the value to Replace on the selected vertices."))
				.Value_Lambda([this]()
				{
					return ToolSettings->ReplaceValue;
				})
				.OnValueChanged_Lambda([this](float NewValue)
				{
					ToolSettings->ReplaceValue = NewValue;
				})
				.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type CommitType)
				{
					ToolSettings->SaveConfig();
				})
			]
		]
	];

	// AVERAGE WEIGHTS category
	EditWeightsCategory.AddCustomRow(LOCTEXT("AverageWeightsRow", "Average"), false)
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		.IsEnabled_Lambda([this]{ return Tool->HasActiveSelectionOnMainMesh(); })

		+ SVerticalBox::Slot()
		.Padding(WeightEditHorizontalPadding, WeightEditVerticalPadding)
		[
			SNew(SHorizontalBox)

			+SHorizontalBox::Slot()
			[
				SNew(SBox)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("AverageWeightsButtonLabel", "Average"))
					.ToolTipText(LOCTEXT("AverageButtonTooltip",
						"Weights on all selected vertices are set to the average of all selected vertices."))
					.OnClicked_Lambda([this]()
					{
						ToolSettings->WeightTool->AverageWeights(ToolSettings->AverageStrength);
						return FReply::Handled();
					})
				]
			]

			+SHorizontalBox::Slot()
			[
				SNew(SSpinBox<float>)
				.MinValue(0.f)
				.MaxValue(1.f)
				.ToolTipText(LOCTEXT("AverageWeightsSliderToolTip", "Blend the amount to Average the weights on the selected vertices."))
				.Value_Lambda([this]()
				{
					return ToolSettings->AverageStrength;
				})
				.OnValueChanged_Lambda([this](float NewValue)
				{
					ToolSettings->AverageStrength = NewValue;
				})
				.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type CommitType)
				{
					ToolSettings->SaveConfig();
				})
			]
		]
	];

	// RELAX WEIGHTS category
	EditWeightsCategory.AddCustomRow(LOCTEXT("RelaxWeightsRow", "Relax"), false)
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		.IsEnabled_Lambda([this]{ return Tool->HasActiveSelectionOnMainMesh(); })

		+ SVerticalBox::Slot()
		.Padding(WeightEditHorizontalPadding, WeightEditVerticalPadding)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			[
				SNew(SBox)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("RelaxWeightsButtonLabel", "Relax"))
					.ToolTipText(LOCTEXT("RelaxButtonTooltip",
						"Relax: the weight of each selected vertex is replaced by the average of it's neighbors. \n"
						"This smooths weights across the mesh."))
					.OnClicked_Lambda([this]()
					{
						constexpr bool bShouldTransact = true;
						constexpr int32 DefaultRelaxIterations = 5; // provides a reasonable falloff distance 
						Tool->EditWeightsOnVertices(
							Tool->GetCurrentBoneIndex(),
							ToolSettings->RelaxStrength,
							DefaultRelaxIterations, 
							EWeightEditOperation::Relax,
							Tool->GetMainMeshSelector()->GetSelectedVertices(),
							bShouldTransact);
						return FReply::Handled();
					})
				]
			]

			+SHorizontalBox::Slot()
			[
				SNew(SSpinBox<float>)
				.MinValue(0)
				.MaxValue(1)
				.ToolTipText(LOCTEXT("RelaxWeightsSliderToolTip", "Blend the amount to Relax the weights on the selected vertices."))
				.Value_Lambda([this]()
				{
					return ToolSettings->RelaxStrength;
				})
				.OnValueChanged_Lambda([this](float NewValue)
				{
					ToolSettings->RelaxStrength = NewValue;
				})
				.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type CommitType)
				{
					ToolSettings->SaveConfig();
				})
			]
		]
	];
	
	// MIRROR WEIGHTS category
	EditWeightsCategory.AddCustomRow(LOCTEXT("MirrorWeightsRow", "Mirror"), false)
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		.IsEnabled_Lambda([this]{ return Tool->HasActiveSelectionOnMainMesh(); })
		
		+ SVerticalBox::Slot()
		.Padding(WeightEditHorizontalPadding, WeightEditVerticalPadding)
		[
			SNew(SHorizontalBox)
			
			+SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.FillWidth(WeightEditingLabelsPercent)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("MirrorPlaneLabel", "Mirror Plane"))
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.ToolTipText(LOCTEXT("MirrorPlaneTooltip", "The plane to copy weights across."))
			]

			+SHorizontalBox::Slot()
			.FillWidth(1.f)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				[
					SNew(SSegmentedControl<EAxis::Type>)
					.ToolTipText(LOCTEXT("MirrorAxisTooltip",
						"X: copies weights across the YZ plane.\n"
						"Y: copies weights across the XZ plane.\n"
						"Z: copies weights across the XY plane."))
					.Value_Lambda([this]()
					{
						return ToolSettings->MirrorAxis;
					})
					.OnValueChanged_Lambda([this](EAxis::Type Mode)
					{
						ToolSettings->MirrorAxis = Mode;
					})
					+ SSegmentedControl<EAxis::Type>::Slot(EAxis::X)
					.Text(LOCTEXT("MirrorXLabel", "X"))
					+ SSegmentedControl<EAxis::Type>::Slot(EAxis::Y)
					.Text(LOCTEXT("MirrorYLabel", "Y"))
					+ SSegmentedControl<EAxis::Type>::Slot(EAxis::Z)
					.Text(LOCTEXT("MirrorZLabel", "Z"))
				]
					
				+SHorizontalBox::Slot()
				[
					SNew(SSegmentedControl<EMirrorDirection>)
					.ToolTipText(LOCTEXT("MirrorDirectionTooltip", "The direction that determines what side of the plane to copy weights from."))
					.Value_Lambda([this]()
					{
						return ToolSettings->MirrorDirection;
					})
					.OnValueChanged_Lambda([this](EMirrorDirection Mode)
					{
						ToolSettings->MirrorDirection = Mode;
					})
					+ SSegmentedControl<EMirrorDirection>::Slot(EMirrorDirection::PositiveToNegative)
					.Text(LOCTEXT("MirrorPosToNegLabel", "+ to -"))
					+ SSegmentedControl<EMirrorDirection>::Slot(EMirrorDirection::NegativeToPositive)
					.Text(LOCTEXT("MirrorNegToPosLabel", "- to +"))
				]
			]
		]

		+SVerticalBox::Slot()
		.Padding(0.f, WeightEditVerticalPadding)
		[
			SNew(SBox)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("MirrorWeightsButtonLabel", "Mirror"))
				.ToolTipText(LOCTEXT("MirrorButtonTooltip",
					"Weights are copied across the given plane in the given direction.\n"
					"This command operates on the selected vertices."))
				.OnClicked_Lambda([this]()
				{
					Tool->MirrorWeights(ToolSettings->MirrorAxis, ToolSettings->MirrorDirection);
					return FReply::Handled();
				})
			]
		]
	];

	// NORMALIZE WEIGHTS category
	EditWeightsCategory.AddCustomRow(LOCTEXT("NormalizeWeightsRow", "Normalize"), false)
	.WholeRowContent()
	[
		SNew(SHorizontalBox)
		.IsEnabled_Lambda([this]{ return Tool->HasActiveSelectionOnMainMesh(); })

		+SHorizontalBox::Slot()
		.Padding(2.f, WeightEditVerticalPadding)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Text(LOCTEXT("NormalizeWeightsButtonLabel", "Normalize"))
			.ToolTipText(LOCTEXT("NormalizeWeightsTooltip",
					"Forces the weights on the selected vertices to sum to 1.\n"
					"This command operates on the selected vertices."))
			.IsEnabled_Lambda([this]()
			{
				return ToolSettings->EditingMode == EWeightEditMode::Mesh;
			})
			.OnClicked_Lambda([this]()
			{
				Tool->NormalizeWeights();
				return FReply::Handled();
			})
		]

		+SHorizontalBox::Slot()
		.Padding(2.f, WeightEditVerticalPadding)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Text(LOCTEXT("HammerWeightsButtonLabel", "Hammer"))
			.ToolTipText(LOCTEXT("HammerWeightsTooltip",
					"Copies the weight of the nearest non-selected vertex.\n"
					"This command operates on the selected vertices."))
			.IsEnabled_Lambda([this]()
			{
				return ToolSettings->EditingMode == EWeightEditMode::Mesh;
			})
			.OnClicked_Lambda([this]()
			{
				Tool->HammerWeights();
				return FReply::Handled();
			})
		]
	];

	// PRUNE WEIGHTS category
	EditWeightsCategory.AddCustomRow(LOCTEXT("PruneWeightsRow", "Prune"), false)
	.WholeRowContent()
	[
		SNew(SHorizontalBox)
		.IsEnabled_Lambda([this]{ return Tool->HasActiveSelectionOnMainMesh(); })

		+SHorizontalBox::Slot()
		.Padding(2.f, WeightEditVerticalPadding)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("PruneWeightsButtonLabel", "Prune"))
			.ToolTipText(LOCTEXT("PruneButtonTooltip",
				"Removes influences with weights below the given threshold value.\n"
				"Pruned bones are removed from the list of bones affecting the given vertex.\n"
				"Pruned bones will no longer recieve weight when a vertex is normalized.\n"
				"This command operates on the selected vertices."))
			.OnClicked_Lambda([this]()
			{
				ToolSettings->WeightTool->PruneWeights(ToolSettings->PruneValue, TArray<BoneIndex>());
				return FReply::Handled();
			})
		]

		+SHorizontalBox::Slot()
		.Padding(2.f, WeightEditVerticalPadding)
		[
			SNew(SSpinBox<float>)
			.MinValue(0.f)
			.MaxValue(1.f)
			.Value_Lambda([this]()
			{
				return ToolSettings->PruneValue;
			})
			.OnValueChanged_Lambda([this](float NewValue)
			{
				ToolSettings->PruneValue = NewValue;
			})
			.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type CommitType)
			{
				ToolSettings->SaveConfig();
			})
		]
	];

	// COPY/PASTE WEIGHTS category
	EditWeightsCategory.AddCustomRow(LOCTEXT("CopyPasteWeightsRow", "Copy Paste"), false)
	.WholeRowContent()
	[
		SNew(SHorizontalBox)
		.IsEnabled_Lambda([this]{ return Tool->HasActiveSelectionOnMainMesh(); })

		+SHorizontalBox::Slot()
		.Padding(2.f, WeightEditVerticalPadding)
		[
			SNew(SBox)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("CopyWeightsButtonLabel", "Copy"))
				.ToolTipText(LOCTEXT("CopyButtonTooltip",
					"Copy the average weights of the selected vertices to the clipboard. \n"
					"This is designed to work with the Paste command."))
				.OnClicked_Lambda([this]()
				{
					ToolSettings->WeightTool->CopyWeights();
					return FReply::Handled();
				})
			]
		]

		+SHorizontalBox::Slot()
		.Padding(2.f, WeightEditVerticalPadding)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("PasteWeightsButtonLabel", "Paste"))
			.ToolTipText(LOCTEXT("PasteButtonTooltip",
				"Paste the weights on the selected vertices.\n"
				"This command requires the clipboard contain weights from the Copy command."))
			.OnClicked_Lambda([this]()
			{
				ToolSettings->WeightTool->PasteWeights();
				return FReply::Handled();
			})
		]
	];
	
	// VERTEX EDITOR category
	EditWeightsCategory.AddCustomRow(LOCTEXT("VertexEditorRow", "Component Editor"), false)
	.WholeRowContent()
	[
		SNew(SBox)
		.IsEnabled_Lambda([this]{ return Tool->HasActiveSelectionOnMainMesh(); })
		[
			SNew(SVertexWeightEditor, ToolSettings->WeightTool)
		]
	];
}

void FSkinWeightDetailCustomization::AddTransferUI(IDetailLayoutBuilder& DetailBuilder) const
{
	if (!ensure(Tool.IsValid()))
	{
		return;
	}
	
	IDetailCategoryBuilder& TransferWeightsCategory = DetailBuilder.EditCategory("WeightTransfer", FText::GetEmpty(), ECategoryPriority::Important);
	TransferWeightsCategory.InitiallyCollapsed(true);

	// TRANSFER BUTTON
	TransferWeightsCategory.AddCustomRow(LOCTEXT("TransferWeightsRow", "Transfer Weights"), false)
	.WholeRowContent()
	[
		SNew(SBox)
		.Padding(0.f, WeightEditVerticalPadding)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("TransferWeightsButtonLabel", "Transfer Weights"))
			.ToolTipText(LOCTEXT("TransferButtonTooltip",
						"Weights are transferred from the source skeletal mesh.\n"
						"Vertices may be selected on the source and/or target mesh to filter which parts to copy from and which parts to copy to.\n"
						"If either mesh has no vertices selected, the whole mesh is considered.\n"))
			.OnClicked_Lambda([this]()
			{
				Tool->GetWeightTransferManager()->TransferWeights();
				return FReply::Handled();
			})
			.IsEnabled_Lambda([this]()
			{
				return Tool->GetWeightTransferManager()->CanTransferWeights();
			})
		]
	];

	// SKELETAL MESH ASSET INPUT
	const TSharedRef<IPropertyHandle> SourceSkeletalMeshHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(USkinWeightsPaintToolProperties, SourceSkeletalMesh), USkinWeightsPaintToolProperties::StaticClass());
	TransferWeightsCategory.AddProperty(SourceSkeletalMeshHandle);
	
	// LOD
	const TSharedRef<IPropertyHandle> LODHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(USkinWeightsPaintToolProperties, SourceLOD), USkinWeightsPaintToolProperties::StaticClass());
	TransferWeightsCategory.AddProperty(LODHandle);

	// PROFILE
	const TSharedRef<IPropertyHandle> SourceProfileHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(USkinWeightsPaintToolProperties, SourceSkinWeightProfile), USkinWeightsPaintToolProperties::StaticClass());
	TransferWeightsCategory.AddProperty(SourceProfileHandle);

	// MESH SELECTION OPTION (SOURCE OR TARGET)
	const TSharedRef<IPropertyHandle> TransferSelectModeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(USkinWeightsPaintToolProperties, MeshSelectMode), USkinWeightsPaintToolProperties::StaticClass());
	FDetailWidgetRow& TransferSelectModeRow = TransferWeightsCategory.AddCustomRow(LOCTEXT("SelectionModeRow", "Selection Mode"), false)
	.NameContent()
	[
		TransferSelectModeHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SBox)
		.Padding(2.0f)
		.HAlign(HAlign_Center)
		[
			SNew(SSegmentedControl<EMeshTransferOption>)
			.ToolTipText(LOCTEXT("SelectionSourceTooltip",
					"Choose which mesh to select components on (vertices/edges/faces).\n"
					"Weights will be transferred from selected components on the source to selected components on the target.\n"
					"If no components are selected on either the source or target, the whole mesh will be considered.\n"
					"Source: The mesh to copy weights FROM.\n"
					"Target: The mesh to copy weights TO (the main mesh in the tool)."))
			.Value_Lambda([this]()
			{
				return ToolSettings->MeshSelectMode;
			})
			.OnValueChanged_Lambda([this](EMeshTransferOption Mode)
			{
				ToolSettings->MeshSelectMode = Mode;
				ToolSettings->WeightTool->UpdateSelectorState();
			})
			+ SSegmentedControl<EMeshTransferOption>::Slot(EMeshTransferOption::Source)
			.Text(LOCTEXT("SourceMode", "Source"))
			+SSegmentedControl<EMeshTransferOption>::Slot(EMeshTransferOption::Target)
			.Text(LOCTEXT("TargetMode", "Target"))
		]
	];

	// PREVIEW OFFSET
	const TSharedRef<IPropertyHandle> PreviewOffsetHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(USkinWeightsPaintToolProperties, SourcePreviewOffset), USkinWeightsPaintToolProperties::StaticClass());
	TransferWeightsCategory.AddProperty(PreviewOffsetHandle);
}

void SVertexWeightItem::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTableView)
{
	Element = InArgs._Element;
	ParentTable = InArgs._ParentTable;
	SMultiColumnTableRow<TSharedPtr<FWeightEditorElement>>::Construct(FSuperRowType::FArguments(), OwnerTableView);
}

TSharedRef<SWidget> SVertexWeightItem::GenerateWidgetForColumn(const FName& ColumnName)
{
	if (ColumnName == ColumnName_Bone)
	{
		const FName BoneName = ParentTable->Tool->GetBoneNameFromIndex(Element->BoneIndex);
		return SNew(STextBlock).Text(FText::FromName(BoneName));
	}

	if (ColumnName == ColumnName_Weight)
	{
		return SNew(SNumericEntryBox<float>)
		.AllowSpin(true)
		.MinSliderValue(0.f)
		.MinValue(0.f)
		.MaxSliderValue(1.0f)
		.MaxValue(1.f)
		.Value_Lambda([this]()
		{
			if (bInTransaction)
			{
				return ValueDuringSlide;
			}
			
			return ParentTable->Tool->GetAverageWeightOnBone(Element->BoneIndex, ParentTable->Tool->GetMainMeshSelector()->GetSelectedVertices());
		})
		.OnValueChanged_Lambda([this](float NewValue)
		{
			if (bInTransaction)
			{
				ValueDuringSlide = NewValue;

				const bool bScalingUp = NewValue >= ValueAtStartOfSlide || FMath::IsNearlyEqual(ValueAtStartOfSlide, 0.f);
				const float RangeEnd = bScalingUp ? 1.f : 0.f;
				float RelativeScale = (NewValue - ValueAtStartOfSlide) * 1.f / (RangeEnd - ValueAtStartOfSlide);
				RelativeScale *= bScalingUp ? 1.f : -1.f;
				constexpr bool bShouldTransact = false;
				ParentTable->Tool->EditWeightsOnVertices(
					Element->BoneIndex,
					RelativeScale,
					0, // iterations
					EWeightEditOperation::RelativeScale,
					ParentTable->Tool->GetMainMeshSelector()->GetSelectedVertices(),
					bShouldTransact);
			}
		})
		.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type CommitType)
		{
			if (!bInTransaction)
			{
				constexpr bool bShouldTransact = true;
				ParentTable->Tool->EditWeightsOnVertices(
					Element->BoneIndex,
					NewValue,
					0, // iterations,
					EWeightEditOperation::Replace,
					ParentTable->Tool->GetMainMeshSelector()->GetSelectedVertices(),
					bShouldTransact);
			}
			bInTransaction = false;
		})
		.OnBeginSliderMovement_Lambda([this]()
		{
			ParentTable->Tool->BeginChange();
			ValueAtStartOfSlide = ParentTable->Tool->GetAverageWeightOnBone(Element->BoneIndex, ParentTable->Tool->GetMainMeshSelector()->GetSelectedVertices());
			ValueDuringSlide = ValueAtStartOfSlide;
			bInTransaction = true;
		})
		.OnEndSliderMovement_Lambda([this](float)
		{
			const FText TransactionLabel = LOCTEXT("DirectWeightChange", "Scale weights on vertices.");
			ParentTable->Tool->EndChange(TransactionLabel);
			bInTransaction = false;
		})
		.ToolTipText(LOCTEXT("WeightSliderToolTip", "Set the weight on this bone for the selected vertices."));
	}

	if (ColumnName == ColumnName_Prune)
	{
		return SNew(SHorizontalBox)
		
		+SHorizontalBox::Slot()
		.FillWidth(1.f)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.HAlign(HAlign_Right)
			.ToolTipText(LOCTEXT("PruneInfluence", "Prune the influence from the selected vertices."))
			.OnClicked_Lambda([this]() -> FReply
			{
				// use a negative threshold weight that no weight will ever
				// be below because we only want to prune based on the bone, regardless of the weight
				constexpr float NegativeThreshold = -1.0f;
				ParentTable->Tool->PruneWeights(NegativeThreshold, {Element->BoneIndex});
				return FReply::Handled();
			})
			.Content()
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush("Icons.Delete"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		];
	}

	checkNoEntry();
	return SNullWidget::NullWidget;
}

SVertexWeightEditor::~SVertexWeightEditor()
{
	if (Tool.IsValid())
	{
		Tool->OnSelectionChanged.RemoveAll(this);
		Tool->OnWeightsChanged.RemoveAll(this);
		Tool.Reset();
	}
}

void SVertexWeightEditor::Construct(const FArguments& InArgs, USkinWeightsPaintTool* InSkinTool)
{
	Tool = InSkinTool;

	ChildSlot
	[
		SNew(SBox)
		[
			SAssignNew( ListView, SWeightEditorListViewType )
			.Visibility_Lambda([this]()
			{
				return Tool->GetMainMeshSelector()->GetSelectedVertices().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
			})
			.SelectionMode(ESelectionMode::Single)
			.ListItemsSource( &ListViewItems )
			.OnGenerateRow_Lambda([this](TSharedPtr<FWeightEditorElement> Element, const TSharedRef<STableViewBase>& OwnerTableView)
			{
				return SNew(SVertexWeightItem, OwnerTableView).Element(Element).ParentTable(SharedThis(this));
			})
			.HeaderRow
			(
				SNew(SHeaderRow)
				+ SHeaderRow::Column(ColumnName_Bone)
				.HAlignHeader(HAlign_Center)
				.DefaultLabel(LOCTEXT("WeightEditorBoneColumn", "Bone"))
				+ SHeaderRow::Column(ColumnName_Weight)
				.HAlignHeader(HAlign_Center)
				.DefaultLabel(LOCTEXT("WeightEditorWeightColumn", "Weight (Average)"))
				+ SHeaderRow::Column(ColumnName_Prune)
				.HAlignHeader(HAlign_Center)
				.DefaultLabel(LOCTEXT("WeightEditorPruneColumn", "Prune"))
				.FixedWidth(60.f)
			)
		]
	];

	RefreshView();
	
	Tool->OnSelectionChanged.AddSP(this, &SVertexWeightEditor::RefreshView);
	Tool->OnWeightsChanged.AddSP(this, &SVertexWeightEditor::RefreshView);
}

void SVertexWeightEditor::RefreshView()
{
	if (!Tool.IsValid())
	{
		return; 
	}
	
	// get all bones affecting the selected vertices
	TArray<int32> Influences;
	Tool->GetInfluences(Tool->GetMainMeshSelector()->GetSelectedVertices(), Influences);

	// generate list view items
	ListViewItems.Reset();
	for (const int32 InfluenceIndex : Influences)
	{
		ListViewItems.Add(MakeShareable(new FWeightEditorElement(InfluenceIndex)));
	}
	
	ListView->RequestListRefresh();
}

#undef LOCTEXT_NAMESPACE
