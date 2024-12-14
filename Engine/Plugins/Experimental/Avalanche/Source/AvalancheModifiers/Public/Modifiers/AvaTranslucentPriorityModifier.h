// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AvaArrangeBaseModifier.h"
#include "AvaBaseModifier.h"
#include "Shared/AvaTranslucentPriorityModifierShared.h"
#include "UObject/WeakObjectPtrTemplatesFwd.h"
#include "AvaTranslucentPriorityModifier.generated.h"

class ACameraActor;
class UPrimitiveComponent;

UENUM(BlueprintType)
enum class EAvaTranslucentPriorityModifierMode : uint8
{
	/** The closer you are from the camera based on camera forward axis, the higher your sort priority will be */
	AutoCameraDistance,
	/** The higher you are in the outline tree, the higher your sort priority will be */
	AutoOutlinerTree,
	/** Set it yourself */
	Manual
};

UCLASS(MinimalAPI, BlueprintType)
class UAvaTranslucentPriorityModifier : public UAvaArrangeBaseModifier
{
	GENERATED_BODY()

	friend class UAvaTranslucentPriorityModifierShared;

public:
	UFUNCTION(BlueprintCallable, Category="Motion Design|Modifiers|TranslucentPriority")
    AVALANCHEMODIFIERS_API void SetMode(EAvaTranslucentPriorityModifierMode InMode);

	UFUNCTION(BlueprintPure, Category="Motion Design|Modifiers|TranslucentPriority")
    EAvaTranslucentPriorityModifierMode GetMode() const
    {
	    return Mode;
    }

	UFUNCTION(BlueprintCallable, Category="Motion Design|Modifiers|TranslucentPriority")
	AVALANCHEMODIFIERS_API void SetCameraActor(ACameraActor* InCameraActor);

	UFUNCTION(BlueprintPure, Category="Motion Design|Modifiers|TranslucentPriority")
	ACameraActor* GetCameraActor() const
	{
		return CameraActorWeak.Get();
	}

    AVALANCHEMODIFIERS_API void SetCameraActorWeak(const TWeakObjectPtr<ACameraActor>& InCameraActor);

	TWeakObjectPtr<ACameraActor> GetCameraActorWeak() const
	{
		return CameraActorWeak;
	}

	UFUNCTION(BlueprintCallable, Category="Motion Design|Modifiers|TranslucentPriority")
    AVALANCHEMODIFIERS_API void SetSortPriority(int32 InSortPriority);

	UFUNCTION(BlueprintPure, Category="Motion Design|Modifiers|TranslucentPriority")
    int32 GetSortPriority() const
    {
	    return SortPriority;
    }

	UFUNCTION(BlueprintCallable, Category="Motion Design|Modifiers|TranslucentPriority")
	AVALANCHEMODIFIERS_API void SetSortPriorityOffset(int32 InOffset);

	UFUNCTION(BlueprintPure, Category="Motion Design|Modifiers|TranslucentPriority")
	int32 GetSortPriorityOffset() const
	{
		return SortPriorityOffset;
	}

	UFUNCTION(BlueprintCallable, Category="Motion Design|Modifiers|TranslucentPriority")
	AVALANCHEMODIFIERS_API void SetSortPriorityStep(int32 InStep);

	UFUNCTION(BlueprintPure, Category="Motion Design|Modifiers|TranslucentPriority")
	int32 GetSortPriorityStep() const
	{
		return SortPriorityStep;
	}

	UFUNCTION(BlueprintCallable, Category="Motion Design|Modifiers|TranslucentPriority")
	AVALANCHEMODIFIERS_API void SetIncludeChildren(bool bInIncludeChildren);

	UFUNCTION(BlueprintPure, Category="Motion Design|Modifiers|TranslucentPriority")
	bool GetIncludeChildren() const
	{
		return bIncludeChildren;
	}

protected:
	//~ Begin UObject
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject

	//~ Begin UActorModifierCoreBase
	virtual void OnModifierCDOSetup(FActorModifierCoreMetadata& InMetadata) override;
	virtual void OnModifierAdded(EActorModifierCoreEnableReason InReason) override;
	virtual void OnModifierRemoved(EActorModifierCoreDisableReason InReason) override;
	virtual void SavePreState() override;
	virtual void RestorePreState() override;
	virtual void Apply() override;
	virtual void OnModifierDisabled(EActorModifierCoreDisableReason InReason) override;
	virtual void OnModifiedActorTransformed() override;
	//~ End UActorModifierCoreBase

