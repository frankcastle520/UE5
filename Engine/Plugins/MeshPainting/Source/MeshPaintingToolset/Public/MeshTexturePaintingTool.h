// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseTools/BaseBrushTool.h"
#include "BaseMeshPaintingToolProperties.h"
#include "MeshPaintingToolsetTypes.h"
#include "MeshPaintInteractions.h"
#include "MeshTexturePaintingTool.generated.h"

enum class EMeshPaintModeAction : uint8;
enum class EToolShutdownType : uint8;
class FScopedTransaction;
class IMeshPaintComponentAdapter;
class UMeshToolManager;
class UTexture2D;
struct FPaintRayResults;
struct FPaintTexture2DData;
struct FTexturePaintMeshSectionInfo;
struct FToolBuilderState;


/**
 * Builder for the texture color mesh paint tool.
 */
UCLASS()
class MESHPAINTINGTOOLSET_API UMeshTextureColorPaintingToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

	TWeakObjectPtr<UMeshToolManager> SharedMeshToolData;
};

/**
 * Builder for the texture asset mesh paint tool.
 */
UCLASS()
class MESHPAINTINGTOOLSET_API UMeshTextureAssetPaintingToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

	TWeakObjectPtr<UMeshToolManager> SharedMeshToolData;
};


/**
 * Base class for mesh texture paint properties.
 */
UCLASS()
class MESHPAINTINGTOOLSET_API UMeshTexturePaintingToolProperties : public UMeshPaintingToolProperties
{
	GENERATED_BODY()

public:
	/** Seam painting flag, True if we should enable dilation to allow the painting of texture seams */
	UPROPERTY(EditAnywhere, Category = TexturePainting)
	bool bEnableSeamPainting = false;

	/** Optional Texture Brush to which Painting should use */
	UPROPERTY(EditAnywhere, Category = TexturePainting, meta = (DisplayThumbnail = "true", TransientToolProperty))
	TObjectPtr<UTexture2D> PaintBrush = nullptr;

	/** Initial Rotation offset to apply to our paint brush */
	UPROPERTY(EditAnywhere, Category = TexturePainting, meta = (TransientToolProperty, UIMin = "0.0", UIMax = "360.0", ClampMin = "0.0", ClampMax = "360.0"))
	float PaintBrushRotationOffset = 0.0f;

	/** Whether or not to continously rotate the brush towards the painting direction */
	UPROPERTY(EditAnywhere, Category = TexturePainting, meta = (TransientToolProperty))
	bool bRotateBrushTowardsDirection = false;

	/** Whether or not to apply Texture Color Painting to the Red Channel */
	UPROPERTY(EditAnywhere, Category = ColorPainting, meta = (DisplayName = "Red"))
	bool bWriteRed = true;

	/** Whether or not to apply Texture Color Painting to the Green Channel */
	UPROPERTY(EditAnywhere, Category = ColorPainting, meta = (DisplayName = "Green"))
	bool bWriteGreen = true;

	/** Whether or not to apply Texture Color Painting to the Blue Channel */
	UPROPERTY(EditAnywhere, Category = ColorPainting, meta = (DisplayName = "Blue"))
	bool bWriteBlue = true;

	/** Whether or not to apply Texture Color Painting to the Alpha Channel */
	UPROPERTY(EditAnywhere, Category = ColorPainting, meta = (DisplayName = "Alpha"))
	bool bWriteAlpha = false;
};

/**
 * Class for texture color paint properties.
 */
UCLASS()
class MESHPAINTINGTOOLSET_API UMeshTextureColorPaintingToolProperties : public UMeshTexturePaintingToolProperties
{
	GENERATED_BODY()

public:
	/** Whether to copy all texture color painting to vertex colors. */
	UPROPERTY(EditAnywhere, Category = ColorPainting)
	bool bPropagateToVertexColor = false;
};

/**
 * Class for texture asset paint properties.
 */
UCLASS()
class MESHPAINTINGTOOLSET_API UMeshTextureAssetPaintingToolProperties : public UMeshTexturePaintingToolProperties
{
	GENERATED_BODY()

public:
	/** UV channel which should be used for painting textures. */
	UPROPERTY(EditAnywhere, Category = TexturePainting, meta = (TransientToolProperty))
	int32 UVChannel = 0;

	/** Texture to which painting should be applied. */
	UPROPERTY(EditAnywhere, Category = TexturePainting, meta = (DisplayThumbnail = "true", TransientToolProperty))
	TObjectPtr<UTexture2D> PaintTexture;
};


