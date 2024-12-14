// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Commandlets/WorldPartitionConvertCommandlet.h"
#include "WorldPartitionEditorSettings.generated.h"

UCLASS(config = EditorSettings, meta = (DisplayName = "World Partition"))
class WORLDPARTITIONEDITOR_API UWorldPartitionEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UWorldPartitionEditorSettings();

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnWorldPartitionEditorSettingsChanged, const FName&, const UWorldPartitionEditorSettings&);
	static FOnWorldPartitionEditorSettingsChanged& OnSettingsChanged();

	static FName GetEnableLoadingInEditorPropertyName();
	static FName GetEnableAdvancedHLODSettingsPropertyName();

	TSubclassOf<UWorldPartitionConvertCommandlet> GetCommandletClass() const;
	void SetCommandletClass(const TSubclassOf<UWorldPartitionConvertCommandlet>& InCommandletClass);

	int32 GetInstancedFoliageGridSize() const;
	void SetInstancedFoliageGridSize(int32 InInstancedFoliageGridSize);

	int32 GetMinimapLowQualityWorldUnitsPerPixelThreshold() const;
	void SetMinimapLowQualityWorldUnitsPerPixelThreshold(int32 InMinimapLowQualityWorldUnitsPerPixelThreshold);

	bool GetEnableLoadingInEditor() const;
	void SetEnableLoadingInEditor(bool bInEnableLoadingInEditor);

	bool GetEnableStreamingGenerationLogOnPIE() const;
	void SetEnableStreamingGenerationLogOnPIE(bool bInEnableStreamingGenerationLogOnPIE);

	bool GetShowHLODsInEditor() const;
	void SetShowHLODsInEditor(bool bInShowHLODsInEditor);

	bool GetShowHLODsOverLoadedRegions() const;
	void SetShowHLODsOverLoadedRegions(bool bInShowHLODsOverLoadedRegions);

	bool GetEnableAdvancedHLODSettings() const;
	void SetEnableAdvancedHLODSettings(bool bInEnableAdvancedHLODSettings);

	double GetHLODMinDrawDistance() const;
	void SetHLODMinDrawDistance(double bInHLODMinDrawDistance);

	double GetHLODMaxDrawDistance() const;
	void SetHLODMaxDrawDistance(double bInHLODMaxDrawDistance);

	bool GetDisableBugIt() const;
	void SetDisableBugIt(bool bInEnableBugIt);

	bool GetDisablePIE() const;
	void SetDisablePIE(bool bInEnablePIE);

	bool GetAdvancedMode() const;
	void SetAdvancedMode(bool bInAdvancedMode);

public:
	UE_DEPRECATED(5.5, "Use Get/SetCommandletClass()")
	UPROPERTY(Config, EditAnywhere, Category = MapConversion, Meta = (ToolTip = "Commandlet class to use for World Partition conversion"))
	TSubclassOf<UWorldPartitionConvertCommandlet> CommandletClass;

	UE_DEPRECATED(5.5, "Use Get/SetInstancedFoliageGridSize()")
	UPROPERTY(Config, EditAnywhere, Category = Foliage, Meta = (ClampMin = 3200, ToolTip = "Editor grid size used for instance foliage actors in World Partition worlds"))
	int32 InstancedFoliageGridSize;

	UE_DEPRECATED(5.5, "Use Get/SetMinimapLowQualityWorldUnitsPerPixelThreshold()")
	UPROPERTY(Config, EditAnywhere, Category = MiniMap, Meta = (ClampMin = 100, ToolTip = "Threshold from which minimap generates a warning if its WorldUnitsPerPixel is above this value"))
	int32 MinimapLowQualityWorldUnitsPerPixelThreshold;

	UE_DEPRECATED(5.5, "Use Get/SetEnableLoadingInEditor()")
	UPROPERTY(Config, EditAnywhere, Category = WorldPartition, Meta = (ToolTip = "Whether to enable dynamic loading in the editor through loading regions"))
	bool bEnableLoadingInEditor;

	UE_DEPRECATED(5.5, "Use Get/SetEnableStreamingGenerationLogOnPIE()")
	UPROPERTY(Config, EditAnywhere, Category = WorldPartition, Meta = (ToolTip = "Whether to enable streaming generation log on PIE"))
	bool bEnableStreamingGenerationLogOnPIE;

	UE_DEPRECATED(5.5, "Use Get/SetShowHLODsInEditor()")
	UPROPERTY(Config, EditAnywhere, Category = HLOD, Meta = (ToolTip = "Whether to show HLODs in the editor"))
	bool bShowHLODsInEditor;

	UE_DEPRECATED(5.5, "Use Get/SetShowHLODsOverLoadedRegions()")
	UPROPERTY(Config, EditAnywhere, Category = HLOD, Meta = (ToolTip = "Control display of HLODs in case actors are loaded"))
	bool bShowHLODsOverLoadedRegions;

	UE_DEPRECATED(5.5, "Use Get/SetEnableAdvancedHLODSettings()")
	UPROPERTY(Config, AdvancedDisplay, Meta = (DisplayName = "Enable Advanced HLOD Settings", ToolTip = "Enable advanced HLODs settings"))
	bool bEnableAdvancedHLODSettings;

	UE_DEPRECATED(5.5, "Use Get/SetHLODMinDrawDistance()")
	UPROPERTY(Config, EditAnywhere, Category = HLOD, Meta = (UIMin = 0, UIMax = 1638400, DisplayName = "HLOD Min Draw Distance", ToolTip = "Minimum distance at which HLODs should be displayed in editor"))
	double HLODMinDrawDistance;

	UE_DEPRECATED(5.5, "Use Get/SetHLODMaxDrawDistance()")
	UPROPERTY(Config, EditAnywhere, Category = HLOD, Meta = (UIMin = 0, UIMax = 1638400, DisplayName = "HLOD Max Draw Distance", ToolTip = "Maximum distance at which HLODs should be displayed in editor"))
	double HLODMaxDrawDistance;

	UE_DEPRECATED(5.5, "Use Get/SetDisableBugIt()")
	bool bDisableBugIt;

	UE_DEPRECATED(5.5, "Use Get/SetDisablePIE()")
	bool bDisablePIE;

	UE_DEPRECATED(5.5, "Use Get/SetAdvancedMode()")
	bool bAdvancedMode;

protected:
	FOnWorldPartitionEditorSettingsChanged SettingsChangedDelegate;

	void TriggerPropertyChangeEvent(FName PropertyName);
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

};