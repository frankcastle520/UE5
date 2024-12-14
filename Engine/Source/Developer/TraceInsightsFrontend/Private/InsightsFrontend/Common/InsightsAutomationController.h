// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InsightsFrontend/ITraceInsightsFrontendModule.h"

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "Containers/Ticker.h"

DECLARE_LOG_CATEGORY_EXTERN(InsightsAutomationController, Log, All);

namespace UE::Insights
{

class TRACEINSIGHTSFRONTEND_API FInsightsAutomationController : public TSharedFromThis<FInsightsAutomationController>
{
	enum class ETestsState
	{
		NotStarted,
		Running,
		Finished,
	};

public:
	virtual ~FInsightsAutomationController();

	virtual void Initialize();

	bool Tick(float DeltaTime);

	void SetAutoQuit(bool InAutoQuit) { bAutoQuit = InAutoQuit; }
	bool GetAutoQuit() const { return bAutoQuit; }

	void RunTests(const FString& InCmd);

private:
	/** The delegate to be invoked when this manager ticks. */
	FTickerDelegate OnTick;

	/** Handle to the registered OnTick. */
	FTSTicker::FDelegateHandle OnTickHandle;

	FString CommandToExecute;

	bool bAutoQuit = false;
	ETestsState RunningTestsState = ETestsState::NotStarted;

	static const TCHAR* AutoQuitMsgOnComplete;
};

} // namespace UE::Insights