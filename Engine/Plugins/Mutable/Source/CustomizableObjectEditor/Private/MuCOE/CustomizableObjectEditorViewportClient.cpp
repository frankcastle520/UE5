// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/CustomizableObjectEditorViewportClient.h"

#include "AnimPreviewInstance.h"
#include "MuCOE/CustomizableObjectInstanceBakingUtils.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Animation/PoseAsset.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/Skeleton.h"
#include "AssetViewerSettings.h"
#include "CanvasTypes.h"
#include "Components/SphereReflectionCaptureComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ContentBrowserModule.h"
#include "CustomizableObjectEditor.h"
#include "DynamicMeshBuilder.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "Editor/UnrealEdTypes.h"
#include "EditorModeManager.h"
#include "GameFramework/WorldSettings.h"
#include "FileHelpers.h"
#include "IContentBrowserSingleton.h"
#include "InputKeyEventArgs.h"
#include "ObjectTools.h"
#include "MaterialDomain.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/ConfigCacheIni.h"
#include "MuCO/CustomizableObjectInstance.h"
#include "MuCO/CustomizableObjectInstancePrivate.h"
#include "MuCO/CustomizableObjectSystem.h"
#include "MuCO/CustomizableSkeletalMeshActor.h"
#include "MuCO/CustomizableSkeletalComponent.h"
#include "MuCO/CustomizableSkeletalComponentPrivate.h"
#include "MuCOE/CustomizableObjectPreviewScene.h"
#include "MuCOE/ICustomizableObjectInstanceEditor.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierClipMorph.h"
#include "MuCOE/Nodes/CustomizableObjectNodeModifierClipWithMesh.h"
#include "MuCOE/Nodes/CustomizableObjectNodeProjectorConstant.h"
#include "MuCOE/Nodes/CustomizableObjectNodeProjectorParameter.h"
#include "MuCOE/UnrealEditorPortabilityHelpers.h"
#include "MuT/UnrealPixelFormatOverride.h"
#include "Preferences/PersonaOptions.h"
#include "ScopedTransaction.h"
#include "SkeletalDebugRendering.h"
#include "UnrealWidget.h"
#include "MuCO/CustomizableObjectMipDataProvider.h"
#include "MuCOE/CustomizableObjectEditorLogger.h"
#include "MuCOE/UnrealBakeHelpers.h"
#include "MuCO/CustomizableObjectPrivate.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Notifications/SNotificationList.h"

class FMaterialRenderProxy;
class UFont;
class UMaterialExpression;
class UTextureMipDataProviderFactory;

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor" 


namespace EMutableAnimationPlaybackSpeeds
{
	// Speed scales for animation playback, must match EMutableAnimationPlaybackSpeeds::Type
	float Values[EMutableAnimationPlaybackSpeeds::NumPlaybackSpeeds] = { 0.1f, 0.25f, 0.5f, 0.75f, 1.0f, 2.0f, 5.0f, 10.0f, 0.f };
}


FCustomizableObjectEditorViewportClient::FCustomizableObjectEditorViewportClient(TWeakPtr<ICustomizableObjectInstanceEditor> InCustomizableObjectEditor, FPreviewScene* InPreviewScene, const TSharedPtr<SEditorViewport>& EditorViewportWidget)
	: FEditorViewportClient(&GLevelEditorModeTools(), InPreviewScene, EditorViewportWidget)
	, CustomizableObjectEditorPtr(InCustomizableObjectEditor)
	, CustomizableObject(nullptr)
{
	// load config
	ConfigOption = UPersonaOptions::StaticClass()->GetDefaultObject<UPersonaOptions>();
	check (ConfigOption);

	bUsingOrbitCamera = true;

	bDrawUVs = false;
	Widget->SetDefaultVisibility(false);
	bCameraLock = true;
	bDrawSky = true;

	bSetOrbitalOnPerspectiveMode = bCameraLock;

	const int32 CameraSpeed = 3;
	SetCameraSpeedSetting(CameraSpeed);

	bShowBones = false;
	
	DrawHelper.bDrawPivot = false;
	DrawHelper.bDrawWorldBox = false;
	DrawHelper.bDrawKillZ = false;
	DrawHelper.bDrawGrid = false;
	DrawHelper.GridColorAxis = FColor(160, 160, 160);
	DrawHelper.GridColorMajor = FColor(144, 144, 144);
	DrawHelper.GridColorMinor = FColor(128, 128, 128);
	DrawHelper.PerspectiveGridSize = 2048.0f;
	DrawHelper.NumCells = DrawHelper.PerspectiveGridSize / (32);
	UpdateShowGrid(true);
	UpdateShowSky(true);

	SetViewMode(VMI_Lit);

	EngineShowFlags.SetSeparateTranslucency(true);
	EngineShowFlags.SetSnap(0);
	EngineShowFlags.SetCompositeEditorPrimitives(true);

	EngineShowFlags.ScreenSpaceReflections = 1;
	EngineShowFlags.AmbientOcclusion = 1;
	EngineShowFlags.Grid = ConfigOption->bShowGrid;

	OverrideNearClipPlane(1.0f);
	
	// now add the ClipMorph plane
	ClipMorphNode = nullptr;
	bClipMorphLocalStartOffset = true;

	// clip mesh StaticMesh preview
	ClipMeshStaticMeshComp = NewObject<UStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transactional);
	PreviewScene->AddComponent(ClipMeshStaticMeshComp, FTransform());
	ClipMeshStaticMeshComp->SetVisibility(false);

	// clip mesh SkeletalMesh preview
	ClipMeshSkeletalMeshComp = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transactional);
	PreviewScene->AddComponent(ClipMeshSkeletalMeshComp, FTransform());
	ClipMeshStaticMeshComp->SetVisibility(false);

	// Assign ClipMesh and ClipMorph Materials from the plugin config
	FConfigFile* PluginConfig = GConfig->FindConfigFileWithBaseName("Mutable");
	if (PluginConfig)
	{
		FString ClipMorphMaterialName;
		PluginConfig->GetString(TEXT("EditorDefaults"), TEXT("ClipMorphMaterialName"), ClipMorphMaterialName);
		if (!ClipMorphMaterialName.IsEmpty())
		{
			ClipMorphMaterial = LoadObject<UMaterial>(NULL, *ClipMorphMaterialName, NULL, LOAD_None, NULL);
			ensure(ClipMorphMaterial);
		}

		// Clip mesh with mesh  material
		FString ClipMeshMaterialName;
		PluginConfig->GetString(TEXT("EditorDefaults"), TEXT("ClipMeshMaterialName"), ClipMeshMaterialName);
		if (!ClipMeshMaterialName.IsEmpty())
		{
			ClipMeshMaterial = LoadObject<UMaterial>(nullptr, *ClipMeshMaterialName, nullptr, LOAD_None, nullptr);
			ensure(ClipMeshMaterial);
		}
	}

	if (!ClipMorphMaterial)
	{
		ClipMorphMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	if (!ClipMeshMaterial)
	{
		ClipMeshMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	
	const float FOVMin = 5.f;
	const float FOVMax = 170.f;
	ViewFOV = FMath::Clamp<float>(53.43f, FOVMin, FOVMax);

	SetRealtime(true);
	if (GEditor->PlayWorld)
	{
		AddRealtimeOverride(false, LOCTEXT("RealtimeOverrideMessage_InstanceViewport", "Instance Viewport")); // We are PIE, don't start in realtime mode
	}
	
	// Lighting 
	SelectedLightComponent = nullptr;
	
	StateChangeShowGeometryDataFlag = false;

	// Register delegate to update the show flags when the post processing is turned on or off
	UAssetViewerSettings::Get()->OnAssetViewerSettingsChanged().AddRaw(this, &FCustomizableObjectEditorViewportClient::OnAssetViewerSettingsChanged);
	// Set correct flags according to current profile settings
	SetAdvancedShowFlagsForScene(UAssetViewerSettings::Get()->Profiles[GetMutableDefault<UEditorPerProjectUserSettings>()->AssetViewerProfileIndex].bPostProcessingEnabled);

	// Set profile so changes in scene lighting affect and match this editor too
	UEditorPerProjectUserSettings* PerProjectSettings = GetMutableDefault<UEditorPerProjectUserSettings>();
	UAssetViewerSettings* DefaultSettings = UAssetViewerSettings::Get();
	PerProjectSettings->AssetViewerProfileIndex = DefaultSettings->Profiles.IsValidIndex(PerProjectSettings->AssetViewerProfileIndex) ? PerProjectSettings->AssetViewerProfileIndex : 0;
	int32 ProfileIndex = PerProjectSettings->AssetViewerProfileIndex;
	FAdvancedPreviewScene* PreviewSceneCasted = static_cast<FAdvancedPreviewScene*>(PreviewScene);
	PreviewSceneCasted->SetProfileIndex(ProfileIndex);

	TransparentPlaneMaterialXY = (UMaterial*)StaticLoadObject(UMaterial::StaticClass(), NULL, TEXT("/Engine/EditorMaterials/WidgetVertexColorMaterial.WidgetVertexColorMaterial"), NULL, LOAD_None, NULL);
}


FCustomizableObjectEditorViewportClient::~FCustomizableObjectEditorViewportClient()
{
	UAssetViewerSettings::Get()->OnAssetViewerSettingsChanged().RemoveAll(this);
}


void DrawEllipse(FPrimitiveDrawInterface* PDI, const FVector& Base, const FVector& X, const FVector& Y, const FLinearColor& Color, float Radius1, float Radius2, int32 NumSides, uint8 DepthPriority, float Thickness, float DepthBias, bool bScreenSpace)
{
	const float	AngleDelta = 2.0f * PI / NumSides;
	FVector	LastVertex = Base + X * Radius1;

	for (int32 SideIndex = 0; SideIndex < NumSides; SideIndex++)
	{
		const FVector Vertex = Base + (X * FMath::Cos(AngleDelta * (SideIndex + 1)) * Radius1 + Y * FMath::Sin(AngleDelta * (SideIndex + 1)) * Radius2);
		PDI->DrawLine(LastVertex, Vertex, Color, DepthPriority, Thickness, DepthBias, bScreenSpace);
		LastVertex = Vertex;
	}
}


void FCustomizableObjectEditorViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FEditorViewportClient::Draw(View, PDI);
	
	switch (WidgetType)
	{
	case EWidgetType::Light:
		{
			check(SelectedLightComponent);

			if (USpotLightComponent* SpotLightComp = Cast<USpotLightComponent>(SelectedLightComponent))
			{
				FTransform TransformNoScale = SpotLightComp->GetComponentToWorld();
				TransformNoScale.RemoveScaling();

				// Draw point light source shape
				DrawWireCapsule(PDI, TransformNoScale.GetTranslation(), -TransformNoScale.GetUnitAxis(EAxis::Z), TransformNoScale.GetUnitAxis(EAxis::Y), TransformNoScale.GetUnitAxis(EAxis::X),
					FColor(231, 239, 0, 255), SpotLightComp->SourceRadius, 0.5f * SpotLightComp->SourceLength + SpotLightComp->SourceRadius, 25, SDPG_World);

				// Draw outer light cone
				DrawWireSphereCappedCone(PDI, TransformNoScale, SpotLightComp->AttenuationRadius, SpotLightComp->OuterConeAngle, 32, 8, 10, FColor(200, 255, 255), SDPG_World);

				// Draw inner light cone (if non zero)
				if (SpotLightComp->InnerConeAngle > UE_KINDA_SMALL_NUMBER)
				{
					DrawWireSphereCappedCone(PDI, TransformNoScale, SpotLightComp->AttenuationRadius, SpotLightComp->InnerConeAngle, 32, 8, 10, FColor(150, 200, 255), SDPG_World);
				}
			}
			else if (UPointLightComponent* PointLightComp = Cast<UPointLightComponent>(SelectedLightComponent))
			{
				FTransform LightTM = PointLightComp->GetComponentToWorld();

				// Draw light radius
				DrawWireSphereAutoSides(PDI, FTransform(LightTM.GetTranslation()), FColor(200, 255, 255), PointLightComp->AttenuationRadius, SDPG_World);

				// Draw point light source shape
				DrawWireCapsule(PDI, LightTM.GetTranslation(), -LightTM.GetUnitAxis(EAxis::Z), LightTM.GetUnitAxis(EAxis::Y), LightTM.GetUnitAxis(EAxis::X),
					FColor(231, 239, 0, 255), PointLightComp->SourceRadius, 0.5f * PointLightComp->SourceLength + PointLightComp->SourceRadius, 25, SDPG_World);
			}
			
			break;
		}
	case EWidgetType::ClipMorph:
		{
			float MaxSphereRadius = 0.f;

			for (const TPair<FName, TWeakObjectPtr<UDebugSkelMeshComponent>>& Key : SkeletalMeshComponents)
			{
				const UDebugSkelMeshComponent* Component = Key.Value.Get();
				if (Component)
				{
					MaxSphereRadius = FMath::Max(MaxSphereRadius, Component->Bounds.SphereRadius);
				}
			}

			if (MaxSphereRadius <= 0.f)
			{
				MaxSphereRadius = 1.f;
			}

			float PlaneRadius1 = MaxSphereRadius * 0.1f;
			float PlaneRadius2 = PlaneRadius1 * 0.5f;

			FMatrix PlaneMatrix = FMatrix(ClipMorphNormal, ClipMorphYAxis, ClipMorphXAxis, ClipMorphOrigin + ClipMorphOffset);

			// Start Plane
			DrawDirectionalArrow(PDI, PlaneMatrix, FColor::Red, MorphLength, MorphLength * 0.1f, 0, 0.1f);
			DrawBox(PDI, PlaneMatrix, FVector(0.01f, PlaneRadius1, PlaneRadius1), ClipMorphMaterial->GetRenderProxy(), 0);

			// End Plane + Ellipse
			PlaneMatrix.SetOrigin(ClipMorphOrigin + ClipMorphOffset + ClipMorphNormal * MorphLength);
			DrawBox(PDI, PlaneMatrix, FVector(0.01f, PlaneRadius2, PlaneRadius2), ClipMorphMaterial->GetRenderProxy(), 0);
			DrawEllipse(PDI, ClipMorphOrigin + ClipMorphOffset + ClipMorphNormal * MorphLength, ClipMorphXAxis, ClipMorphYAxis, FColor::Red, Radius1, Radius2, 15, 1, 0.f, 0, false);
			
			break;
		}

	case EWidgetType::Projector:
		{
			const FColor Color = WidgetColorDelegate.IsBound() ?
			WidgetColorDelegate.Execute() :
			FColor::Green;

			const ECustomizableObjectProjectorType ProjectorType = ProjectorTypeDelegate.IsBound() ?
				ProjectorTypeDelegate.Execute() :
				ECustomizableObjectProjectorType::Planar;

			const FVector WidgetScale = WidgetScaleDelegate.IsBound() ?
				WidgetScaleDelegate.Execute() :
				FVector::OneVector;

			const float CylindricalAngle = WidgetAngleDelegate.IsBound() ? 
				FMath::DegreesToRadians<float>(WidgetAngleDelegate.Execute()) :
				0.0f;

			const FVector CorrectedWidgetScale = FVector(WidgetScale.Z, WidgetScale.X, WidgetScale.Y);

			switch (ProjectorType)
			{
				case ECustomizableObjectProjectorType::Planar:
				{
					FVector Min = FVector(0.f, -0.5f, -0.5f);
					FVector Max = FVector(1.0f, 0.5f, 0.5f);
					FBox Box = FBox(Min * CorrectedWidgetScale, Max * CorrectedWidgetScale);
					FMatrix Mat = GetWidgetCoordSystem();
					Mat.SetOrigin(GetWidgetLocation());
					DrawWireBox(PDI, Mat, Box, Color, 1, 0.f);
					break;
				}
				case ECustomizableObjectProjectorType::Cylindrical:
				{
					// Draw the cylinder
					FMatrix Mat = GetWidgetCoordSystem();
					FVector Location = GetWidgetLocation();
					Mat.SetOrigin(Location);
					FVector TransformedX = Mat.TransformVector(FVector(1, 0, 0));
					FVector TransformedY = Mat.TransformVector(FVector(0, 1, 0));
					FVector TransformedZ = Mat.TransformVector(FVector(0, 0, 1));

					FVector Min = FVector(0.f, -0.5f, -0.5f);
					FVector Max = FVector(1.0f, 0.5f, 0.5f);
					FBox Box = FBox(Min * CorrectedWidgetScale, Max * CorrectedWidgetScale);
					FVector BoxExtent = Box.GetExtent();
					float CylinderHalfHeight = BoxExtent.X;
					//float CylinderRadius = (BoxExtent.Y + BoxExtent.Z) * 0.5f;
					float CylinderRadius = FMath::Abs(BoxExtent.Y);

					DrawWireCylinder(PDI, Location + TransformedX * CylinderHalfHeight, TransformedY, TransformedZ, TransformedX, Color, CylinderRadius, CylinderHalfHeight, 16, SDPG_World, 0.1f, 0, false);

					// Draw the arcs: the locations are Location with an offset towards the local forward direction
					FVector Location0 = Location - TransformedX * CylinderHalfHeight * 0.8f + TransformedX * CylinderHalfHeight;
					FVector Location1 = Location + TransformedX * CylinderHalfHeight * 0.8f + TransformedX * CylinderHalfHeight;
					FMatrix Mat0 = Mat;
					FMatrix Mat1 = Mat;
					Mat0.SetOrigin(Location0);
					Mat1.SetOrigin(Location1);
					DrawCylinderArc(PDI, Mat0, FVector(0.0f, 0.0f, 0.0f), FVector(0, 1, 0), FVector(0, 0, 1), FVector(1, 0, 0),  CylinderRadius, CylinderHalfHeight * 0.1f, 16, TransparentPlaneMaterialXY->GetRenderProxy(), SDPG_World, FColor(255, 85, 0, 192), CylindricalAngle);
					DrawCylinderArc(PDI, Mat1, FVector(0.0f, 0.0f, 0.0f), FVector(0, 1, 0), FVector(0, 0, 1), FVector(1, 0, 0), CylinderRadius, CylinderHalfHeight * 0.1f, 16, TransparentPlaneMaterialXY->GetRenderProxy(), SDPG_World, FColor(255, 85, 0, 192), CylindricalAngle);
					break;
				}
				case ECustomizableObjectProjectorType::Wrapping:
		        {
		            FVector Min = FVector(0.f, -0.5f, -0.5f);
		            FVector Max = FVector(1.0f, 0.5f, 0.5f);
		            FBox Box = FBox(Min * CorrectedWidgetScale, Max * CorrectedWidgetScale);
		            FMatrix Mat = GetWidgetCoordSystem();
		            Mat.SetOrigin(GetWidgetLocation());
		            DrawWireBox(PDI, Mat, Box, Color, 1, 0.f);
					break;
				}
				default:
				{
					check(false);
					break;
				}
			}
			break;
		}

	case EWidgetType::ClipMesh:
	case EWidgetType::Hidden:
		break;

	default:
		unimplemented(); // Case not implemented	
	}
	

	if (bShowBones)
	{
		for (const TPair<FName, TWeakObjectPtr<UDebugSkelMeshComponent>>& Key : SkeletalMeshComponents)
		{
			const UDebugSkelMeshComponent* Component = Key.Value.Get();
			if (Component)
			{
				DrawMeshBones(Component, PDI);
			}
		}
	}

	if (bShowDebugClothing)
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("p.ChaosClothEditor.DebugDrawPhysMeshWired"));
		check(CVar);
		const bool bPreviousValue = CVar->GetBool();
		CVar->Set(true);
		
		for (const TPair<FName, TWeakObjectPtr<UDebugSkelMeshComponent>>& Key : SkeletalMeshComponents)
		{
			UDebugSkelMeshComponent* Component = Key.Value.Get();
			if (Component)
			{
				Component->DebugDrawClothing(PDI);
			}
		}
		
		CVar->Set(bPreviousValue);
	}
}


