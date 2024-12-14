// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowEditorPreviewSceneBase.h"
#include "AssetEditorModeManager.h"
#include "Dataflow/DataflowEditor.h"
#include "Components/PrimitiveComponent.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Selection.h"

#define LOCTEXT_NAMESPACE "FDataflowPreviewSceneBase"


bool bDataflowShowFloorDefault = true;
FAutoConsoleVariableRef CVARDataflowShowFloorDefault(TEXT("p.Dataflow.Editor.ShowFloor"), bDataflowShowFloorDefault, TEXT("Show the floor in the dataflow editor[def:false]"));

bool bDataflowShowEnvironmentDefault = true;
FAutoConsoleVariableRef CVARDataflowShowEnvironmentDefault(TEXT("p.Dataflow.Editor.ShowEnvironment"), bDataflowShowEnvironmentDefault, TEXT("Show the environment in the dataflow editor[def:false]"));

FDataflowPreviewSceneBase::FDataflowPreviewSceneBase(FPreviewScene::ConstructionValues ConstructionValues, UDataflowEditor* InEditor)
	: FAdvancedPreviewScene(ConstructionValues)
	, DataflowEditor(InEditor)
{
	RootSceneActor = GetWorld()->SpawnActor<AActor>(AActor::StaticClass());
	
	check(DataflowEditor);
	
	SetFloorVisibility(bDataflowShowFloorDefault, false);
	SetEnvironmentVisibility(bDataflowShowEnvironmentDefault, false);
}

FDataflowPreviewSceneBase::~FDataflowPreviewSceneBase()
{}

TObjectPtr<UDataflowBaseContent>& FDataflowPreviewSceneBase::GetEditorContent() 
{ 
	return DataflowEditor->GetEditorContent();
}

const TObjectPtr<UDataflowBaseContent>& FDataflowPreviewSceneBase::GetEditorContent() const 
{ 
	return DataflowEditor->GetEditorContent();
}

TArray<TObjectPtr<UDataflowBaseContent>>& FDataflowPreviewSceneBase::GetTerminalContents() 
{ 
	return DataflowEditor->GetTerminalContents();
}

const TArray<TObjectPtr<UDataflowBaseContent>>& FDataflowPreviewSceneBase::GetTerminalContents() const 
{ 
	return DataflowEditor->GetTerminalContents();
}

void FDataflowPreviewSceneBase::AddReferencedObjects(FReferenceCollector& Collector)
{
	FAdvancedPreviewScene::AddReferencedObjects(Collector);
	if (const TObjectPtr<UDataflowBaseContent> EditorContent = GetEditorContent())
	{
		EditorContent->AddContentObjects(Collector);
	}
}

bool FDataflowPreviewSceneBase::IsComponentSelected(const UPrimitiveComponent* InComponent) const
{
	if(DataflowModeManager.IsValid())
	{
		if (const UTypedElementSelectionSet* const TypedElementSelectionSet = DataflowModeManager->GetEditorSelectionSet())
		{
			if (const FTypedElementHandle ComponentElement = UEngineElementsLibrary::AcquireEditorComponentElementHandle(InComponent))
			{
				const bool bElementSelected = TypedElementSelectionSet->IsElementSelected(ComponentElement, FTypedElementIsSelectedOptions());
				return bElementSelected;
			}
		}
	}
	return false;
}

FBox FDataflowPreviewSceneBase::GetBoundingBox() const
{
	FBox SceneBounds(ForceInitToZero);
	if(DataflowModeManager.IsValid())
	{
		USelection* const SelectedComponents = DataflowModeManager->GetSelectedComponents();

		TArray<TWeakObjectPtr<UObject>> SelectedObjects;
		const int32 NumSelected = SelectedComponents->GetSelectedObjects(SelectedObjects);
		
		if(NumSelected > 0)
		{
			for(const TWeakObjectPtr<UObject> SelectedObject : SelectedObjects)
			{
				if(const UPrimitiveComponent* SelectedComponent = Cast<UPrimitiveComponent>(SelectedObject))
				{
					SceneBounds += SelectedComponent->Bounds.GetBox();
				}
			}
		}
		else
		{
			SceneBounds += RootSceneActor->GetComponentsBoundingBox(true);
		}
	}
	return SceneBounds;
}


#undef LOCTEXT_NAMESPACE

