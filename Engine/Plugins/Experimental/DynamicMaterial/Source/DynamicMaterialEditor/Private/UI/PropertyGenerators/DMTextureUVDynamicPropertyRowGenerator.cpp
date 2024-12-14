// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/PropertyGenerators/DMTextureUVDynamicPropertyRowGenerator.h"

#include "Components/DMMaterialComponent.h"
#include "Components/DMMaterialStage.h"
#include "Components/DMTextureUV.h"
#include "Components/DMTextureUVDynamic.h"
#include "DMEDefs.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "UI/Utils/DMWidgetStatics.h"
#include "UI/Widgets/Editor/SDMMaterialComponentEditor.h"
#include "UI/Widgets/SDMMaterialEditor.h"
#include "UI/Widgets/Visualizers/SDMTextureUVVisualizerProperty.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "DMTextureUVDynamicPropertyRowGenerator"

const TSharedRef<FDMTextureUVDynamicPropertyRowGenerator>& FDMTextureUVDynamicPropertyRowGenerator::Get()
{
	static TSharedRef<FDMTextureUVDynamicPropertyRowGenerator> Generator = MakeShared<FDMTextureUVDynamicPropertyRowGenerator>();
	return Generator;
}

namespace UE::DynamicMaterialEditor::Private
{
	void AddTextureUVDynamicPropertyRow(const TSharedRef<SWidget>& InComponentEditorWidget, UDMMaterialComponent* InTextureUVDynamic, 
		FName InProperty, bool bInEnabled, TArray<FDMPropertyHandle>& InOutPropertyRows);

	void AddTextureUVDynamicVisualizerRow(const TSharedRef<SWidget>& InComponentEditorWidget, UDMTextureUVDynamic* InTextureUVDynamic, 
		TArray<FDMPropertyHandle>& InOutPropertyRows);

	bool CanResetTextureUVDynamicDynamicPropertyToDefault(TSharedPtr<IPropertyHandle> InPropertyHandle);

	void ResetTextureUVDynamicDynamicPropertyToDefault(TSharedPtr<IPropertyHandle> InPropertyHandle);
}

void FDMTextureUVDynamicPropertyRowGenerator::AddComponentProperties(const TSharedRef<SDMMaterialComponentEditor>& InComponentEditorWidget, 
	UDMMaterialComponent* InComponent, TArray<FDMPropertyHandle>& InOutPropertyRows, TSet<UDMMaterialComponent*>& InOutProcessedObjects)
{
	if (!IsValid(InComponent))
	{
		return;
	}

	if (InOutProcessedObjects.Contains(InComponent))
	{
		return;
	}

	UDMTextureUVDynamic* TextureUVDynamic = Cast<UDMTextureUVDynamic>(InComponent);

	if (!TextureUVDynamic)
	{
		return;
	}

	UDMTextureUV* TextureUV = TextureUVDynamic->GetParentTextureUV();

	if (!TextureUV)
	{
		return;
	}

	InOutProcessedObjects.Add(InComponent);

	using namespace UE::DynamicMaterialEditor::Private;

	AddTextureUVDynamicPropertyRow(InComponentEditorWidget, TextureUVDynamic, UDMTextureUV::NAME_Offset, /* Enabled */ true, InOutPropertyRows);
	AddTextureUVDynamicPropertyRow(InComponentEditorWidget, TextureUVDynamic, UDMTextureUV::NAME_Rotation, /* Enabled */ true, InOutPropertyRows);
	AddTextureUVDynamicPropertyRow(InComponentEditorWidget, TextureUVDynamic, UDMTextureUV::NAME_Tiling, /* Enabled */ true, InOutPropertyRows);
	AddTextureUVDynamicPropertyRow(InComponentEditorWidget, TextureUVDynamic, UDMTextureUV::NAME_Pivot, /* Enabled */ true, InOutPropertyRows);
	AddTextureUVDynamicPropertyRow(InComponentEditorWidget, TextureUV, UDMTextureUV::NAME_bMirrorOnX, /* Enabled */ false, InOutPropertyRows);
	AddTextureUVDynamicPropertyRow(InComponentEditorWidget, TextureUV, UDMTextureUV::NAME_bMirrorOnY, /* Enabled */ false, InOutPropertyRows);
	AddTextureUVDynamicVisualizerRow(InComponentEditorWidget, TextureUVDynamic, InOutPropertyRows);
}

