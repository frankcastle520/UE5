// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AutomatedPerfTestControllerBase.h"
#include "UObject/Object.h"
#include "Engine/DeveloperSettings.h"
#include "AutomatedStaticCameraPerfTestBase.generated.h"


UCLASS(BlueprintType, Config=Engine, DefaultConfig, DisplayName="Automated Performance Testing | Static Camera")
class AUTOMATEDPERFTESTING_API UAutomatedStaticCameraPerfTestProjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

	TMap<FString, FSoftObjectPath> MapNameMap; 
	
public:
	UAutomatedStaticCameraPerfTestProjectSettings(const FObjectInitializer&);

	/** Gets the settings container name for the settings, either Project or Editor */
	virtual FName GetContainerName() const override { return FName("Project"); }
	/** Gets the category for the settings, some high level grouping like, Editor, Engine, Game...etc. */
	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	UFUNCTION(BlueprintCallable, Category="Static Camera Perf Test")
	bool GetMapFromAssetName(FString AssetName, FSoftObjectPath& OutSoftObjectPath) const;
	
	/*
	 * List of levels to test
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Static Cameras", meta=(AllowedClasses="/Script/Engine.World"))
	TArray<FSoftObjectPath> MapsToTest;

	/*
     * If set, will launch the material performance test map with this game mode alias (make sure you've set the game mode alias in
     * the Maps and Modes settings of your project!)
     */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Static Cameras")
	FString GameModeOverride;
	
	/*
	 * If true, will capture a screenshot for each camera tested after gathering data
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Static Cameras")
	bool bCaptureScreenshots;

	/*
	 * For how long the material performance test should delay before beginning to gather data for a material, in seconds
     */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Static Cameras")
	float WarmUpTime;
	
	/*
	 * For how long the static camera performance test should gather data on each camera, in seconds
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Static Cameras")
	float SoakTime;

	/*
	 * For how long the static camera performance test should delay after ending evaluation before switching to the next camera
     */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Static Cameras")
	float CooldownTime;

	/*
	 * For Static Camera Perf Tests, Separate will output one CSV per map tested, and Granular will output one CSV per camera. 
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Static Cameras")
	EAutomatedPerfTestCSVOutputMode CSVOutputMode;
};

/**
 * 
 */
UCLASS()
class AUTOMATEDPERFTESTING_API UAutomatedStaticCameraPerfTestBase : public UAutomatedPerfTestControllerBase
{
	GENERATED_BODY()
	
public:
	// ~Begin UAutomatedPerfTestControllerBase Interface
	virtual void SetupTest() override;
	
	UFUNCTION()
	virtual void RunTest() override;

	virtual FString GetTestID() override;
	// ~End UAutomatedPerfTestControllerBase Interface
	
	UFUNCTION()
	void SetUpNextCamera();

	UFUNCTION()
	void EvaluateCamera();
	
	UFUNCTION()
	void FinishCamera();
	
	UFUNCTION()
	void ScreenshotCamera();

	UFUNCTION()
	void NextMap();

	virtual TArray<ACameraActor*> GetMapCameraActors();
	
	ACameraActor* GetCurrentCamera() const;
	FString GetCurrentCameraRegionName();
	FString GetCurrentCameraRegionFullName();

	void MarkCameraStart();
	void MarkCameraEnd();
	
protected:
	virtual void OnInit() override;
	virtual void UnbindAllDelegates() override;

private:
	UPROPERTY()
	TArray<TObjectPtr<ACameraActor>> CamerasToTest;

	UPROPERTY()
	TObjectPtr<ACameraActor> CurrentCamera;
	
	FString CurrentMapName;
	FSoftObjectPath CurrentMapPath;
	TArray<FSoftObjectPath> MapsToTest;
	const UAutomatedStaticCameraPerfTestProjectSettings* Settings;
};
