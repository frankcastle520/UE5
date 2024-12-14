// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaNetworkClient.h"
#include "UbaThread.h"
#include "UbaTraceReader.h"

namespace uba
{
	class Config;

	#define UBA_VISUALIZER_FLAGS1 \
		UBA_VISUALIZER_FLAG(Progress, true, L"progress") \
		UBA_VISUALIZER_FLAG(Status, true, L"status") \
		UBA_VISUALIZER_FLAG(ActiveProcesses, false, L"active processes") \
		UBA_VISUALIZER_FLAG(TitleBars, true, L"instance title bars") \
		UBA_VISUALIZER_FLAG(DetailedData, false, L"detailed data (use -UbaDetailedTrace for even more)") \
		UBA_VISUALIZER_FLAG(NetworkStats, true, L"network stats") \
		UBA_VISUALIZER_FLAG(CpuMemStats, true, L"cpu/mem stats") \
		UBA_VISUALIZER_FLAG(ProcessBars, true, L"process bars") \
		UBA_VISUALIZER_FLAG(FinishedProcesses, true, L"finished process bars") \
		UBA_VISUALIZER_FLAG(Timeline, true, L"timeline") \
		UBA_VISUALIZER_FLAG(Workers, false, L"workers (threads on host taking care of requests from helpers)") \
		UBA_VISUALIZER_FLAG(CursorLine, false, L"cursor (vertical line)") \

	#define UBA_VISUALIZER_FLAGS2 \
		UBA_VISUALIZER_FLAG(ShowProcessText, true, L"Show text in process bars") \
		UBA_VISUALIZER_FLAG(ShowReadWriteColors, true, L"Show colors for read/write times in process bars") \
		UBA_VISUALIZER_FLAG(ScaleHorizontalWithScrollWheel, false, L"Use scroll wheel to scale horizontally") \
		UBA_VISUALIZER_FLAG(DarkMode, false, L"Use dark mode to draw visualizer") \
		UBA_VISUALIZER_FLAG(AutoSaveSettings, true, L"Auto save Position/Settings on close") \
		UBA_VISUALIZER_FLAG(ShowAllTraces, true, L"Show all traces started on channel") \
		UBA_VISUALIZER_FLAG(SortActiveRemoteSessions, true, L"Sort active sessions on top") \
		UBA_VISUALIZER_FLAG(AutoScaleHorizontal, true, L"Automatically scale horizontally to fit processes") \
		UBA_VISUALIZER_FLAG(LockTimelineToBottom, true, L"Lock timeline to always paint at bottom") \

	struct VisualizerConfig
	{
		VisualizerConfig(const tchar* filename);

		bool Load(Logger& logger);
		bool Save(Logger& logger);

		TString filename;

		int x = 100;
		int y = 100;
		u32 width = 1500;
		u32 height = 1500;
		u32 fontSize = 13;
		TString fontName;
		u32 maxActiveVisible = 5;
		u32 maxActiveProcessHeight = 16;

		#define UBA_VISUALIZER_FLAG(name, defaultValue, desc) bool show##name = defaultValue;
		UBA_VISUALIZER_FLAGS1
		#undef UBA_VISUALIZER_FLAG

		#define UBA_VISUALIZER_FLAG(name, defaultValue, desc) bool name = defaultValue;
		UBA_VISUALIZER_FLAGS2
		#undef UBA_VISUALIZER_FLAG

		u64 parent = 0;
	};

	class Visualizer
	{
	public:
		Visualizer(VisualizerConfig& config, Logger& logger);
		~Visualizer();

		bool ShowUsingListener(const wchar_t* channelName);
		bool ShowUsingNamedTrace(const wchar_t* namedTrace);
		bool ShowUsingSocket(NetworkBackend& backend, const wchar_t* host, u16 port = DefaultPort);
		bool ShowUsingFile(const wchar_t* fileName, u32 replay);

		bool HasWindow();
		HWND GetHwnd();
		void Lock(bool lock);

	private:
		bool StartHwndThread();
		bool Unselect();
		void Reset();
		void PaintClient(const Function<void(HDC hdc, HDC memDC, RECT& clientRect)>& paintFunc);
		void PaintAll(HDC hdc, const RECT& clientRect);
		void PaintActiveProcesses(int& posY, const RECT& clientRect, const Function<void(TraceView::ProcessLocation&, u32, bool)>& drawProcess);
		void PaintProcessRect(TraceView::Process& process, HDC hdc, RECT rect, const RECT& progressRect, bool selected, bool writingBitmap);
		void PaintTimeline(HDC hdc, const RECT& clientRect);
		using DrawTextFunc = Function<void(const StringBufferBase& text, RECT& rect)>;
		void PaintDetailedStats(int& posY, const RECT& progressRect, TraceView::Session& session, bool isRemote, u64 playTime, const DrawTextFunc& drawTextFunc);

		struct Stats
		{
			u64 recvBytesPerSecond = 0;
			u64 sendBytesPerSecond = 0;
			u64 ping = 0;
			u64 memAvail = 0;
			u64 memTotal = 0;
			float cpuLoad = 0;
		};
		struct HitTestResult
		{
			u32 section = ~0u;
			TraceView::ProcessLocation processLocation;
			bool processSelected = false;
			u32 sessionSelectedIndex = ~0u;
			bool statsSelected = false;
			Stats stats;
			u32 buttonSelected = ~0u;
			float timelineSelected = 0;
			u32 fetchedFilesSelected = ~0u;
			bool workSelected = false;
			u32 workTrack = ~0u;
			u32 workIndex = ~0u;
			TString hyperLink;
		};
		u64 GetPlayTime();
		int GetTimelineHeight();
		int GetTimelineTop(const RECT& clientRect);
		void HitTest(HitTestResult& outResult, const POINT& pos);

