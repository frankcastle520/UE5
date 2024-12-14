// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowConstructionScene.h"

#include "AssetEditorModeManager.h"
#include "Dataflow/CollectionRenderingPatternUtility.h"
#include "Dataflow/DataflowEditorCollectionComponent.h"
#include "Dataflow/DataflowEditor.h"
#include "Dataflow/DataflowObject.h"
#include "Dataflow/DataflowEditorStyle.h"
#include "Dataflow/DataflowRenderingFactory.h"
#include "Drawing/MeshElementsVisualizer.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Selection.h"
#include "AssetViewerSettings.h"

#define LOCTEXT_NAMESPACE "FDataflowConstructionScene"

//
// Construction Scene
//

FDataflowConstructionScene::FDataflowConstructionScene(FPreviewScene::ConstructionValues ConstructionValues, UDataflowEditor* InEditor)
	: FDataflowPreviewSceneBase(ConstructionValues, InEditor)
{}

FDataflowConstructionScene::~FDataflowConstructionScene()
{
	ResetWireframeMeshElementsVisualizer();
	ResetDynamicMeshComponents();
}

TArray<TObjectPtr<UDynamicMeshComponent>> FDataflowConstructionScene::GetDynamicMeshComponents() const
{
	TArray<TObjectPtr<UDynamicMeshComponent>> OutValues;
	DynamicMeshComponents.GenerateValueArray(OutValues);
	return MoveTemp(OutValues);
}

/** Hide all or a single component */
void FDataflowConstructionScene::SetVisibility(bool bVisibility, UActorComponent* InComponent)
{
	auto SetCollectionVisiblity = [](bool bVisibility,TObjectPtr<UDataflowEditorCollectionComponent> Component) {
		Component->SetVisibility(bVisibility);
		if (Component->WireframeComponent)
		{
			Component->WireframeComponent->SetVisibility(bVisibility);
		}
	};

	for (FRenderElement& RenderElement : DynamicMeshComponents)
	{
		if (TObjectPtr<UDataflowEditorCollectionComponent> DynamicMeshComponent = Cast<UDataflowEditorCollectionComponent>(RenderElement.Value))
		{
			if (InComponent != nullptr)
			{
				if (InComponent == DynamicMeshComponent.Get())
				{
					SetCollectionVisiblity(bVisibility,DynamicMeshComponent);
				}
			}
			else
			{
				SetCollectionVisiblity(bVisibility,DynamicMeshComponent);
			}
		}
	}
}


void FDataflowConstructionScene::AddReferencedObjects(FReferenceCollector& Collector)
{
	FDataflowPreviewSceneBase::AddReferencedObjects(Collector);

	Collector.AddReferencedObjects(DynamicMeshComponents);
	Collector.AddReferencedObjects(WireframeElements);
}

void FDataflowConstructionScene::TickDataflowScene(const float DeltaSeconds)
{
	if (const TObjectPtr<UDataflowBaseContent>& EditorContent = GetEditorContent())
	{
		if (const UDataflow* Dataflow = EditorContent->GetDataflowAsset())
		{
			if (Dataflow->GetDataflow())
			{
				UE::Dataflow::FTimestamp SystemTimestamp = UE::Dataflow::FTimestamp::Invalid;
				bool bMustUpdateConstructionScene = false;
				for (TObjectPtr<const UDataflowBaseContent> DataflowBaseContent : GetTerminalContents())
				{
					const FName DataflowTerminalName(DataflowBaseContent->GetDataflowTerminal());
					if (TSharedPtr<const FDataflowNode> DataflowTerminalNode = Dataflow->GetDataflow()->FindBaseNode(DataflowTerminalName))
					{
						SystemTimestamp = DataflowTerminalNode->GetTimestamp();
					}

					if (LastRenderedTimestamp < SystemTimestamp)
					{
						LastRenderedTimestamp = SystemTimestamp;
						bMustUpdateConstructionScene = true;
					}
				}
				if (bMustUpdateConstructionScene || EditorContent->IsConstructionDirty())
				{
					UpdateConstructionScene();
				}
			}
		}
	}
	for (TObjectPtr<UInteractiveToolPropertySet>& Propset : PropertyObjectsToTick)
	{
		if (Propset)
		{
			if (Propset->IsPropertySetEnabled())
			{
				Propset->CheckAndUpdateWatched();
			}
			else
			{
				Propset->SilentUpdateWatched();
			}
		}
	}

	for (FRenderWireElement Elem : WireframeElements)
	{
		Elem.Value->OnTick(DeltaSeconds);
	}
}

