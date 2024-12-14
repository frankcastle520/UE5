// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorSubsystem.h"
#include "MeshPaintModeHelpers.generated.h"

class FMeshPaintParameters;
class UImportVertexColorOptions;
class UTexture2D;
class UStaticMeshComponent;
class UStaticMesh;
class USkeletalMesh;
class IMeshPaintComponentAdapter;
class UPaintBrushSettings;
class FEditorViewportClient;
class UMeshComponent;
class USkeletalMeshComponent;
class UViewportInteractor;
class FViewport;
class FPrimitiveDrawInterface;
class FSceneView;
class UInteractiveTool;

struct FStaticMeshComponentLODInfo;
struct FPerComponentVertexColorData;

UENUM()
enum class EMeshPaintActiveMode : uint8
{
	VertexColor UMETA(DisplayName = "VertexColor"),
	VertexWeights UMETA(DisplayName = "VertexWeights"),
	TextureColor UMETA(DisplayName = "TextureColor"),
	Texture UMETA(DisplayName = "Texture"),
};

enum class EMeshPaintDataColorViewMode : uint8;

class MESHPAINTEDITORMODE_API UMeshPaintModeSubsystem : public UEditorSubsystem
{
public:
	/** Forces the Viewport Client to render using the given Viewport Color ViewMode */
	void SetViewportColorMode(EMeshPaintActiveMode ActiveMode, EMeshPaintDataColorViewMode ColorViewMode, FEditorViewportClient* ViewportClient, UInteractiveTool const* ActiveTool);

	/** Sets whether or not the level viewport should be real time rendered move or viewport as parameter? */
	void SetRealtimeViewport(bool bRealtime);


	/** Helper function to import Vertex Colors from a Texture to the specified MeshComponent (makes use of SImportVertexColorsOptions Widget) */
	void ImportVertexColorsFromTexture(UMeshComponent* MeshComponent);

	/** Imports vertex colors from a Texture to the specified Skeletal Mesh according to user-set options */
	void ImportVertexColorsToSkeletalMesh(USkeletalMesh* SkeletalMesh, const UImportVertexColorOptions* Options, UTexture2D* Texture);

	/** Helper function to import Vertex Colors from a the MeshPaintTexture on the mesh component */
	void ImportVertexColorsFromMeshPaintTexture(UMeshComponent* MeshComponent);

	/** Helper function to import the MeshPaintTexture on the mesh component from the vertex colors */
	void ImportMeshPaintTextureFromVertexColors(UMeshComponent* MeshComponent);

	struct FPaintRay
	{
		FVector CameraLocation;
		FVector RayStart;
		FVector RayDirection;
		UViewportInteractor* ViewportInteractor;
	};


	bool RetrieveViewportPaintRays(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI, TArray<FPaintRay>& OutPaintRays);

	/** Imports vertex colors from a Texture to the specified Static Mesh according to user-set options */
	void ImportVertexColorsToStaticMesh(UStaticMesh* StaticMesh, const UImportVertexColorOptions* Options, UTexture2D* Texture);

	/** Imports vertex colors from a Texture to the specified Static Mesh Component according to user-set options */
	void ImportVertexColorsToStaticMeshComponent(UStaticMeshComponent* StaticMeshComponent, const UImportVertexColorOptions* Options, UTexture2D* Texture);

	void PropagateVertexColors(const TArray<UStaticMeshComponent *> StaticMeshComponents);
	bool CanPropagateVertexColors(TArray<UStaticMeshComponent*>& StaticMeshComponents, TArray<UStaticMesh*>& StaticMeshes, int32 NumInstanceVertexColorBytes);
	void CopyVertexColors(const TArray<UStaticMeshComponent*> StaticMeshComponents, TArray<FPerComponentVertexColorData>& CopiedVertexColors);
	bool CanCopyInstanceVertexColors(const TArray<UStaticMeshComponent*>& StaticMeshComponents, int32 PaintingMeshLODIndex);
	void PasteVertexColors(const TArray<UStaticMeshComponent*>& StaticMeshComponents, TArray<FPerComponentVertexColorData>& CopiedColorsByComponent);
	bool CanPasteInstanceVertexColors(const TArray<UStaticMeshComponent*>& StaticMeshComponents, const TArray<FPerComponentVertexColorData>& CopiedColorsByComponent);
	void RemovePerLODColors(const TArray<UMeshComponent*>& PaintableComponents);
	
	bool CanFixTextureColors(const TArray<UMeshComponent*>& Components);
	void FixTextureColors(const TArray<UMeshComponent*>& Components);

	void SwapColors();
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "IMeshPaintComponentAdapter.h"
#include "MeshPaintingToolsetTypes.h"
#endif