void FCustomizableObjectEditorViewportClient::Draw(FViewport* InViewport, FCanvas* Canvas)
{
	// Defensive check to avoid unreal crashing inside render if the mesh is degenereated
	for (const TPair<FName, TWeakObjectPtr<UDebugSkelMeshComponent>>& Key : SkeletalMeshComponents)
	{
		UDebugSkelMeshComponent* Component = Key.Value.Get();

		if (Component && Component->GetSkinnedAsset() && Component->GetSkinnedAsset()->GetLODNum() == 0)
		{
			Component->SetSkeletalMesh(nullptr);
		}
	}

	// Configure the initial orbital position of the camera
	if (!bIsCameraSetup)
	{
		if (!Actor.IsValid())
		{
			return;
		}

		FVector Center;
		FVector Extents;
		Actor.Get()->GetActorBounds(false, Center, Extents, true);

		bIsCameraSetup = Extents.X * Extents.Y * Extents.Z > 0.0;

		static FRotator CustomOrbitRotation(-33.75, -135, 0);
		FVector CustomOrbitZoom(0, Extents.GetMax() * 2.5 / (75.0 * PI / 360.0), 0);

		SetCameraSetup(Center, CustomOrbitRotation, CustomOrbitZoom, Center, FVector::Zero(), {} /** Not used since orbital is enable just after. */);
		EnableCameraLock(true);
	}


	FEditorViewportClient::Draw(InViewport, Canvas);

	FSceneViewFamilyContext ViewFamily( FSceneViewFamily::ConstructionValues( InViewport, GetScene(), EngineShowFlags ));

	if(bDrawUVs)
	{
		constexpr int32 YPos = 24;
		DrawUVs(InViewport, Canvas, YPos);
	}

	if (StateChangeShowGeometryDataFlag)
	{
		ShowInstanceGeometryInformation(Canvas);
	}
}

namespace
{
	template<typename Real>
	FVector2D ClampUVRange(Real U, Real V)
	{
		return FVector2D( FMath::Wrap(U, Real(0), Real(1)), FMath::Wrap(V, Real(0), Real(1)) );
	}
}