/**
 * Base class for mesh texture painting tool.
 */
UCLASS(Abstract)
class MESHPAINTINGTOOLSET_API UMeshTexturePaintingTool : public UBaseBrushTool, public IMeshPaintSelectionInterface
{
	GENERATED_BODY()

public:
	UMeshTexturePaintingTool();

	DECLARE_DELEGATE_OneParam(FOnPaintingFinishedDelegate, UMeshComponent*);
	FOnPaintingFinishedDelegate& OnPaintingFinished() { return OnPaintingFinishedDelegate; }

	void FloodCurrentPaintTexture();

	virtual void GetModifiedTexturesToSave(TArray<UObject*>& OutTexturesToSave) const {}
	virtual int32 GetSelectedUVChannel(UMeshComponent const* InMeshComponent) const { return 0; }

protected:
	// Begin UInteractiveTool Interface.
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void OnTick(float DeltaTime) override;
	virtual bool HasCancel() const override { return false; }
	virtual bool HasAccept() const override { return false; }
	virtual bool CanAccept() const override { return false; }
	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;
	virtual void OnUpdateModifierState(int ModifierID, bool bIsOn) override;
	virtual void OnBeginDrag(const FRay& Ray) override;
	virtual void OnUpdateDrag(const FRay& Ray) override;
	virtual void OnEndDrag(const FRay& Ray) override;
	virtual	bool HitTest(const FRay& Ray, FHitResult& OutHit) override;
	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;
	virtual double EstimateMaximumTargetDimension() override;
	// End UInteractiveTool Interface.

	FPaintTexture2DData* GetPaintTargetData(const UTexture2D* InTexture);
	FPaintTexture2DData* AddPaintTargetData(UTexture2D* InTexture);
	
	void SetAllTextureOverrides();
	void ClearAllTextureOverrides();

	virtual UTexture2D* GetSelectedPaintTexture(UMeshComponent const* InMeshComponent) const { return nullptr; }
	virtual void CacheTexturePaintData() {}
	virtual bool CanPaintTextureToComponent(UTexture* InTexture, UMeshComponent const* InMeshComponent) const { return false; }

private:
	void CacheSelectionData();
	void AddTextureOverrideToComponent(FPaintTexture2DData& TextureData, UMeshComponent* MeshComponent, const IMeshPaintComponentAdapter* MeshPaintAdapter = nullptr);
	double CalculateTargetEdgeLength(int TargetTriCount);
	void StartPaintingTexture(UMeshComponent* InMeshComponent, const IMeshPaintComponentAdapter& GeometryInfo);
	void UpdateResult();
	void GatherTextureTriangles(IMeshPaintComponentAdapter* Adapter, int32 TriangleIndex, const int32 VertexIndices[3], TArray<FTexturePaintTriangleInfo>* TriangleInfo, TArray<FTexturePaintMeshSectionInfo>* SectionInfos, int32 UVChannelIndex);
	bool Paint(const FVector& InRayOrigin, const FVector& InRayDirection);
	bool Paint(const TArrayView<TPair<FVector, FVector>>& Rays);
	void PaintTexture(FMeshPaintParameters& InParams, int32 UVChannel, TArray<FTexturePaintTriangleInfo>& InInfluencedTriangles, const IMeshPaintComponentAdapter& GeometryInfo, FMeshPaintParameters* LastParams = nullptr);
	bool PaintInternal(const TArrayView<TPair<FVector, FVector>>& Rays, EMeshPaintModeAction PaintAction, float PaintStrength);
	void FinishPaintingTexture();
	void FinishPainting();

protected:
	/** Textures eligible for painting retrieved from the current selection */
	TArray<FPaintableTexture> PaintableTextures;

	/** Stores data associated with our paint target textures */
	UPROPERTY(Transient)
	TMap<TObjectPtr<UTexture2D>, FPaintTexture2DData> PaintTargetData;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMeshPaintSelectionMechanic> SelectionMechanic;

	UPROPERTY(Transient)
	TObjectPtr<UMeshTexturePaintingToolProperties> TextureProperties;

	/** The original texture that we're painting */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> PaintingTexture2D;

	/** The mesh component that we're currently painting on */
	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> TexturePaintingCurrentMeshComponent;

	/** Hold the transaction while we are painting */
	TUniquePtr<FScopedTransaction> PaintingTransaction;

