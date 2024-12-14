// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CineCameraComponent.h"
#include "EVCamTargetViewportID.h"
#include "Output/Data/EViewportChangeReply.h"
#include "Output/Data/VCamStringPrompt.h"
#include "UI/WidgetSnapshots.h"
#include "Util/OutputProviderUtils.h"
#include "Widgets/VPFullScreenUserWidget.h"
#include "VCamOutputProviderBase.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogVCamOutputProvider, Log, All);

class APlayerController;
class FSceneViewport;
class SWindow;
class UGameplayViewTargetPolicy;
class UUserWidget;
class UVCamComponent;
class UVCamWidget;
class UVPFullScreenUserWidget;

#if WITH_EDITOR
struct FEditorViewportViewModifierParams;
struct FSceneViewExtensionContext;
class FLevelEditorViewportClient;
class ISceneViewExtension;
#endif

/**
 * Output providers implement methods of overlaying a widget onto a target viewport. The composition of viewport and widget is then usually streamed
 * to an application outside the engine, e.g. via Pixel Streaming or Remote Session.
 *
 * To start outputting, the owning UVCamComponent must be enabled and the output provider activated.
 *
 * Output providers are managed by UVCamComponent, which own them and must be attached as a child to a UCineCameraComponent.
 * Output providers have a target viewport that the widget is overlayed onto. The target viewport can be locked to the target camera, which happens when:
 *	1. The output provider is outputting IsOutputting() == true
 *	2. The output provider is configured to do so, either by 2.1 NeedsForceLockToViewport returning true or 2.2 UVCamComponent::ViewportLocker being configured accordingly.
 * When a viewport is locked, the owning output provider can affect its resolution (see bUseOverrideResolution and OverrideResolution).
 *
 * A concept of viewport ownership is implemented in FViewportManager ensuring that at most 1 output provider affects a viewport's lock and resolution at a time;
 * the first output provider to request lock, gets the ownership over that viewport. When lock, resolution or target viewport change, call RequestResolutionRefresh
 * to updat the viewport state.
 */
UCLASS(Abstract, BlueprintType, EditInlineNew, CollapseCategories)
class VCAMCORE_API UVCamOutputProviderBase : public UObject
{
	GENERATED_BODY()
	friend UVCamComponent;
public:

