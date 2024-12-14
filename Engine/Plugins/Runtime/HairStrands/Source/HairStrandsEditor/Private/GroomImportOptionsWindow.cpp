// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomImportOptionsWindow.h"

#include "Styling/AppStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "GroomCacheImportOptions.h"
#include "GroomImportOptions.h"
#include "IDetailsView.h"
#include "Interfaces/IMainFrameModule.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "GroomAsset.h"

#define LOCTEXT_NAMESPACE "GroomImportOptionsWindow"

void SGroomImportOptionsWindow::UpdateStatus(UGroomHairGroupsPreview* Description) const
{
	CurrentStatus = EHairDescriptionStatus::None;
	if (!Description)
	{
		EnumAddFlags(CurrentStatus, EHairDescriptionStatus::Unknown);
		return;
	}

	const bool bImportGroomAsset = !GroomCacheImportOptions || GroomCacheImportOptions->ImportSettings.bImportGroomAsset;
	const bool bImportGroomCache = GroomCacheImportOptions && GroomCacheImportOptions->ImportSettings.bImportGroomCache;
	if (!bImportGroomAsset && !bImportGroomCache)
	{
		EnumAddFlags(CurrentStatus, EHairDescriptionStatus::Unknown);
		return;
	}

	if (Description->Groups.Num() == 0)
	{
		EnumAddFlags(CurrentStatus, EHairDescriptionStatus::NoGroup);
		return;
	}

	// Check the validity of the groom to import

	bool bGuidesOnly = false;
	for (const FGroomHairGroupPreview& Group : Description->Groups)
	{
		if (Group.CurveCount == 0)
		{
			EnumAddFlags(CurrentStatus, EHairDescriptionStatus::NoCurve);
			if (Group.GuideCount > 0)
			{
				bGuidesOnly = true;
			}
			break;
		}
	}

	// Check if any curve or point have been trimmed
	for (const FGroomHairGroupPreview& Group : Description->Groups)
	{
		if (Group.Flags & uint32(EHairGroupInfoFlags::HasTrimmedCurve))
		{
			EnumAddFlags(CurrentStatus, EHairDescriptionStatus::CurveLimit);
		}
		if (Group.Flags & uint32(EHairGroupInfoFlags::HasTrimmedPoint))
		{
			EnumAddFlags(CurrentStatus, EHairDescriptionStatus::PointLimit);
		}
		if (Group.Flags & uint32(EHairGroupInfoFlags::HasInvalidPoint))
		{
			EnumAddFlags(CurrentStatus, EHairDescriptionStatus::InvalidPoint);
		}		
	}

	if (!bImportGroomCache)
	{
		EnumAddFlags(CurrentStatus, EHairDescriptionStatus::GroomValid);
		return;
	}

	// Update the states of the properties being monitored
	bImportGroomAssetState = GroomCacheImportOptions->ImportSettings.bImportGroomAsset;
	bImportGroomCacheState = GroomCacheImportOptions->ImportSettings.bImportGroomCache;
	GroomAsset = GroomCacheImportOptions->ImportSettings.GroomAsset;

	if (!GroomCacheImportOptions->ImportSettings.bImportGroomAsset)
	{
		// When importing a groom cache with a provided groom asset, check their compatibility
		UGroomAsset* GroomAssetForCache = Cast<UGroomAsset>(GroomCacheImportOptions->ImportSettings.GroomAsset.TryLoad());
		if (!GroomAssetForCache)
		{
			// No groom asset provided or loaded but one is needed with this setting
			EnumAddFlags(CurrentStatus, bGuidesOnly ? EHairDescriptionStatus::GuidesOnly : EHairDescriptionStatus::GroomCache);
			return;
		}

		const TArray<FHairGroupPlatformData>& GroomHairGroupsData = GroomAssetForCache->GetHairGroupsPlatformData();
		if (GroomHairGroupsData.Num() != Description->Groups.Num())
		{
			EnumAddFlags(CurrentStatus, bGuidesOnly ? EHairDescriptionStatus::GuidesOnlyIncompatible : EHairDescriptionStatus::GroomCacheIncompatible);
			return;
		}

		for (int32 Index = 0; Index < GroomHairGroupsData.Num(); ++Index)
		{
			// Check the strands compatibility
			if (!bGuidesOnly && Description->Groups[Index].CurveCount != GroomHairGroupsData[Index].Strands.BulkData.GetNumCurves())
			{
				EnumAddFlags(CurrentStatus, EHairDescriptionStatus::GroomCacheIncompatible);
				break;
			}

			// Check the guides compatibility if there were strands tagged as guides
			// Otherwise, guides will be generated according to the groom asset interpolation settings
			// and compatibility cannot be determined here
			if (Description->Groups[Index].GuideCount > 0 &&
				Description->Groups[Index].GuideCount != GroomHairGroupsData[Index].Guides.BulkData.GetNumCurves())
			{
				EnumAddFlags(CurrentStatus, bGuidesOnly ? EHairDescriptionStatus::GuidesOnlyIncompatible : EHairDescriptionStatus::GroomCacheIncompatible);
				break;
			}
		}

		EnumAddFlags(CurrentStatus, bGuidesOnly ? EHairDescriptionStatus::GuidesOnlyCompatible : EHairDescriptionStatus::GroomCacheCompatible);
	}
	else
	{
		// A guides-only groom cannot be imported as asset, but otherwise the imported groom asset
		// is always compatible with the groom cache since they are from the same file
		EnumAddFlags(CurrentStatus, bGuidesOnly ? EHairDescriptionStatus::GuidesOnly : EHairDescriptionStatus::GroomValid);
	}
}


