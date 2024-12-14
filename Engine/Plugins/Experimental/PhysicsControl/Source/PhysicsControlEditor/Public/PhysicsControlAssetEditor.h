// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "IHasPersonaToolkit.h"
#include "IPersonaPreviewScene.h"
#include "IPersonaViewport.h"
#include "PersonaAssetEditorToolkit.h"
#include "BodySetupEnums.h"
#include "Preferences/PhysicsAssetEditorOptions.h"

class FPhysicsControlAssetEditorEditMode;
class FPhysicsControlAssetEditorData;
class UAnimPreviewInstance;
class UPhysicsControlAsset;
class ISkeletonTree;
class FPhysicsControlAssetEditorSkeletonTreeBuilder;
class FUICommandList_Pinnable;

namespace PhysicsControlAssetEditorModes
{
	extern const FName PhysicsControlAssetEditorMode;
}

/**
 * The main toolkit/editor for working with Physics Control Assets
 */
class PHYSICSCONTROLEDITOR_API FPhysicsControlAssetEditor :
	public FPersonaAssetEditorToolkit,
	public IHasPersonaToolkit,
	public FGCObject,
	public FEditorUndoClient,
	public FTickableEditorObject
{
public:
	friend class FPhysicsControlAssetApplicationMode;
	friend class FPhysicsControlAssetEditorEditMode;
	friend class FPhysicsControlAssetProfileDetailsCustomization;
	friend class FPhysicsControlAssetPreviewDetailsCustomization;
	friend class FPhysicsControlAssetSetupDetailsCustomization;
	friend class FPhysicsControlAssetInfoDetailsCustomization;

public:
	/** Initialize the asset editor. This will register the application mode, init the preview scene, etc. */
	void InitAssetEditor(
		const EToolkitMode::Type        Mode,
		const TSharedPtr<IToolkitHost>& InitToolkitHost,
		UPhysicsControlAsset*    InPhysicsControlAsset);

	/** Shared data accessor */
	TSharedPtr<FPhysicsControlAssetEditorData> GetEditorData() const;

	// FAssetEditorToolkit overrides.
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FText GetToolkitName() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual void InitToolMenuContext(FToolMenuContext& MenuContext) override;
	// ~END FAssetEditorToolkit overrides.

	// FGCObject overrides.
	virtual FString GetReferencerName() const override { return TEXT("FPhysicsControlAssetEditor"); }
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	// ~END FGCObject overrides.

	// FTickableEditorObject overrides.
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }
	virtual TStatId GetStatId() const override;
	// ~END FTickableEditorObject overrides.

	// IHasPersonaToolkit overrides.
	virtual TSharedRef<IPersonaToolkit> GetPersonaToolkit() const override;
	// ~END IHasPersonaToolkit overrides.

	IPersonaToolkit* GetPersonaToolkitPointer() const { return PersonaToolkit.Get(); }

	/** Repopulates the hierarchy tree view */
	void RefreshHierachyTree();

	/** Refreshes the preview viewport */
	void RefreshPreviewViewport();

	/** Invokes the control profile with the name, assuming simulation is running */
	void InvokeControlProfile(FName ProfileName);

	/** Invokes the most recently invoked control profile */
	void ReinvokeControlProfile();

	/** Destroys all existing controls modifiers and then recreates them from the control asset */
	void RecreateControlsAndModifiers();

protected:
	FText GetSimulationToolTip() const;
	FSlateIcon GetSimulationIcon() const;

	/** Preview scene setup. */
	void HandlePreviewSceneCreated(const TSharedRef<IPersonaPreviewScene>& InPersonaPreviewScene);
	void HandleViewportCreated(const TSharedRef<IPersonaViewport>& InPersonaViewport);
	void HandleDetailsCreated(const TSharedRef<class IDetailsView>& InDetailsView);
	void HandleSelectionChanged(const TArrayView<TSharedPtr<ISkeletonTreeItem>>& InSelectedItems, ESelectInfo::Type InSelectInfo);
	void HandleGetFilterLabel(TArray<FText>& InOutItems) const;
	void HandleExtendFilterMenu(FMenuBuilder& InMenuBuilder);
	void HandleExtendContextMenu(FMenuBuilder& InMenuBuilder);
	void OnFinishedChangingDetails(const FPropertyChangedEvent& PropertyChangedEvent);
	void ShowEmptyDetails() const;

	void ExtendMenu();
	void ExtendToolbar();
	void ExtendViewportMenus();
	void BindCommands();

	/** Building the context menus */
	void BuildMenuWidgetSelection(FMenuBuilder& InMenuBuilder);
	void BuildMenuWidgetBody(FMenuBuilder& InMenuBuilder);

	// Toolbar/menu commands
	void OnCompile();
	bool IsCompilationNeeded();
	void OnToggleSimulation();
	void OnToggleSimulationNoGravity();
	bool IsNoGravitySimulationEnabled() const;
	void OnToggleSimulationFloorCollision();
	bool IsSimulationFloorCollisionEnabled() const;
	void OnMeshRenderingMode(EPhysicsAssetEditorMeshViewMode Mode, bool bSimulation);
	bool IsMeshRenderingMode(EPhysicsAssetEditorMeshViewMode Mode, bool bSimulation) const;
	void OnCollisionRenderingMode(EPhysicsAssetEditorCollisionViewMode Mode, bool bSimulation);
	bool IsCollisionRenderingMode(EPhysicsAssetEditorCollisionViewMode Mode, bool bSimulation) const;
	void OnConstraintRenderingMode(EPhysicsAssetEditorConstraintViewMode Mode, bool bSimulation);
	bool IsConstraintRenderingMode(EPhysicsAssetEditorConstraintViewMode Mode, bool bSimulation) const;
	void ToggleDrawViolatedLimits();
	bool IsDrawingViolatedLimits() const;
	bool IsRunningSimulation() const;
	bool IsNotRunningSimulation() const;

	/** Make the constraint scale widget */
	TSharedRef<SWidget> MakeConstraintScaleWidget();

	/** Make the collision opacity widget */
	TSharedRef<SWidget> MakeCollisionOpacityWidget();

protected:
	/** The persona toolkit. */
	TSharedPtr<IPersonaToolkit> PersonaToolkit = nullptr;

	// Persona viewport.
	TSharedPtr<IPersonaViewport> PersonaViewport = nullptr;

	/** Data and methods shared across multiple classes */
	TSharedPtr<FPhysicsControlAssetEditorData> EditorData;

	// Asset properties tab 
	TSharedPtr<IDetailsView> DetailsView;

	/** The skeleton tree widget */
	TSharedPtr<ISkeletonTree> SkeletonTree;

	/** The skeleton tree builder */
	TSharedPtr<FPhysicsControlAssetEditorSkeletonTreeBuilder> SkeletonTreeBuilder;

	/** Command list for skeleton tree operations */
	TSharedPtr<FUICommandList_Pinnable> SkeletonTreeCommandList;

	/** Command list for viewport operations */
	TSharedPtr<FUICommandList_Pinnable> ViewportCommandList;

	/** Has the asset editor been initialized? */
	bool bIsInitialized = false;

	/** True if in OnTreeSelectionChanged()... protects against infinite recursion */
	bool bSelecting;

	/** Stored when a control profile is invoked */
	FName PreviouslyInvokedControlProfile;
};
