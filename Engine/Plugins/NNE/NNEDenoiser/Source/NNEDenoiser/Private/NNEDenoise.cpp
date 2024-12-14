// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNEDenoiser.h"
#include "Misc/CoreDelegates.h"
#include "NNEDenoiserLog.h"
#include "NNEDenoiserViewExtension.h"

DEFINE_LOG_CATEGORY(LogNNEDenoiser);

void FNNEDenoiserModule::StartupModule()
{
	// During cook, we are not allowed to call IsRayTracingAllowed() later, therefore we assume Ray Tracing is not available and do not register the View Extension.
	if (IsRunningCookCommandlet())
	{
		return;
	}

	FCoreDelegates::OnPostEngineInit.AddLambda([this] ()
	{
		// Only register View Extension if Ray Tracing is available.
		if (IsRayTracingAllowed())
		{
			ViewExtension = FSceneViewExtensions::NewExtension<UE::NNEDenoiser::Private::FViewExtension>();
		}
		else
		{
			UE_LOG(LogNNEDenoiser, Log, TEXT("Ray Tracing is not enabled, therefore NNEDenoiser is not registered!"));
		}
	});
}

void FNNEDenoiserModule::ShutdownModule()
{
	ViewExtension.Reset();
}

IMPLEMENT_MODULE(FNNEDenoiserModule, NNEDenoiser)