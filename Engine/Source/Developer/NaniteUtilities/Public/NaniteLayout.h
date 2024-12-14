// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "NaniteDefinitions.h"
#include "PropertyHandle.h"
#include "IDetailGroup.h"
#include "IDetailChildrenBuilder.h"
#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformWrapPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SFilePathPicker.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Engine/EngineTypes.h"
#include "Misc/ScopedSlowTask.h"
#include "EditorDirectories.h"
#include "EngineAnalytics.h"
#include "FbxMeshUtils.h"

class IDetailCategoryBuilder;
class IDetailChildrenBuilder;
class IDetailLayoutBuilder;
class IDetailGroup;

namespace Nanite
{

#define LOCTEXT_NAMESPACE "NaniteLayout"

template< typename StructType, typename CopyFuncType >
IDetailPropertyRow& AddDefaultRow( IDetailCategoryBuilder& CategoryBuilder, StructType& Struct, FName PropertyName, CopyFuncType CopyFunc )
{
	TSharedPtr<FStructOnScope> TempStruct = MakeShared< FStructOnScope >( StructType::StaticStruct() );
	StructType::StaticStruct()->CopyScriptStruct( TempStruct->GetStructMemory(), &Struct, 1 );
	IDetailPropertyRow* PropertyRow = CategoryBuilder.AddExternalStructureProperty( TempStruct, PropertyName );
	PropertyRow->GetPropertyHandle()->SetOnPropertyValueChanged( FSimpleDelegate::CreateLambda(
		[ &Struct, TempStruct, CopyFunc ] 
		{
			StructType* TempStruct2 = (StructType*)TempStruct->GetStructMemory();
			CopyFunc( Struct, *TempStruct2 );
		}
	));
	PropertyRow->GetPropertyHandle()->SetOnChildPropertyValueChanged( FSimpleDelegate::CreateLambda(
		[ &Struct, TempStruct, CopyFunc ] 
		{
			StructType* TempStruct2 = (StructType*)TempStruct->GetStructMemory();
			CopyFunc( Struct, *TempStruct2 );
		}
	));
	return *PropertyRow;
}

template< typename StructType, typename MemberType >
IDetailPropertyRow& AddDefaultRow( IDetailCategoryBuilder& CategoryBuilder, StructType& Struct, MemberType (StructType::*MemberPointer), FName PropertyName )
{
	return AddDefaultRow(CategoryBuilder, Struct, PropertyName,
		[MemberPointer]( StructType& Dst, StructType& Src )
		{
			Dst.*MemberPointer = Src.*MemberPointer;
		} );
}

#define NANITE_ADD_DEFAULT_ROW(PropertyName) \
	AddDefaultRow( NaniteSettingsCategory, NaniteSettings, GET_MEMBER_NAME_CHECKED( FMeshNaniteSettings, PropertyName ), \
		[]( FMeshNaniteSettings& Dst, FMeshNaniteSettings& Src ) \
		{ \
			Dst.PropertyName = Src.PropertyName; \
		})

template<typename TMeshType, bool bSupportsForceEnable, bool bSupportsHighRes>
class FSettingsLayout : public TSharedFromThis<FSettingsLayout<TMeshType, bSupportsForceEnable, bSupportsHighRes>>
{
public:
	typedef TMeshType MeshType;

public:
	FSettingsLayout()
	{
		// Position options
		PositionPrecisionOptions.Add(MakeShared<FString>(LOCTEXT("PositionPrecisionAuto", "Auto").ToString()));
		for (int32 i = DisplayPositionPrecisionMin; i <= DisplayPositionPrecisionMax; i++)
		{
			PositionPrecisionOptions.Add(MakeShared<FString>(PositionPrecisionValueToDisplayString(i)));
		}

		// Normal options
		const FText NormalAutoText = FText::Format(LOCTEXT("NormalPrecisionAuto", "Auto ({0} bits)"), 8);	//TODO: Just use Auto=8 for now
		NormalPrecisionOptions.Add(MakeShared<FString>(NormalAutoText.ToString()));
		for (int32 i = DisplayNormalPrecisionMin; i <= DisplayNormalPrecisionMax; i++)
		{
			NormalPrecisionOptions.Add(MakeShared<FString>(NormalPrecisionValueToDisplayString(i)));
		}

		// Tangent options
		const FText TangentAutoText = FText::Format(LOCTEXT("TangentPrecisionAuto", "Auto ({0} bits)"), 7);	//TODO: Just use Auto=7 for now
		TangentPrecisionOptions.Add(MakeShared<FString>(TangentAutoText.ToString()));
		for (int32 i = DisplayTangentPrecisionMin; i <= DisplayTangentPrecisionMax; i++)
		{
			TangentPrecisionOptions.Add(MakeShared<FString>(TangentPrecisionValueToDisplayString(i)));
		}

		// Residency options
		const FText ResidencyMinimalText = FText::Format(LOCTEXT("ResidencyMinimum", "Minimal ({0}KB)"), NANITE_ROOT_PAGE_GPU_SIZE >> 10);
		ResidencyOptions.Add(MakeShared<FString>(ResidencyMinimalText.ToString()));
		for (int32 i = DisplayMinimumResidencyExpRangeMin; i <= DisplayMinimumResidencyExpRangeMax; i++)
		{
			ResidencyOptions.Add(MakeShared<FString>(MinimumResidencyValueToDisplayString(1 << i), false));
		}

		ResidencyOptions.Add(MakeShared<FString>(LOCTEXT("ResidencyFull", "Full").ToString()));
	}

