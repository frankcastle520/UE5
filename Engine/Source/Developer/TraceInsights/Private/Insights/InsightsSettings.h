// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/UnrealString.h"

/** Contains all settings for the Unreal Insights, accessible through the main manager. */
class FInsightsSettings
{
	friend class SInsightsSettings;

public:
	FInsightsSettings(bool bInIsDefault = false);
	~FInsightsSettings();

	void LoadFromConfig();
	void SaveToConfig();

	const FInsightsSettings& GetDefaults() const
	{
		return Defaults;
	}

	void ResetToDefaults();

	void EnterEditMode()
	{
		bIsEditing = true;
	}

	void ExitEditMode()
	{
		bIsEditing = false;
	}

	const bool IsEditing() const
	{
		return bIsEditing;
	}

	#define SET_AND_SAVE(Option, Value) { if (Option != Value) { Option = Value; SaveToConfig(); } }

	//////////////////////////////////////////////////
	// [Insights.TimingProfiler]

	double GetDefaultZoomLevel() const { return DefaultZoomLevel; }
	void SetDefaultZoomLevel(double ZoomLevel) { DefaultZoomLevel = ZoomLevel; }
	void SetAndSaveDefaultZoomLevel(double ZoomLevel) { SET_AND_SAVE(DefaultZoomLevel, ZoomLevel); }

	bool IsAutoHideEmptyTracksEnabled() const { return bAutoHideEmptyTracks; }
	void SetAutoHideEmptyTracks(bool bOnOff) { bAutoHideEmptyTracks = bOnOff; }
	void SetAndSaveAutoHideEmptyTracks(bool bOnOff) { SET_AND_SAVE(bAutoHideEmptyTracks, bOnOff); }

	bool IsPanningOnScreenEdgesEnabled() const { return bAllowPanningOnScreenEdges; }
	void SetPanningOnScreenEdges(bool bOnOff) { bAllowPanningOnScreenEdges = bOnOff; }
	void SetAndSavePanningOnScreenEdges(bool bOnOff) { SET_AND_SAVE(bAllowPanningOnScreenEdges, bOnOff); }

	bool IsAutoScrollEnabled() const { return bAutoScroll; }
	void SetAutoScroll(bool bOnOff) { bAutoScroll = bOnOff; }
	void SetAndSaveAutoScroll(bool bOnOff) { SET_AND_SAVE(bAutoScroll, bOnOff); }

	int32 GetAutoScrollFrameAlignment() const { return AutoScrollFrameAlignment; }
	void SetAutoScrollFrameAlignment(int32 FrameType) { AutoScrollFrameAlignment = FrameType; }
	void SetAndSaveAutoScrollFrameAlignment(int32 FrameType) { SET_AND_SAVE(AutoScrollFrameAlignment, FrameType); }

	double GetAutoScrollViewportOffsetPercent() const { return AutoScrollViewportOffsetPercent; }
	void SetAutoScrollViewportOffsetPercent(double OffsetPercent) { AutoScrollViewportOffsetPercent = OffsetPercent; }
	void SetAndSaveAutoScrollViewportOffsetPercent(double OffsetPercent) { SET_AND_SAVE(AutoScrollViewportOffsetPercent, OffsetPercent); }

	double GetAutoScrollMinDelay() const { return AutoScrollMinDelay; }
	void SetAutoScrollMinDelay(double Delay) { AutoScrollMinDelay = Delay; }
	void SetAndSaveAutoScrollMinDelay(double Delay) { SET_AND_SAVE(AutoScrollMinDelay, Delay); }

	//////////////////////////////////////////////////
	// [Insights.TimingProfiler.FramesView]

	bool IsShowUpperThresholdLineEnabled() const { return bShowUpperThresholdLine; }
	void SetShowUpperThresholdLineEnabled(bool bOnOff) { bShowUpperThresholdLine = bOnOff; }
	void SetAndSaveShowUpperThresholdLineEnabled(bool bOnOff) { SET_AND_SAVE(bShowUpperThresholdLine, bOnOff); }

