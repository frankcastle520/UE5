// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/PropertyGenerators/DMTextureUVPropertyRowGenerator.h"

#include "Components/DMMaterialComponent.h"
#include "Components/DMMaterialStage.h"
#include "Components/DMTextureUV.h"
#include "DMEDefs.h"
#include "DynamicMaterialEditorModule.h"
#include "Model/DynamicMaterialModelDynamic.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "UI/Utils/DMWidgetStatics.h"
#include "UI/Widgets/Editor/SDMMaterialComponentEditor.h"
#include "UI/Widgets/SDMMaterialEditor.h"
#include "UI/Widgets/Visualizers/SDMTextureUVVisualizerProperty.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "DMTextureUVPropertyRowGenerator"

const TSharedRef<FDMTextureUVPropertyRowGenerator>& FDMTextureUVPropertyRowGenerator::Get()
{
	static TSharedRef<FDMTextureUVPropertyRowGenerator> Generator = MakeShared<FDMTextureUVPropertyRowGenerator>();
	return Generator;
}

namespace UE::DynamicMaterialEditor::Private
{
	void AddTextureUVPropertyRow(const TSharedRef<SWidget>& InComponentEditorWidget, UDMTextureUV* InTextureUV, FName InProperty, 
		TArray<FDMPropertyHandle>& InOutPropertyRows);

	void AddTextureUVVisualizerRow(const TSharedRef<SWidget>& InComponentEditorWidget, UDMTextureUV* InTextureUV, 
		TArray<FDMPropertyHandle>& InOutPropertyRows);

	bool CanResetTextureUVPropertyToDefault(TSharedPtr<IPropertyHandle> InPropertyHandle);

	void ResetTextureUVPropertyToDefault(TSharedPtr<IPropertyHandle> InPropertyHandle);
}

void FDMTextureUVPropertyRowGenerator::AddComponentProperties(const TSharedRef<SDMMaterialComponentEditor>& InComponentEditorWidget, 
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

	UDMTextureUV* TextureUV = Cast<UDMTextureUV>(InComponent);

	if (!TextureUV)
	{
		return;
	}

	InOutProcessedObjects.Add(InComponent);

	if (TSharedPtr<SDMMaterialEditor> EditorWidget = InComponentEditorWidget->GetEditorWidget())
	{
		if (UDynamicMaterialModelBase* MaterialModelBase = EditorWidget->GetMaterialModelBase())
		{
			if (UDynamicMaterialModelDynamic* MaterialModelDynamic = Cast<UDynamicMaterialModelDynamic>(MaterialModelBase))
			{
				if (UDMMaterialComponentDynamic* ComponentDynamic = MaterialModelDynamic->GetComponentDynamic(TextureUV->GetFName()))
				{
					FDynamicMaterialEditorModule::Get().GeneratorComponentPropertyRows(InComponentEditorWidget, ComponentDynamic, InOutPropertyRows, InOutProcessedObjects);
				}

				return;
			}
		}
	}

	using namespace UE::DynamicMaterialEditor::Private;

	AddTextureUVPropertyRow(InComponentEditorWidget, TextureUV, UDMTextureUV::NAME_Offset, InOutPropertyRows);
	AddTextureUVPropertyRow(InComponentEditorWidget, TextureUV, UDMTextureUV::NAME_Rotation, InOutPropertyRows);
	AddTextureUVPropertyRow(InComponentEditorWidget, TextureUV, UDMTextureUV::NAME_Tiling, InOutPropertyRows);
	AddTextureUVPropertyRow(InComponentEditorWidget, TextureUV, UDMTextureUV::NAME_Pivot, InOutPropertyRows);
	AddTextureUVPropertyRow(InComponentEditorWidget, TextureUV, UDMTextureUV::NAME_bMirrorOnX, InOutPropertyRows);
	AddTextureUVPropertyRow(InComponentEditorWidget, TextureUV, UDMTextureUV::NAME_bMirrorOnY, InOutPropertyRows);
	AddTextureUVVisualizerRow(InComponentEditorWidget, TextureUV, InOutPropertyRows);
}