	DECLARE_MULTICAST_DELEGATE_OneParam(FActivationDelegate, bool /*bNewIsActive*/);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FActivationDelegate_Blueprint, bool, bNewIsActive);
	FActivationDelegate OnActivatedDelegate; 
	/** Called when the activation state of this output provider changes. */
	UPROPERTY(BlueprintAssignable, meta = (DisplayName = "OnActivated"))
	FActivationDelegate_Blueprint OnActivatedDelegate_Blueprint;

	/** Override the default output resolution with a custom value - NOTE you must toggle bIsActive off then back on for this to take effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (DisplayPriority = "5"))
	bool bUseOverrideResolution = false;

	/** When bUseOverrideResolution is set, use this custom resolution */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Output", meta = (DisplayPriority = "6"), meta = (EditCondition = "bUseOverrideResolution", ClampMin = 1))
	FIntPoint OverrideResolution = { 2048, 1536 };

	UVCamOutputProviderBase();
	
	//~ Begin UObject Interface
	virtual void BeginDestroy() override;
	//~ End UObject Interface

	/**
	 * Called when the provider is brought online such as after instantiating or loading a component containing this provider
	 * Use Initialize for any setup logic that needs to survive between Start / Stop cycles such as spawning transient objects
	 */
	virtual void Initialize();
	/** Called when the provider is being shutdown such as before changing level or on exit */
	virtual void Deinitialize();
	
	virtual void Tick(const float DeltaTime);

	/** @return Whether this output provider should require the viewport to be locked to the camera in order to function correctly. */
	virtual bool NeedsForceLockToViewport() const;

	/**
	 * Request string input from the streaming client.
	 * Returns true if the request was handled.
	 */
	virtual TFuture<FVCamStringPromptResponse> PromptClientForString(const FVCamStringPromptRequest& Request)
	{
		return MakeFulfilledPromise<FVCamStringPromptResponse>(EVCamStringPromptResult::Unavailable).GetFuture();
	}
	
	/** Temporarily disable the output.  Caller must eventually call RestoreOutput. */
	void SuspendOutput();
	/** Restore the output state from previous call to disable output. */
	void RestoreOutput();
	
	/** Calls the VCamModifierInterface on the widget if it exists and also requests any child VCam Widgets to reconnect */
	void NotifyAboutComponentChange();

	/** Called to turn on or off this output provider */
	UFUNCTION(BlueprintCallable, Category = "Output")
	void SetActive(const bool bInActive);
	/** Returns if this output provider is currently active or not */
	UFUNCTION(BlueprintPure, Category = "Output")
	bool IsActive() const { return bIsActive; };

	/** Returns if this output provider has been initialized or not */
	UFUNCTION(BlueprintPure, Category = "Output")
	bool IsInitialized() const { return bInitialized; };
	
	UFUNCTION(BlueprintPure, Category = "Output")
	EVCamTargetViewportID GetTargetViewport() const { return TargetViewport; }
	UFUNCTION(BlueprintCallable, Category = "Output")
	void SetTargetViewport(EVCamTargetViewportID Value);
	/** Uses this version in constructors (e.g. for initializing a CDO). */
	void InitTargetViewport(EVCamTargetViewportID Value);
	
	UFUNCTION(BlueprintPure, Category = "Output")
	TSubclassOf<UUserWidget> GetUMGClass() const { return UMGClass; }
	UFUNCTION(BlueprintCallable, Category = "Output")
	void SetUMGClass(const TSubclassOf<UUserWidget> InUMGClass);

	UFUNCTION(BlueprintPure, Category = "Output")
	UVCamComponent* GetVCamComponent() const;
	UVPFullScreenUserWidget* GetUMGWidget() { return UMGWidget; };

	/** Utility that gets the owning VCam component and gets another output provider by its index. */
	UVCamOutputProviderBase* GetOtherOutputProviderByIndex(int32 Index) const { return UE::VCamCore::GetOtherOutputProviderByIndex(*this, Index); }
	/** Gets the index of this output provider in the owning UVCamComponent::OutputProviders array. */
	int32 FindOwnIndexInOwner() const { return UE::VCamCore::FindOutputProviderIndex(*this); }

	UE_DEPRECATED(5.5, "Use RequestResolutionRefresh instead")
	void ReapplyOverrideResolution() const { RequestResolutionRefresh(); }
	/**
	 * Requests that at end of the frame the target viewport's resolution is updated to match this provider's settings.
	 * 
	 * The update will have no effect if this output provider does not have ownership over the target viewport; ownership is granted if the
	 * viewport is locked to this output provider (either NeedsForceLockToViewport returns true or the UVCamComponent::ViewportLocker is configured accordingly).
	 */
	void RequestResolutionRefresh() const;

	/** Gets the scene viewport identified by the currently configured TargetViewport. */
	TSharedPtr<FSceneViewport> GetTargetSceneViewport() const { return GetSceneViewport(TargetViewport); }
	/** Gets the viewport identified by the passed in parameters. */
	TSharedPtr<FSceneViewport> GetSceneViewport(EVCamTargetViewportID InTargetViewport) const;
	TWeakPtr<SWindow> GetTargetInputWindow() const;

	/** @return Whether it is allowed to change the activation state into bRequestActiveState. */
	bool IsActivationChangeAllowed(bool bRequestActiveState);
	/** @return Whether it is allowed to change the activation state into bRequestActiveState. OutReason is only set if the return value is false. */
	bool IsActivationChangeAllowedWithReason(bool bRequestActiveState, FText& OutReason);
	/** @return Whether it is allowed to toggle (true -> false, false -> true) the activation state of this output provider. */
	UFUNCTION(BlueprintPure, Category = "Output")
	bool CanToggleActivation() { return IsActivationChangeAllowed(!bIsActive); }

	/** @return Whether this output provider is currently outputting (initialized, active, and owning VCam is enabled). */
	bool IsOutputting() const { return IsActive() && IsInitialized() && IsOuterComponentEnabledAndInitialized(); }
	UGameplayViewTargetPolicy* GetGameplayViewTargetPolicy() const { return GameplayViewTargetPolicy; }

	//~ Begin UObject Interface
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
	//~ End UObject Interface