void FCustomizableObjectEditorViewportClient::DrawUVs(FViewport* InViewport, FCanvas* InCanvas, int32 InTextYPos)
{
	const FName ComponentName = UVDrawComponentName;
	const uint32 LODLevel = UVDrawLODIndex; 	// TODO use the overriden LOD level
	const int32 SectionIndex = UVDrawSectionIndex;
	const int32 UVChannel = UVDrawUVIndex;

	//draw a string showing what UV channel and LOD is being displayed
	InCanvas->DrawShadowedString( 
		6,
		InTextYPos,
		*FText::Format( NSLOCTEXT("CustomizableObjectEditor", "UVOverlay_F", "Showing UV channel {0} for LOD {1}"), FText::AsNumber(UVChannel), FText::AsNumber(LODLevel) ).ToString(),
		GEngine->GetSmallFont(),
		FLinearColor::White
		);
	InTextYPos += 18;

	//calculate scaling
	const uint32 BorderWidth = 5;
	const uint32 MinY = InTextYPos + BorderWidth;
	const uint32 MinX = BorderWidth;
	const FVector2D UVBoxOrigin(MinX, MinY);
	const FVector2D BoxOrigin( MinX - 1, MinY - 1 );
	const FVector2D ViewportSize = FVector2D(InViewport->GetSizeXY())/InCanvas->GetDPIScale(); // Remove Window (OS) scale.
	const uint32 UVBoxScale = FMath::Min(ViewportSize.X - MinX, ViewportSize.Y - MinY) - BorderWidth;
	const uint32 BoxSize = UVBoxScale + 2;
	const FVector2D Box[ 4 ] = {
		BoxOrigin,									// topleft
		BoxOrigin + FVector2D( BoxSize, 0 ),		// topright
		BoxOrigin + FVector2D( BoxSize, BoxSize ),	// bottomright
		BoxOrigin + FVector2D( 0, BoxSize ),		// bottomleft
	};
	
	//draw texture border
	FLinearColor BorderColor = FLinearColor::White;
	FBatchedElements* BatchedElements = InCanvas->GetBatchedElements(FCanvas::ET_Line);
	FHitProxyId HitProxyId = InCanvas->GetHitProxyId();

	// Reserve line vertices (4 border lines, then up to the maximum number of graph lines)
	BatchedElements->AddReserveLines( 4 );

	// Left
	BatchedElements->AddLine( FVector( Box[ 0 ], 0.0f ), FVector( Box[ 1 ], 0.0f ), BorderColor, HitProxyId );
	BatchedElements->AddLine( FVector( Box[ 1 ], 0.0f ), FVector( Box[ 2 ], 0.0f ), BorderColor, HitProxyId );
	BatchedElements->AddLine( FVector( Box[ 2 ], 0.0f ), FVector( Box[ 3 ], 0.0f ), BorderColor, HitProxyId );
	BatchedElements->AddLine( FVector( Box[ 3 ], 0.0f ), FVector( Box[ 0 ], 0.0f ), BorderColor, HitProxyId );

	if (SkeletalMeshComponents.Num())
	{
		TWeakObjectPtr<UDebugSkelMeshComponent>* ComponentPtr = SkeletalMeshComponents.Find(ComponentName);
		if (ComponentPtr && ComponentPtr->IsValid())
		{
			TWeakObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent = *ComponentPtr;

			if (!SkeletalMeshComponent.IsValid() || !SkeletalMeshComponent->GetSkinnedAsset())
			{
				return;
			}

			const FSkeletalMeshRenderData* MeshRes = SkeletalMeshComponent->GetSkinnedAsset()->GetResourceForRendering();
			if (!MeshRes->LODRenderData.IsValidIndex(LODLevel))
			{
				return;
			}
			
			if (UVChannel >= 0 && UVChannel < static_cast<int32>(MeshRes->LODRenderData[LODLevel].GetNumTexCoords()))
			{
				// Find material index from name
				const FSkeletalMeshLODRenderData& lodModel = MeshRes->LODRenderData[LODLevel];

				if (!lodModel.RenderSections.IsValidIndex(SectionIndex))
				{
					return;
				}

				const FStaticMeshVertexBuffer& Vertices = lodModel.StaticVertexBuffers.StaticMeshVertexBuffer;

				TArray<uint32> Indices;
				lodModel.MultiSizeIndexContainer.GetIndexBuffer(Indices);

				uint32 NumTriangles = lodModel.RenderSections[SectionIndex].NumTriangles;
				int IndexIndex = lodModel.RenderSections[SectionIndex].BaseIndex;

				BatchedElements->AddReserveLines(NumTriangles * 3);

				for (uint32 FaceIndex = 0
					; FaceIndex < NumTriangles
					; ++FaceIndex, IndexIndex += 3)
				{
					FVector2D UV1(Vertices.GetVertexUV(Indices[IndexIndex + 0], UVChannel));
					FVector2D UV2(Vertices.GetVertexUV(Indices[IndexIndex + 1], UVChannel));
					FVector2D UV3(Vertices.GetVertexUV(Indices[IndexIndex + 2], UVChannel));

					// Draw lines in black unless the UVs are outside of the 0.0 - 1.0 range.  For out-of-bounds
					// UVs, we'll draw the line segment in red

					// If we are supporting a version lower than LWC get the right real type. 
					using Vector2DRealType = TDecay<decltype(DeclVal<FVector2D>().X)>::Type;

					constexpr Vector2DRealType Zero = static_cast<Vector2DRealType>(0);

					UV1 = ClampUVRange(UV1.X, UV1.Y) * UVBoxScale + UVBoxOrigin;
					UV2 = ClampUVRange(UV2.X, UV2.Y) * UVBoxScale + UVBoxOrigin;
					UV3 = ClampUVRange(UV3.X, UV3.Y) * UVBoxScale + UVBoxOrigin;

					BatchedElements->AddLine( FVector(UV1, Zero), FVector(UV2, Zero), BorderColor, HitProxyId );
					BatchedElements->AddLine( FVector(UV2, Zero), FVector(UV3, Zero), BorderColor, HitProxyId );
					BatchedElements->AddLine( FVector(UV3, Zero), FVector(UV1, Zero), BorderColor, HitProxyId );
				}
			}
		}
	}
}


void FCustomizableObjectEditorViewportClient::ShowGizmoClipMorph(UCustomizableObjectNodeModifierClipMorph& NodeMeshClipMorph)
{
	SetWidgetType(EWidgetType::ClipMorph);

	ClipMorphNode = &NodeMeshClipMorph;

	bClipMorphLocalStartOffset = NodeMeshClipMorph.bLocalStartOffset;
	MorphLength = NodeMeshClipMorph.B;
	Radius1 = NodeMeshClipMorph.Radius;
	Radius2 = NodeMeshClipMorph.Radius2;
	RotationAngle = NodeMeshClipMorph.RotationAngle;
	ClipMorphOrigin = NodeMeshClipMorph.Origin;
	ClipMorphLocalOffset = NodeMeshClipMorph.StartOffset;

	NodeMeshClipMorph.FindLocalAxes(ClipMorphXAxis, ClipMorphYAxis, ClipMorphNormal);

	if (bClipMorphLocalStartOffset)
	{
		ClipMorphOffset = ClipMorphLocalOffset.X * ClipMorphXAxis
			+ ClipMorphLocalOffset.Y * ClipMorphYAxis
			+ ClipMorphLocalOffset.Z * ClipMorphNormal;
	}
	else
	{
		ClipMorphOffset = ClipMorphLocalOffset;
	}
}


void FCustomizableObjectEditorViewportClient::HideGizmoClipMorph()
{
	if (WidgetType == EWidgetType::ClipMorph)
	{
		SetWidgetType(EWidgetType::Hidden);
	}
}


void FCustomizableObjectEditorViewportClient::ShowGizmoClipMesh(UCustomizableObjectNode& InClipMeshNode, FTransform* InClipMeshTransform, UObject& ClipMesh, int32 LODIndex, int32 SectionIndex, int32 MaterialSlotIndex)
{
	HideGizmoClipMesh();

	SetWidgetType(EWidgetType::ClipMesh);

	ClipMeshNode = &InClipMeshNode;
	ClipMeshTransform = InClipMeshTransform;

	if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(&ClipMesh))
	{
		ClipMeshStaticMeshComp->SetStaticMesh(StaticMesh);
		ClipMeshStaticMeshComp->SetVisibility(true);
		ClipMeshStaticMeshComp->SetWorldTransform(*ClipMeshTransform);
		ClipMeshStaticMeshComp->EmptyOverrideMaterials();
		ClipMeshStaticMeshComp->SetMaterial(MaterialSlotIndex, ClipMeshMaterial);
		ClipMeshStaticMeshComp->SetSectionPreview(SectionIndex);
	}
	else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(&ClipMesh))
	{
		ClipMeshSkeletalMeshComp->SetSkeletalMesh(SkeletalMesh);
		ClipMeshSkeletalMeshComp->SetVisibility(true);
		ClipMeshSkeletalMeshComp->SetWorldTransform(*ClipMeshTransform);
		ClipMeshSkeletalMeshComp->SetForcedLOD(LODIndex);
		ClipMeshSkeletalMeshComp->EmptyOverrideMaterials();
		ClipMeshSkeletalMeshComp->SetMaterial(MaterialSlotIndex, ClipMeshMaterial);
		ClipMeshSkeletalMeshComp->SetSectionPreview(SectionIndex);
	}
}


void FCustomizableObjectEditorViewportClient::HideGizmoClipMesh()
{
	if (WidgetType == EWidgetType::ClipMesh)
	{
		ClipMeshStaticMeshComp->SetVisibility(false);
		ClipMeshSkeletalMeshComp->SetVisibility(false);

		SetWidgetType(EWidgetType::Hidden);
	}
}


void FCustomizableObjectEditorViewportClient::ShowGizmoProjector(
	const FWidgetLocationDelegate& InWidgetLocationDelegate,
	const FOnWidgetLocationChangedDelegate& InOnWidgetLocationChangedDelegate,
	const FWidgetDirectionDelegate& InWidgetDirectionDelegate,
	const FOnWidgetDirectionChangedDelegate& InOnWidgetDirectionChangedDelegate,
	const FWidgetUpDelegate& InWidgetUpDelegate, const FOnWidgetUpChangedDelegate& InOnWidgetUpChangedDelegate,
	const FWidgetScaleDelegate& InWidgetScaleDelegate, const FOnWidgetScaleChangedDelegate& InOnWidgetScaleChangedDelegate,
	const FWidgetAngleDelegate& InWidgetAngleDelegate, const FProjectorTypeDelegate& InProjectorTypeDelegate,
	const FWidgetColorDelegate& InWidgetColorDelegate,
	const FWidgetTrackingStartedDelegate& InWidgetTrackingStartedDelegate)
{
	SetWidgetType(EWidgetType::Projector);

	WidgetLocationDelegate = InWidgetLocationDelegate;
	OnWidgetLocationChangedDelegate = InOnWidgetLocationChangedDelegate;
	WidgetDirectionDelegate = InWidgetDirectionDelegate;
	OnWidgetDirectionChangedDelegate = InOnWidgetDirectionChangedDelegate;
	WidgetUpDelegate = InWidgetUpDelegate;
	OnWidgetUpChangedDelegate = InOnWidgetUpChangedDelegate;
	WidgetScaleDelegate = InWidgetScaleDelegate;
	OnWidgetScaleChangedDelegate = InOnWidgetScaleChangedDelegate;
	WidgetAngleDelegate = InWidgetAngleDelegate;
	ProjectorTypeDelegate = InProjectorTypeDelegate;
	WidgetColorDelegate = InWidgetColorDelegate;
	WidgetTrackingStartedDelegate = InWidgetTrackingStartedDelegate;
}


void FCustomizableObjectEditorViewportClient::HideGizmoProjector()
{
	if (WidgetType == EWidgetType::Projector)
	{
		SetWidgetType(EWidgetType::Hidden);
	}
}


void FCustomizableObjectEditorViewportClient::ShowGizmoLight(ULightComponent& Light)
{
	SelectedLightComponent = &Light;
	
	SetWidgetType(EWidgetType::Light);
}


void FCustomizableObjectEditorViewportClient::HideGizmoLight()
{
	if (WidgetType == EWidgetType::Light)
	{
		SetWidgetType(EWidgetType::Hidden);
	}
}


void FCustomizableObjectEditorViewportClient::CreatePreviewActor(const TWeakObjectPtr<UCustomizableObjectInstance>& InInstance)
{
	if (Actor)
	{
		Actor->Destroy();
		SkeletalMeshComponents.Reset();
	}

	Actor.Reset(GetWorld()->SpawnActor<ASkeletalMeshActor>());

	bUpdated = false;
	
	InInstance->UpdatedNativeDelegate.AddSP(SharedThis(this), &FCustomizableObjectEditorViewportClient::OnInstanceUpdate);
	
	Invalidate();
}


TMap<FName, TWeakObjectPtr<UDebugSkelMeshComponent>>& FCustomizableObjectEditorViewportClient::GetPreviewMeshComponents()
{
	return SkeletalMeshComponents;
}


