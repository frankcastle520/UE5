// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseTools/BaseBrushTool.h"
#include "BaseMeshPaintingToolProperties.h"
#include "MeshPaintingToolsetTypes.h"
#include "MeshPaintInteractions.h"
#include "MeshVertexPaintingTool.generated.h"

enum class EMeshPaintModeAction : uint8;
enum class EToolShutdownType : uint8;
struct FPerVertexPaintActionArgs;
struct FToolBuilderState;
class IMeshPaintComponentAdapter;

struct FPaintRayResults
{
	FMeshPaintParameters Params;
	FHitResult BestTraceResult;
};

UENUM()
enum class EMeshPaintWeightTypes : uint8
{
	/** Lerp Between Two Textures using Alpha Value */
	AlphaLerp = 2 UMETA(DisplayName = "Alpha (Two Textures)"),

	/** Weighting Three Textures according to Channels*/
	RGB = 3 UMETA(DisplayName = "RGB (Three Textures)"),

	/**  Weighting Four Textures according to Channels*/
	ARGB = 4 UMETA(DisplayName = "ARGB (Four Textures)"),

	/**  Weighting Five Textures according to Channels */
	OneMinusARGB = 5 UMETA(DisplayName = "ARGB - 1 (Five Textures)")
};

UENUM()
enum class EMeshPaintTextureIndex : uint8
{
	TextureOne = 0,
	TextureTwo,
	TextureThree,
	TextureFour,
	TextureFive
};


/**
 *
 */
UCLASS()
class MESHPAINTINGTOOLSET_API UMeshVertexColorPaintingToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};

UCLASS()
class MESHPAINTINGTOOLSET_API UMeshVertexWeightPaintingToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};


UCLASS()
class MESHPAINTINGTOOLSET_API UMeshVertexPaintingToolProperties : public UMeshPaintingToolProperties
{
	GENERATED_BODY()

public:
	UMeshVertexPaintingToolProperties();

	/** When unchecked the painting on the base LOD will be propagate automatically to all other LODs when exiting the mode or changing the selection */
	UPROPERTY(EditAnywhere, Category = VertexPainting, meta = (InlineEditConditionToggle, TransientToolProperty))
	bool bPaintOnSpecificLOD = false;

	/** Index of LOD to paint. If not set then paint is applied to all LODs. */
	UPROPERTY(EditAnywhere, Category = VertexPainting, meta = (UIMin = "0", ClampMin = "0", EditCondition = "bPaintOnSpecificLOD", TransientToolProperty))
	int32 LODIndex = 0;

	/** Size of vertex points drawn when mesh painting is active. */
	UPROPERTY(EditAnywhere, Category = VertexPainting, meta = (UIMin = "0", ClampMin = "0"))
	float VertexPreviewSize;
};

UCLASS()
class MESHPAINTINGTOOLSET_API UMeshVertexColorPaintingToolProperties : public UMeshVertexPaintingToolProperties
{
	GENERATED_BODY()

public:
	/** Whether or not to apply Vertex Color Painting to the Red Channel */
	UPROPERTY(EditAnywhere, Category = ColorPainting, DisplayName = "Red")
	bool bWriteRed = true;

	/** Whether or not to apply Vertex Color Painting to the Green Channel */
	UPROPERTY(EditAnywhere, Category = ColorPainting, DisplayName = "Green")
	bool bWriteGreen = true;

	/** Whether or not to apply Vertex Color Painting to the Blue Channel */
	UPROPERTY(EditAnywhere, Category = ColorPainting, DisplayName = "Blue")
	bool bWriteBlue = true;

	/** Whether or not to apply Vertex Color Painting to the Alpha Channel */
	UPROPERTY(EditAnywhere, Category = ColorPainting, DisplayName = "Alpha")
	bool bWriteAlpha = false;
};

UCLASS()
class MESHPAINTINGTOOLSET_API UMeshVertexWeightPaintingToolProperties : public UMeshVertexPaintingToolProperties
{
	GENERATED_BODY()

public:
	UMeshVertexWeightPaintingToolProperties();

	/** Texture Blend Weight Painting Mode */
	UPROPERTY(EditAnywhere, Category = WeightPainting, meta = (EnumCondition = 1))
	EMeshPaintWeightTypes TextureWeightType;

	/** Texture Blend Weight index which should be applied during Painting */
	UPROPERTY(EditAnywhere, Category = WeightPainting, meta = (EnumCondition = 1))
	EMeshPaintTextureIndex PaintTextureWeightIndex;