FText SGroomImportOptionsWindow::GetStatusText() const
{
	FString Out;
	if (EnumHasAnyFlags(CurrentStatus, EHairDescriptionStatus::Error)) 						{ Out += LOCTEXT("GroomOptionsWindow_ValidationText0",  "Error\n").ToString(); }
	else if (EnumHasAnyFlags(CurrentStatus, EHairDescriptionStatus::Warning)) 				{ Out += LOCTEXT("GroomOptionsWindow_ValidationText1",  "Warning\n").ToString(); }
	else if (EnumHasAnyFlags(CurrentStatus, EHairDescriptionStatus::Valid))					{ Out += LOCTEXT("GroomOptionsWindow_ValidationText2",  "Valid\n").ToString(); }

	if (EnumHasAnyFlags(CurrentStatus, EHairDescriptionStatus::NoCurve)) 					{ Out += LOCTEXT("GroomOptionsWindow_ValidationText3",  "Some groups have 0 curves.\n").ToString(); }
	if (EnumHasAnyFlags(CurrentStatus, EHairDescriptionStatus::NoGroup)) 					{ Out += LOCTEXT("GroomOptionsWindow_ValidationText4",  "The groom does not contain any group.\n").ToString(); }
	if (EnumHasAnyFlags(CurrentStatus, EHairDescriptionStatus::GroomCache))					{ Out += LOCTEXT("GroomOptionsWindow_ValidationText5",  "A compatible groom asset must be provided to import the groom cache.\n").ToString(); }
	if (EnumHasAnyFlags(CurrentStatus, EHairDescriptionStatus::GroomCacheCompatible)) 		{ Out += LOCTEXT("GroomOptionsWindow_ValidationText6",  "The groom cache is compatible with the groom asset provided.\n").ToString(); }
	if (EnumHasAnyFlags(CurrentStatus, EHairDescriptionStatus::GroomCacheIncompatible))		{ Out += LOCTEXT("GroomOptionsWindow_ValidationText7",  "The groom cache is incompatible with the groom asset provided.\n").ToString(); }
	if (EnumHasAnyFlags(CurrentStatus, EHairDescriptionStatus::GuidesOnly)) 				{ Out += LOCTEXT("GroomOptionsWindow_ValidationText8",  "Only guides were detected. A compatible groom asset must be provided.\n").ToString(); }
	if (EnumHasAnyFlags(CurrentStatus, EHairDescriptionStatus::GuidesOnlyCompatible)) 		{ Out += LOCTEXT("GroomOptionsWindow_ValidationText9",  "Only guides were detected. The groom asset provided is compatible.\n").ToString(); }
	if (EnumHasAnyFlags(CurrentStatus, EHairDescriptionStatus::GuidesOnlyIncompatible))		{ Out += LOCTEXT("GroomOptionsWindow_ValidationText10", "Only guides were detected. The groom asset provided is incompatible.\n").ToString(); }
	if (EnumHasAnyFlags(CurrentStatus, EHairDescriptionStatus::CurveLimit))					{ Out += LOCTEXT("GroomOptionsWindow_ValidationText11", "At least one group contains more curves than allowed limit (Max:4M). Curves beyond that limit will be trimmed.\n").ToString(); static_assert(HAIR_MAX_NUM_CURVE_PER_GROUP == 4194303); }
	if (EnumHasAnyFlags(CurrentStatus, EHairDescriptionStatus::PointLimit))					{ Out += LOCTEXT("GroomOptionsWindow_ValidationText12", "At least one group contains more control points per curve than the allowed limit (Max:255). Control points beyond that limit will be trimmed.\n").ToString(); static_assert(HAIR_MAX_NUM_POINT_PER_CURVE == 255); }
	if (EnumHasAnyFlags(CurrentStatus, EHairDescriptionStatus::InvalidPoint)) 				{ Out += LOCTEXT("GroomOptionsWindow_ValidationText13", "At least one group contains a curve with invalid points. These curves will be trimmed from the asset.\n").ToString(); }
	if (EnumHasAnyFlags(CurrentStatus, EHairDescriptionStatus::Unknown)) 					{ Out += LOCTEXT("GroomOptionsWindow_ValidationText14", "Unknown\n").ToString(); }

	return FText::FromString(Out);
}