	~FSettingsLayout()
	{
	}

	/** Position Precision range selectable in the UI. */
	static const int32 DisplayPositionPrecisionAuto = MIN_int32;
	static const int32 DisplayPositionPrecisionMin = -6;
	static const int32 DisplayPositionPrecisionMax = 13;

	static int32 PositionPrecisionIndexToValue(int32 Index)
	{
		check(Index >= 0);

		if (Index == 0)
		{
			return DisplayPositionPrecisionAuto;
		}
		else
		{
			int32 Value = DisplayPositionPrecisionMin + (Index - 1);
			Value = FMath::Min(Value, DisplayPositionPrecisionMax);
			return Value;
		}
	}

	static int32 PositionPrecisionValueToIndex(int32 Value)
	{
		if (Value == DisplayPositionPrecisionAuto)
		{
			return 0;
		}
		else
		{
			Value = FMath::Clamp(Value, DisplayPositionPrecisionMin, DisplayPositionPrecisionMax);
			return Value - DisplayPositionPrecisionMin + 1;
		}
	}

	/** Display string to show in menus. */
	static FString PositionPrecisionValueToDisplayString(int32 Value)
	{
		check(Value != DisplayPositionPrecisionAuto);
	
		if(Value <= 0)
		{
			return FString::Printf(TEXT("%dcm"), 1 << (-Value));
		}
		else
		{
			const float fValue = static_cast<float>(FMath::Exp2((double)-Value));
			return FString::Printf(TEXT("1/%dcm (%.3gcm)"), 1 << Value, fValue);
		}
	}

	/** Normal Precision range selectable in the UI. */
	static const int32 DisplayNormalPrecisionAuto = -1;
	static const int32 DisplayNormalPrecisionMin = 5;
	static const int32 DisplayNormalPrecisionMax = 15;

	static int32 NormalPrecisionIndexToValue(int32 Index)
	{
		check(Index >= 0);

		if (Index == 0)
		{
			return DisplayNormalPrecisionAuto;
		}
		else
		{
			int32 Value = DisplayNormalPrecisionMin + (Index - 1);
			Value = FMath::Min(Value, DisplayNormalPrecisionMax);
			return Value;
		}
	}

	static int32 NormalPrecisionValueToIndex(int32 Value)
	{
		if (Value == DisplayNormalPrecisionAuto)
		{
			return 0;
		}
		else
		{
			Value = FMath::Clamp(Value, DisplayNormalPrecisionMin, DisplayNormalPrecisionMax);
			return Value - DisplayNormalPrecisionMin + 1;
		}
	}

	/** Display string to show in menus. */
	static FString NormalPrecisionValueToDisplayString(int32 Value)
	{
		check(Value != DisplayNormalPrecisionAuto);
		return FString::Printf(TEXT("%d bits"), Value);
	}

	/** Tangent Precision range selectable in the UI. */
	static const int32 DisplayTangentPrecisionAuto = -1;
	static const int32 DisplayTangentPrecisionMin = 4;
	static const int32 DisplayTangentPrecisionMax = 12;

	static int32 TangentPrecisionIndexToValue(int32 Index)
	{
		check(Index >= 0);

		if (Index == 0)
		{
			return DisplayTangentPrecisionAuto;
		}
		else
		{
			int32 Value = DisplayTangentPrecisionMin + (Index - 1);
			Value = FMath::Min(Value, DisplayTangentPrecisionMax);
			return Value;
		}
	}

	static int32 TangentPrecisionValueToIndex(int32 Value)
	{
		if (Value == DisplayTangentPrecisionAuto)
		{
			return 0;
		}
		else
		{
			Value = FMath::Clamp(Value, DisplayTangentPrecisionMin, DisplayTangentPrecisionMax);
			return Value - DisplayTangentPrecisionMin + 1;
		}
	}

	/** Display string to show in menus. */
	static FString TangentPrecisionValueToDisplayString(int32 Value)
	{
		check(Value != DisplayTangentPrecisionAuto);
		return FString::Printf(TEXT("%d bits"), Value);
	}

