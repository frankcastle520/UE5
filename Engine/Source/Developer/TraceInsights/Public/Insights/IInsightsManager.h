// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Insights/IUnrealInsightsModule.h"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

/** Interface for an Insights manager. */
class TRACEINSIGHTS_API IInsightsManager : public IInsightsComponent
{
public:
	/** The event to execute when the session has changed. */
	DECLARE_MULTICAST_DELEGATE(FSessionChangedEvent);
	virtual FSessionChangedEvent& GetSessionChangedEvent() = 0;

	/** The event to execute when session analysis is complete. */
	DECLARE_MULTICAST_DELEGATE(FSessionAnalysisCompletedEvent);
	virtual FSessionAnalysisCompletedEvent& GetSessionAnalysisCompletedEvent() = 0;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