void FDMTextureUVDynamicPropertyRowGenerator::AddPopoutComponentProperties(const TSharedRef<SWidget>& InParentWidget, UDMMaterialComponent* InComponent, 
	TArray<FDMPropertyHandle>& InOutPropertyRows)
{
	if (!IsValid(InComponent))
	{
		return;
	}

	UDMTextureUVDynamic* TextureUVDynamic = Cast<UDMTextureUVDynamic>(InComponent);

	if (!TextureUVDynamic)
	{
		return;
	}

	UDMTextureUV* TextureUV = TextureUVDynamic->GetParentTextureUV();

	if (!TextureUV)
	{
		return;
	}

	using namespace UE::DynamicMaterialEditor::Private;

	AddTextureUVDynamicPropertyRow(InParentWidget, TextureUVDynamic, UDMTextureUV::NAME_Offset, /* Enabled */ true, InOutPropertyRows);
	AddTextureUVDynamicPropertyRow(InParentWidget, TextureUVDynamic, UDMTextureUV::NAME_Rotation, /* Enabled */ true, InOutPropertyRows);
	AddTextureUVDynamicPropertyRow(InParentWidget, TextureUVDynamic, UDMTextureUV::NAME_Tiling, /* Enabled */ true, InOutPropertyRows);
	AddTextureUVDynamicPropertyRow(InParentWidget, TextureUVDynamic, UDMTextureUV::NAME_Pivot, /* Enabled */ true, InOutPropertyRows);
	AddTextureUVDynamicPropertyRow(InParentWidget, TextureUV, UDMTextureUV::NAME_bMirrorOnX, /* Enabled */ false, InOutPropertyRows);
	AddTextureUVDynamicPropertyRow(InParentWidget, TextureUV, UDMTextureUV::NAME_bMirrorOnY, /* Enabled */ false, InOutPropertyRows);
}

bool FDMTextureUVDynamicPropertyRowGenerator::AllowKeyframeButton(UDMMaterialComponent* InComponent, FProperty* InProperty)
{
	if (InProperty)
	{
		const bool* AddKeyframeButtonPtr = UDMTextureUV::TextureProperties.Find(InProperty->GetFName());

		if (AddKeyframeButtonPtr)
		{
			return *AddKeyframeButtonPtr;
		}
	}

	return FDMComponentPropertyRowGenerator::AllowKeyframeButton(InComponent, InProperty);
}

void UE::DynamicMaterialEditor::Private::AddTextureUVDynamicPropertyRow(const TSharedRef<SWidget>& InComponentEditorWidget, 
	UDMMaterialComponent* InTextureUVDynamic, FName InProperty, bool bInEnabled, TArray<FDMPropertyHandle>& InOutPropertyRows)
{
	FDMPropertyHandle& NewHandle = InOutPropertyRows.Add_GetRef(FDMWidgetStatics::Get().GetPropertyHandle(&*InComponentEditorWidget, InTextureUVDynamic, InProperty));

	NewHandle.ResetToDefaultOverride = FResetToDefaultOverride::Create(
		FIsResetToDefaultVisible::CreateStatic(&UE::DynamicMaterialEditor::Private::CanResetTextureUVDynamicDynamicPropertyToDefault),
		FResetToDefaultHandler::CreateStatic(&UE::DynamicMaterialEditor::Private::ResetTextureUVDynamicDynamicPropertyToDefault)
	);

	NewHandle.bEnabled = bInEnabled;
}