	double InitialMeshArea = 0;
	bool bArePainting = false;
	bool bResultValid = false;
	bool bStampPending = false;
	bool bInDrag = false;
	bool bRequestPaintBucketFill = false;

	bool bCachedClickRay = false;
	FRay PendingStampRay;
	FRay PendingClickRay;
	FVector2D PendingClickScreenPosition;
	
	TArray<FPaintRayResults> LastPaintRayResults;
	FHitResult LastBestHitResult;

	FOnPaintingFinishedDelegate OnPaintingFinishedDelegate;
};

/**
 * Class for texture color painting tool.
 * This paints to special textures stored on the mesh components.
 * Behavior should be similar to vertex painting (per instance painting stored on components).
 * But painting texture colors instead of vertex colors is a better fit for very dense mesh types such as used by nanite.
 */
UCLASS()
class MESHPAINTINGTOOLSET_API UMeshTextureColorPaintingTool : public UMeshTexturePaintingTool
{
	GENERATED_BODY()

public:
	UMeshTextureColorPaintingTool();

protected:
	// Begin UInteractiveTool Interface.
	virtual void Setup() override;
	// End UInteractiveTool Interface.

	// Begin UMeshTexturePaintingTool Interface.
	virtual bool AllowsMultiselect() const override { return true; }
	virtual bool IsMeshAdapterSupported(TSharedPtr<IMeshPaintComponentAdapter> MeshAdapter) const override;
	virtual UTexture2D* GetSelectedPaintTexture(UMeshComponent const* InMeshComponent) const override;
	virtual int32 GetSelectedUVChannel(UMeshComponent const* InMeshComponent) const override;
	virtual void GetModifiedTexturesToSave(TArray<UObject*>& OutTexturesToSave) const override;
	virtual void CacheTexturePaintData() override;
	virtual bool CanPaintTextureToComponent(UTexture* InTexture, UMeshComponent const* InMeshComponent) const override;
	// End UMeshTexturePaintingTool Interface.

private:
	UPROPERTY(Transient)
	TObjectPtr<UMeshTextureColorPaintingToolProperties> ColorProperties;

	UPROPERTY(Transient)
	TObjectPtr<UTexture> MeshPaintDummyTexture;
};

/**
 * Class for texture asset painting tool.
 * This paints to texture assets directly from the mesh.
 * The texture asset to paint is selected from the ones referenced in the mesh component's materials.
 */
UCLASS()
class MESHPAINTINGTOOLSET_API UMeshTextureAssetPaintingTool : public UMeshTexturePaintingTool
{
	GENERATED_BODY()

public:
	UMeshTextureAssetPaintingTool();
	
	/** Change selected texture to previous or next available. */
	void CycleTextures(int32 Direction);

	/** Get the selected paint texture, and return the modified overriden texture if currently painting. */
	UTexture* GetSelectedPaintTextureWithOverride() const;

	/** Returns true if asset shouldn't be shown in UI because it is not in our paintable texture array. */
	bool ShouldFilterTextureAsset(const FAssetData& AssetData) const;

	virtual int32 GetSelectedUVChannel(UMeshComponent const* InMeshComponent) const override;

protected:
	// Begin UInteractiveTool Interface.
	virtual void Setup() override;
	// End UInteractiveTool Interface.

	// Begin UMeshTexturePaintingTool Interface.
	virtual bool AllowsMultiselect() const override { return false; }
	virtual bool IsMeshAdapterSupported(TSharedPtr<IMeshPaintComponentAdapter> MeshAdapter) const override;
	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;
	virtual UTexture2D* GetSelectedPaintTexture(UMeshComponent const* InMeshComponent) const override;
	virtual void GetModifiedTexturesToSave(TArray<UObject*>& OutTexturesToSave) const override;
	virtual void CacheTexturePaintData() override;
	virtual bool CanPaintTextureToComponent(UTexture* InTexture, UMeshComponent const* InMeshComponent) const override;
	// End UMeshTexturePaintingTool Interface.

private:
	UPROPERTY(Transient)
	TObjectPtr<UMeshTextureAssetPaintingToolProperties> AssetProperties;
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "Components/MeshComponent.h"
#include "CoreMinimal.h"
#include "Delegates/DelegateCombinations.h"
#include "MeshVertexPaintingTool.h"
#include "TexturePaintToolset.h"
#include "UObject/NoExportTypes.h"
#endif