#if WITH_EDITOR
	//~ Begin UObject Interface
	virtual void PreEditUndo() override;
	virtual void PostEditUndo() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject Interface
#endif
	
	static FName GetIsActivePropertyName()			{ return GET_MEMBER_NAME_CHECKED(UVCamOutputProviderBase, bIsActive); }
	static FName GetTargetViewportPropertyName()	{ return GET_MEMBER_NAME_CHECKED(UVCamOutputProviderBase, TargetViewport); }
	static FName GetUMGClassPropertyName()			{ return GET_MEMBER_NAME_CHECKED(UVCamOutputProviderBase, UMGClass); }

protected:
	
	/** Defines how the overlay widget should be added to the viewport. This should set as early as possible: in the constructor. */
	UPROPERTY(Transient)
	EVPWidgetDisplayType DisplayType = EVPWidgetDisplayType::Inactive;
	
	/**
	 * In game worlds, such as PIE or shipped games, determines which a player controller whose view target should be set to the owning cine camera.
	 * 
	 * Note that multiple output providers may have a policy set and policies might choose the same player controllers to set the view target for.
	 * This conflict is resolved as follows: if a player controller already has the cine camera as view target, the policy is not used.
	 * Hence, you can order your output providers array in the VCamComponent. The first policies will get automatically get higher priority.
	 */
	UPROPERTY(EditAnywhere, Instanced, Category = "Output", meta = (DisplayPriority = "99"))
	TObjectPtr<UGameplayViewTargetPolicy> GameplayViewTargetPolicy;

	/** Triggers all callbacks without checking whether the bIsActive flag is actually being changed. */
	void SetActiveInternal(bool bInActive);
	
	/** Called when the provider is Activated */
	virtual void OnActivate();
	/** Called when the provider is Deactivated */
	virtual void OnDeactivate();
	
	/** Called to create the UMG overlay widget. */
	virtual void CreateUMG();
	void DisplayUMG();
	void DestroyUMG();

	void DisplayNotification_ViewportNotFound() const;
	
	/** Called by owning UVCamComponent when the target camera changes. */
	void OnSetTargetCamera(const UCineCameraComponent* InTargetCamera);

#if WITH_EDITOR
	FLevelEditorViewportClient* GetTargetLevelViewportClient() const;
#endif

	/**
	 * Called after changing viewport. Handles processing all updates that must happen in response:
	 * 1. Updating the override viewport resolutions
	 * 2. Warning user that the target viewport is not available (they should open the viewport x tab)
	 * 3. If currently outputting, recreate the UMG widget into the new target viewport.
	 */
	void ReinitializeViewportIfNeeded();
	/** Called while a UMG widget is being outputted. This moves the displayed UMG widget from the old viewport to the new target viewport. */
	void ReinitializeViewport();

	/**
	 * Called a new target viewport has been set while outputting but before the viewport change is processed.
	 * Subclass can indicate whether the dynamic change is supported or not.
	 */
	virtual UE::VCamCore::EViewportChangeReply PreReapplyViewport() { return UE::VCamCore::EViewportChangeReply::Reinitialize; }
	/** If PreReapplyViewport returned EVCamViewportChangeReply::ApplyViewportChange, then this function is called after the UMG widget has been placed in the new target viewport. */
	virtual void PostReapplyViewport() {}
	
	UVPFullScreenUserWidget* GetUMGWidget() const { return UMGWidget; }
	