void FCustomizableObjectEditorViewportClient::SetPreviewAnimationAsset(UAnimationAsset* AnimAsset)
{
	for (TPair<FName,TWeakObjectPtr<UDebugSkelMeshComponent>>& Entry : SkeletalMeshComponents)
	{
		UDebugSkelMeshComponent* SkeletalMeshComponent = Entry.Value.Get();
		if (!SkeletalMeshComponent)
		{
			continue;
		}
			
		if (AnimAsset)
		{
			// Early out if the new preview asset is the same as the current one, to avoid replaying from the beginning, etc...
			if (SkeletalMeshComponent->PreviewInstance &&
				AnimAsset == SkeletalMeshComponent->PreviewInstance->GetCurrentAsset() &&
				SkeletalMeshComponent->IsPreviewOn())
			{
				return;
			}

			// Treat it as invalid if it's got a bogus skeleton pointer
			if (AnimAsset->GetSkeleton() == nullptr)
			{
				return;
			}
		}

		SkeletalMeshComponent->EnablePreview(true, AnimAsset);
	}
}


void FCustomizableObjectEditorViewportClient::OnInstanceUpdate(UCustomizableObjectInstance* Instance)
{
	if (!Instance)
	{
		return;
	}

	const UCustomizableObject* CO = Instance->GetCustomizableObject();
	if (!CO)
	{
		return;
	}

	// Remove components that are no longer there
	TArray<FName> ToRemove;
	ToRemove.Reserve(SkeletalMeshComponents.Num());
	for (const TPair<FName,TWeakObjectPtr<UDebugSkelMeshComponent>>& Item : SkeletalMeshComponents)
	{
		// TODO: This will not work with different types of components like grooms or panel clothing.
		bool bInstanceHasComponent = Instance->GetComponentMeshSkeletalMesh(Item.Key) != nullptr;

		if (!bInstanceHasComponent)
		{ 
			if (UDebugSkelMeshComponent* Comp = Item.Value.Get())
			{
				Comp->DestroyComponent();
			}
			ToRemove.Add(Item.Key);
		}
	}

	for (const FName Name: ToRemove)
	{
		SkeletalMeshComponents.Remove(Name);
	}

	// Add new components
	for (int32 ObjectComponentIndex = 0; ObjectComponentIndex < CO->GetComponentCount(); ++ObjectComponentIndex)
	{
		FName Name = CO->GetComponentName(ObjectComponentIndex);
		if (Instance->GetComponentMeshSkeletalMesh(Name))
		{
			if (!SkeletalMeshComponents.Contains(Name))
			{
				// We need to add it.
				UDebugSkelMeshComponent* DebugComponent = NewObject<UDebugSkelMeshComponent>(Actor.Get(), Name, RF_Transient);
				DebugComponent->bCastInsetShadow = true; // For better quality shadows in the editor previews, more similar to the in-game ones
				DebugComponent->bCanHighlightSelectedSections = false;
				DebugComponent->bComponentUseFixedSkelBounds = true; // First bounds computed would be using physics asset
				DebugComponent->MarkRenderStateDirty();
				DebugComponent->AttachToComponent(Actor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
				DebugComponent->RegisterComponent();

				SkeletalMeshComponents.Add(Name, DebugComponent);

				UCustomizableSkeletalComponent* CustomizableComponent = NewObject<UCustomizableSkeletalComponent>(DebugComponent);
				CustomizableComponent->SetSkipSetReferenceSkeletalMesh(true);
				CustomizableComponent->CustomizableObjectInstance = Instance;
				CustomizableComponent->SetComponentName(Name);
				CustomizableComponent->AttachToComponent(DebugComponent, FAttachmentTransformRules::KeepRelativeTransform);
				CustomizableComponent->RegisterComponent();

				DebugComponent->bDisableClothSimulation = bDisableClothSimulation;
				DebugComponent->bDrawNormals = bDrawNormals;
				DebugComponent->bDrawTangents = bDrawTangents;
				DebugComponent->bDrawBinormals = bDrawBinormals;
			}
		}
	}


	if (!bUpdated)
	{
		bUpdated = true;
		
		SetAnimation(CustomizableObjectEditorPtr.Pin()->GetCustomSettings()->Animation);
	}
	
	Invalidate();
	
	if (Actor)
	{
		if (Instance->GetPrivate()->SkeletalMeshStatus != ESkeletalMeshStatus::Success)
		{
			Actor->GetRootComponent()->SetVisibility(false, true);
		}
		Actor->GetRootComponent()->UpdateBounds();
	}
}


void FCustomizableObjectEditorViewportClient::SetDrawUVOverlay()
{
	bDrawUVs = !bDrawUVs;
	Invalidate();
}


void FCustomizableObjectEditorViewportClient::SetDrawUV(const FName ComponentName, const int32 LODIndex, const int32 SectionIndex, const int32 UVIndex)
{
	UVDrawComponentName = ComponentName;
	UVDrawLODIndex = LODIndex;
	UVDrawSectionIndex = SectionIndex;
	UVDrawUVIndex = UVIndex;

	Invalidate();
}


bool FCustomizableObjectEditorViewportClient::IsSetDrawUVOverlayChecked() const
{
	return bDrawUVs;
}

void FCustomizableObjectEditorViewportClient::UpdateShowGrid(bool bKeepOldValue)
{
	UAssetViewerSettings* Settings = UAssetViewerSettings::Get();
	int32 ProfileIndex = GetMutableDefault<UEditorPerProjectUserSettings>()->AssetViewerProfileIndex;

	bool bNewShowGridValue = true;

	if (Settings->Profiles.IsValidIndex(ProfileIndex))
	{
		bool bOldShowGridValue = Settings->Profiles[ProfileIndex].bShowFloor;

		if (bKeepOldValue)
		{
			// Do not toggle the value when the viewport is being constructed
			bNewShowGridValue = bOldShowGridValue;
		}
		else
		{
			// Toggle it when actually changing the option
			bNewShowGridValue = !bOldShowGridValue;
		}

		Settings->Profiles[ProfileIndex].bShowFloor = bNewShowGridValue;
	}
	
	DrawHelper.bDrawGrid = bNewShowGridValue;

	FAdvancedPreviewScene* AdvancedScene = static_cast<FAdvancedPreviewScene*>(PreviewScene);
	if (AdvancedScene != nullptr)
	{
		AdvancedScene->SetFloorVisibility(DrawHelper.bDrawGrid,true);
	}

	EngineShowFlags.Grid = DrawHelper.bDrawGrid;

	Invalidate();
}

void FCustomizableObjectEditorViewportClient::UpdateShowGridFromButton()
{
	UpdateShowGrid(false);
}

bool FCustomizableObjectEditorViewportClient::IsShowGridChecked() const
{
	return DrawHelper.bDrawGrid;
}

void FCustomizableObjectEditorViewportClient::UpdateShowSky(bool bKeepOldValue)
{
	UAssetViewerSettings* Settings = UAssetViewerSettings::Get();
	int32 ProfileIndex = GetMutableDefault<UEditorPerProjectUserSettings>()->AssetViewerProfileIndex;

	if (Settings->Profiles.IsValidIndex(ProfileIndex))
	{
		bool bOldDrawSky = Settings->Profiles[ProfileIndex].bShowEnvironment;

		if (bKeepOldValue)
		{
			bDrawSky = bOldDrawSky;
		}
		else
		{
			bDrawSky = !bOldDrawSky;
		}

		Settings->Profiles[ProfileIndex].bShowEnvironment = bDrawSky;
	}

	FAdvancedPreviewScene* PreviewSceneCasted = static_cast<FAdvancedPreviewScene*>(PreviewScene);
	PreviewSceneCasted->SetEnvironmentVisibility(bDrawSky, true);

	Invalidate();
}

void FCustomizableObjectEditorViewportClient::UpdateShowSkyFromButton()
{
	UpdateShowSky(false);
}

bool FCustomizableObjectEditorViewportClient::IsShowSkyChecked() const
{
	return bDrawSky;
}

void FCustomizableObjectEditorViewportClient::SetShowBounds()
{
	EngineShowFlags.Bounds = 1 - EngineShowFlags.Bounds;
	Invalidate();
}


bool FCustomizableObjectEditorViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
{
	const bool bMouseButtonDown = EventArgs.Viewport->KeyState(EKeys::LeftMouseButton) || EventArgs.Viewport->KeyState(EKeys::MiddleMouseButton) || EventArgs.Viewport->KeyState(EKeys::RightMouseButton);

	if (EventArgs.Event == IE_Pressed && !bMouseButtonDown)
	{
		if (EventArgs.Key == EKeys::F)
		{
			if (Actor.IsValid())
			{
				FVector Center;
				FVector Extents;
				Actor.Get()->GetActorBounds(false, Center, Extents, true);

				FocusViewportOnBox(FBox(Center - Extents, Center + Extents), true);
                return true;
			}
		
		}
		else if (WidgetType != EWidgetType::Hidden) // Do not change the type when hidden.
		{
			if (EventArgs.Key == EKeys::W)
			{
				SetWidgetMode(UE::Widget::WM_Translate);
				return true;
			}
			else if (EventArgs.Key == EKeys::E)
			{
				SetWidgetMode(UE::Widget::WM_Rotate);
				return true;
			}
			else if (EventArgs.Key == EKeys::R)
			{
				SetWidgetMode(UE::Widget::WM_Scale);
				return true;
			}	
		}
		else if (EventArgs.Key == EKeys::Q) // Not sure why, pressing Q the super class hides the widget.
		{
			SetWidgetType(EWidgetType::Hidden);
			return true;
		}
	}

	// Pass keys to standard controls, if we didn't consume input
	return FEditorViewportClient::InputKey(EventArgs);
}


bool FCustomizableObjectEditorViewportClient::InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale)
{
	if (CurrentAxis == EAxisList::None)
	{
		return false;
	}

	const UE::Widget::EWidgetMode WidgetMode = GetWidgetMode();
	
	switch (WidgetType)
	{
	case EWidgetType::Projector:
		{
			if (WidgetLocationDelegate.IsBound() && OnWidgetLocationChangedDelegate.IsBound())
			{
				if (Drag != FVector::ZeroVector)
				{
					OnWidgetLocationChangedDelegate.Execute(WidgetLocationDelegate.Execute() + Drag);				
				}
			}

			if (WidgetDirectionDelegate.IsBound() && OnWidgetDirectionChangedDelegate.IsBound())
			{
				const FVector WidgetDirection = WidgetDirectionDelegate.Execute();
				const FVector NewWidgetDirection = Rot.RotateVector(WidgetDirection);

				if (WidgetDirection != NewWidgetDirection)
				{
					OnWidgetDirectionChangedDelegate.Execute(NewWidgetDirection);				
				}
			}

			if (WidgetUpDelegate.IsBound() && OnWidgetUpChangedDelegate.IsBound())
			{
				const FVector WidgetUp = WidgetUpDelegate.Execute();
				const FVector NewWidgetUp = Rot.RotateVector(WidgetUp);

				if (WidgetUp != NewWidgetUp)
				{
					OnWidgetUpChangedDelegate.Execute(NewWidgetUp);				
				}
			}

			if (WidgetScaleDelegate.IsBound() && OnWidgetScaleChangedDelegate.IsBound())
			{
				const FVector CorrectedScale(Scale.Y, Scale.Z, Scale.X);
				if (CorrectedScale != FVector::ZeroVector)
				{
					OnWidgetScaleChangedDelegate.Execute(WidgetScaleDelegate.Execute() + CorrectedScale);
				}
			}
		
			return true;
		}
		
	case EWidgetType::ClipMorph:
		{
			if (WidgetMode == UE::Widget::WM_Translate)
			{
				if (CurrentAxis == EAxisList::Screen) // true when selecting the widget center
				{
					CurrentAxis = EAxisList::XYZ;
				}
				
				if (CurrentAxis & EAxisList::Z)
				{
					const float dragZ = bClipMorphLocalStartOffset ? FVector::DotProduct(Drag, ClipMorphNormal) : Drag.Z;
					ClipMorphLocalOffset.Z += dragZ;
					ClipMorphOffset += (bClipMorphLocalStartOffset) ? dragZ * ClipMorphNormal : FVector(0,0,dragZ);
				}

				if(CurrentAxis & EAxisList::X)
				{
					const float dragX = bClipMorphLocalStartOffset ? FVector::DotProduct(Drag, ClipMorphXAxis) : Drag.X;
					ClipMorphLocalOffset.X += dragX;
					ClipMorphOffset += (bClipMorphLocalStartOffset) ? dragX * ClipMorphXAxis : FVector(dragX, 0, 0);
				}

				if (CurrentAxis & EAxisList::Y)
				{
					const float dragY = bClipMorphLocalStartOffset ? FVector::DotProduct(Drag, ClipMorphYAxis) : Drag.Y;
					ClipMorphLocalOffset.Y += dragY;
					ClipMorphOffset += (bClipMorphLocalStartOffset) ? dragY * ClipMorphYAxis : FVector(0, dragY, 0);
				}
				
				ClipMorphNode->StartOffset = ClipMorphLocalOffset;
			}
			else if (WidgetMode == UE::Widget::WM_Rotate)
			{
				bool bClipMorphViewPortRotation = false;

				if (CurrentAxis == EAxisList::X)
				{
					bClipMorphViewPortRotation = true;
					float Angle = ClipMorphNode->bInvertNormal ? Rot.GetComponentForAxis(EAxis::X) : -Rot.GetComponentForAxis(EAxis::X);
					ClipMorphNormal = ClipMorphNormal.RotateAngleAxis(Angle, ClipMorphXAxis);
				}
				else if (CurrentAxis == EAxisList::Y)
				{
					bClipMorphViewPortRotation = true;
					float Angle = Rot.GetComponentForAxis(EAxis::Y);
					ClipMorphNormal = ClipMorphNormal.RotateAngleAxis(Angle, ClipMorphYAxis);
				}

				if (bClipMorphViewPortRotation)
				{
					ClipMorphNormal.Normalize();
					ClipMorphNode->Normal = ClipMorphNormal;
					ClipMorphNode->FindLocalAxes(ClipMorphXAxis, ClipMorphYAxis, ClipMorphNormal);

					if (bClipMorphLocalStartOffset)
					{
						ClipMorphLocalOffset.Z = FVector::DotProduct(ClipMorphOffset, ClipMorphNormal);
						ClipMorphLocalOffset.Y = FVector::DotProduct(ClipMorphOffset, ClipMorphYAxis);
						ClipMorphLocalOffset.X = FVector::DotProduct(ClipMorphOffset, ClipMorphXAxis);
					}

					ClipMorphNode->StartOffset = ClipMorphLocalOffset;
				}
			}

			return true;
		}
	case EWidgetType::ClipMesh:
		{
			if (WidgetMode == UE::Widget::WM_Translate)
			{
				ClipMeshTransform->AddToTranslation(Drag);
			}
			else if (WidgetMode == UE::Widget::WM_Rotate)
			{
				ClipMeshTransform->ConcatenateRotation(Rot.Quaternion());
			}
			if (WidgetMode == UE::Widget::WM_Scale)
			{
				ClipMeshTransform->SetScale3D(ClipMeshTransform->GetScale3D() + Scale);
			}

			ClipMeshStaticMeshComp->Modify();
			ClipMeshSkeletalMeshComp->Modify();
			ClipMeshStaticMeshComp->SetWorldTransform(*ClipMeshTransform);
			ClipMeshSkeletalMeshComp->SetWorldTransform(*ClipMeshTransform);

			return true;
		}
		
	case EWidgetType::Light:
		{
			if (WidgetMode == UE::Widget::WM_Translate)
			{
				SelectedLightComponent->AddWorldOffset(Drag);
				SelectedLightComponent->MarkForNeededEndOfFrameRecreate();
			}
			else if (WidgetMode == UE::Widget::WM_Rotate)
			{
				SelectedLightComponent->AddWorldRotation(Rot.Quaternion());
				SelectedLightComponent->MarkForNeededEndOfFrameRecreate();
			}

			return true;
		}
		
	case EWidgetType::Hidden:
		{
			return false;
		}
		
	default:
		{
			unimplemented()
			return false;
		}
	}	
}