void FDataflowConstructionScene::UpdateDynamicMeshComponents()
{
	using namespace UE::Geometry;//FDynamicMesh3

	// The preview scene for the construction view will be
	// cleared and rebuilt from scratch. This will genrate a 
	// list of UPrimitiveComponents for rendering.
	ResetDynamicMeshComponents();

	if (const TObjectPtr<UDataflowBaseContent>& EditorContent = GetEditorContent())
	{
		const TObjectPtr<UDataflow>& DataflowAsset = EditorContent->GetDataflowAsset();
		const TSharedPtr<UE::Dataflow::FEngineContext>& DataflowContext = EditorContent->GetDataflowContext();
		if(DataflowAsset && DataflowContext)
		{
			for (TObjectPtr<const UDataflowEdNode> Target : DataflowAsset->GetRenderTargets())
			{
				if (Target)
				{
					TSharedPtr<FManagedArrayCollection> RenderCollection(new FManagedArrayCollection);
					GeometryCollection::Facades::FRenderingFacade Facade(*RenderCollection);
					Facade.DefineSchema();

					UE::Dataflow::RenderNodeOutput(Facade, *Target, *EditorContent);

					const int32 NumGeometry = Facade.NumGeometry();
					for (int32 MeshIndex = 0; MeshIndex < NumGeometry; ++MeshIndex)
					{
						FDynamicMesh3 DynamicMesh;
						UE::Dataflow::Conversion::RenderingFacadeToDynamicMesh(Facade, MeshIndex, DynamicMesh);

						if (DynamicMesh.VertexCount())
						{
							if (Target == EditorContent->GetSelectedNode())
							{
								EditorContent->SetRenderCollection(RenderCollection);
							}
							const FString MeshName = Facade.GetGeometryName()[MeshIndex];
							AddDynamicMeshComponent({Target, MeshIndex}, MeshName, MoveTemp(DynamicMesh), {});
						}
					}
				}
			}

			// Add hidden DynamicMeshComponents for any targets that we want to render in wireframe
			// 
			// Note: UMeshElementsVisualizers need source meshes to pull from. We add invisible dynamic mesh components to the existing DynamicMeshComponents collection
			// for this purpose, but could have instead created a separate collection of meshes for wireframe rendering. We are choosing to keep all the scene DynamicMeshComponents 
			// in one place and using separate structures to dictate how they are used (MeshComponentsForWireframeRendering in this case), in case visualization requirements 
			// change in the future.
			//

			MeshComponentsForWireframeRendering.Reset();
			for (TObjectPtr<const UDataflowEdNode> Target : DataflowAsset->GetWireframeRenderTargets())
			{
				if (Target)
				{
					TSharedPtr<FManagedArrayCollection> RenderCollection(new FManagedArrayCollection);
					GeometryCollection::Facades::FRenderingFacade Facade(*RenderCollection);
					Facade.DefineSchema();

					UE::Dataflow::RenderNodeOutput(Facade, *Target, *EditorContent);

					const int32 NumGeometry = Facade.NumGeometry();
					for (int32 MeshIndex = 0; MeshIndex < NumGeometry; ++MeshIndex)
					{
						FDataflowRenderKey WireframeDynamicMeshKey{ Target, MeshIndex };

						if (DynamicMeshComponents.Contains(WireframeDynamicMeshKey))
						{
							UDynamicMeshComponent* const ExistingMeshComponent = DynamicMeshComponents[WireframeDynamicMeshKey];
							MeshComponentsForWireframeRendering.Add(ExistingMeshComponent);
						}
						else
						{
							FDynamicMesh3 DynamicMesh;
							UE::Dataflow::Conversion::RenderingFacadeToDynamicMesh(Facade, MeshIndex, DynamicMesh);

							if (DynamicMesh.VertexCount())
							{
								if (Target == EditorContent->GetSelectedNode())
								{
									EditorContent->SetRenderCollection(RenderCollection);
								}
								const FString MeshName = Facade.GetGeometryName()[MeshIndex];
								const FString UniqueObjectName = MakeUniqueObjectName(RootSceneActor, UDataflowEditorCollectionComponent::StaticClass(), FName(MeshName)).ToString();
								UDynamicMeshComponent* const NewDynamicMeshComponent = AddDynamicMeshComponent(WireframeDynamicMeshKey, UniqueObjectName, MoveTemp(DynamicMesh), {});
								NewDynamicMeshComponent->SetVisibility(false);
								MeshComponentsForWireframeRendering.Add(NewDynamicMeshComponent);
							}
						}
					}
				}
			}

			// Hide the floor in orthographic view modes
			if (const UE::Dataflow::IDataflowConstructionViewMode* ConstructionViewMode = EditorContent->GetConstructionViewMode())
			{
				if (!ConstructionViewMode->IsPerspective())
				{
					constexpr bool bDontModifyProfile = true;
					SetFloorVisibility(false, bDontModifyProfile);
				}
				else
				{
					// Restore visibility from profile settings
					const int32 ProfileIndex = GetCurrentProfileIndex();
					if (DefaultSettings->Profiles.IsValidIndex(ProfileIndex))
					{
						const bool bProfileSetting = DefaultSettings->Profiles[CurrentProfileIndex].bShowFloor;
						constexpr bool bDontModifyProfile = true;
						SetFloorVisibility(bProfileSetting, bDontModifyProfile);
					}
				}
			}

		}
	}
}