FSlateColor SGroomImportOptionsWindow::GetStatusColor() const
{
	if (EnumHasAnyFlags(CurrentStatus, EHairDescriptionStatus::Error))  	{ return FLinearColor(0.80f, 0, 0, 1); }
	if (EnumHasAnyFlags(CurrentStatus, EHairDescriptionStatus::Warning))  	{ return FLinearColor(0.80f, 0.80f, 0, 1); }
	if (EnumHasAnyFlags(CurrentStatus, EHairDescriptionStatus::Valid))		{ return FLinearColor(0, 0.80f, 0, 1); }

	return FLinearColor(1, 1, 1);
}

static void AddAttribute(SVerticalBox::FScopedWidgetSlotArguments& Slot, FText AttributeLegend)
{
	const FLinearColor AttributeColor(0.72f, 0.72f, 0.20f);
	const FSlateFontInfo AttributeFont = FAppStyle::GetFontStyle("CurveEd.InfoFont");
	const FSlateFontInfo AttributeResultFont = FAppStyle::GetFontStyle("CurveEd.InfoFont");

	Slot
	.AutoHeight()
	.Padding(2)
	[
		SNew(SBorder)
		.Padding(FMargin(3))
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(10, 0, 0, 0)
			[
				SNew(STextBlock)
				.Font(AttributeFont)
				.Text(AttributeLegend)
				.ColorAndOpacity(AttributeColor)
			]				
		]
	];
}

FText GetHairAttributeLocText(EHairAttribute In, uint32 InFlags)
{
	// If a new optional attribute is added, please add its UI/text description here
	static_assert(uint32(EHairAttribute::Count) == 7);

	switch (In)
	{
	case EHairAttribute::RootUV:					return HasHairAttributeFlags(InFlags, EHairAttributeFlags::HasRootUDIM) ? LOCTEXT("GroomOptionsWindow_HasRootUDIM", "Root UV (UDIM)") : LOCTEXT("GroomOptionsWindow_HasRootUV", "Root UV");
	case EHairAttribute::ClumpID:					return HasHairAttributeFlags(InFlags, EHairAttributeFlags::HasMultipleClumpIDs) ? LOCTEXT("GroomOptionsWindow_HasClumpIDs", "Clump IDs (3)") : LOCTEXT("GroomOptionsWindow_HasClumpID", "Clump ID");
	case EHairAttribute::StrandID:					return LOCTEXT("GroomOptionsWindow_HasStrandID", "Strand ID");
	case EHairAttribute::PrecomputedGuideWeights:	return LOCTEXT("GroomOptionsWindow_HasPercomputedGuideWeights", "Pre-Computed Guide Weights");
	case EHairAttribute::Color:						return LOCTEXT("GroomOptionsWindow_HasColor", "Color");
	case EHairAttribute::Roughness:					return LOCTEXT("GroomOptionsWindow_HasRoughness", "Roughness");
	case EHairAttribute::AO:						return LOCTEXT("GroomOptionsWindow_HasAO", "AO");
	}
	return FText::GetEmpty();
}