	/** Texture Blend Weight index which should be erased during Painting */
	UPROPERTY(EditAnywhere, Category = WeightPainting, meta = (EnumCondition = 1))
	EMeshPaintTextureIndex EraseTextureWeightIndex;
};


UCLASS(Abstract)
class MESHPAINTINGTOOLSET_API UMeshVertexPaintingTool : public UBaseBrushTool, public IMeshPaintSelectionInterface
{
	GENERATED_BODY()

public:
	UMeshVertexPaintingTool();

	void PaintLODChanged();
	void LODPaintStateChanged(const bool bLODPaintingEnabled);

	int32 GetMaxLODIndexToPaint() const;
	int32 GetCachedLODIndex() const { return CachedLODIndex; }

	void CycleMeshLODs(int32 Direction);

	FSimpleDelegate& OnPaintingFinished() { return OnPaintingFinishedDelegate; }

protected:
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void OnTick(float DeltaTime) override;
	virtual bool HasCancel() const override { return false; }
	virtual bool HasAccept() const override { return false; }
	virtual bool CanAccept() const override { return false; }
	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;
	virtual	void OnUpdateModifierState(int ModifierID, bool bIsOn) override;
	virtual void OnBeginDrag(const FRay& Ray) override;
	virtual void OnUpdateDrag(const FRay& Ray) override;
	virtual void OnEndDrag(const FRay& Ray) override;
	virtual	bool HitTest(const FRay& Ray, FHitResult& OutHit) override;
	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;
	virtual bool AllowsMultiselect() const override { return true; }
	virtual bool IsMeshAdapterSupported(TSharedPtr<IMeshPaintComponentAdapter> MeshAdapter) const override;
	virtual double EstimateMaximumTargetDimension() override;

	virtual void SetAdditionalPaintParameters(FMeshPaintParameters& InPaintParameters) {};

private:
	void CacheSelectionData();
	void ApplyForcedLODIndex(int32 ForcedLODIndex);
	void UpdateResult();
	bool Paint(const FVector& InRayOrigin, const FVector& InRayDirection);
	bool Paint(const TArrayView<TPair<FVector, FVector>>& Rays);
	bool PaintInternal(const TArrayView<TPair<FVector, FVector>>& Rays, EMeshPaintModeAction PaintAction, float PaintStrength);
	void ApplyVertexData(FPerVertexPaintActionArgs& InArgs, int32 VertexIndex, FMeshPaintParameters Parameters);
	void FinishPainting();
	double CalculateTargetEdgeLength(int TargetTriCount);

private:
	UPROPERTY(Transient)
	TObjectPtr<UMeshPaintSelectionMechanic> SelectionMechanic;

	UPROPERTY(Transient)
	TObjectPtr<UMeshVertexPaintingToolProperties> VertexProperties;

	/** Current LOD index used for painting / forcing */
	int32 CachedLODIndex;
	/** Whether or not a specific LOD level should be forced */
	bool bCachedForceLOD;

	double InitialMeshArea = 0;
	bool bArePainting = false;
	bool bResultValid = false;
	bool bStampPending = false;
	bool bInDrag = false;
	
	bool bCachedClickRay = false;
	FRay PendingStampRay;
	FRay PendingClickRay;
	FVector2D PendingClickScreenPosition;
	FHitResult LastBestHitResult;
	
	FSimpleDelegate OnPaintingFinishedDelegate;
};

UCLASS()
class MESHPAINTINGTOOLSET_API UMeshVertexColorPaintingTool : public UMeshVertexPaintingTool
{
	GENERATED_BODY()

public:
	UMeshVertexColorPaintingTool();

protected:
	virtual void Setup() override;

	virtual void SetAdditionalPaintParameters(FMeshPaintParameters& InPaintParameters) override;
	
private:
	UPROPERTY(Transient)
	TObjectPtr<UMeshVertexColorPaintingToolProperties> ColorProperties;
};

UCLASS()
class MESHPAINTINGTOOLSET_API UMeshVertexWeightPaintingTool : public UMeshVertexPaintingTool
{
	GENERATED_BODY()

public:
	UMeshVertexWeightPaintingTool();

protected:
	virtual void Setup() override;
	
	virtual void SetAdditionalPaintParameters(FMeshPaintParameters& InPaintParameters);

private:
	UPROPERTY(Transient)
	TObjectPtr<UMeshVertexWeightPaintingToolProperties> WeightProperties;
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "Components/MeshComponent.h"
#include "CoreMinimal.h"
#include "MeshPaintHelpers.h"
#include "UObject/NoExportTypes.h"
#endif