void FCustomizableObjectEditorViewportClient::TrackingStarted(const FInputEventState& InInputState, bool bIsDraggingWidget, bool bNudge)
{
	if (!bIsDraggingWidget || !InInputState.IsLeftMouseButtonPressed() || (Widget->GetCurrentAxis() & EAxisList::All) == 0)
	{
		return;
	}
	
	(void)HandleBeginTransform();
}


void FCustomizableObjectEditorViewportClient::TrackingStopped()
{
	(void)HandleEndTransform();
}

bool FCustomizableObjectEditorViewportClient::BeginTransform(const FGizmoState& InState)
{
	return HandleBeginTransform();
}

bool FCustomizableObjectEditorViewportClient::EndTransform(const FGizmoState& InState)
{
	return HandleEndTransform();
}

bool FCustomizableObjectEditorViewportClient::HandleBeginTransform()
{
	switch (WidgetType)
	{
	case EWidgetType::Projector:
	case EWidgetType::ClipMorph:
	case EWidgetType::ClipMesh:
	case EWidgetType::Light:
		{
			bManipulating = true;

			const UE::Widget::EWidgetMode WidgetMode = GetWidgetMode();
				
			if (WidgetMode == UE::Widget::WM_Translate)
			{
				GEditor->BeginTransaction(LOCTEXT("CustomizableObjectEditor_Translate", "Translate"));
			}
			else if (WidgetMode == UE::Widget::WM_Rotate)
			{
				GEditor->BeginTransaction(LOCTEXT("CustomizableObjectEditor_Rotate", "Rotate"));
			}
			else if (WidgetMode == UE::Widget::WM_Scale)
			{
				GEditor->BeginTransaction(LOCTEXT("CustomizableObjectEditor_Scale", "Scale"));
			}

			break;
		}
		
	case EWidgetType::Hidden:
		break;
	default:
		unimplemented();
	}
	
	switch (WidgetType)
	{
	case EWidgetType::Projector:
		WidgetTrackingStartedDelegate.ExecuteIfBound();
		break;
	case EWidgetType::ClipMorph:
		ClipMorphNode->Modify();
		break;
	case EWidgetType::ClipMesh:
		ClipMeshNode->Modify();
		break;
	case EWidgetType::Light:
		SelectedLightComponent->Modify();
		break;
	case EWidgetType::Hidden:
		return true;
		break;
	default:
		unimplemented();
	}
	return false;
}

bool FCustomizableObjectEditorViewportClient::HandleEndTransform()
{
	switch (WidgetType)
	{
	case EWidgetType::Projector:
	case EWidgetType::ClipMorph:
	case EWidgetType::ClipMesh:
	case EWidgetType::Light:
		if (bManipulating)
		{
			bManipulating = false;
			GEditor->EndTransaction();
			return true;
		}
		
		break;
		
	case EWidgetType::Hidden:
		return true;
		break;
	default:
		unimplemented();
	}
	return false;
}


FVector FCustomizableObjectEditorViewportClient::GetWidgetLocation() const
{
	switch (WidgetType)
	{
	case EWidgetType::Projector:
		return WidgetLocationDelegate.IsBound() ?WidgetLocationDelegate.Execute() : FVector::ZeroVector;
		
	case EWidgetType::ClipMorph:
		return ClipMorphOrigin + ClipMorphOffset;

	case EWidgetType::ClipMesh:
		return ClipMeshTransform->GetTranslation();

	case EWidgetType::Light:
		return SelectedLightComponent->GetComponentLocation();

	case EWidgetType::Hidden:
		return FVector::ZeroVector;

	default:
		unimplemented()
		return FVector::ZeroVector;
	}
}


FMatrix FCustomizableObjectEditorViewportClient::GetWidgetCoordSystem() const
{
	switch (WidgetType)
	{
	case EWidgetType::Projector:
		{
			const FVector WidgetDirection = WidgetDirectionDelegate.IsBound() ?
				WidgetDirectionDelegate.Execute() :
				FVector::ForwardVector;

			const FVector WidgetUp = WidgetUpDelegate.IsBound() ?
				WidgetUpDelegate.Execute() :
				FVector::UpVector;
	
			const FVector YVector = FVector::CrossProduct(WidgetDirection, WidgetUp);
			return FMatrix(WidgetDirection, YVector, WidgetUp, FVector::ZeroVector);
		}		
	case EWidgetType::ClipMorph:
		{
			if (bClipMorphLocalStartOffset)
			{
				return FMatrix(-ClipMorphXAxis, -ClipMorphYAxis, -ClipMorphNormal, FVector::ZeroVector);
			}
			else
			{			
				return FMatrix(FVector(1, 0, 0), FVector(0,1,0), FVector(0,0,1), FVector::ZeroVector);
			}
		}
		
	case EWidgetType::ClipMesh:
		{
			return ClipMeshTransform->ToMatrixNoScale().RemoveTranslation();			
		}
		
	case EWidgetType::Light:
		{
			FMatrix Rotation = SelectedLightComponent->GetComponentTransform().ToMatrixNoScale();
			Rotation.SetOrigin(FVector::ZeroVector);
			return Rotation;
		}
		
	case EWidgetType::Hidden:
		{
			return FMatrix::Identity;
		}

	default:
		{
			unimplemented()
			return FMatrix::Identity;
		}
	}
}


ECoordSystem FCustomizableObjectEditorViewportClient::GetWidgetCoordSystemSpace() const
{
	return ModeTools->GetCoordSystem();
}


void FCustomizableObjectEditorViewportClient::SetWidgetCoordSystemSpace(ECoordSystem NewCoordSystem)
{
	ModeTools->SetCoordSystem(NewCoordSystem);
	Invalidate();
}

void FCustomizableObjectEditorViewportClient::SetViewportType(ELevelViewportType InViewportType)
{
	// Getting camera mode on perspective view
	if (ViewportType == ELevelViewportType::LVT_Perspective)
	{
		bSetOrbitalOnPerspectiveMode = bCameraLock;
	}

	// Set Camera mode
	if (InViewportType == ELevelViewportType::LVT_Perspective || ViewportType == ELevelViewportType::LVT_Perspective)
	{
		if (InViewportType == ELevelViewportType::LVT_Perspective)
		{
			SetCameraMode(bSetOrbitalOnPerspectiveMode);
		}
		else
		{
			SetCameraMode(false);
		}
	}

	// Set Camera view
	FEditorViewportClient::SetViewportType(InViewportType);
}



bool FCustomizableObjectEditorViewportClient::CanSetWidgetMode(UE::Widget::EWidgetMode NewMode) const
{
	return true;
}


void FCustomizableObjectEditorViewportClient::SetAnimation(UAnimationAsset* Animation)
{
	for (TPair<FName, TWeakObjectPtr<UDebugSkelMeshComponent>>& Entry : SkeletalMeshComponents)
	{
		UDebugSkelMeshComponent* PreviewMeshComponent = Entry.Value.Get();
		if (!PreviewMeshComponent)
		{
			continue;
		}

		PreviewMeshComponent->EnablePreview(true, Animation);
	}
}


void FCustomizableObjectEditorViewportClient::AddLightToScene(ULightComponent* AddedLight)
{
	if (!AddedLight)
	{
		return;
	}

	LightComponents.Add(AddedLight);
	PreviewScene->AddComponent(AddedLight, AddedLight->GetComponentTransform());
}


void FCustomizableObjectEditorViewportClient::RemoveLightFromScene(ULightComponent* RemovedLight)
{
	if (!RemovedLight)
	{
		return;
	}

	LightComponents.Remove(RemovedLight);
	PreviewScene->RemoveComponent(RemovedLight);
}


void FCustomizableObjectEditorViewportClient::RemoveAllLightsFromScene()
{
	for (ULightComponent* Light : LightComponents)
	{
		PreviewScene->RemoveComponent(Light);
	}

	LightComponents.Empty();
}


//-------------------------------------------------------------------------------------------------

class SMutableSelectFolderDlg : public SWindow
{
public:
	SLATE_BEGIN_ARGS(SMutableSelectFolderDlg)
	{
	}