void SGroomImportOptionsWindow::Construct(const FArguments& InArgs)
{
	ImportOptions = InArgs._ImportOptions;
	GroomCacheImportOptions = InArgs._GroomCacheImportOptions;
	GroupsPreview = InArgs._GroupsPreview;
	WidgetWindow = InArgs._WidgetWindow;

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->SetObject(ImportOptions);
	
	DetailsView2 = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView2->SetObject(GroupsPreview);

	GroomCacheDetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	GroomCacheDetailsView->SetObject(GroomCacheImportOptions);

	CurrentStatus = EHairDescriptionStatus::None;
	UpdateStatus(GroupsPreview);

	// Aggregate attributes from all groups (ideally we should display each group attribute separately, to check if one groom is not missing data)
	uint32 Attributes = 0;
	uint32 AttributeFlags = 0;
	for (const FGroomHairGroupPreview& Group : GroupsPreview->Groups)
	{
		Attributes |= Group.Attributes;
		AttributeFlags |= Group.AttributeFlags;
	}

	FText bHasAttributeText = LOCTEXT("GroomOptionsWindow_HasAttributeNone", "None");
	FLinearColor bHasAttributeColor = FLinearColor(0.80f, 0, 0, 1);
	if (Attributes != 0)
	{
		bHasAttributeText = LOCTEXT("GroomOptionsWindow_HasAttributeValid", "Valid");
		bHasAttributeColor = FLinearColor(0, 0.80f, 0, 1);
	}

	auto VerticalSlot = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SNew(SBorder)
			.Padding(FMargin(3))
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle("CurveEd.LabelFont"))
					.Text(LOCTEXT("CurrentFile", "Current File: "))
				]
				+ SHorizontalBox::Slot()
				.Padding(5, 0, 0, 0)
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle("CurveEd.InfoFont"))
					.Text(InArgs._FullPath)
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SNew(SBorder)
			.Padding(FMargin(3))
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle("CurveEd.LabelFont"))
					.Text(LOCTEXT("GroomOptionsWindow_StatusFile", "Status File: "))
				]
				+ SHorizontalBox::Slot()
				.Padding(5, 0, 0, 0)
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle("CurveEd.InfoFont"))
					.Text(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SGroomImportOptionsWindow::GetStatusText)))
					.ColorAndOpacity(TAttribute<FSlateColor>::Create(TAttribute<FSlateColor>::FGetter::CreateSP(this, &SGroomImportOptionsWindow::GetStatusColor)))
				]
			]
		]

		
		// Insert title of for the attributes
		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SNew(SBorder)
			.Padding(FMargin(3))
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle("CurveEd.LabelFont"))
					.Text(LOCTEXT("GroomOptionsWindow_Attribute", "Attributes: "))
				]
				+ SHorizontalBox::Slot()
				.Padding(5, 0, 0, 0)
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle("CurveEd.InfoFont"))
					.Text(bHasAttributeText)
					.ColorAndOpacity(bHasAttributeColor)
				]
			]
		]

		// All optional attribute will be inserted here
		// The widget are inserted at the end of this function

		+ SVerticalBox::Slot()
		.Padding(2)
		.MaxHeight(500.0f)
		[
			DetailsView->AsShared()
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)		
		[
			GroomCacheDetailsView->AsShared()
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)		
		[
			DetailsView2->AsShared()
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(2)
		[
			SNew(SUniformGridPanel)
			.SlotPadding(2)
			+ SUniformGridPanel::Slot(0, 0)
			[
				SAssignNew(ImportButton, SButton)
				.HAlign(HAlign_Center)
				.Text(InArgs._ButtonLabel)
				.IsEnabled(this, &SGroomImportOptionsWindow::CanImport)
				.OnClicked(this, &SGroomImportOptionsWindow::OnImport)
			]
			+ SUniformGridPanel::Slot(1, 0)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("Cancel", "Cancel"))
				.OnClicked(this, &SGroomImportOptionsWindow::OnCancel)
			]
		];

	// Insert all the optional attributes
	uint32 AttributeSlotIndex = 3;
	for (uint32 AttributeIt = 0; AttributeIt < uint32(EHairAttribute::Count); ++AttributeIt)
	{
		const EHairAttribute AttributeType = (EHairAttribute)AttributeIt;
		if (HasHairAttribute(Attributes, AttributeType))
		{
			SVerticalBox::FScopedWidgetSlotArguments SlotArg = VerticalSlot->InsertSlot(AttributeSlotIndex++);
			AddAttribute(SlotArg, GetHairAttributeLocText(AttributeType, AttributeFlags));
		}
	}

	this->ChildSlot
	[
		VerticalSlot
	];
}