	bool IsShowLowerThresholdLineEnabled() const { return bShowLowerThresholdLine; }
	void SetShowLowerThresholdLineEnabled(bool bOnOff) { bShowLowerThresholdLine = bOnOff; }
	void SetAndSaveShowLowerThresholdLineEnabled(bool bOnOff) { SET_AND_SAVE(bShowLowerThresholdLine, bOnOff); }

	double GetUpperThresholdTime() const { return UpperThresholdTime; }
	void SetUpperThresholdTime(double InUpperThresholdTime) { UpperThresholdTime = InUpperThresholdTime; }
	void SetAndSaveUpperThresholdTime(double InUpperThresholdTime) { SET_AND_SAVE(UpperThresholdTime, InUpperThresholdTime); }

	double GetLowerThresholdTime() const { return LowerThresholdTime; }
	void SetLowerThresholdTime(double InLowerThresholdTime) { LowerThresholdTime = InLowerThresholdTime; }
	void SetAndSaveLowerThresholdTime(double InLowerThresholdTime) { SET_AND_SAVE(LowerThresholdTime, InLowerThresholdTime); }

	bool IsShowUpperThresholdAsFpsEnabled() const { return bShowUpperThresholdAsFps; }
	void SetShowUpperThresholdAsFpsEnabled(bool bOnOff) { bShowUpperThresholdAsFps = bOnOff; }
	void SetAndSaveShowUpperThresholdAsFpsEnabled(bool bOnOff) { SET_AND_SAVE(bShowUpperThresholdAsFps, bOnOff); }

	bool IsShowLowerThresholdAsFpsEnabled() const { return bShowLowerThresholdAsFps; }
	void SetShowLowerThresholdAsFpsEnabled(bool bOnOff) { bShowLowerThresholdAsFps = bOnOff; }
	void SetAndSaveShowLowerThresholdAsFpsEnabled(bool bOnOff) { SET_AND_SAVE(bShowLowerThresholdAsFps, bOnOff); }

	void SetAndSaveThresholds(double InUpperThresholdTime, double InLowerThresholdTime, bool bInShowUpperThresholdAsFps, bool bInShowLowerThresholdAsFps)
	{
		bool bChanged = false;
		if (UpperThresholdTime != InUpperThresholdTime)
		{
			UpperThresholdTime = InUpperThresholdTime;
			bChanged = true;
		}
		if (LowerThresholdTime != InLowerThresholdTime)
		{
			LowerThresholdTime = InLowerThresholdTime;
			bChanged = true;
		}
		if (bShowUpperThresholdAsFps != bInShowUpperThresholdAsFps)
		{
			bShowUpperThresholdAsFps = bInShowUpperThresholdAsFps;
			bChanged = true;
		}
		if (bShowLowerThresholdAsFps != bInShowLowerThresholdAsFps)
		{
			bShowLowerThresholdAsFps = bInShowLowerThresholdAsFps;
			bChanged = true;
		}
		if (bChanged)
		{
			SaveToConfig();
		}
	}

	bool IsAutoZoomOnFrameSelectionEnabled() const { return bAutoZoomOnFrameSelection; }
	void SetAutoZoomOnFrameSelection(bool bOnOff) { bAutoZoomOnFrameSelection = bOnOff; }
	void SetAndSaveAutoZoomOnFrameSelection(bool bOnOff) { SET_AND_SAVE(bAutoZoomOnFrameSelection, bOnOff); }

	//////////////////////////////////////////////////
	// [Insights.TimingProfiler.MainGraph]

	bool GetTimingViewMainGraphShowPoints() const { return bTimingViewMainGraphShowPoints; }
	void SetTimingViewMainGraphShowPoints(bool InValue) { bTimingViewMainGraphShowPoints = InValue; }
	void SetAndSaveTimingViewMainGraphShowPoints(bool InValue) { SET_AND_SAVE(bTimingViewMainGraphShowPoints, InValue); }

	bool GetTimingViewMainGraphShowPointsWithBorder() const { return bTimingViewMainGraphShowPointsWithBorder; }
	void SetTimingViewMainGraphShowPointsWithBorder(bool InValue) { bTimingViewMainGraphShowPointsWithBorder = InValue; }
	void SetAndSaveTimingViewMainGraphShowPointsWithBorder(bool InValue) { SET_AND_SAVE(bTimingViewMainGraphShowPointsWithBorder, InValue); }