void FDataflowConstructionScene::ResetDynamicMeshComponents()
{
	USelection* SelectedComponents = DataflowModeManager->GetSelectedComponents();
	for (FRenderElement RenderElement : DynamicMeshComponents)
	{
		TObjectPtr<UDynamicMeshComponent>& DynamicMeshComponent = RenderElement.Value;

		DynamicMeshComponent->SelectionOverrideDelegate.Unbind();
		if (SelectedComponents->IsSelected(DynamicMeshComponent))
		{
			SelectedComponents->Deselect(DynamicMeshComponent);
			DynamicMeshComponent->PushSelectionToProxy();
		}
		RemoveComponent(DynamicMeshComponent);
		DynamicMeshComponent->DestroyComponent();
	}
	DynamicMeshComponents.Reset();
}

TObjectPtr<UDynamicMeshComponent>& FDataflowConstructionScene::AddDynamicMeshComponent(FDataflowRenderKey InKey, const FString& MeshName, UE::Geometry::FDynamicMesh3&& DynamicMesh, const TArray<UMaterialInterface*>& MaterialSet)
{
	// Dont use the MakeUniqueObjectName for the component, we need to keep the name aligned with the collection so selection will work in 
	// other editors. 
	// const FName UniqueObjectName = MakeUniqueObjectName(RootSceneActor, UDataflowEditorCollectionComponent::StaticClass(), FName(MeshName));
	TObjectPtr<UDataflowEditorCollectionComponent> DynamicMeshComponent = NewObject<UDataflowEditorCollectionComponent>(RootSceneActor, FName(MeshName));

	DynamicMeshComponent->MeshIndex = InKey.Value;
	DynamicMeshComponent->Node = InKey.Key;
	DynamicMeshComponent->SetMesh(MoveTemp(DynamicMesh));
	
	// @todo(Material) This is just to have a material, we should transfer the materials from the assets if they have them. 
	const  TObjectPtr<UDataflowBaseContent>& EditorContent = GetEditorContent();
	if (EditorContent && EditorContent->GetDataflowAsset() && EditorContent->GetDataflowAsset()->Material)
	{
		DynamicMeshComponent->ConfigureMaterialSet({ EditorContent->GetDataflowAsset()->Material });
	}
	else
	{
		ensure(FDataflowEditorStyle::Get().DefaultTwoSidedMaterial);
		DynamicMeshComponent->SetOverrideRenderMaterial(FDataflowEditorStyle::Get().DefaultTwoSidedMaterial);
		DynamicMeshComponent->SetShadowsEnabled(false);
	}
	//else if (FDataflowEditorStyle::Get().DefaultMaterial)
	//{
	//	DynamicMeshComponent->ConfigureMaterialSet({ FDataflowEditorStyle::Get().DefaultMaterial });
	//}
	//else
	//{
	//	DynamicMeshComponent->ValidateMaterialSlots(true, false);
	//}

	DynamicMeshComponent->SelectionOverrideDelegate = UPrimitiveComponent::FSelectionOverride::CreateRaw(this, &FDataflowPreviewSceneBase::IsComponentSelected);
	DynamicMeshComponent->UpdateBounds();

	AddComponent(DynamicMeshComponent, DynamicMeshComponent->GetRelativeTransform());	
	DynamicMeshComponents.Emplace(InKey, DynamicMeshComponent);
	return DynamicMeshComponents[InKey];
}