void UE::DynamicMaterialEditor::Private::AddTextureUVDynamicVisualizerRow(const TSharedRef<SWidget>& InComponentEditorWidget, UDMTextureUVDynamic* InTextureUVDynamic,
	TArray<FDMPropertyHandle>& InOutPropertyRows)
{
	UDMTextureUV* TextureUV = InTextureUVDynamic->GetParentTextureUV();

	if (!TextureUV)
	{
		return;
	}

	// Make sure we don't get a substage
	UDMMaterialStage* Stage = TextureUV->GetTypedParent<UDMMaterialStage>(/* Allow Subclasses */ false);

	if (!Stage)
	{
		return;
	}

	if (InComponentEditorWidget->GetWidgetClass().GetWidgetType() != SDMMaterialComponentEditor::StaticWidgetClass().GetWidgetType())
	{
		return;
	}

	TSharedPtr<SDMMaterialEditor> EditorWidget = StaticCastSharedRef<SDMMaterialComponentEditor>(InComponentEditorWidget)->GetEditorWidget();

	if (!EditorWidget.IsValid())
	{
		return;
	}

	FDMPropertyHandle VisualizerHandle;
	VisualizerHandle.NameOverride = LOCTEXT("Visualizer", "UV Visualizer");
	VisualizerHandle.NameToolTipOverride = LOCTEXT("VisualizerToolTip", "A graphical Texture UV editor.\n\n- Offset Mode: Change the Texture UV offset.\n- Pivot Mode: Change the Texture UV pivot, rotation and tiling.\n\nControl+click to reset values to default.");
	VisualizerHandle.ValueName = FName(*InTextureUVDynamic->GetComponentPath());
	VisualizerHandle.ValueWidget = SNew(SDMTextureUVVisualizerProperty, EditorWidget.ToSharedRef(), Stage).TextureUVDynamic(InTextureUVDynamic);
	VisualizerHandle.CategoryOverrideName = TEXT("Texture UV");
	VisualizerHandle.bEnabled = true;
	InOutPropertyRows.Add(VisualizerHandle);
}


bool UE::DynamicMaterialEditor::Private::CanResetTextureUVDynamicDynamicPropertyToDefault(TSharedPtr<IPropertyHandle> InPropertyHandle)
{
	FProperty* Property = InPropertyHandle->GetProperty();

	if (!Property)
	{
		return false;
	}

	const FName PropertyName = Property->GetFName();

	if (PropertyName.IsNone())
	{
		return false;
	}

	TArray<UObject*> Outers;
	InPropertyHandle->GetOuterObjects(Outers);

	if (Outers.IsEmpty())
	{
		return false;
	}

	const UDMTextureUVDynamic* PropertyObject = Cast<UDMTextureUVDynamic>(Outers[0]);

	if (!PropertyObject)
	{
		return false;
	}

	const UDMTextureUVDynamic* DefaultObject = GetDefault<UDMTextureUVDynamic>();

	if (!DefaultObject)
	{
		return false;
	}

	if (PropertyName == UDMTextureUV::NAME_Offset)
	{
		return !DefaultObject->GetOffset().Equals(PropertyObject->GetOffset());
	}

	if (PropertyName == UDMTextureUV::NAME_Pivot)
	{
		return !DefaultObject->GetPivot().Equals(PropertyObject->GetPivot());
	}

	if (PropertyName == NAME_Rotation)
	{
		return DefaultObject->GetRotation() != PropertyObject->GetRotation();
	}

	if (PropertyName == UDMTextureUV::NAME_Tiling)
	{
		return !DefaultObject->GetTiling().Equals(PropertyObject->GetTiling());
	}

	return false;
}

void UE::DynamicMaterialEditor::Private::ResetTextureUVDynamicDynamicPropertyToDefault(TSharedPtr<IPropertyHandle> InPropertyHandle)
{
	InPropertyHandle->ResetToDefault();
}

#undef LOCTEXT_NAMESPACE