void FDMTextureUVPropertyRowGenerator::AddPopoutComponentProperties(const TSharedRef<SWidget>& InParentWidget, UDMMaterialComponent* InComponent, 
	TArray<FDMPropertyHandle>& InOutPropertyRows)
{
	if (!IsValid(InComponent))
	{
		return;
	}

	UDMTextureUV* TextureUV = Cast<UDMTextureUV>(InComponent);

	if (!TextureUV)
	{
		return;
	}

	using namespace UE::DynamicMaterialEditor::Private;

	AddTextureUVPropertyRow(InParentWidget, TextureUV, UDMTextureUV::NAME_Offset, InOutPropertyRows);
	AddTextureUVPropertyRow(InParentWidget, TextureUV, UDMTextureUV::NAME_Rotation, InOutPropertyRows);
	AddTextureUVPropertyRow(InParentWidget, TextureUV, UDMTextureUV::NAME_Tiling, InOutPropertyRows);
	AddTextureUVPropertyRow(InParentWidget, TextureUV, UDMTextureUV::NAME_Pivot, InOutPropertyRows);
	AddTextureUVPropertyRow(InParentWidget, TextureUV, UDMTextureUV::NAME_bMirrorOnX, InOutPropertyRows);
	AddTextureUVPropertyRow(InParentWidget, TextureUV, UDMTextureUV::NAME_bMirrorOnY, InOutPropertyRows);
}

bool FDMTextureUVPropertyRowGenerator::AllowKeyframeButton(UDMMaterialComponent* InComponent, FProperty* InProperty)
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

void UE::DynamicMaterialEditor::Private::AddTextureUVPropertyRow(const TSharedRef<SWidget>& InComponentEditorWidget, UDMTextureUV* InTextureUV, 
	FName InProperty, TArray<FDMPropertyHandle>& InOutPropertyRows)
{
	FDMPropertyHandle& NewHandle = InOutPropertyRows.Add_GetRef(FDMWidgetStatics::Get().GetPropertyHandle(&*InComponentEditorWidget, InTextureUV, InProperty));

	NewHandle.ResetToDefaultOverride = FResetToDefaultOverride::Create(
		FIsResetToDefaultVisible::CreateStatic(&UE::DynamicMaterialEditor::Private::CanResetTextureUVPropertyToDefault),
		FResetToDefaultHandler::CreateStatic(&UE::DynamicMaterialEditor::Private::ResetTextureUVPropertyToDefault)
	);

	NewHandle.bEnabled = true;
}

void UE::DynamicMaterialEditor::Private::AddTextureUVVisualizerRow(const TSharedRef<SWidget>& InComponentEditorWidget, UDMTextureUV* InTextureUV, 
	TArray<FDMPropertyHandle>& InOutPropertyRows)
{
	// Make sure we don't get a substage
	UDMMaterialStage* Stage = InTextureUV->GetTypedParent<UDMMaterialStage>(/* Allow Subclasses */ false);

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
	VisualizerHandle.ValueName = FName(*InTextureUV->GetComponentPath());
	VisualizerHandle.ValueWidget = SNew(SDMTextureUVVisualizerProperty, EditorWidget.ToSharedRef(), Stage).TextureUV(InTextureUV);
	VisualizerHandle.CategoryOverrideName = TEXT("Texture UV");
	VisualizerHandle.bEnabled = true;
	InOutPropertyRows.Add(VisualizerHandle);
}


bool UE::DynamicMaterialEditor::Private::CanResetTextureUVPropertyToDefault(TSharedPtr<IPropertyHandle> InPropertyHandle)
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

	const UDMTextureUV* PropertyObject = Cast<UDMTextureUV>(Outers[0]);

	if (!PropertyObject)
	{
		return false;
	}

	const UDMTextureUV* DefaultObject = GetDefault<UDMTextureUV>();

	if (!DefaultObject)
	{
		return false;
	}

	if (PropertyName == UDMTextureUV::NAME_UVSource)
	{
		return DefaultObject->GetUVSource() != PropertyObject->GetUVSource();
	}

	if (PropertyName == UDMTextureUV::NAME_bMirrorOnX)
	{
		return DefaultObject->GetMirrorOnX() != PropertyObject->GetMirrorOnX();
	}

	if (PropertyName == UDMTextureUV::NAME_bMirrorOnY)
	{
		return DefaultObject->GetMirrorOnY() != PropertyObject->GetMirrorOnY();
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

void UE::DynamicMaterialEditor::Private::ResetTextureUVPropertyToDefault(TSharedPtr<IPropertyHandle> InPropertyHandle)
{
	InPropertyHandle->ResetToDefault();
}

#undef LOCTEXT_NAMESPACE
