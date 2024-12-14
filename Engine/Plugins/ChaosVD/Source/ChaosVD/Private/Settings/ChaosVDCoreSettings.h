// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPtr.h"

#include "ChaosVDCoreSettings.generated.h"

class UChaosVDCoreSettings;
class UMaterial;
class UTextureCube;

DECLARE_MULTICAST_DELEGATE_OneParam(FChaosVDSettingChanged, UObject* SettingsObject)

UCLASS()
class UChaosVDSettingsObjectsOuter : public UObject
{
	GENERATED_BODY()
};

UCLASS(config = ChaosVD)
class UChaosVDSettingsObjectBase : public UObject
{
public:
	UChaosVDSettingsObjectBase();

private:
	GENERATED_BODY()

public:


	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	FChaosVDSettingChanged& OnSettingsChanged() { return SettingsChangedDelegate; }

	virtual void PostEditUndo() override;

	virtual void OverridePerObjectConfigSection(FString& SectionName) override;

	FStringView GetConfigSectionName()
	{
		return OverrideConfigSectionName;
	}

protected:
	virtual void BroadcastSettingsChanged();
	
private:

	FString OverrideConfigSectionName;
	FChaosVDSettingChanged SettingsChangedDelegate;

	friend class FChaosVDSettingsManager;
};

UCLASS(config = ChaosVD)
class UChaosVDVisualizationSettingsObjectBase : public UChaosVDSettingsObjectBase
{
	GENERATED_BODY()
protected:
	virtual void BroadcastSettingsChanged() override;
};

UCLASS(config = Engine)
class UChaosVDCoreSettings : public UChaosVDSettingsObjectBase
{
	GENERATED_BODY()
public:

	UPROPERTY(Config, Transient)
	TSoftObjectPtr<UMaterial> QueryOnlyMeshesMaterial;

	UPROPERTY(Config, Transient)
	TSoftObjectPtr<UMaterial> SimOnlyMeshesMaterial;

	UPROPERTY(Config, Transient)
	TSoftObjectPtr<UMaterial> InstancedMeshesMaterial;

	UPROPERTY(Config, Transient)
	TSoftObjectPtr<UMaterial> InstancedMeshesQueryOnlyMaterial;

	UPROPERTY(Config)
	FSoftClassPath SkySphereActorClass;

	UPROPERTY(Config)
	TSoftObjectPtr<UTextureCube> AmbientCubeMapTexture;
};