		void WriteProcessStats(Logger& out, const TraceView::Process& process);
		void CopyTextToClipboard(const TString& str);
		void UnselectAndRedraw();
		bool UpdateAutoscroll();
		bool UpdateSelection();
		void UpdateScrollbars(bool redraw);
		StringBufferBase& GetTitlePrefix(StringBufferBase& out);
		void InitBrushes();
		void ThreadLoop();
		void Pause(bool pause);
		void StartDragToScroll(const POINT& anchor);
		void StopDragToScroll();
		void SaveSettings();
		void DirtyBitmaps(bool full);

		struct Font
		{
			HFONT handle = 0;
			HFONT handleUnderlined = 0;
			int height = 0;
			int offset = 0;
		};

		void UpdateFont(Font& font, int height, bool createUnderline);
		void UpdateDefaultFont();
		void UpdateProcessFont();
		void ChangeFontSize(int offset);
		void Redraw(bool now);
		void SetActiveFont(const Font& font);

		StringBuffer<256> m_namedTrace;
		StringBuffer<256> m_fileName;
		u32 m_replay = 0;
		u64 m_startTime = 0;
		u64 m_pauseTime = 0;

		struct ProcessBrushes
		{
			HBRUSH inProgress = 0;
			HBRUSH success = 0;
			HBRUSH error = 0;
			HBRUSH returned = 0;
			HBRUSH recv = 0;
			HBRUSH send = 0;
			HBRUSH cacheFetch = 0;
		};

		ProcessBrushes m_processBrushes[2]; // Non-selected and selected

		Atomic<bool> m_looping;
		HWND m_hwnd = 0;
		HWND m_parentHwnd = 0;
		COLORREF m_textColor = {};
		COLORREF m_textWarningColor = {};
		COLORREF m_textErrorColor = {};
		COLORREF m_sendColor = {};
		COLORREF m_recvColor = {};
		COLORREF m_cpuColor = {};
		COLORREF m_memColor = {};
		HBRUSH m_backgroundBrush = 0;
		HBRUSH m_tooltipBackgroundBrush = 0;
		HBRUSH m_workBrush = 0;
		HPEN m_textPen = 0;
		HPEN m_separatorPen = 0;
		HPEN m_sendPen = 0;
		HPEN m_recvPen = 0;
		HPEN m_cpuPen = 0;
		HPEN m_memPen = 0;
		HPEN m_processUpdatePen = 0;
		HPEN m_checkboxPen = 0;
		int m_boxHeight = 12;
		int m_sessionStepY = 0;

		Font m_defaultFont;
		Font m_processFont;
		Font m_timelineFont;
		Font m_popupFont;

		int m_processFontOffsetY = 0;

		Font m_activeProcessFont[32];
		u32 m_activeProcessCountHistory[5];
		u32 m_activeProcessCountHistoryIterator = 0;

		HDC m_activeHdc = 0;
		Font m_activeFont;

		int m_progressRectLeft = 30;

		Logger& m_logger;
		VisualizerConfig m_config;
		TraceReader m_trace;
		TraceView m_traceView;

		NetworkClient* m_client = nullptr;
		Event m_clientDisconnect;

		StringBuffer<256>m_listenChannel;
		StringBuffer<256> m_newTraceName;
		Event m_listenTimeout;

		int m_contentWidth = 0;
		int m_contentHeight = 0;

		int m_contentWidthWhenThumbTrack = 0;

		float m_scrollPosX = 0;
		float m_scrollPosY = 0;
		float m_zoomValue = 0.5f;
		float m_horizontalScaleValue = 0.5f;
		bool m_autoScroll = true;
		bool m_paused = false;
		u64 m_pauseStart = 0;

		static constexpr int BitmapCacheHeight = 1024*1024;
		HBITMAP m_lastBitmap = 0;
		int m_lastBitmapOffset = BitmapCacheHeight;

		u32 m_activeSection = ~0u;
		TraceView::ProcessLocation m_processSelectedLocation;
		bool m_processSelected = false;
		u32 m_sessionSelectedIndex = ~0u;
		bool m_statsSelected = false;
		Stats m_stats;
		u32 m_buttonSelected = ~0u;
		float m_timelineSelected = 0;
		u32 m_fetchedFilesSelected = ~0u;
		TString m_hyperLinkSelected;

		bool m_workSelected = false;
		u32 m_workTrack = ~0u;
		u32 m_workIndex = ~0u;
		
		bool m_mouseOverWindow = false;
		bool m_showPopup = false;
		bool m_locked = false;

		HBITMAP m_cachedBitmap = 0;
		RECT m_cachedBitmapRect = { INT_MIN, INT_MIN, INT_MIN, INT_MIN };

		Vector<HBITMAP> m_textBitmaps;

		POINT m_mouseAnchor = {};
		float m_scrollAtAnchorX = 0;
		float m_scrollAtAnchorY = 0;
		int m_dragToScrollCounter = 0;

		Thread m_thread;

		void PostNewTrace(u32 replay, bool paused);
		void PostNewTitle(const StringView& title);
		void PostQuit();
		LRESULT WinProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
		static LRESULT CALLBACK StaticWinProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
	};
}
