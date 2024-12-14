// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/IAnalyticsProvider.h"
#include "Interfaces/IAnalyticsTracer.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "Engine/EngineTypes.h"

class FTelemetryRouter;
struct FTimerHandle;

/**
 * A class that implements a variety of pre-configured Core and Editor telemetry events that can be used to evaluate the efficiency of the most common developer workflows
 */
class EDITORTELEMETRY_API FEditorTelemetry
{
public:
	FEditorTelemetry() {};
	~FEditorTelemetry() {};

	static FEditorTelemetry& Get();

	void StartSession();
	void EndSession();

private:	

	/** Various event handling functions */
	static void RecordEvent_Cooking(TArray<FAnalyticsEventAttribute> Attributes = {});
	static void RecordEvent_Loading(const FString& LoadingName, double LoadingSeconds, TArray<FAnalyticsEventAttribute> Attributes = {});
	static void RecordEvent_CoreSystems(const FString& Context, TArray<FAnalyticsEventAttribute> Attributes = {});
	static void RecordEvent_DDCResource(const FString& Context, TArray<FAnalyticsEventAttribute> Attributes = {});
	static void RecordEvent_DDCSummary(const FString& Context, TArray<FAnalyticsEventAttribute> Attributes = {});
	static void RecordEvent_IAS(const FString& Context, TArray<FAnalyticsEventAttribute> Attributes = {});
	static void RecordEvent_Zen(const FString& Context, TArray<FAnalyticsEventAttribute> Attributes = {});
	static void RecordEvent_VirtualAssets(const FString& Context, TArray<FAnalyticsEventAttribute> Attributes = {});
	static void RegisterCollectionWorkflowDelegates(FTelemetryRouter& Router);
	void HitchSamplerCallback();
	void HeartbeatCallback();
	
	TSharedPtr<IAnalyticsSpan> EditorSpan;
	TSharedPtr<IAnalyticsSpan> EditorBootSpan;
	TSharedPtr<IAnalyticsSpan> EditorInteractSpan;
	TSharedPtr<IAnalyticsSpan> EditorInitilizeSpan;
	TSharedPtr<IAnalyticsSpan> EditorLoadMapSpan;
	TSharedPtr<IAnalyticsSpan> PIESpan;
	TSharedPtr<IAnalyticsSpan> PIEPreBeginSpan;
	TSharedPtr<IAnalyticsSpan> PIEStartupSpan;
	TSharedPtr<IAnalyticsSpan> PIELoadMapSpan;
	TSharedPtr<IAnalyticsSpan> PIEInteractSpan;		
	TSharedPtr<IAnalyticsSpan> PIEShutdownSpan;
	TSharedPtr<IAnalyticsSpan> CookingSpan;
	TSharedPtr<IAnalyticsSpan> HitchingSpan;
	TSharedPtr<IAnalyticsSpan> AssetRegistryScanSpan;
	
	const FName EditorSpanName = TEXT("Editor");
	const FName EditorBootSpanName = TEXT("Editor.Boot");
	const FName EditorInitilizeSpanName = TEXT("Editor.Initialize");
	const FName EditorInteractSpanName = TEXT("Editor.Interact");
	const FName EditorLoadMapSpanName = TEXT("Editor.LoadMap");
	const FName AssetRegistryScanSpanName = TEXT("Editor.AssetRegistryScan");
	const FName PIESpanName = TEXT("PIE");
	const FName PIEStartupSpanName = TEXT("PIE.Startup");
	const FName PIEPreBeginSpanName = TEXT("PIE.PreBegin");
	const FName PIELoadMapSpanName = TEXT("PIE.LoadMap");
	const FName PIEInteractSpanName = TEXT("PIE.Interact");
	const FName PIEShutdownSpanName = TEXT("PIE.Shutdown");
	const FName CookingSpanName = TEXT("Cooking");
	const FName HitchingSpanName = TEXT("Hitching");
	const FName OpenAssetEditorSpan = TEXT("Open Asset Editor");
	const float HeartbeatIntervalSeconds = 1.0;
	const float HitchSamplerIntervalSeconds = 0.1;
	const float MinFPSForHitching = 5.0;

	TMap<FGuid, TSharedPtr<IAnalyticsSpan>> TaskSpans;
	FCriticalSection TaskSpanCriticalSection;

	FTimerHandle TelemetryHeartbeatTimerHandle;
	FTimerHandle TelemetryHitchSamplerTimerHandle;
	FString EditorMapName;
	FString PIEMapName;
	uint32 EditorSessionCount = 0;
	uint32 PIESessionCount = 0;
	double SessionStartTime;
	double AssetOpenStartTime;
	double TimeToBootEditor;
	double HitchAvergageFPS = 0;
	uint32 HitchSampleCount = 0;
	uint32 TotalPluginCount = 0;
	uint32 AssetRegistryScanCount = 0;
};