	bool GetTimingViewMainGraphShowConnectedLines() const { return bTimingViewMainGraphShowConnectedLines; }
	void SetTimingViewMainGraphShowConnectedLines(bool InValue) { bTimingViewMainGraphShowConnectedLines = InValue; }
	void SetAndSaveTimingViewMainGraphShowConnectedLines(bool InValue) { SET_AND_SAVE(bTimingViewMainGraphShowConnectedLines, InValue); }

	bool GetTimingViewMainGraphShowPolygons() const { return bTimingViewMainGraphShowPolygons; }
	void SetTimingViewMainGraphShowPolygons(bool InValue) { bTimingViewMainGraphShowPolygons = InValue; }
	void SetAndTimingViewMainGraphShowPolygons(bool InValue) { SET_AND_SAVE(bTimingViewMainGraphShowPolygons, InValue); }

	bool GetTimingViewMainGraphShowEventDuration() const { return bTimingViewMainGraphShowEventDuration; }
	void SetTimingViewMainGraphShowEventDuration(bool InValue) { bTimingViewMainGraphShowEventDuration = InValue; }
	void SetAndSaveTimingViewMainGraphShowEventDuration(bool InValue) { SET_AND_SAVE(bTimingViewMainGraphShowEventDuration, InValue); }

	bool GetTimingViewMainGraphShowBars() const { return bTimingViewMainGraphShowBars; }
	void SetTimingViewMainGraphShowBars(bool InValue) { bTimingViewMainGraphShowBars = InValue; }
	void SetAndSaveTimingViewMainGraphShowBars(bool InValue) { SET_AND_SAVE(bTimingViewMainGraphShowBars, InValue); }

	bool GetTimingViewMainGraphShowGameFrames() const { return bTimingViewMainGraphShowGameFrames; }
	void SetTimingViewMainGraphShowGameFrames(bool InValue) { bTimingViewMainGraphShowGameFrames = InValue; }
	void SetAndSaveTimingViewMainGraphShowGameFrames(bool InValue) { SET_AND_SAVE(bTimingViewMainGraphShowGameFrames, InValue); }

	bool GetTimingViewMainGraphShowRenderingFrames() const { return bTimingViewMainGraphShowRenderingFrames; }
	void SetTimingViewMainGraphShowRenderingFrames(bool InValue) { bTimingViewMainGraphShowRenderingFrames = InValue; }
	void SetAndSaveTimingViewMainGraphShowRenderingFrames(bool InValue) { SET_AND_SAVE(bTimingViewMainGraphShowRenderingFrames, InValue); }

	//////////////////////////////////////////////////
	// [Insights.TimingProfiler.TimersView]

	const TArray<FString>& GetTimersViewInstanceVisibleColumns() const { return TimersViewInstanceVisibleColumns; }
	void SetTimersViewInstanceVisibleColumns(const TArray<FString>& Columns) { TimersViewInstanceVisibleColumns = Columns; }
	void SetAndSaveTimersViewInstanceVisibleColumns(const TArray<FString>& Columns) { SET_AND_SAVE(TimersViewInstanceVisibleColumns, Columns); }

	const TArray<FString>& GetTimersViewGameFrameVisibleColumns() const { return TimersViewGameFrameVisibleColumns; }
	void SetTimersViewGameFrameVisibleColumns(const TArray<FString>& Columns) { TimersViewGameFrameVisibleColumns = Columns; }
	void SetAndSaveTimersViewGameFrameVisibleColumns(const TArray<FString>& Columns) { SET_AND_SAVE(TimersViewGameFrameVisibleColumns, Columns); }

	const TArray<FString>& GetTimersViewRenderingFrameVisibleColumns() const { return TimersViewRenderingFrameVisibleColumns; }
	void SetTimersViewRenderingFrameVisibleColumns(const TArray<FString>& Columns) { TimersViewRenderingFrameVisibleColumns = Columns; }
	void SetAndSaveTimersViewRenderingFrameVisibleColumns(const TArray<FString>& Columns) { SET_AND_SAVE(TimersViewRenderingFrameVisibleColumns, Columns); }

