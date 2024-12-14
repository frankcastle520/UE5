// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGCommon.h"
#include "PCGManagedResource.h"

#include "PCGDynamicMeshManagedComponent.generated.h"

class AActor;
struct FPCGContext;
class UDynamicMeshComponent;
class UPCGData;
class UPCGDynamicMeshData;
class UPCGSettingsInterface;

UCLASS(BlueprintType, MinimalAPI)
class UPCGDynamicMeshManagedComponent : public UPCGManagedComponent
{
	GENERATED_BODY()

public:
	//~Begin UPCGManagedComponents interface
	virtual void ResetComponent() override { /* Does nothing, but implementation is required to support reuse. */ }
	virtual bool SupportsComponentReset() const override { return true; }
	virtual void MarkAsReused() override;
	virtual void ForgetComponent() override;
	//~End UPCGManagedComponents interface

	PCGGEOMETRYSCRIPTINTEROP_API UDynamicMeshComponent* GetComponent() const;
	PCGGEOMETRYSCRIPTINTEROP_API void SetComponent(UDynamicMeshComponent* InComponent);

	uint64 GetDataUID() const { return DataUID; }
	void SetDataUID(uint64 InDataUID) { DataUID = InDataUID; }

protected:
	UPROPERTY(Transient)
	uint64 DataUID = -1;

	// Cached raw pointer to UDynamicMeshComponent
	mutable UDynamicMeshComponent* CachedRawComponentPtr = nullptr;
};

namespace PCGDynamicMeshManagedComponent
{
	PCGGEOMETRYSCRIPTINTEROP_API UPCGDynamicMeshManagedComponent* GetOrCreateDynamicMeshManagedComponent(FPCGContext* Context, const UPCGSettingsInterface* SettingsInterface, const UPCGDynamicMeshData* InMeshData, AActor* TargetActor, TOptional<EPCGEditorDirtyMode> OptionalDirtyModeOverride = TOptional<EPCGEditorDirtyMode>{});
}