	/** Residency range selectable in the UI. */
	static const int32 DisplayMinimumResidencyMinimalIndex = 0;
	static const int32 DisplayMinimumResidencyExpRangeMin = 5;
	static const int32 DisplayMinimumResidencyExpRangeMax = 15;
	static const int32 DisplayMinimumResidencyFullIndex = DisplayMinimumResidencyExpRangeMax - DisplayMinimumResidencyExpRangeMin + 2;

	static uint32 MinimumResidencyIndexToValue(int32 Index)
	{
		if (Index == DisplayMinimumResidencyMinimalIndex)
		{
			return 0;
		}
		else if (Index == DisplayMinimumResidencyFullIndex)
		{
			return MAX_uint32;
		}
		else
		{
			return 1u << (DisplayMinimumResidencyExpRangeMin + Index - 1);
		}
	}

	static int32 MinimumResidencyValueToIndex(uint32 Value)
	{
		if (Value == 0)
		{
			return DisplayMinimumResidencyMinimalIndex;
		}
		else if (Value == MAX_uint32)
		{
			return DisplayMinimumResidencyFullIndex;
		}
		else
		{
			int32 Exp = (int32)FMath::CeilLogTwo(Value);
			return FMath::Clamp(Exp, DisplayMinimumResidencyExpRangeMin, DisplayMinimumResidencyExpRangeMax) - DisplayMinimumResidencyExpRangeMin + 1;
		}
	}

	/** Display string to show in menus. */
	static FString MinimumResidencyValueToDisplayString(uint32 Value)
	{
		if (Value < 1024)
		{
			return FString::Printf(TEXT("%dKB"), Value);
		}
		else
		{
			return FString::Printf(TEXT("%dMB"), Value >> 10);
		}
	}

	const FMeshNaniteSettings& GetSettings() const
	{
		return NaniteSettings;
	}

	void UpdateSettings(const FMeshNaniteSettings& InSettings)
	{
		NaniteSettings = InSettings;
	}

	/** Returns true if settings have been changed and an Apply is needed to update the asset. */
	bool IsApplyNeeded() const
	{
		const MeshType* MeshAsset = GetMesh();
		check(MeshAsset);
		return MeshAsset->NaniteSettings != NaniteSettings;
	}

	/** Apply current Nanite settings to the mesh. */
	void ApplyChanges()
	{
		TMeshType* MeshAsset = GetMesh();
		check(MeshAsset);

		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("MeshName"), FText::FromString(MeshAsset->GetName()));
			FScopedSlowTask SlowTask(0, FText::Format(LOCTEXT("ApplyNaniteChanges", "Applying changes to {MeshName}..."), Args), true);
			SlowTask.MakeDialog();

			MeshAsset->Modify();
			MeshAsset->NaniteSettings = NaniteSettings;

			FProperty* ChangedProperty = FindFProperty<FProperty>(TMeshType::StaticClass(), GET_MEMBER_NAME_CHECKED(TMeshType, NaniteSettings));
			FPropertyChangedEvent Event(ChangedProperty);
			MeshAsset->PostEditChangeProperty(Event);
		}

		RefreshTool();
	}

