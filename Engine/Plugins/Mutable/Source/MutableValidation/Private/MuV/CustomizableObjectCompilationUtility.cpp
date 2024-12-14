// Copyright Epic Games, Inc. All Rights Reserved.

#include "CustomizableObjectCompilationUtility.h"

#include "ScopedLogSection.h"
#include "Commandlets/Commandlet.h"
#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectPrivate.h"
#include "MuCO/CustomizableObjectSystem.h"
#include "MuCO/CustomizableObjectCompilerTypes.h"
#include "MuCO/ICustomizableObjectEditorModule.h"
#include "Interfaces/ITargetPlatform.h"
#include "MuCOE/CustomizableObjectEditorFunctionLibrary.h"



bool FCustomizableObjectCompilationUtility::CompileCustomizableObject(UCustomizableObject* InCustomizableObject, const bool bShouldLogMutableLogs /* = true */, const FCompilationOptions* InCompilationOptionsOverride  /* nullptr */)
{
	LLM_SCOPE_BYNAME(TEXT("FCustomizableObjectCompilationUtility/Compile"));
	const FScopedLogSection CompilationSection (EMutableLogSection::Compilation);	
	check(InCustomizableObject);
	
	CustomizableObject = TStrongObjectPtr(InCustomizableObject);
	
	// Handle the overriding of the compilation configuration of the CO ------------------------------------------------------------------------------
	
	// Override the compilation options if an override has been provided by the user
	FCompilationOptions CompilationOptions;
	const bool bOverrideCompilationOptions = InCompilationOptionsOverride != nullptr;
	if (bOverrideCompilationOptions)
	{
		CompilationOptions = *InCompilationOptionsOverride;
		UE_LOG(LogMutable,Display,TEXT("CO Compilation options overriden by the user defined ones."));
	}
	else
	{
		CompilationOptions = InCustomizableObject->GetPrivate()->GetCompileOptions();
		UE_LOG(LogMutable,Display,TEXT("Compiling CO using it's own compilation options."));
	}

	// Ensure that the user has provided a target compilation platform.
	// Mutable is able to run without one but we want to be explicit in the context of testing.
	if (!CompilationOptions.TargetPlatform)
	{
		UE_LOG(LogMutable, Error, TEXT("The compilation of the %s model could not be started : No explicit platform was provided."), *InCustomizableObject->GetName());
		return false;
	}

	// Report the compilation configuration : We use this flag just to be sure we only generate logs when required since MongoDB does not want to find
	// duplicated keys. The addition of duplicated entries in MongoDB is not available so this way we avoid having to handle them
	// when we are sure we do not require it.
	if (bShouldLogMutableLogs)
	{
		UE_LOG(LogMutable, Log, TEXT("(string) model_compile_options_overriden : %s "), bOverrideCompilationOptions ? TEXT("true") : TEXT("false"));
		UE_LOG(LogMutable, Log, TEXT("(int) model_optimization_level : %d "), CompilationOptions.OptimizationLevel);
		UE_LOG(LogMutable, Log, TEXT("(string) model_texture_compression : %s "), *UEnum::GetValueAsString(CompilationOptions.TextureCompression));
		UE_LOG(LogMutable, Log, TEXT("(string) model_disk_compilation : %s "), CompilationOptions.bUseDiskCompilation ? TEXT("true") : TEXT("false"));
		UE_LOG(LogMutable, Log, TEXT("(string) model_compile_platform_name : %s "), *CompilationOptions.TargetPlatform->PlatformName());
		UE_LOG(LogMutable, Log, TEXT("(int) model_package_data_bytes_limit_bytes : %llu "), CompilationOptions.PackagedDataBytesLimit);
	}

	// Compilation completed successfully
	bool bCompilationSuccess = false;
	
	// Run and wait for the compilation to be completed ----------------------------------------------------------------------------------------------
	{
		// Get the memory usage before staring the compilation
#if ENABLE_LOW_LEVEL_MEM_TRACKER
		FLowLevelMemTracker& LowLevelMemoryTracker = FLowLevelMemTracker::Get();

		// Memory in use before starting the compilation itself
		int64 CompilationStartTotalBytes = 0;
		// Peak memory usage as reported by LLM during the compilation operation.
		// Note : This value will not discriminate and therefore will report memory usage by the compilation of the CO as well as other operations that
		// may be running during it's operation
		int64 CompilationEndTotalRealPeakBytes = 0;
		
		if (FLowLevelMemTracker::IsEnabled())
		{
			UE_LOG(LogMutable,Display,TEXT("LLM system enabled: Peak memory usage during mutable CO compilation will be logged after said compilation takes place."));
			
			LowLevelMemoryTracker.UpdateStatsPerFrame();
			CompilationStartTotalBytes = LowLevelMemoryTracker.GetTagAmountForTracker(ELLMTracker::Default, ELLMTag::Total, UE::LLM::ESizeParams::ReportCurrent);
		}
#endif
		
		// Get the memory being used by mutable before the compilation
		const int64 CompilationStartMutableBytes = mu::FGlobalMemoryCounter::GetAbsoluteCounter();
		mu::FGlobalMemoryCounter::Zero();
		
		UE_LOG(LogMutable,Display,TEXT("Compiling Customizable Object..."));
		TSharedRef<FCompilationRequest> CompileRequest = MakeShared<FCompilationRequest>(*InCustomizableObject, true);
		CompileRequest->GetCompileOptions() = CompilationOptions;
		ICustomizableObjectEditorModule::GetChecked().CompileCustomizableObject(CompileRequest);
		
		// Wait while the compilation takes place
		const double CompilationStartSeconds = FPlatformTime::Seconds();
		while (CompileRequest->GetCompilationState() != ECompilationStatePrivate::Completed)
		{
			LLM_SCOPE_BYNAME(TEXT("FCustomizableObjectCompilationUtility/CompileLoop"));
			
			// Tick the engine
			CommandletHelpers::TickEngine();

			// todo: Will this be required in the future? It feels odd having to tick the compiler manually when we are already ticking the engine itself
			ICustomizableObjectEditorModule::GetChecked().Tick(false);
			
			// Cache the peak value found during the compilation of the CO
#if ENABLE_LOW_LEVEL_MEM_TRACKER
			if (FLowLevelMemTracker::IsEnabled())
			{
				LowLevelMemoryTracker.UpdateStatsPerFrame();
				const int64 CurrentTotalMemoryUsage = LowLevelMemoryTracker.GetTagAmountForTracker(ELLMTracker::Default, ELLMTag::Total, UE::LLM::ESizeParams::ReportCurrent);
				CompilationEndTotalRealPeakBytes = CompilationEndTotalRealPeakBytes > CurrentTotalMemoryUsage ? CompilationEndTotalRealPeakBytes : CurrentTotalMemoryUsage;
			}
#endif
		
			// Stop if exit was requested
			if (IsEngineExitRequested())
			{
				break;
			}
		}
		

		bCompilationSuccess = CompileRequest->GetCompilationResult() == ECompilationResultPrivate::Success ||
			CompileRequest->GetCompilationResult() == ECompilationResultPrivate::Warnings;

		// Report the time we took to run the compilation
		const double CompilationEndSeconds = FPlatformTime::Seconds() - CompilationStartSeconds;
		UE_LOG(LogMutable, Display, TEXT("The compilation of the %s CO model took %f seconds."), *InCustomizableObject->GetName(), CompilationEndSeconds);
		if (bShouldLogMutableLogs)
		{
			UE_LOG(LogMutable, Log, TEXT("(double) model_compile_time_ms : %f "), CompilationEndSeconds * 1000);

			// Also report compilation end status
			const ECustomizableObjectCompilationState CompilationEndResult = bCompilationSuccess ? ECustomizableObjectCompilationState::Completed
				: ECustomizableObjectCompilationState::Failed;
			UE_LOG(LogMutable, Log, TEXT("(string) model_compile_end_state : %s "), *UEnum::GetValueAsString(CompilationEndResult));
		}
		
		// Get the peak mutable memory used during the compilation operation
		const int64 CompilationEndPeakMutableBytes = mu::FGlobalMemoryCounter::GetPeak();
		const int64 CompilationEndRealMutablePeakBytes = CompilationStartMutableBytes + CompilationEndPeakMutableBytes;
		UE_LOG(LogMutable, Display, TEXT("Pre-Compilation Mutable memory usage : %lld"), CompilationStartMutableBytes);
		UE_LOG(LogMutable, Display, TEXT("Compilation Mutable peak memory usage : %lld"), CompilationEndPeakMutableBytes);
		UE_LOG(LogMutable, Display, TEXT("Compilation Mutable real peak memory usage : %lld"), CompilationEndRealMutablePeakBytes);
		if (bShouldLogMutableLogs)
		{
			// Mutable system reported memory usage
			UE_LOG(LogMutable, Log, TEXT("(int) model_compilation_start_bytes : %lld "), CompilationStartMutableBytes);
			UE_LOG(LogMutable, Log, TEXT("(int) model_compilation_end_peak_bytes : %lld "), CompilationEndPeakMutableBytes);
			UE_LOG(LogMutable, Log, TEXT("(int) model_compilation_end_real_peak_bytes : %lld "), CompilationEndRealMutablePeakBytes);
		}

		
		// Now report the peak memory usage reported by the "total" tag from the LLM system
#if ENABLE_LOW_LEVEL_MEM_TRACKER
		if (FLowLevelMemTracker::IsEnabled())
		{
			UE_LOG(LogMutable, Display, TEXT("LLM Pre-Compilation Total memory usage (bytes) : %lld"), CompilationStartTotalBytes);
			// CompilationPeakBytes : Peak memory recorded during the compilation subtracting from it the memory in use before the compilation itself.
			// It should reflect the peak memory usage OF the compilation (as direct result of the compilation itself) but it should be used as an approximation
			// since we can never be too sure no other system is doing work in parallel that is not part of the mutable compilation operation.
			const int64 CompilationEndTotalPeakBytes = CompilationEndTotalRealPeakBytes - CompilationStartTotalBytes;
			UE_LOG(LogMutable, Display, TEXT("LLM Total Compilation Peak memory usage (bytes) : %lld"), CompilationEndTotalPeakBytes);
			UE_LOG(LogMutable, Display, TEXT("LLM Total Compilation Real Peak memory usage (bytes) : %lld"), CompilationEndTotalRealPeakBytes);
			if (bShouldLogMutableLogs)
			{
				UE_LOG(LogMutable, Log, TEXT("(int) model_compilation_llm_total_start_bytes : %lld "), CompilationStartTotalBytes);
				UE_LOG(LogMutable, Log, TEXT("(int) model_compilation_llm_total_end_peak_bytes : %lld "), CompilationEndTotalPeakBytes);
				UE_LOG(LogMutable, Log, TEXT("(int) model_compilation_llm_total_end_real_peak_bytes : %lld "), CompilationEndTotalRealPeakBytes);
			}
		}
#endif
	}

	// Return the success state of the operation
	return bCompilationSuccess;
}