	int32 GetTimersViewMode() const { return TimersViewMode; }
	void SetTimersViewMode(int32 InMode) { TimersViewMode = InMode; }
	void SetAndSaveTimersViewMode(int32 InMode) { SET_AND_SAVE(TimersViewMode, InMode); }

	int32 GetTimersViewGroupingMode() const { return TimersViewGroupingMode; }
	void SetTimersViewGroupingMode(int32 InValue) { TimersViewGroupingMode = InValue; }
	void SetAndSaveTimersViewGroupingMode(int32 InValue) { SET_AND_SAVE(TimersViewGroupingMode, InValue); }

	bool GetTimersViewShowCpuEvents() const { return bTimersViewShowCpuTimers; }
	void SetTimersViewShowCpuEvents(bool InValue) { bTimersViewShowCpuTimers = InValue; }
	void SetAndSaveTimersViewShowCpuEvents(bool InValue) { SET_AND_SAVE(bTimersViewShowCpuTimers, InValue); }

	bool GetTimersViewShowGpuEvents() const { return bTimersViewShowGpuTimers; }
	void SetTimersViewShowGpuEvents(bool InValue) { bTimersViewShowGpuTimers = InValue; }
	void SetAndSaveTimersViewShowGpuEvents(bool InValue) { SET_AND_SAVE(bTimersViewShowGpuTimers, InValue); }

	bool GetTimersViewShowZeroCountTimers() const { return bTimersViewShowZeroCountTimers; }
	void SetTimersViewShowZeroCountTimers(bool InValue) { bTimersViewShowZeroCountTimers = InValue; }
	void SetAndSaveTimersViewShowZeroCountTimers(bool InValue) { SET_AND_SAVE(bTimersViewShowZeroCountTimers, InValue); }

	//////////////////////////////////////////////////
	// [Insights.MemoryProfiler]

	const TArray<FString>& GetSymbolSearchPaths() const { return SymbolSearchPaths; }
	void SetSymbolSearchPaths(const TArray<FString>& SearchPaths) { SymbolSearchPaths = SearchPaths; }
	void SetAndSaveSymbolSearchPaths(const TArray<FString>& SearchPaths) { SET_AND_SAVE(SymbolSearchPaths, SearchPaths); }

	//////////////////////////////////////////////////

	#undef SET_AND_SAVE

private:
	/** Contains default settings. */
	static FInsightsSettings Defaults;

	/** Whether this instance contains defaults. */
	bool bIsDefault = false;

	/** Whether profiler settings is in edit mode. */
	bool bIsEditing = false;

	/** Settings filename ini. */
	FString SettingsIni;

	//////////////////////////////////////////////////
	// [Insights.TimingProfiler]

	/** The default (initial) zoom level of the Timing view. */
	double DefaultZoomLevel = 5.0; // 5 seconds between major tick marks

	/** Auto hide empty tracks (ex.: ones without timing events in the current viewport). */
	bool bAutoHideEmptyTracks = true;

	/** If enabled, the panning is allowed to continue when mouse cursor reaches the edges of the screen. */
	bool bAllowPanningOnScreenEdges = false;

	/** If enabled, the Timing View will start with auto-scroll enabled. */
	bool bAutoScroll = false;

	/** -1 to disable frame alignment or the type of frame to align with (0 = Game or 1 = Rendering). */
	int32 AutoScrollFrameAlignment = 0; // -1 = none, 0 = game, 1 = rendering

	/**
	 * Viewport offset while auto-scrolling, as percent of viewport width.
	 * If positive, it offsets the viewport forward, allowing an empty space at the right side of the viewport (i.e. after end of session).
	 * If negative, it offsets the viewport backward (i.e. end of session will be outside viewport).
	 */
	double AutoScrollViewportOffsetPercent = 0.1; // scrolls forward 10% of viewport's width

	/** Minimum time between two auto-scroll updates, in [seconds]. */
	double AutoScrollMinDelay = 0.3; // [seconds]