protected:
	FReply OnApply()
	{
		ApplyChanges();
		return FReply::Handled();
	}

	ECheckBoxState IsEnabledChecked() const
	{
		bool bEnabled = NaniteSettings.bEnabled;
		if constexpr (bSupportsForceEnable)
		{
			const MeshType* MeshAsset = GetMesh();
			bEnabled |= (MeshAsset && MeshAsset->IsNaniteForceEnabled());
		}
		return bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	void OnEnabledChanged(ECheckBoxState NewState)
	{
		NaniteSettings.bEnabled = NewState == ECheckBoxState::Checked ? true : false;
	}

	void OnPositionPrecisionChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
	{
		int32 NewValueInt = PositionPrecisionIndexToValue(PositionPrecisionOptions.Find(NewValue));
		if (NaniteSettings.PositionPrecision != NewValueInt)
		{
			if (FEngineAnalytics::IsAvailable())
			{
				FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.NaniteSettings"), TEXT("PositionPrecision"), *NewValue.Get());
			}
			NaniteSettings.PositionPrecision = NewValueInt;
		}
	}

	void OnNormalPrecisionChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
	{
		int32 NewValueInt = NormalPrecisionIndexToValue(NormalPrecisionOptions.Find(NewValue));
		if (NaniteSettings.NormalPrecision != NewValueInt)
		{
			if (FEngineAnalytics::IsAvailable())
			{
				FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.NaniteSettings"), TEXT("NormalPrecision"), *NewValue.Get());
			}
			NaniteSettings.NormalPrecision = NewValueInt;
		}
	}

	void OnTangentPrecisionChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
	{
		int32 NewValueInt = TangentPrecisionIndexToValue(TangentPrecisionOptions.Find(NewValue));
		if (NaniteSettings.TangentPrecision != NewValueInt)
		{
			if (FEngineAnalytics::IsAvailable())
			{
				FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.NaniteSettings"), TEXT("TangentPrecision"), *NewValue.Get());
			}
			NaniteSettings.TangentPrecision = NewValueInt;
		}
	}

	void OnResidencyChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
	{
		int32 NewValueInt = MinimumResidencyIndexToValue(ResidencyOptions.Find(NewValue));
		if (NaniteSettings.TargetMinimumResidencyInKB != NewValueInt)
		{
			if (FEngineAnalytics::IsAvailable())
			{
				FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.NaniteSettings"), TEXT("MinimumResidency"), *NewValue.Get());
			}
			NaniteSettings.TargetMinimumResidencyInKB = NewValueInt;
		}
	}

	float GetKeepPercentTriangles() const
	{
		return NaniteSettings.KeepPercentTriangles * 100.0f; // Display fraction as percentage.
	}

	void OnKeepPercentTrianglesChanged(float NewValue)
	{
		// Percentage -> fraction.
		NaniteSettings.KeepPercentTriangles = NewValue * 0.01f;
	}

	void OnKeepPercentTrianglesCommitted(float NewValue, ETextCommit::Type TextCommitType)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.NaniteSettings"), TEXT("KeepPercentTriangles"), FString::Printf(TEXT("%.1f"), NewValue));
		}
		OnKeepPercentTrianglesChanged(NewValue);
	}

	float GetTrimRelativeError() const
	{
		return NaniteSettings.TrimRelativeError;
	}

	void OnTrimRelativeErrorChanged(float NewValue)
	{
		NaniteSettings.TrimRelativeError = NewValue;
	}

	float GetFallbackPercentTriangles() const
	{
		return NaniteSettings.FallbackPercentTriangles * 100.0f; // Display fraction as percentage.
	}

	void OnFallbackPercentTrianglesChanged(float NewValue)
	{
		// Percentage -> fraction.
		NaniteSettings.FallbackPercentTriangles = NewValue * 0.01f;
	}

	void OnFallbackPercentTrianglesCommitted(float NewValue, ETextCommit::Type TextCommitType)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.NaniteSettings"), TEXT("FallbackPercentTriangles"), FString::Printf(TEXT("%.1f"), NewValue));
		}
		OnFallbackPercentTrianglesChanged(NewValue);
	}

	float GetFallbackRelativeError() const
	{
		return NaniteSettings.FallbackRelativeError;
	}

	void OnFallbackRelativeErrorChanged(float NewValue)
	{
		NaniteSettings.FallbackRelativeError = NewValue;
	}

	int32 GetDisplacementUVChannel() const
	{
		return NaniteSettings.DisplacementUVChannel;
	}

	void OnDisplacementUVChannelChanged(int32 NewValue)
	{
		NaniteSettings.DisplacementUVChannel = NewValue;
	}

	FString GetHiResSourceFilename() const
	{
		if constexpr (bSupportsHighRes)
		{
			if (const TMeshType* MeshAsset = GetMesh())
			{
				return MeshAsset->GetHiResSourceModel().SourceImportFilename;
			}
		}

		return FString();
	}

	void SetHiResSourceFilename(const FString& NewSourceFile)
	{
		if constexpr (bSupportsHighRes)
		{
			//Reimport with new file
			TMeshType* MeshAsset = GetMesh();
			if (!MeshAsset)
			{
				return;
			}

			if (MeshAsset->GetHiResSourceModel().SourceImportFilename.Equals(NewSourceFile))
			{
				return;
			}

			MeshAsset->GetHiResSourceModel().SourceImportFilename = NewSourceFile;
			
			// Trigger a reimport with new file
			FbxMeshUtils::ImportStaticMeshHiResSourceModelDialog(MeshAsset);

			RefreshTool();
		}
	}

	bool DoesHiResDataExists() const
	{
		if constexpr (bSupportsHighRes)
		{
			const TMeshType* MeshAsset = GetMesh();
			if (MeshAsset)
			{
				return (MeshAsset->GetHiResMeshDescription() != nullptr);
			}
		}

		return false;
	}

	bool IsHiResDataEmpty() const
	{
		return !DoesHiResDataExists();
	}
	
	FReply OnImportHiRes()
	{
		if constexpr (bSupportsHighRes)
		{
			if (TMeshType* MeshAsset = GetMesh())
			{
				MeshAsset->GetHiResSourceModel().SourceImportFilename = FString();
				FbxMeshUtils::ImportStaticMeshHiResSourceModelDialog(MeshAsset);
				
				//If we import a hires we should enable Nanite
				NaniteSettings.bEnabled = true;
				
				ApplyChanges();
			}
		}

		return FReply::Handled();
	}

	FReply OnRemoveHiRes()
	{
		if constexpr (bSupportsHighRes)
		{
			if (TMeshType* MeshAsset = GetMesh())
			{
				MeshAsset->GetHiResSourceModel().SourceImportFilename = FString();
				FbxMeshUtils::RemoveStaticMeshHiRes(MeshAsset);
				RefreshTool();
			}
		}

		return FReply::Handled();
	}

	FReply OnReimportHiRes()
	{
		if constexpr (bSupportsHighRes)
		{
			if (TMeshType* MeshAsset = GetMesh())
			{
				FbxMeshUtils::ImportStaticMeshHiResSourceModelDialog(MeshAsset);
				RefreshTool();
			}
		}

		return FReply::Handled();
	}

	FReply OnReimportHiResWithNewFile()
	{
		if constexpr (bSupportsHighRes)
		{
			if (TMeshType* MeshAsset = GetMesh())
			{
				MeshAsset->GetHiResSourceModel().SourceImportFilename = FString();
				FbxMeshUtils::ImportStaticMeshHiResSourceModelDialog(MeshAsset);
				RefreshTool();
			}
		}

		return FReply::Handled();
	}