	//~ Begin IAvaSceneTreeUpdateModifierExtension
	virtual void OnSceneTreeTrackedActorChildrenChanged(int32 InIdx, const TSet<TWeakObjectPtr<AActor>>& InPreviousChildrenActors, const TSet<TWeakObjectPtr<AActor>>& InNewChildrenActors) override;
	virtual void OnSceneTreeTrackedActorDirectChildrenChanged(int32 InIdx, const TArray<TWeakObjectPtr<AActor>>& InPreviousChildrenActors, const TArray<TWeakObjectPtr<AActor>>& InNewChildrenActors) override;
	virtual void OnSceneTreeTrackedActorRearranged(int32 InIdx, AActor* InRearrangedActor) override;
	//~ End IAvaSceneTreeUpdateModifierExtension

	//~ Begin IAvaRenderStateUpdateExtension
	virtual void OnRenderStateUpdated(AActor* InActor, UActorComponent* InComponent) override;
	//~ End IAvaRenderStateUpdateExtension

	//~ Begin IAvaTransformUpdateExtension
	virtual void OnTransformUpdated(AActor* InActor, bool bInParentMoved) override;
	//~ End IAvaTransformUpdateExtension

	void OnModeChanged();
	void OnCameraActorChanged();
	void OnSortPriorityChanged();
	void OnSortPriorityLevelGlobalsChanged() const;
	void OnIncludeChildrenChanged();

	void OnGlobalSortPriorityOffsetChanged();

	ACameraActor* GetDefaultCameraActor() const;

	/** The sort mode we are currently in */
	UPROPERTY(EditInstanceOnly, Setter, Getter, Category="TranslucentPriority", meta=(AllowPrivateAccess="true"))
	EAvaTranslucentPriorityModifierMode Mode = EAvaTranslucentPriorityModifierMode::Manual;

	/** The camera actor to compute the distance from */
	UPROPERTY(EditInstanceOnly, Setter, Getter, Category="TranslucentPriority", meta=(DisplayName="CameraActor", EditCondition="Mode == EAvaTranslucentPriorityModifierMode::AutoCameraDistance", AllowPrivateAccess="true"))
	TWeakObjectPtr<ACameraActor> CameraActorWeak = nullptr;

	/** The sort priority that will be set on the primitive component for manual mode */
	UPROPERTY(EditInstanceOnly, Setter, Getter, Category="TranslucentPriority", meta=(EditCondition="Mode == EAvaTranslucentPriorityModifierMode::Manual", EditConditionHides, AllowPrivateAccess="true"))
	int32 SortPriority = 0;

	/** Sort priority offset shared across all modifiers in this same level */
	UPROPERTY(Transient, EditInstanceOnly, Setter, Getter, Category="TranslucentPriority", meta=(AllowPrivateAccess="true"))
	int32 SortPriorityOffset = 0;

	/** Sort priority incremental step shared across all modifiers in this same level */
	UPROPERTY(Transient, EditInstanceOnly, Setter, Getter, Category="TranslucentPriority", meta=(AllowPrivateAccess="true"))
	int32 SortPriorityStep = 1;

	/** If true, will include children too and update their sort priority */
	UPROPERTY(EditInstanceOnly, Setter="SetIncludeChildren", Getter="GetIncludeChildren", Category="TranslucentPriority", meta=(AllowPrivateAccess="true"))
	bool bIncludeChildren = true;

	/** The components this modifier is managing */
	UPROPERTY()
	TSet<TWeakObjectPtr<UPrimitiveComponent>> PrimitiveComponentsWeak;

private:
	/** The previous sort priority to restore when disabling this modifier */
	UPROPERTY()
	TMap<TWeakObjectPtr<UPrimitiveComponent>, int32> PreviousSortPriorities;

	/** Last primitive components assigned sort priority, used for comparison on change */
	TMap<FObjectKey, int32> LastSortPriorities;

	/** Used to avoid querying again the full list of component states */
	TArray<FAvaTranslucentPriorityModifierComponentState> CachedSortedComponentStates;
};