bool SGroomImportOptionsWindow::CanImport() const
{
	bool bNeedUpdate = CurrentStatus == EHairDescriptionStatus::None;
	if (GroomCacheImportOptions)
	{
		bNeedUpdate |= bImportGroomAssetState != GroomCacheImportOptions->ImportSettings.bImportGroomAsset;
		bNeedUpdate |= bImportGroomCacheState != GroomCacheImportOptions->ImportSettings.bImportGroomCache;
		bNeedUpdate |= GroomAsset != GroomCacheImportOptions->ImportSettings.GroomAsset;
	}

	if (bNeedUpdate)
	{
		UpdateStatus(GroupsPreview);
	}

	return EnumHasAnyFlags(CurrentStatus, EHairDescriptionStatus::Valid | EHairDescriptionStatus::Warning);
}

enum class EGroomOptionsVisibility : uint8
{
	None = 0x00,
	ConversionOptions = 0x01,
	BuildOptions = 0x02,
	All = ConversionOptions | BuildOptions
};

ENUM_CLASS_FLAGS(EGroomOptionsVisibility);

TSharedPtr<SGroomImportOptionsWindow> DisplayOptions(
	UGroomImportOptions* ImportOptions, 
	UGroomCacheImportOptions* GroomCacheImportOptions, 
	UGroomHairGroupsPreview* GroupsPreview,
	const FString& FilePath, 
	EGroomOptionsVisibility VisibilityFlag, 
	FText WindowTitle, 
	FText InButtonLabel)
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(WindowTitle)
		.SizingRule(ESizingRule::Autosized);

	TSharedPtr<SGroomImportOptionsWindow> OptionsWindow;

	FProperty* ConversionOptionsProperty = FindFProperty<FProperty>(ImportOptions->GetClass(), GET_MEMBER_NAME_CHECKED(UGroomImportOptions, ConversionSettings));
	if (ConversionOptionsProperty)
	{
		if (EnumHasAnyFlags(VisibilityFlag, EGroomOptionsVisibility::ConversionOptions))
		{
			ConversionOptionsProperty->SetMetaData(TEXT("ShowOnlyInnerProperties"), TEXT("1"));
			ConversionOptionsProperty->SetMetaData(TEXT("Category"), TEXT("Conversion"));
		}
		else
		{
			// Note that UGroomImportOptions HideCategories named "Hidden",
			// but the hiding doesn't work with ShowOnlyInnerProperties 
			ConversionOptionsProperty->RemoveMetaData(TEXT("ShowOnlyInnerProperties"));
			ConversionOptionsProperty->SetMetaData(TEXT("Category"), TEXT("Hidden"));
		}
	}

	FString FileName = FPaths::GetCleanFilename(FilePath);
	Window->SetContent
	(
		SAssignNew(OptionsWindow, SGroomImportOptionsWindow)
		.ImportOptions(ImportOptions)
		.GroomCacheImportOptions(GroomCacheImportOptions)
		.GroupsPreview(GroupsPreview)
		.WidgetWindow(Window)
		.FullPath(FText::FromString(FileName))
		.ButtonLabel(InButtonLabel)
	);

	TSharedPtr<SWindow> ParentWindow;

	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
		ParentWindow = MainFrame.GetParentWindow();
	}

	FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);

	return OptionsWindow;
}

TSharedPtr<SGroomImportOptionsWindow> SGroomImportOptionsWindow::DisplayImportOptions(UGroomImportOptions* ImportOptions, UGroomCacheImportOptions* GroomCacheImportOptions, UGroomHairGroupsPreview* GroupsPreview, const FString& FilePath)
{
	// If there's no groom cache to import, don't show its import options
	UGroomCacheImportOptions* GroomCacheOptions = GroomCacheImportOptions && GroomCacheImportOptions->ImportSettings.bImportGroomCache ? GroomCacheImportOptions : nullptr;
	return DisplayOptions(ImportOptions, GroomCacheOptions, GroupsPreview, FilePath, EGroomOptionsVisibility::All, LOCTEXT("GroomImportWindowTitle", "Groom Import Options"), LOCTEXT("Import", "Import"));
}

TSharedPtr<SGroomImportOptionsWindow> SGroomImportOptionsWindow::DisplayRebuildOptions(UGroomImportOptions* ImportOptions, UGroomHairGroupsPreview* GroupsPreview, const FString& FilePath)
{
	return DisplayOptions(ImportOptions, nullptr, GroupsPreview, FilePath, EGroomOptionsVisibility::BuildOptions, LOCTEXT("GroomRebuildWindowTitle ", "Groom Build Options"), LOCTEXT("Build", "Build"));
}


#undef LOCTEXT_NAMESPACE