private:
	
	/** If set, this output provider will execute every frame */
	UPROPERTY(EditAnywhere, BlueprintGetter = "IsActive", BlueprintSetter = "SetActive", Category = "Output", meta = (EditCondition = "CanToggleActivation", DisplayPriority = "1"))
	bool bIsActive = false;
	
	/**
	 * This makes sure that every OnActivate call is matched with exactly one OnDeactivate call, and vice versa.
	 * These functions allocate external resources (e.g. signalling server in pixel streaming), so the calls must be matched exactly.
	 * 
	 * Without this variable, it would be difficult to keep track of whether we're actually active because of the many systems that set  bIsActive directly,
	 * e.g. undo / redo and Multi-User.
	 */
	UPROPERTY(Transient, NonTransactional)
	bool bIsActuallyActive = false;

	/** Which viewport to use for this VCam */
	UPROPERTY(EditAnywhere, BlueprintGetter = "GetTargetViewport", BlueprintSetter = "SetTargetViewport", Category = "Output", meta = (DisplayPriority = "2"))
	EVCamTargetViewportID TargetViewport = EVCamTargetViewportID::Viewport1;
	
	/** The UMG class to be rendered in this output provider */
	UPROPERTY(EditAnywhere, BlueprintGetter = "GetUMGClass", BlueprintSetter = "SetUMGClass", Category = "Output", meta = (DisplayName="UMG Overlay", DisplayPriority = "3"))
	TSubclassOf<UUserWidget> UMGClass;
	
	/** FOutputProviderLayoutCustomization allows remapping connections and their bound widgets. This is used to persist those overrides since UUserWidgets cannot be saved. */
	UPROPERTY()
	FWidgetTreeSnapshot WidgetSnapshot;
	
	UPROPERTY(Transient, NonTransactional)
	bool bInitialized = false;

	/** Valid when active and if UMGClass is valid. */
	UPROPERTY(Transient, NonTransactional)
	TObjectPtr<UVPFullScreenUserWidget> UMGWidget = nullptr;

#if WITH_EDITORONLY_DATA
	/** We call UVPFullScreenUserWidget::SetCustomPostProcessSettingsSource(this), which will cause these settings to be discovered. They are later passed down to FEditorViewportViewModifierDelegate. */
	UPROPERTY(Transient)
	FPostProcessSettings PostProcessSettingsForWidget;

	/** Prevents certain messages from being generated while undoing. */
	UPROPERTY(Transient, NonTransactional)
	bool bIsUndoing = false;
	
	/** Handle to ModifyViewportPostProcessSettings */
	FDelegateHandle ModifyViewportPostProcessSettingsDelegateHandle;
#endif
	
	UPROPERTY(Transient)
	TSoftObjectPtr<UCineCameraComponent> TargetCamera;

	/** SuspendOutput can disable output while we're active. This flag indicates whether we should reactivate when RestoreOutput is called. */
	UPROPERTY(Transient)
	bool bWasOutputSuspendedWhileActive = false;

	/** If in a game world, these player controllers must have their view targets reverted when this output provider is deactivated. */
	UPROPERTY(Transient, NonTransactional)
	TSet<TWeakObjectPtr<APlayerController>> PlayersWhoseViewTargetsWereSet; 

	bool IsActiveAndOuterComponentAllowsActivity(bool bSkipGarbageCheck = false) const { return bIsActive && IsOuterComponentEnabledAndInitialized(bSkipGarbageCheck); }
	bool IsOuterComponentEnabledAndInitialized(bool bSkipGarbageCheck = false) const;

	/** Calls OnActivate, it it has not yet been.  */
	void HandleCallingOnActivate();
	/** Calls OnDeactivate, it it has not yet been.  */
	void HandleCallingOnDeactivate();

#if WITH_EDITOR
	/** Passed to FEditorViewportClient::ViewModifiers whenever DisplayType == EVPWidgetDisplayType::PostProcessWithBlendMaterial. */
	void ModifyViewportPostProcessSettings(FEditorViewportViewModifierParams& EditorViewportViewModifierParams);
	/** Callback when DisplayType == EVPWidgetDisplayType::PostProcessSceneViewExtension that decides whether a given viewport should be rendered to. */
	TOptional<bool> GetRenderWidgetStateInContext(const ISceneViewExtension* SceneViewExtension, const FSceneViewExtensionContext& Context);
	
	void StartDetectAndSnapshotWhenConnectionsChange();
	void StopDetectAndSnapshotWhenConnectionsChange();
	void OnConnectionReinitialized(TWeakObjectPtr<UVCamWidget> Widget);
#endif
};