	SLATE_ARGUMENT(FText, DefaultAssetPath)
	SLATE_ARGUMENT(FText, DefaultFileName)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

public:
	/** Displays the dialog in a blocking fashion */
	EAppReturnType::Type ShowModal();

	/** Gets the resulting asset path */
	FString GetAssetPath();

	/** FileName getter */
	FString GetFileName();

	bool GetExportAllResources() const;
	bool GetGenerateConstantMaterialInstances() const;

protected:
	void OnPathChange(const FString& NewPath);
	FReply OnButtonClick(EAppReturnType::Type ButtonID);
	void OnNameChange(const FText& NewName, ETextCommit::Type CommitInfo);
	void OnBoolParameterChanged(ECheckBoxState InCheckboxState);
	void OnConstantMaterialInstancesBoolParameterChanged(ECheckBoxState InCheckboxState);

	EAppReturnType::Type UserResponse = EAppReturnType::Cancel; 
	FText AssetPath;
	FText FileName;
	bool bExportAllResources = false;
	bool bGenerateConstantMaterialInstances = false;
};


//-------------------------------------------------------------------------------------------------


void FCustomizableObjectEditorViewportClient::BakeInstance()
{
	// Early exit if no instance is set in the editor 
	UCustomizableObjectInstance* Instance = CustomizableObjectEditorPtr.Pin()->GetPreviewInstance();
	if (!Instance)
	{
		UE_LOG(LogMutable, Error, TEXT("No Mutable Customizable Object instance was found in the current editor."));
		return;
	}
	
	BakeTempInstance = Instance->Clone();

	// Call the instance update async method
	FInstanceUpdateNativeDelegate UpdateDelegate;
	UpdateDelegate.AddRaw(this, &FCustomizableObjectEditorViewportClient::OnInstanceForBakingUpdate);
	UpdateInstanceForBaking(*BakeTempInstance, UpdateDelegate);
}


void FCustomizableObjectEditorViewportClient::OnInstanceForBakingUpdate(const FUpdateContext& Result)
{
	// Early exit if no instance was provided
	if (!BakeTempInstance)
	{
		UE_LOG(LogMutable, Error, TEXT("No Mutable Customizable Object instance was provided for the baking."));
		return;
	}

	// Early exit if update result is not success
	if ( !UCustomizableObjectSystem::IsUpdateResultValid(Result.UpdateResult) )
	{
		UE_LOG(LogMutable, Warning ,TEXT("Instance finished update with an error state : %s. Skipping instance baking"), *UEnum::GetValueAsString(Result.UpdateResult));
		BakeTempInstance = nullptr;
		return;
	}
	
	const UCustomizableObject* CO = BakeTempInstance->GetCustomizableObject();

	if (!CO)
	{
		UE_LOG(LogMutable, Error, TEXT("The Customizable Object instance provided for the baking does not have a Customizable Object."));
		return;
	}
	
	// Let the user set some configurations at the editor level
	const FText DefaultFileName = FText::Format(LOCTEXT("DefaultFileNameForBakeInstance", "{0}"), FText::AsCultureInvariant(CO->GetName()));
	
	TSharedRef<SMutableSelectFolderDlg> FolderDlg =
		SNew(SMutableSelectFolderDlg)
		.DefaultAssetPath(FText())
		.DefaultFileName(DefaultFileName);
	
	if (FolderDlg->ShowModal() != EAppReturnType::Cancel)
	{
		TArray<TPair<EPackageSaveResolutionType,UPackage*>> SavedPackages;
		BakeCustomizableObjectInstance(
			*BakeTempInstance,
			FolderDlg->GetFileName(),
			FolderDlg->GetAssetPath(),
			FolderDlg->GetExportAllResources(),
			FolderDlg->GetGenerateConstantMaterialInstances(),
			false,
			false,
			SavedPackages);
	}
	
	BakeTempInstance = nullptr;
}


void FCustomizableObjectEditorViewportClient::StateChangeShowGeometryData()
{
	StateChangeShowGeometryDataFlag = !StateChangeShowGeometryDataFlag;
	Invalidate();
}


void FCustomizableObjectEditorViewportClient::ShowInstanceGeometryInformation(FCanvas* InCanvas)
{
	float YOffset = 50.0f;
	int32 ComponentIndex = 0;

	// Show total number of triangles and vertices
	for (TPair<FName, TWeakObjectPtr<UDebugSkelMeshComponent>>& Entry : SkeletalMeshComponents)
	{
		UDebugSkelMeshComponent* SkeletalMeshComponent = Entry.Value.Get();

		if (SkeletalMeshComponent && SkeletalMeshComponent->GetSkinnedAsset())
		{
			const FSkeletalMeshRenderData* MeshRes = SkeletalMeshComponent->GetSkinnedAsset()->GetResourceForRendering();
			int32 NumTriangles;
			int32 NumVertices;
			int32 NumLODLevel = MeshRes->LODRenderData.Num();

			for (int32 i = 0; i < NumLODLevel; ++i)
			{
				NumTriangles = 0;
				NumVertices = 0;
				const FSkeletalMeshLODRenderData& lodModel = MeshRes->LODRenderData[i];
				for (int32 j = 0; j < lodModel.RenderSections.Num(); ++j)
				{
					NumTriangles += lodModel.RenderSections[j].NumTriangles;
					NumVertices += lodModel.RenderSections[j].NumVertices;
				}

				//draw a string showing what UV channel and LOD is being displayed
				InCanvas->DrawShadowedString(
					6.0f,
					YOffset,
					*FText::Format(NSLOCTEXT("CustomizableObjectEditor", "ComponentGeometryReport", "Component {3} LOD {0} has {1} vertices and {2} triangles"),
						FText::AsNumber(i), FText::AsNumber(NumVertices), FText::AsNumber(NumTriangles), FText::AsNumber(ComponentIndex)).ToString(),
					GEngine->GetSmallFont(),
					FLinearColor::White
				);

				YOffset += 20.0f;
			}
		}

		YOffset += 40.0f;
		ComponentIndex++;
	}
}


void FCustomizableObjectEditorViewportClient::SetCustomizableObject(UCustomizableObject* CustomizableObjectParameter)
{
	CustomizableObject = CustomizableObjectParameter;
}


void FCustomizableObjectEditorViewportClient::DrawShadowedString(FCanvas* Canvas, float StartX, float StartY, const FLinearColor& Color, float TextScale, FString String)
{
	UFont* StatFont = nullptr;

	if (TextScale > 2.0f)
	{
		StatFont = GEngine->GetLargeFont();
	}
	else if (TextScale > 1.0f)
	{
		StatFont = GEngine->GetMediumFont();
	}
	else
	{
		StatFont = GEngine->GetSmallFont();
	}

	Canvas->DrawShadowedString(StartX, StartY, *String, StatFont, Color);
}


void FCustomizableObjectEditorViewportClient::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(ClipMorphMaterial);
	Collector.AddReferencedObject(ClipMeshMaterial);
	Collector.AddReferencedObject(TransparentPlaneMaterialXY);

	if (BakeTempInstance)
	{
		Collector.AddReferencedObject(BakeTempInstance);
	}
}


void FCustomizableObjectEditorViewportClient::SetAdvancedShowFlagsForScene(const bool bAdvancedShowFlags)
{
	if (bAdvancedShowFlags)
	{
		EngineShowFlags.EnableAdvancedFeatures();
	}
	else
	{
		EngineShowFlags.DisableAdvancedFeatures();
	}
}


void FCustomizableObjectEditorViewportClient::OnAssetViewerSettingsChanged(const FName& InPropertyName)
{
	UAssetViewerSettings* Settings = UAssetViewerSettings::Get();
	int32 ProfileIndex = GetMutableDefault<UEditorPerProjectUserSettings>()->AssetViewerProfileIndex;

	if (InPropertyName == GET_MEMBER_NAME_CHECKED(FPreviewSceneProfile, bPostProcessingEnabled) || InPropertyName == NAME_None)
	{
		if (Settings->Profiles.IsValidIndex(ProfileIndex))
		{
			SetAdvancedShowFlagsForScene(Settings->Profiles[ProfileIndex].bPostProcessingEnabled);
		}
	}

	else if (InPropertyName == GET_MEMBER_NAME_CHECKED(FPreviewSceneProfile, bShowEnvironment))
	{
		if (Settings->Profiles.IsValidIndex(ProfileIndex))
		{
			bDrawSky = Settings->Profiles[ProfileIndex].bShowEnvironment;
		}
		else
		{
			bDrawSky = !bDrawSky;
		}
	}

	else if (InPropertyName == GET_MEMBER_NAME_CHECKED(FPreviewSceneProfile, bShowFloor))
	{
		if (Settings->Profiles.IsValidIndex(ProfileIndex))
		{
			DrawHelper.bDrawGrid = Settings->Profiles[ProfileIndex].bShowFloor;
		}
		else
		{
			DrawHelper.bDrawGrid = !DrawHelper.bDrawGrid;
		}

		EngineShowFlags.Grid = DrawHelper.bDrawGrid;

		Invalidate();
	}
}


void FCustomizableObjectEditorViewportClient::DrawCylinderArc(FPrimitiveDrawInterface* PDI, const FMatrix& CylToWorld, const FVector& Base, const FVector& XAxis, const FVector& YAxis, const FVector& ZAxis, float Radius, float HalfHeight, uint32 Sides, const FMaterialRenderProxy* MaterialRenderProxy, uint8 DepthPriority, FColor Color, float MaxAngle)
{
	TArray<FDynamicMeshVertex> MeshVerts;
	TArray<uint32> MeshIndices;

	const float	AngleDelta = MaxAngle / (Sides - 1);
	const float Offset = 0.5f * MaxAngle;

	FVector2f TC = FVector2f(0.0f, 0.0f);
	float TCStep = 1.0f / (Sides - 1);

	FVector TopOffset = HalfHeight * ZAxis;
	int32 BaseVertIndex = MeshVerts.Num();

	//Compute vertices for base circle.
	for (uint32 SideIndex = 0; SideIndex < Sides; SideIndex++)
	{
		const FVector Vertex = Base + (XAxis * FMath::Cos(AngleDelta * SideIndex - Offset) + YAxis * FMath::Sin(AngleDelta * SideIndex - Offset)) * Radius;
		FVector Normal = Vertex - Base;
		Normal.Normalize();

		FDynamicMeshVertex MeshVertex;
		MeshVertex.Position = FVector3f(Vertex - TopOffset);
		MeshVertex.TextureCoordinate[0] = TC;
		MeshVertex.SetTangents((FVector3f)-ZAxis, FVector3f((-ZAxis) ^ Normal), (FVector3f)Normal);
		MeshVertex.Color = Color;
		MeshVerts.Add(MeshVertex); //Add bottom vertex

		TC.X += TCStep;
	}

	TC = FVector2f(0.0f, 1.0f);

	//Compute vertices for the top circle
	for (uint32 SideIndex = 0; SideIndex < Sides; SideIndex++)
	{
		const FVector Vertex = Base + (XAxis * FMath::Cos(AngleDelta * SideIndex - Offset) + YAxis * FMath::Sin(AngleDelta * SideIndex - Offset)) * Radius;
		FVector Normal = Vertex - Base;
		Normal.Normalize();

		FDynamicMeshVertex MeshVertex;
		MeshVertex.Position = FVector3f(Vertex + TopOffset);	// LWC_TODO: Precision Loss
		MeshVertex.TextureCoordinate[0] = TC;
		MeshVertex.SetTangents((FVector3f)-ZAxis, FVector3f((-ZAxis) ^ Normal), (FVector3f)Normal);
		MeshVertex.Color = Color;
		MeshVerts.Add(MeshVertex); //Add top vertex

		TC.X += TCStep;
	}

	//Add sides.
	for (uint32 SideIndex = 0; SideIndex < (Sides - 1); SideIndex++)
	{
		int32 V0 = BaseVertIndex + SideIndex;
		int32 V1 = BaseVertIndex + ((SideIndex + 1) % Sides);
		int32 V2 = V0 + Sides;
		int32 V3 = V1 + Sides;

		MeshIndices.Add(V0);
		MeshIndices.Add(V2);
		MeshIndices.Add(V1);

		MeshIndices.Add(V2);
		MeshIndices.Add(V3);
		MeshIndices.Add(V1);
	}

	FDynamicMeshBuilder MeshBuilder(PDI->View->GetFeatureLevel());
	MeshBuilder.AddVertices(MeshVerts);
	MeshBuilder.AddTriangles(MeshIndices);

	MeshBuilder.Draw(PDI, CylToWorld, MaterialRenderProxy, DepthPriority, 0.f);
}