void FDataflowConstructionScene::AddWireframeMeshElementsVisualizer()
{
	ensure(WireframeElements.Num() == 0);
	for (UDynamicMeshComponent* Elem : MeshComponentsForWireframeRendering)
	{
		if (TObjectPtr<UDataflowEditorCollectionComponent> DynamicMeshComponent = Cast<UDataflowEditorCollectionComponent>(Elem))
		{
			// Set up the wireframe display of the rest space mesh.

			TObjectPtr<UMeshElementsVisualizer> WireframeDraw = NewObject<UMeshElementsVisualizer>(RootSceneActor);
			WireframeElements.Add(DynamicMeshComponent, WireframeDraw);

			WireframeDraw->CreateInWorld(GetWorld(), FTransform::Identity);
			checkf(WireframeDraw->Settings, TEXT("Expected UMeshElementsVisualizer::Settings to exist after CreateInWorld"));

			WireframeDraw->Settings->DepthBias = 2.0;
			WireframeDraw->Settings->bAdjustDepthBiasUsingMeshSize = false;
			WireframeDraw->Settings->bShowWireframe = true;
			WireframeDraw->Settings->bShowBorders = true;
			WireframeDraw->Settings->bShowUVSeams = false;
			WireframeDraw->WireframeComponent->BoundaryEdgeThickness = 2;
			DynamicMeshComponent->WireframeComponent = WireframeDraw->WireframeComponent;

			WireframeDraw->SetMeshAccessFunction([DynamicMeshComponent](UMeshElementsVisualizer::ProcessDynamicMeshFunc ProcessFunc)
				{
					ProcessFunc(*DynamicMeshComponent->GetMesh());
				});

			for (FRenderElement RenderElement : DynamicMeshComponents)
			{
				RenderElement.Value->OnMeshChanged.Add(FSimpleMulticastDelegate::FDelegate::CreateLambda([WireframeDraw, this]()
					{
						WireframeDraw->NotifyMeshChanged();
					}));
			}

			WireframeDraw->Settings->bVisible = false;
			PropertyObjectsToTick.Add(WireframeDraw->Settings);
		}
	}
}

void FDataflowConstructionScene::ResetWireframeMeshElementsVisualizer()
{
	for (FRenderWireElement Elem : WireframeElements)
	{
		Elem.Value->Disconnect();
	}
	WireframeElements.Empty();
}

void FDataflowConstructionScene::UpdateWireframeMeshElementsVisualizer()
{
	ResetWireframeMeshElementsVisualizer();
	AddWireframeMeshElementsVisualizer();
}

bool FDataflowConstructionScene::HasRenderableGeometry()
{
	for (FRenderElement RenderElement : DynamicMeshComponents)
	{
		if (RenderElement.Value->GetMesh()->TriangleCount() > 0)
		{
			return true;
		}
	}
	return false;
}

void FDataflowConstructionScene::ResetConstructionScene()
{
	// The ModeManagerss::USelection will hold references to Components, but 
	// does not report them to the garbage collector. We need to clear the
	// saved selection when the scene is rebuilt. @todo(Dataflow) If that 
	// selection needs to persist across render resets, we will also need to
	// buffer the names of the selected objects so they can be reselected.
	if (GetDataflowModeManager())
	{
		if (USelection* SelectedComponents = GetDataflowModeManager()->GetSelectedComponents())
		{
			SelectedComponents->DeselectAll();
		}
	}

	// Some objects, like the UMeshElementsVisualizer and Settings Objects
	// are not part of a tool, so they won't get ticked.This member holds
	// ticked objects that get rebuilt on Update
	PropertyObjectsToTick.Empty();

	ResetWireframeMeshElementsVisualizer();

	ResetDynamicMeshComponents();
}

void FDataflowConstructionScene::UpdateConstructionScene()
{
	ResetConstructionScene();

	// The preview scene for the construction view will be
	// cleared and rebuilt from scratch. This will generate a 
	// list of UPrimitiveComponents for rendering.
	UpdateDynamicMeshComponents();
	
	// Attach a wireframe renderer to the DynamicMeshComponents
	UpdateWireframeMeshElementsVisualizer();

	for (const UDynamicMeshComponent* const DynamicMeshComponent : MeshComponentsForWireframeRendering)
	{
		WireframeElements[DynamicMeshComponent]->Settings->bVisible = true;
	}

	if (const TObjectPtr<UDataflowBaseContent>& EditorContent = GetEditorContent())
	{
		EditorContent->SetConstructionDirty(false);
	}

	for(const TObjectPtr<UDataflowBaseContent>& TerminalContent : GetTerminalContents())
	{
		TerminalContent->SetConstructionDirty(false);
	}
}

#undef LOCTEXT_NAMESPACE

