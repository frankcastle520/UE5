// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AdvancedPreviewScene.h"
#include "Math/Transform.h"
#include "ClothEditorPreviewScene.generated.h"

class UChaosClothAsset;
class USkeletalMesh;
class UChaosClothComponent;
class FAssetEditorModeManager;
class UAnimationAsset;
class UAnimSingleNodeInstance;
class FTransformGizmoDataBinder;

namespace UE::Chaos::ClothAsset
{
class FChaosClothPreviewScene;
}

DECLARE_EVENT(UChaosClothPreviewSceneDescription, FClothPreviewSceneDescriptionChanged)

///
/// The UChaosClothPreviewSceneDescription is a description of the Preview scene contents, intended to be editable in an FAdvancedPreviewSettingsWidget
/// 
UCLASS()
class CHAOSCLOTHASSETEDITOR_API UChaosClothPreviewSceneDescription : public UObject
{
public:
	GENERATED_BODY()

	FClothPreviewSceneDescriptionChanged ClothPreviewSceneDescriptionChanged;

	UChaosClothPreviewSceneDescription()
	{
		SetFlags(RF_Transactional);
	}

	void SetPreviewScene(UE::Chaos::ClothAsset::FChaosClothPreviewScene* PreviewScene);

	/* Whether the preview viewport should pause animation and simulation while Play In Editor (PIE) or Simulate In Editor is active */
	UPROPERTY(EditAnywhere, Transient, Category = "Viewport")
	bool bPauseWhilePlayingInEditor = true;

	// Skeletal Mesh source asset
	UPROPERTY(EditAnywhere, Transient, Category="SkeletalMesh")
	TObjectPtr<USkeletalMesh> SkeletalMeshAsset;

	UPROPERTY(EditAnywhere, Transient, Category = "SkeletalMesh")
	TObjectPtr<UAnimationAsset> AnimationAsset;

	UPROPERTY(EditAnywhere, Transient, Category = "SkeletalMesh")
	bool bPostProcessBlueprint;

	UPROPERTY(EditAnywhere, Transient, Category = "Transform", Meta = (EditCondition = "bValidSelectionForTransform", HideEditConditionToggle))
	FVector3d Translation = FVector3d::ZeroVector;

	UPROPERTY(EditAnywhere, Transient, Category = "Transform", Meta = (EditCondition = "bValidSelectionForTransform", HideEditConditionToggle))
	FVector3d Rotation = FVector3d::ZeroVector;

	UPROPERTY(EditAnywhere, Transient, Category = "Transform", Meta = (AllowPreserveRatio, EditCondition = "bValidSelectionForTransform", HideEditConditionToggle))
	FVector3d Scale = FVector3d::OneVector;

	UPROPERTY(EditAnywhere, Transient, Category = "ClothComponent", Meta = (UIMin = 0.0, UIMax = 10.0, ClampMin = 0.0, ClampMax = 10000.0))
	float SolverGeometryScale = 1.f;

	UPROPERTY(Transient)
	bool bValidSelectionForTransform = false;

	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;

private:

	virtual void PostTransacted(const FTransactionObjectEvent& TransactionEvent) override;

	UE::Chaos::ClothAsset::FChaosClothPreviewScene* PreviewScene;
};


namespace UE::Chaos::ClothAsset
{
///
/// FChaosClothPreviewScene is the actual Preview scene, with contents specified by the SceneDescription
/// 
class CHAOSCLOTHASSETEDITOR_API FChaosClothPreviewScene : public FAdvancedPreviewScene
{
public:

	FChaosClothPreviewScene(FPreviewScene::ConstructionValues ConstructionValues);
	virtual ~FChaosClothPreviewScene();

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	const UChaosClothPreviewSceneDescription* GetPreviewSceneDescription() const { return PreviewSceneDescription; }
	UChaosClothPreviewSceneDescription* GetPreviewSceneDescription() { return PreviewSceneDescription; }

	void SetClothAsset(UChaosClothAsset* Asset);

	// Update Scene in response to the SceneDescription changing
	void SceneDescriptionPropertyChanged(const FName& PropertyName);

	UAnimSingleNodeInstance* GetPreviewAnimInstance();
	const UAnimSingleNodeInstance* const GetPreviewAnimInstance() const;

	UChaosClothComponent* GetClothComponent();
	const UChaosClothComponent* GetClothComponent() const;
	
	const USkeletalMeshComponent* GetSkeletalMeshComponent() const;

	void SetModeManager(TSharedPtr<FAssetEditorModeManager> InClothPreviewEditorModeManager);
	const TSharedPtr<const FAssetEditorModeManager> GetClothPreviewEditorModeManager() const;

	void SetGizmoDataBinder(TSharedPtr<FTransformGizmoDataBinder> InDataBinder);

private:

	virtual void Tick(float DeltaT) override;

	// Create the PreviewAnimationInstance if the AnimationAsset and SkeletalMesh both exist, and set the animation to run on the SkeletalMeshComponent
	void UpdateSkeletalMeshAnimation();

	// Attach the cloth component to the skeletal mesh component, if it exists
	void UpdateClothComponentAttachment();

	bool IsComponentSelected(const UPrimitiveComponent* InComponent);

	void SaveAnimationState();
	void RestoreSavedAnimationState();
	void HandlePackageReloaded(const EPackageReloadPhase InPackageReloadPhase, FPackageReloadedEvent* InPackageReloadedEvent);
	void HandleReimportManagerPostReimport(UObject* ReimportedObject, bool bWasSuccessful);

	TObjectPtr<UChaosClothPreviewSceneDescription> PreviewSceneDescription;

	TSharedPtr<FAssetEditorModeManager> ClothPreviewEditorModeManager;

	TObjectPtr<AActor> SceneActor;

	TObjectPtr<UChaosClothComponent> ClothComponent;

	TObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent;

	TSharedPtr<FTransformGizmoDataBinder> DataBinder = nullptr;

	struct FAnimState
	{
		float Time;
		bool bIsReverse;
		bool bIsLooping;
		bool bIsPlaying;
	};
	TOptional<FAnimState> SavedAnimState;

	FDelegateHandle OnPackageReloadedDelegateHandle;
	FDelegateHandle OnPostReimportDelegateHandle;
};
} // namespace UE::Chaos::ClothAsset