bool FCustomizableObjectEditorViewportClient::GetFloorVisibility()
{
	FAdvancedPreviewScene* AdvancedScene = static_cast<FAdvancedPreviewScene*>(PreviewScene);
	if (AdvancedScene != nullptr)
	{
		const UStaticMeshComponent* FloorMeshComponent = AdvancedScene->GetFloorMeshComponent();
		if (FloorMeshComponent != nullptr)
		{
			return FloorMeshComponent->IsVisible();
		}
	}

	return false;
}


void FCustomizableObjectEditorViewportClient::SetFloorVisibility(bool Value)
{
	FAdvancedPreviewScene* AdvancedScene = static_cast<FAdvancedPreviewScene*>(PreviewScene);
	if (AdvancedScene != nullptr)
	{
		AdvancedScene->SetFloorVisibility(Value);
	}
}


bool FCustomizableObjectEditorViewportClient::GetGridVisibility()
{
	return DrawHelper.bDrawGrid;
}


bool FCustomizableObjectEditorViewportClient::GetEnvironmentMeshVisibility()
{
	FCustomizableObjectPreviewScene* CustomizableObjectPreviewScene = static_cast<FCustomizableObjectPreviewScene*>(PreviewScene);
	if (CustomizableObjectPreviewScene != nullptr)
	{
		return CustomizableObjectPreviewScene->GetSkyComponent()->IsVisible();
	}

	return false;
}


void FCustomizableObjectEditorViewportClient::SetEnvironmentMeshVisibility(uint32 Value)
{
	FCustomizableObjectPreviewScene* CustomizableObjectPreviewScene = static_cast<FCustomizableObjectPreviewScene*>(PreviewScene);
	if (CustomizableObjectPreviewScene != nullptr)
	{
		CustomizableObjectPreviewScene->GetSkyComponent()->SetVisibility(Value == 1, true);
	}

	Invalidate();
}

bool FCustomizableObjectEditorViewportClient::IsOrbitalCameraActive() const
{
	return bCameraLock;
}

void FCustomizableObjectEditorViewportClient::SetCameraMode(bool Value)
{
	EnableCameraLock(Value);
}


void FCustomizableObjectEditorViewportClient::SetShowBones()
{
	bShowBones = !bShowBones;
}


bool FCustomizableObjectEditorViewportClient::IsShowingBones() const
{
	return bShowBones;
}


const TArray<ULightComponent*>& FCustomizableObjectEditorViewportClient::GetLightComponents() const
{
	return LightComponents;
}


void FCustomizableObjectEditorViewportClient::SetPlaybackSpeedMode(EMutableAnimationPlaybackSpeeds::Type InMode)
{
	AnimationPlaybackSpeedMode = InMode;

	if (const UWorld* World = GetWorld())
	{
		const float AnimationSpeed = (InMode == EMutableAnimationPlaybackSpeeds::Custom) ? GetCustomAnimationSpeed() : EMutableAnimationPlaybackSpeeds::Values[AnimationPlaybackSpeedMode];
		World->GetWorldSettings()->TimeDilation = AnimationSpeed;
	}
}


void FCustomizableObjectEditorViewportClient::SetCustomAnimationSpeed(float Speed)
{
	CustomAnimationSpeed = Speed;
	SetPlaybackSpeedMode(EMutableAnimationPlaybackSpeeds::Custom);
}


float FCustomizableObjectEditorViewportClient::GetCustomAnimationSpeed() const
{
	return CustomAnimationSpeed;
}


EMutableAnimationPlaybackSpeeds::Type FCustomizableObjectEditorViewportClient::GetPlaybackSpeedMode() const
{
	return AnimationPlaybackSpeedMode;
}


void FCustomizableObjectEditorViewportClient::OnShowDisplayInfo()
{
	bShowDisplayInfo = !bShowDisplayInfo;
	
	Invalidate();
}


bool FCustomizableObjectEditorViewportClient::IsShowingMeshInfo() const
{
	return bShowDisplayInfo;
}


void FCustomizableObjectEditorViewportClient::OnEnableClothSimulation()
{
	bDisableClothSimulation = !bDisableClothSimulation;
	
	for (TPair<FName, TWeakObjectPtr<UDebugSkelMeshComponent>>& Entry : SkeletalMeshComponents)
	{
		UDebugSkelMeshComponent* SkeletalMeshComponent = Entry.Value.Get();
		if (SkeletalMeshComponent)
		{
			SkeletalMeshComponent->bDisableClothSimulation = bDisableClothSimulation;
		}
	}

	Invalidate();
}


bool FCustomizableObjectEditorViewportClient::IsClothSimulationEnabled() const
{
	return bDisableClothSimulation;	
}


void FCustomizableObjectEditorViewportClient::OnDebugDrawPhysMeshWired()
{
	bShowDebugClothing = !bShowDebugClothing;
	
	Invalidate();
}


bool FCustomizableObjectEditorViewportClient::IsDebugDrawPhysMeshWired() const
{
	return bShowDebugClothing;
}


FText MergeLine(const FText& InText, const FText& InNewLine)
{
	if (InText.IsEmpty())
	{
		return InNewLine;
	}

	return FText::Format(LOCTEXT("ViewportTextNewlineFormatter", "{0}\n{1}"), InText, InNewLine);
}


// Based on FAnimationViewportClient::GetDisplayInfo(bool)
FText FCustomizableObjectEditorViewportClient::GetMeshInfoText() const
{
	FText TextValue;
	bool bFirst = true;

	for (const TPair<FName, TWeakObjectPtr<UDebugSkelMeshComponent>>& Entry : SkeletalMeshComponents)
	{
		UDebugSkelMeshComponent* PreviewMeshComponent = Entry.Value.Get();
		
		if (!PreviewMeshComponent)
		{
			continue;
		}

		FSkeletalMeshRenderData* SkelMeshResource = PreviewMeshComponent->GetSkeletalMeshRenderData();
		if (!SkelMeshResource)
		{
			continue;
		}

		// Draw stats about the mesh
		int32 NumBonesInUse;
		int32 NumBonesMappedToVerts;
		int32 NumSectionsInUse;

		const int32 LODIndex = FMath::Clamp(PreviewMeshComponent->GetPredictedLODLevel(), 0, SkelMeshResource->LODRenderData.Num() - 1);
		FSkeletalMeshLODRenderData& LODData = SkelMeshResource->LODRenderData[LODIndex];

		NumBonesInUse = LODData.RequiredBones.Num();
		NumBonesMappedToVerts = LODData.ActiveBoneIndices.Num();
		NumSectionsInUse = LODData.RenderSections.Num();

		// Calculate polys based on non clothing sections so we don't duplicate the counts.
		uint32 NumTotalTriangles = 0;
		int32 NumSections = LODData.RenderSections.Num();
		for(int32 SectionIndex = 0; SectionIndex < NumSections; SectionIndex++)
		{
			NumTotalTriangles += LODData.RenderSections[SectionIndex].NumTriangles;
		}

		if (!bFirst)
		{
			TextValue = FText::Format(LOCTEXT("MeshInfoComponentSeparation", "{0}\n"), TextValue);
		}
		bFirst = false;
		
		TextValue = MergeLine(TextValue, FText::Format(LOCTEXT("MeshInfoFormat", "Component: {0}, LOD: {1}, Bones: {2} (Mapped to Vertices: {3}), Polys: {4}"),
			FText::FromString(Entry.Key.ToString()),
			FText::AsNumber(LODIndex),
			FText::AsNumber(NumBonesInUse),
			FText::AsNumber(NumBonesMappedToVerts),
			FText::AsNumber(NumTotalTriangles)));

		for (int32 SectionIndex = 0; SectionIndex < LODData.RenderSections.Num(); SectionIndex++)
		{
			int32 SectionVerts = LODData.RenderSections[SectionIndex].GetNumVertices();

			FText SectionDisabledText = LODData.RenderSections[SectionIndex].bDisabled ? LOCTEXT("SectionIsDisbable", " Disabled") : FText::GetEmpty();
			TextValue = MergeLine(TextValue, FText::Format(LOCTEXT("SectionFormat", " [Section {0}]{1} Verts: {2}, Bones: {3}, Max Influences: {4}"),
				FText::AsNumber(SectionIndex),
				SectionDisabledText,
				FText::AsNumber(SectionVerts),
				FText::AsNumber(LODData.RenderSections[SectionIndex].BoneMap.Num()),
				FText::AsNumber(LODData.RenderSections[SectionIndex].MaxBoneInfluences)
				));
		}

		TextValue = MergeLine(TextValue, FText::Format(LOCTEXT("TotalVerts", "TOTAL Verts: {0}"),
			FText::AsNumber(LODData.GetNumVertices())));

		TextValue = MergeLine(TextValue, FText::Format(LOCTEXT("Sections", "Sections: {0}"),
			NumSectionsInUse
			));

		TArray<FTransform> LocalBoneTransforms = PreviewMeshComponent->GetBoneSpaceTransforms();
		if (PreviewMeshComponent->BonesOfInterest.Num() > 0)
		{
			int32 BoneIndex = PreviewMeshComponent->BonesOfInterest[0];
			FTransform ReferenceTransform = PreviewMeshComponent->GetReferenceSkeleton().GetRefBonePose()[BoneIndex];
			FTransform LocalTransform = LocalBoneTransforms[BoneIndex];
			FTransform ComponentTransform = PreviewMeshComponent->GetDrawTransform(BoneIndex);

			auto GetDisplayTransform = [](const FTransform& InTransform) -> FText
			{
				FRotator R(InTransform.GetRotation());
				FVector T(InTransform.GetTranslation());
				FVector S(InTransform.GetScale3D());

				FString Output = FString::Printf(TEXT("Rotation: X(Roll) %f Y(Pitch)  %f Z(Yaw) %f\r\n"), R.Roll, R.Pitch, R.Yaw);
				Output += FString::Printf(TEXT("Translation: %f %f %f\r\n"), T.X, T.Y, T.Z);
				Output += FString::Printf(TEXT("Scale3D: %f %f %f\r\n"), S.X, S.Y, S.Z);

				return FText::FromString(Output);
			};

			TextValue = MergeLine(TextValue, FText::Format(LOCTEXT("LocalTransform", "Local: {0}"), GetDisplayTransform(LocalTransform)));

			TextValue = MergeLine(TextValue, FText::Format(LOCTEXT("ComponentTransform", "Component: {0}"), GetDisplayTransform(ComponentTransform)));

			TextValue = MergeLine(TextValue, FText::Format(LOCTEXT("ReferenceTransform", "Reference: {0}"), GetDisplayTransform(ReferenceTransform)));
		}

		TextValue = MergeLine(TextValue, FText::Format(LOCTEXT("ApproximateSize", "Approximate Size: {0}x{1}x{2}"),
			FText::AsNumber(FMath::RoundToInt(PreviewMeshComponent->Bounds.BoxExtent.X * 2.0f)),
			FText::AsNumber(FMath::RoundToInt(PreviewMeshComponent->Bounds.BoxExtent.Y * 2.0f)),
			FText::AsNumber(FMath::RoundToInt(PreviewMeshComponent->Bounds.BoxExtent.Z * 2.0f))));

		uint32 NumNotiesWithErrors = PreviewMeshComponent->AnimNotifyErrors.Num();
		for (uint32 i = 0; i < NumNotiesWithErrors; ++i)
		{
			uint32 NumErrors = PreviewMeshComponent->AnimNotifyErrors[i].Errors.Num();
			for (uint32 ErrorIdx = 0; ErrorIdx < NumErrors; ++ErrorIdx)
			{
				TextValue = MergeLine(TextValue, FText::FromString(PreviewMeshComponent->AnimNotifyErrors[i].Errors[ErrorIdx]));
			}
		}	
	}
	
	return TextValue;
}


