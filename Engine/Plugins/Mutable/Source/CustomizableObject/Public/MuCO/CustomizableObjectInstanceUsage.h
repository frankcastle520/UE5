// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCO/CustomizableObjectInstance.h"
#include "MuCO/CustomizableObjectInstanceUsagePrivate.h"
#include "Tickable.h"
#include "Engine/EngineBaseTypes.h"

#include "CustomizableObjectInstanceUsage.generated.h"

class AActor;
class FObjectPreSaveContext;
class UObject;
class USkeletalMesh;
class UPhysicsAsset;
struct FFrame;
enum class EUpdateResult : uint8;

DECLARE_DELEGATE(FCustomizableObjectInstanceUsageUpdatedDelegate);

// This class can be used instead of a UCustomizableComponent (for example for non-BP projects) to link a 
// UCustomizableObjectInstance and a USkeletalComponent so that the CustomizableObjectSystem takes care of updating it and its LODs, 
// streaming, etc. It's a UObject, so it will be much cheaper than a UCustomizableComponent as it won't have to refresh its transforms
// every time it's moved.
UCLASS(Blueprintable, BlueprintType, ClassGroup = (CustomizableObject), meta = (BlueprintSpawnableComponent))
class CUSTOMIZABLEOBJECT_API UCustomizableObjectInstanceUsage : public UObject
{
	friend UCustomizableObjectInstanceUsagePrivate;
	
public:
	GENERATED_BODY()

	// Own interface
	UCustomizableObjectInstanceUsage();
	
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstanceUsage)
	void SetCustomizableObjectInstance(UCustomizableObjectInstance* CustomizableObjectInstance);

	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstanceUsage)
	UCustomizableObjectInstance* GetCustomizableObjectInstance() const;
	
	void SetComponentIndex(int32 ObjectComponentIndex);

	int32 GetComponentIndex() const;
	
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstanceUsage)
	void SetComponentName(const FName& Name);

	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstanceUsage)
	FName GetComponentName() const;

	/** Attach this Customizable Object Instance Usage to a Skeletal Mesh Component to be customized. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstanceUsage)
	void AttachTo(USkeletalMeshComponent* SkeletalMeshComponent);
	
	/** Get the parent Skeletal Mesh Component this Customizable Object Instance Usage is attached to. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstanceUsage)
	USkeletalMeshComponent* GetAttachParent() const;
	
	/** Update Skeletal Mesh asynchronously. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObject)
	void UpdateSkeletalMeshAsync(bool bNeverSkipUpdate = false);

	/** Update Skeletal Mesh asynchronously. Callback will be called once the update finishes, even if it fails. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstance)
	void UpdateSkeletalMeshAsyncResult(FInstanceUpdateDelegate Callback, bool bIgnoreCloseDist = false, bool bForceHighPriority = false);
	
	/** Set to true to avoid automatically replacing the Skeletal Mesh of the parent Skeletal Mesh Component by the Reference Skeletal Mesh.
	 * If SkipSetSkeletalMeshOnAttach is true, it will not replace it. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstanceUsage)
	void SetSkipSetReferenceSkeletalMesh(bool bSkip);

	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstanceUsage)
	bool GetSkipSetReferenceSkeletalMesh() const;

	/** Set to true to avoid automatically replacing the Skeletal Mesh of the parent Skeletal Mesh Component with any mesh. */
	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstanceUsage)
	void SetSkipSetSkeletalMeshOnAttach(bool bSkip);

	UFUNCTION(BlueprintCallable, Category = CustomizableObjectInstanceUsage)
	bool GetSkipSetSkeletalMeshOnAttach() const;
	
	UCustomizableObjectInstanceUsagePrivate* GetPrivate();

	const UCustomizableObjectInstanceUsagePrivate* GetPrivate() const;

	FCustomizableObjectInstanceUsageUpdatedDelegate UpdatedDelegate;

private:
	// If this CustomizableSkeletalComponent is not null, it means this Usage was created by it, and all persistent properties should be obtained through it
	UPROPERTY()
	TObjectPtr<UCustomizableSkeletalComponent> CustomizableSkeletalComponent;

	// If no CustomizableSkeletalComponent is associated, this SkeletalComponent will be used
	UPROPERTY()
	TWeakObjectPtr<USkeletalMeshComponent> UsedSkeletalMeshComponent;

	// If no CustomizableSkeletalComponent is associated, this Instance will be used
	UPROPERTY()
	TObjectPtr<UCustomizableObjectInstance> UsedCustomizableObjectInstance;

	// If no CustomizableSkeletalComponent is associated, this Index will be used
	UPROPERTY()
	int32 UsedComponentIndex;

	/** Only used if the ComponentIndex is INDEX_NONE. */
	UPROPERTY()
	FName UsedComponentName;
	
	// Used to replace the SkeletalMesh of the parent component by the ReferenceSkeletalMesh or the generated SkeletalMesh 
	bool bUsedPendingSetSkeletalMesh = false;

	// Used to avoid replacing the SkeletalMesh of the parent component by the ReferenceSkeletalMesh if bPendingSetSkeletalMesh is true
	UPROPERTY()
	bool bUsedSkipSetReferenceSkeletalMesh = false;

	UPROPERTY()
	bool bUsedSkipSetSkeletalMeshOnAttach = false;
	
	UPROPERTY()
	TObjectPtr<UCustomizableObjectInstanceUsagePrivate> Private;
	
	friend UCustomizableObjectInstanceUsagePrivate;
};