	//////////////////////////////////////////////////
	// [Insights.TimingProfiler.FramesView]

	/** If enabled, the upper threshold line is visible.The frame coloring by threshold is enabled regardless of this setting. */
	bool bShowUpperThresholdLine = false;

	/** If enabled, the lower threshold line is visible.The frame coloring by threshold is enabled regardless of this setting. */
	bool bShowLowerThresholdLine = false;

	/**
	 * The upper threshold for frames.
	 * Can be specified as a frame duration([0.001 .. 1.0] seconds; ex.: "0.010" for 10 ms) or as a framerate([1 fps .. 1000 fps]; ex: "100 fps").
	 */
	double UpperThresholdTime = 1.0 / 30.0;
	/**
	 * The lower threshold for frames.
	 * Can be specified as a frame duration([0.001 .. 1.0] seconds; ex.: "0.010" for 10 ms) or as a framerate([1 fps .. 1000 fps]; ex: "100 fps").
	 */
	double LowerThresholdTime = 1.0 / 60.0;

	bool bShowUpperThresholdAsFps = true;
	bool bShowLowerThresholdAsFps = true;

	/** If enabled, the Timing View will also be zoomed when a new frame is selected in the Frames track. */
	bool bAutoZoomOnFrameSelection = false;

	//////////////////////////////////////////////////
	// [Insights.TimingProfiler.MainGraph]

	/** If enabled, values will be displayed as points in the Main Graph Track in Timing Insights. */
	bool bTimingViewMainGraphShowPoints = false;

	/** If enabled, values will be displayed as points with border in the Main Graph Track in Timing Insights. */
	bool bTimingViewMainGraphShowPointsWithBorder = true;

	/** If enabled, values will be displayed as connected lines in the Main Graph Track in Timing Insights. */
	bool bTimingViewMainGraphShowConnectedLines = true;

	/** If enabled, values will be displayed as polygons in the Main Graph Track in Timing Insights. */
	bool bTimingViewMainGraphShowPolygons = true;

	/** If enabled, uses duration of timing events for connected lines and polygons in the Main Graph Track in Timing Insights. */
	bool bTimingViewMainGraphShowEventDuration = true;

	/** If enabled, shows bars corresponding to the duration of the timing events in the Main Graph Track in Timing Insights. */
	bool bTimingViewMainGraphShowBars = false;

	/** If enabled, shows game frames in the Main Graph Track in Timing Insights. */
	bool bTimingViewMainGraphShowGameFrames = true;

	/** If enabled, shows rendering frames in the Main Graph Track in Timing Insights. */
	bool bTimingViewMainGraphShowRenderingFrames = true;

	//////////////////////////////////////////////////
	// [Insights.TimingProfiler.TimersView]

	/** The list of visible columns in the Timers view in the Instance mode. */
	TArray<FString> TimersViewInstanceVisibleColumns;

	/** The list of visible columns in the Timers view in the Game Frame mode. */
	TArray<FString> TimersViewGameFrameVisibleColumns;

	/** The list of visible columns in the Timers view in the Rendering Frame mode. */
	TArray<FString> TimersViewRenderingFrameVisibleColumns;

	/**
	 * The mode for the timers panel.
	 * See ETraceFrameType in MiscTrace.h.
	 */
	int32 TimersViewMode = 2; // (int32)TraceFrameType_Count

	/** The grouping mode for the timers panel. */
	int32 TimersViewGroupingMode = 3; // ByType

	/** If enabled, CPU timers will be displayed in the Timing View. */
	bool bTimersViewShowCpuTimers = true;

	/** If enabled, GPU timers will be displayed in the Timing View. */
	bool bTimersViewShowGpuTimers = true;

	/** If enabled, timers with no instances in the selected interval will still be displayed in the Timers View. */
	bool bTimersViewShowZeroCountTimers = true;

	//////////////////////////////////////////////////
	// [Insights.MemoryProfiler]

	/** List of search paths to look for symbol files */
	TArray<FString> SymbolSearchPaths;

	//////////////////////////////////////////////////
};