void FCustomizableObjectEditorViewportClient::ToggleShowNormals()
{
	bDrawNormals = !bDrawNormals;
	
	for (TPair<FName, TWeakObjectPtr<UDebugSkelMeshComponent>>& Entry : SkeletalMeshComponents)
	{
		UDebugSkelMeshComponent* SkeletalMeshComponent = Entry.Value.Get();
		if (SkeletalMeshComponent)
		{
			SkeletalMeshComponent->bDrawNormals = bDrawNormals;
			SkeletalMeshComponent->MarkRenderStateDirty();
		}
	}

	Invalidate();
}


bool FCustomizableObjectEditorViewportClient::IsSetShowNormalsChecked() const
{
	return bDrawNormals;
}


void FCustomizableObjectEditorViewportClient::ToggleShowTangents()
{
	bDrawTangents = !bDrawTangents;

	for (TPair<FName, TWeakObjectPtr<UDebugSkelMeshComponent>>& Entry : SkeletalMeshComponents)
	{
		UDebugSkelMeshComponent* SkeletalMeshComponent = Entry.Value.Get();
		if (SkeletalMeshComponent)
		{
			SkeletalMeshComponent->bDrawTangents = bDrawTangents;
			SkeletalMeshComponent->MarkRenderStateDirty();
		}
	}

	Invalidate();
}


bool FCustomizableObjectEditorViewportClient::IsSetShowTangentsChecked() const
{
	return bDrawTangents;
}


void FCustomizableObjectEditorViewportClient::ToggleShowBinormals()
{
	bDrawBinormals = !bDrawBinormals;
	
	for (TPair<FName, TWeakObjectPtr<UDebugSkelMeshComponent>>& Entry : SkeletalMeshComponents)
	{
		UDebugSkelMeshComponent* SkeletalMeshComponent = Entry.Value.Get();
		if (SkeletalMeshComponent)
		{
			SkeletalMeshComponent->bDrawBinormals = bDrawBinormals;
			SkeletalMeshComponent->MarkRenderStateDirty();
		}
	}
	
	GetWorld()->SendAllEndOfFrameUpdates();

	Invalidate();
}


bool FCustomizableObjectEditorViewportClient::IsSetShowBinormalsChecked() const
{
	return bDrawBinormals;
}


void FCustomizableObjectEditorViewportClient::DrawMeshBones(const UDebugSkelMeshComponent* MeshComponent, FPrimitiveDrawInterface* PDI)
{
	if (!MeshComponent ||
		!MeshComponent->GetSkeletalMeshAsset() ||
		MeshComponent->GetNumDrawTransform() == 0 ||
		MeshComponent->SkeletonDrawMode == ESkeletonDrawMode::Hidden)
	{
		return;
	}

	TArray<FTransform> WorldTransforms;
	WorldTransforms.AddUninitialized(MeshComponent->GetNumDrawTransform());

	TArray<FLinearColor> BoneColors;
	BoneColors.AddUninitialized(MeshComponent->GetNumDrawTransform());
	
	const TArray<FBoneIndexType>& DrawBoneIndices = MeshComponent->GetDrawBoneIndices();
	for (int32 Index = 0; Index < DrawBoneIndices.Num(); ++Index)
	{
		const int32 BoneIndex = DrawBoneIndices[Index];
		WorldTransforms[BoneIndex] = MeshComponent->GetDrawTransform(BoneIndex) * MeshComponent->GetComponentTransform();
		BoneColors[BoneIndex] = MeshComponent->GetBoneColor(BoneIndex);
	}

	// color virtual bones
	const FLinearColor VirtualBoneColor = GetDefault<UPersonaOptions>()->VirtualBoneColor;
	for (const int16 VirtualBoneIndex : MeshComponent->GetReferenceSkeleton().GetRequiredVirtualBones())
	{
		BoneColors[VirtualBoneIndex] = VirtualBoneColor;
	}

	constexpr bool bForceDraw = false;

	// don't allow selection if the skeleton draw mode is greyed out
	//const bool bAddHitProxy = MeshComponent->SkeletonDrawMode != ESkeletonDrawMode::GreyedOut;

	FSkelDebugDrawConfig DrawConfig;
	DrawConfig.BoneDrawMode = EBoneDrawMode::All;
	DrawConfig.BoneDrawSize = 1.0f;
	DrawConfig.bAddHitProxy = false;
	DrawConfig.bForceDraw = bForceDraw;
	DrawConfig.DefaultBoneColor = GetMutableDefault<UPersonaOptions>()->DefaultBoneColor;
	DrawConfig.AffectedBoneColor = GetMutableDefault<UPersonaOptions>()->AffectedBoneColor;
	DrawConfig.SelectedBoneColor = GetMutableDefault<UPersonaOptions>()->SelectedBoneColor;
	DrawConfig.ParentOfSelectedBoneColor = GetMutableDefault<UPersonaOptions>()->ParentOfSelectedBoneColor;

	//No user interaction right now
	TArray<TRefCountPtr<HHitProxy>> HitProxies;

	SkeletalDebugRendering::DrawBones(
		PDI,
		MeshComponent->GetComponentLocation(),
		DrawBoneIndices,
		MeshComponent->GetReferenceSkeleton(),
		WorldTransforms,
		MeshComponent->BonesOfInterest,
		BoneColors,
		HitProxies,
		DrawConfig
	);
}


void FCustomizableObjectEditorViewportClient::SetWidgetType(EWidgetType Type)
{
	WidgetType = Type;
	
	SetWidgetMode(UE::Widget::WM_Translate);
	Widget->SetDefaultVisibility(Type != EWidgetType::Hidden);
}	


/////////////////////////////////////////////////
// select folder dialog \todo: move to its own file
//////////////////////////////////////////////////
void SMutableSelectFolderDlg::Construct(const FArguments& InArgs)
{
	AssetPath = FText::FromString(FPackageName::GetLongPackagePath(InArgs._DefaultAssetPath.ToString()));
	FileName = InArgs._DefaultFileName;

	bExportAllResources = false;

	if (AssetPath.IsEmpty())
	{
		AssetPath = FText::FromString(TEXT("/Game"));
	}

	FPathPickerConfig PathPickerConfig;
	PathPickerConfig.DefaultPath = AssetPath.ToString();
	PathPickerConfig.OnPathSelected = FOnPathSelected::CreateSP(this, &SMutableSelectFolderDlg::OnPathChange);
	PathPickerConfig.bAddDefaultPath = true;

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	SWindow::Construct(SWindow::FArguments()
		.Title(LOCTEXT("SMutableSelectFolderDlg_Title", "Select target folder for baked resources"))
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		//.SizingRule( ESizingRule::Autosized )
		.ClientSize(FVector2D(450, 450))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot() // Add user input block
		.Padding(2)
		[
			SNew(SBorder)
			.BorderImage(UE_MUTABLE_GET_BRUSH("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SelectPath", "Select Path"))
			.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 14, "Regular"))
		]

	+ SVerticalBox::Slot()
		.FillHeight(1)
		.Padding(3)
		[
			ContentBrowserModule.Get().CreatePathPicker(PathPickerConfig)
		]

	+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("FileName", "File Name"))
			.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 14, "Regular"))
		]

	+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SEditableTextBox)
			.Text(InArgs._DefaultFileName)
			.OnTextCommitted(this, &SMutableSelectFolderDlg::OnNameChange)
			.MinDesiredWidth(250)
		]

		]
		]

	+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ExportAllUsedResources", "Export all used resources  "))
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 12, "Regular"))
				.ToolTipText(LOCTEXT("Export all used Resources", "All the resources used by the object will be baked/stored in the target folder. Otherwise, only the assets that Mutable modifies will be baked/stored."))
			]
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.AutoWidth()
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("ExportAllResources", "Export all resources"))
				.HAlign(HAlign_Right)
				.IsChecked(bExportAllResources ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.OnCheckStateChanged(this, &SMutableSelectFolderDlg::OnBoolParameterChanged)
			]
		]

	+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("GenerateConstantMaterialInstances", "Generate Constant Material Instances  "))
				.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 12, "Regular"))
				.ToolTipText(LOCTEXT("Generate Constant Material Instances", "All the material instances in the baked skeletal meshes will be constant instead of dynamic. They cannot be changed at runtime but they are lighter and required for UEFN."))
			]
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.AutoWidth()
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("GenerateConstantMaterialInstances_Checkbox", "Generate Constant Material Instances"))
				.HAlign(HAlign_Right)
				.IsChecked(bGenerateConstantMaterialInstances ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.OnCheckStateChanged(this, &SMutableSelectFolderDlg::OnConstantMaterialInstancesBoolParameterChanged)
			]
		]

	+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(5)
		[
			SNew(SUniformGridPanel)
			.SlotPadding(UE_MUTABLE_GET_MARGIN("StandardDialog.SlotPadding"))
		.MinDesiredSlotWidth(UE_MUTABLE_GET_FLOAT("StandardDialog.MinDesiredSlotWidth"))
		.MinDesiredSlotHeight(UE_MUTABLE_GET_FLOAT("StandardDialog.MinDesiredSlotHeight"))
		+ SUniformGridPanel::Slot(0, 0)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
		.ContentPadding(UE_MUTABLE_GET_MARGIN("StandardDialog.ContentPadding"))
		.Text(LOCTEXT("OK", "OK"))
		.OnClicked(this, &SMutableSelectFolderDlg::OnButtonClick, EAppReturnType::Ok)
		]
	+ SUniformGridPanel::Slot(1, 0)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
		.ContentPadding(UE_MUTABLE_GET_MARGIN("StandardDialog.ContentPadding"))
		.Text(LOCTEXT("Cancel", "Cancel"))
		.OnClicked(this, &SMutableSelectFolderDlg::OnButtonClick, EAppReturnType::Cancel)
		]
		]
		]);
}

void SMutableSelectFolderDlg::OnPathChange(const FString& NewPath)
{
	AssetPath = FText::FromString(NewPath);
}

FReply SMutableSelectFolderDlg::OnButtonClick(EAppReturnType::Type ButtonID)
{
	UserResponse = ButtonID;

	RequestDestroyWindow();

	return FReply::Handled();
}


void SMutableSelectFolderDlg::OnNameChange(const FText& NewName, ETextCommit::Type CommitInfo)
{
	FileName = NewName;
}


void SMutableSelectFolderDlg::OnBoolParameterChanged(ECheckBoxState InCheckboxState)
{
	bExportAllResources = InCheckboxState == ECheckBoxState::Checked;
}


void SMutableSelectFolderDlg::OnConstantMaterialInstancesBoolParameterChanged(ECheckBoxState InCheckboxState)
{
	bGenerateConstantMaterialInstances = InCheckboxState == ECheckBoxState::Checked;
}


EAppReturnType::Type SMutableSelectFolderDlg::ShowModal()
{
	GEditor->EditorAddModalWindow(SharedThis(this));
	return UserResponse;
}

FString SMutableSelectFolderDlg::GetAssetPath()
{
	return AssetPath.ToString();
}


FString SMutableSelectFolderDlg::GetFileName()
{
	return FileName.ToString();
}


bool SMutableSelectFolderDlg::GetExportAllResources() const
{
	return bExportAllResources;
}


bool SMutableSelectFolderDlg::GetGenerateConstantMaterialInstances() const
{
	return bGenerateConstantMaterialInstances;
}

#undef LOCTEXT_NAMESPACE 