public:
	void AddToDetailsPanel(
		TWeakObjectPtr<TMeshType> WeakMeshPtr,
		IDetailLayoutBuilder& DetailBuilder,
		int32 SortOrder,
		bool bInitiallyCollapsed
	)
	{
		typedef FSettingsLayout<TMeshType, bSupportsForceEnable, bSupportsHighRes> TSettingsType;

		const FText NaniteCategoryName = LOCTEXT("NaniteSettingsCategory", "Nanite Settings");

		IDetailCategoryBuilder& NaniteSettingsCategory = DetailBuilder.EditCategory("NaniteSettings", NaniteCategoryName, ECategoryPriority::Uncommon);
		NaniteSettingsCategory.SetSortOrder(SortOrder);
		NaniteSettingsCategory.InitiallyCollapsed(bInitiallyCollapsed);

		auto CategoryContentLambda = [WeakMeshPtr]()
		{
			TMeshType* MeshAsset = WeakMeshPtr.Get();
			if (MeshAsset && MeshAsset->IsNaniteEnabled())
			{
				if constexpr (bSupportsHighRes)
				{
					if (!MeshAsset->GetHiResSourceModel().SourceImportFilename.IsEmpty())
					{
						return LOCTEXT("NaniteSettingsCategory_Imported", "[Imported]");
					}
				}
			}

			return FText::GetEmpty();
		};

		auto CategoryContentTooltipLambda = [WeakMeshPtr]()
		{
			TMeshType* MeshAsset = WeakMeshPtr.Get();
			if (MeshAsset && MeshAsset->IsNaniteEnabled())
			{
				if constexpr (bSupportsHighRes)
				{
					if (!MeshAsset->GetHiResSourceModel().SourceImportFilename.IsEmpty())
					{
						return FText::Format(LOCTEXT("NaniteSettingsCategory_ImportedTooltip", "The Nanite high resolution data is imported from file {0}"), FText::FromString(MeshAsset->GetHiResSourceModel().SourceImportFilename));
					}
				}
			}

			return FText::GetEmpty();
		};

		NaniteSettingsCategory.HeaderContent
		(
			SNew( SHorizontalBox )
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.Padding(FMargin(5.0f, 0.0f))
				[
					SNew(STextBlock)
					.Text_Lambda(CategoryContentLambda)
					.ToolTipText_Lambda(CategoryContentTooltipLambda)
					.Font(IDetailLayoutBuilder::GetDetailFontItalic())
				]
			]
		);

		TSharedPtr<SCheckBox> NaniteEnabledCheck;
		{
			NaniteSettingsCategory.AddCustomRow( LOCTEXT("Enabled", "Enabled") )
			.RowTag("EnabledNaniteSupport")
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("EnabledNaniteSupport", "Enable Nanite Support"))
			]
			.ValueContent()
			[
				SAssignNew(NaniteEnabledCheck, SCheckBox)
				.IsChecked(this, &TSettingsType::IsEnabledChecked)
				.OnCheckStateChanged(this, &TSettingsType::OnEnabledChanged)
			];
		}

		TAttribute<bool> NaniteEnabledAttr = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([NaniteEnabledCheck]() -> bool { return NaniteEnabledCheck->IsChecked(); }));
		TAttribute<bool> NaniteEnabledAndNoHiResDataAttr = TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateLambda([this, NaniteEnabledCheck]() -> bool {return NaniteEnabledCheck->IsChecked() && IsHiResDataEmpty(); }));

		NANITE_ADD_DEFAULT_ROW( bPreserveArea )
		.IsEnabled( NaniteEnabledAttr );

		NANITE_ADD_DEFAULT_ROW( bExplicitTangents )
		.IsEnabled( NaniteEnabledAttr );

		NANITE_ADD_DEFAULT_ROW( bLerpUVs )
		.IsEnabled( NaniteEnabledAttr );

		{
			TSharedPtr<STextComboBox> ComboBox;
			NaniteSettingsCategory.AddCustomRow(LOCTEXT("PositionPrecision", "Position Precision"))
			.RowTag("PositionPrecision")
			.IsEnabled( NaniteEnabledAttr )
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("PositionPrecision", "Position Precision"))
				.ToolTipText(LOCTEXT("PositionPrecisionTooltip", "Precision of vertex positions."))
			]
			.ValueContent()
			.VAlign(VAlign_Center)
			[
				SAssignNew(ComboBox, STextComboBox)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.OptionsSource(&PositionPrecisionOptions)
				.InitiallySelectedItem(PositionPrecisionOptions[PositionPrecisionValueToIndex(NaniteSettings.PositionPrecision)])
				.OnSelectionChanged(this, &TSettingsType::OnPositionPrecisionChanged)
			];
		}

		{
			TSharedPtr<STextComboBox> ComboBox;
			NaniteSettingsCategory.AddCustomRow(LOCTEXT("NormalPrecision", "Normal Precision"))
			.IsEnabled( NaniteEnabledAttr )
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("NormalPrecision", "Normal Precision"))
				.ToolTipText(LOCTEXT("NormalPrecisionTooltip", "Precision of vertex normals."))
			]
			.ValueContent()
			.VAlign(VAlign_Center)
			[
				SAssignNew(ComboBox, STextComboBox)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.OptionsSource(&NormalPrecisionOptions)
				.InitiallySelectedItem(NormalPrecisionOptions[NormalPrecisionValueToIndex(NaniteSettings.NormalPrecision)])
				.OnSelectionChanged(this, &TSettingsType::OnNormalPrecisionChanged)
			];
		}

		{
			TSharedPtr<STextComboBox> ComboBox;
			NaniteSettingsCategory.AddCustomRow(LOCTEXT("TangentPrecision", "Tangent Precision"))
			.IsEnabled( NaniteEnabledAttr )
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("TangentPrecision", "Tangent Precision"))
				.ToolTipText(LOCTEXT("TangentPrecisionTooltip", "Precision of vertex tangents."))
			]
			.ValueContent()
			.VAlign(VAlign_Center)
			[
				SAssignNew(ComboBox, STextComboBox)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.OptionsSource(&TangentPrecisionOptions)
				.InitiallySelectedItem(TangentPrecisionOptions[TangentPrecisionValueToIndex(NaniteSettings.TangentPrecision)])
				.OnSelectionChanged(this, &TSettingsType::OnTangentPrecisionChanged)
			]
			.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda([this]()
				{
					return NaniteSettings.bExplicitTangents ? EVisibility::Visible : EVisibility::Hidden;
				})));
		}

		{
			TSharedPtr<STextComboBox> ComboBox;
			NaniteSettingsCategory.AddCustomRow(LOCTEXT("MinimumResidency", "Minimum Residency"))
			.RowTag("MinimumResidency")
			.IsEnabled( NaniteEnabledAttr )
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("MinimumResidencyRootGeometry", "Minimum Residency (Root Geometry)"))
				.ToolTipText(LOCTEXT("ResidencyTooltip", "How much should always be in memory. The rest will be streamed. Higher values require more memory, but also mitigate streaming pop-in issues."))
			]
			.ValueContent()
			[
				SAssignNew(ComboBox, STextComboBox)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.OptionsSource(&ResidencyOptions)
				.InitiallySelectedItem(ResidencyOptions[MinimumResidencyValueToIndex(NaniteSettings.TargetMinimumResidencyInKB)])
				.OnSelectionChanged(this, &TSettingsType::OnResidencyChanged)
			];
		}

		{
			NaniteSettingsCategory.AddCustomRow( LOCTEXT("KeepTrianglePercent", "Keep Triangle Percent") )
			.RowTag("KeepTrianglePercent")
			.IsEnabled( NaniteEnabledAttr )
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("KeepTrianglePercent", "Keep Triangle Percent"))
				.ToolTipText(LOCTEXT("KeepTrianglePercentTooltip", "Percentage of triangles to keep. Reduce to optimize for disk size."))
			]
			.ValueContent()
			.VAlign(VAlign_Center)
			[
				SNew(SSpinBox<float>)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.MinValue(0.0f)
				.MaxValue(100.0f)
				.Value(this, &TSettingsType::GetKeepPercentTriangles)
				.OnValueChanged(this, &TSettingsType::OnKeepPercentTrianglesChanged)
				.OnValueCommitted(this, &TSettingsType::OnKeepPercentTrianglesCommitted)
			];
		}

		{
			NaniteSettingsCategory.AddCustomRow( LOCTEXT("TrimRelativeError", "Trim Relative Error") )
			.RowTag("TrimRelativeError")
			.IsEnabled( NaniteEnabledAttr )
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("TrimRelativeError", "Trim Relative Error"))
				.ToolTipText(LOCTEXT("TrimRelativeErrorTooltip", "Trim all detail with less than this relative error. Error is calculated relative to the mesh's size.\nIncrease to optimize for disk size."))
			]
			.ValueContent()
			.VAlign(VAlign_Center)
			[
				SNew(SSpinBox<float>)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.MinValue(0.0f)
				.Value(this, &TSettingsType::GetTrimRelativeError)
				.OnValueChanged(this, &TSettingsType::OnTrimRelativeErrorChanged)
			];
		}

		NANITE_ADD_DEFAULT_ROW( FallbackTarget )
		.IsEnabled( NaniteEnabledAndNoHiResDataAttr );
		{
			NaniteSettingsCategory.AddCustomRow( LOCTEXT("FallbackTrianglePercent", "Fallback Triangle Percent") )
			.RowTag("FallbackTrianglePercent")
			.IsEnabled( NaniteEnabledAndNoHiResDataAttr )
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("FallbackTrianglePercent", "Fallback Triangle Percent"))
				.ToolTipText(LOCTEXT("FallbackTrianglePercentTooltip", "Reduce until no more than this percentage of triangles remain when generating a fallback\nmesh that will be used anywhere the full detail Nanite data can't,\nincluding platforms that don't support Nanite rendering."))
			]
			.ValueContent()
			.VAlign(VAlign_Center)
			[
				SNew(SSpinBox<float>)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.MinValue(0.0f)
				.MaxValue(100.0f)
				.Value(this, &TSettingsType::GetFallbackPercentTriangles)
				.OnValueChanged(this, &TSettingsType::OnFallbackPercentTrianglesChanged)
				.OnValueCommitted(this, &TSettingsType::OnFallbackPercentTrianglesCommitted)
			]
			.Visibility(TAttribute<EVisibility>::Create( TAttribute<EVisibility>::FGetter::CreateLambda([this]()
			{
				return NaniteSettings.FallbackTarget == ENaniteFallbackTarget::PercentTriangles ? EVisibility::Visible : EVisibility::Hidden;
			} )));
		}

		{
			NaniteSettingsCategory.AddCustomRow( LOCTEXT("FallbackRelativeError", "Fallback Relative Error") )
			.RowTag("FallbackRelativeError")
			.IsEnabled( NaniteEnabledAndNoHiResDataAttr )
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("FallbackRelativeError", "Fallback Relative Error"))
				.ToolTipText(LOCTEXT("FallbackRelativeErrorTooltip", "Reduce until at least this amount of error is reached relative to its size\nwhen generating a fallback mesh that will be used anywhere the full detail Nanite data can't,\nincluding platforms that don't support Nanite rendering."))
			]
			.ValueContent()
			.VAlign(VAlign_Center)
			[
				SNew(SSpinBox<float>)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.MinValue(0.0f)
				.Value(this, &TSettingsType::GetFallbackRelativeError)
				.OnValueChanged(this, &TSettingsType::OnFallbackRelativeErrorChanged)
			]
			.Visibility(TAttribute<EVisibility>::Create( TAttribute<EVisibility>::FGetter::CreateLambda([this]()
				{
					return NaniteSettings.FallbackTarget == ENaniteFallbackTarget::RelativeError ? EVisibility::Visible : EVisibility::Hidden;
				} )));
		}

		//Source import filename
		{
			FString FileFilterText = TEXT("Filmbox (*.fbx)|*.fbx|All files (*.*)|*.*");
			NaniteSettingsCategory.AddCustomRow( LOCTEXT("NANITE_SourceImportFilename", "Source Import Filename") )
			.RowTag("NANITE_SourceImportFilename")
			.IsEnabled( NaniteEnabledAttr )
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("NANITE_SourceImportFilename", "Source Import Filename"))
				.ToolTipText(LOCTEXT("NANITE_SourceImportFilenameTooltip", "The file path that was used to import this hi res nanite mesh."))
			]
			.ValueContent()
			.VAlign(VAlign_Center)
			[
				SNew(SFilePathPicker)
				.BrowseButtonImage(FAppStyle::GetBrush("PropertyWindow.Button_Ellipsis"))
				.BrowseButtonStyle(FAppStyle::Get(), "HoverHintOnly")
				.BrowseButtonToolTip(LOCTEXT("NaniteSourceImportFilenamePathLabel_Tooltip", "Choose a nanite hi res source import file"))
				.BrowseDirectory(FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_OPEN))
				.BrowseTitle(LOCTEXT("NaniteSourceImportFilenameBrowseTitle", "Nanite hi res source import file picker..."))
				.FilePath(this, &TSettingsType::GetHiResSourceFilename)
				.FileTypeFilter(FileFilterText)
				.OnPathPicked(this, &TSettingsType::SetHiResSourceFilename)
			];
		}

		{
			NaniteSettingsCategory.AddCustomRow( LOCTEXT("DisplacementUVChannel", "Displacement UV Channel") )
			.IsEnabled( NaniteEnabledAttr )
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("DisplacementUVChannel", "Displacement UV Channel"))
				.ToolTipText(LOCTEXT("DisplacementUVChannelTooltip", "UV channel to use when sampling displacement maps."))
			]
			.ValueContent()
			.VAlign(VAlign_Center)
			[
				SNew(SSpinBox<int32>)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.MinValue(0)
				.MaxValue(4)
				.Value(this, &TSettingsType::GetDisplacementUVChannel)
				.OnValueChanged(this, &TSettingsType::OnDisplacementUVChannelChanged)
			];
		}

		NANITE_ADD_DEFAULT_ROW( DisplacementMaps )
		.IsEnabled( NaniteEnabledAttr );

		NANITE_ADD_DEFAULT_ROW( MaxEdgeLengthFactor )
		.IsEnabled( NaniteEnabledAttr );

		//Nanite import button
		{
			NaniteSettingsCategory.AddCustomRow(LOCTEXT("NaniteHiResButtons", "Nanite Hi Res buttons"))
			.RowTag("NaniteHiResButtons")
			.ValueContent()
			.HAlign(HAlign_Fill)
			[
				SNew(SUniformWrapPanel)
				+ SUniformWrapPanel::Slot() // Nanite apply changes
				[
					SNew(SButton)
					.OnClicked(this, &TSettingsType::OnApply)
					.IsEnabled(this, &TSettingsType::IsApplyNeeded)
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ApplyChanges", "Apply Changes"))
						.Font(DetailBuilder.GetDetailFont())
					]
				]
				+ SUniformWrapPanel::Slot() // Nanite import button
				[
					SNew(SButton)
					.OnClicked(this, &TSettingsType::OnImportHiRes)
					.IsEnabled(this, &TSettingsType::IsHiResDataEmpty)
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("NaniteImportHiRes", "Import"))
						.Font(DetailBuilder.GetDetailFont())
					]
				]
				+ SUniformWrapPanel::Slot() // Nanite remove button
				[
					SNew(SButton)
					.OnClicked(this, &TSettingsType::OnRemoveHiRes)
					.IsEnabled(this, &TSettingsType::DoesHiResDataExists)
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("NaniteRemoveHiRes", "Remove"))
						.Font(DetailBuilder.GetDetailFont())
					]
				]
				+ SUniformWrapPanel::Slot() // Nanite reimport button
				[
					SNew(SButton)
					.OnClicked(this, &TSettingsType::OnReimportHiRes)
					.IsEnabled(this, &TSettingsType::DoesHiResDataExists)
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("NaniteReimportHiRes", "Reimport"))
						.Font(DetailBuilder.GetDetailFont())
					]
				]
				+ SUniformWrapPanel::Slot() // Nanite reimport with new file button
				[
					SNew(SButton)
					.OnClicked(this, &TSettingsType::OnReimportHiResWithNewFile)
					.IsEnabled(this, &TSettingsType::DoesHiResDataExists)
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("NaniteReimportHiResWithNewFile", "Reimport New File"))
						.Font(DetailBuilder.GetDetailFont())
					]
				]
			];
		}
	}

	inline MeshType* GetMesh() const
	{
		if (OnGetMesh.IsBound())
		{
			return OnGetMesh.Execute();
		}

		return nullptr;
	}

	inline void RefreshTool()
	{
		if (OnRefreshTool.IsBound())
		{
			OnRefreshTool.Execute();
		}
	}

	TDelegate<MeshType* ()> OnGetMesh;
	TDelegate<void()> OnRefreshTool;

protected:
	TArray<TSharedPtr<FString>> PositionPrecisionOptions;
	TArray<TSharedPtr<FString>> NormalPrecisionOptions;
	TArray<TSharedPtr<FString>> TangentPrecisionOptions;
	TArray<TSharedPtr<FString>> ResidencyOptions;

	FMeshNaniteSettings NaniteSettings;
};

#undef LOCTEXT_NAMESPACE

}

#endif // WITH_EDITOR