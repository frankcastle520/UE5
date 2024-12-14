// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

#include "Framework/Commands/Commands.h"
#include "Templates/SharedPointer.h"

namespace UE::Insights::LoadingProfiler
{
	
class FLoadingTimingViewCommands : public TCommands<FLoadingTimingViewCommands>
{
public:
	FLoadingTimingViewCommands();
	virtual ~FLoadingTimingViewCommands();
	virtual void RegisterCommands() override;

public:
	TSharedPtr<FUICommandInfo> ShowHideAllLoadingTracks;
};

} // namespace UE::Insights::LoadingProfiler
