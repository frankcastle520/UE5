// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "WorldPartition/IWorldPartitionEditorModule.h"
#include "WorldPartition/WorldPartitionActorLoaderInterface.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "Templates/PimplPtr.h"

#include "HLODEditorSubsystem.generated.h"


class AActor;
class AWorldPartitionHLOD;
class UPrimitiveComponent;
class UWorldPartition;
class UWorldPartitionEditorSettings;
struct FWorldPartitionHLODEditorData;

// Visibility level for HLOD settings
// By default, settings are classified in the "AllSettings" category
enum class EHLODSettingsVisibility : uint8
{
	BasicSettings,
	AllSettings
};


/**
 * UWorldPartitionHLODEditorSubsystem
 */
UCLASS()
class WORLDPARTITIONEDITOR_API UWorldPartitionHLODEditorSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UWorldPartitionHLODEditorSubsystem();
	virtual ~UWorldPartitionHLODEditorSubsystem();

	//~ Begin USubsystem Interface.
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem Interface.

	//~ Begin UWorldSubsystem Interface.
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	//~ End UWorldSubsystem Interface.

	//~ Begin FTickableGameObject Interface
	virtual bool IsTickable() const override { return true; }
	virtual bool IsTickableInEditor() const override { return true; }
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject Interface

	virtual bool WriteHLODStats(const IWorldPartitionEditorModule::FWriteHLODStatsParams& Params) const;

	static void AddHLODSettingsFilter(EHLODSettingsVisibility InSettingsVisibility, TSoftObjectPtr<UStruct> InStruct, FName InPropertyName);
	
private:
	bool IsHLODInEditorEnabled();
	void SetHLODInEditorEnabled(bool bInEnable);

	void OnWorldPartitionInitialized(UWorldPartition* InWorldPartition);
	void OnWorldPartitionUninitialized(UWorldPartition* InWorldPartition);

	void OnLoaderAdapterStateChanged(const IWorldPartitionActorLoaderInterface::ILoaderAdapter* LoaderAdapter);

	void ForceHLODStateUpdate();

	bool WriteHLODStats(const FString& InFilename) const;
	bool WriteHLODInputStats(const FString& InFilename) const;
	
	void OnWorldPartitionEditorSettingsChanged(const FName& PropertyName, const UWorldPartitionEditorSettings& WorldPartitionEditorSettings);
	void ApplyHLODSettingsFiltering();

private:
	FVector CachedCameraLocation;
	double CachedHLODMinDrawDistance;
	double CachedHLODMaxDrawDistance;
	bool bCachedShowHLODsOverLoadedRegions;
	bool bForceHLODStateUpdate;

	TPimplPtr<FWorldPartitionHLODEditorData> HLODEditorData;

	typedef TMap<TSoftObjectPtr<UStruct>, TSet<FName>> FStructsPropertiesMap;
	static TMap<EHLODSettingsVisibility, FStructsPropertiesMap> StructsPropertiesVisibility;
};


// Macros to simplify registration of HLOD settings filtering
#define HLOD_ADD_CLASS_SETTING_FILTER_NAME(SettingsLevel, TypeIdentifier, PropertyName) \
	UWorldPartitionHLODEditorSubsystem::AddHLODSettingsFilter(EHLODSettingsVisibility::SettingsLevel, TypeIdentifier::StaticClass(), (PropertyName))

#define HLOD_ADD_STRUCT_SETTING_FILTER_NAME(SettingsLevel, TypeIdentifier, PropertyName) \
	UWorldPartitionHLODEditorSubsystem::AddHLODSettingsFilter(EHLODSettingsVisibility::SettingsLevel, TypeIdentifier::StaticStruct(), (PropertyName))

#define HLOD_ADD_CLASS_SETTING_FILTER(SettingsLevel, TypeIdentifier, PropertyIdentifier) \
	HLOD_ADD_CLASS_SETTING_FILTER_NAME(SettingsLevel, TypeIdentifier, GET_MEMBER_NAME_CHECKED(TypeIdentifier, PropertyIdentifier))

#define HLOD_ADD_STRUCT_SETTING_FILTER(SettingsLevel, TypeIdentifier, PropertyIdentifier) \
	HLOD_ADD_STRUCT_SETTING_FILTER_NAME(SettingsLevel, TypeIdentifier, GET_MEMBER_NAME_CHECKED(TypeIdentifier, PropertyIdentifier))