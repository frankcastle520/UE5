// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/IAnalyticsProvider.h"
#include "Interfaces/IAnalyticsTracer.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Templates/SharedPointer.h"

class FAnalyticsProviderMulticast;
/**
 * Studio Telemetry Plugin API
 * 
 * Notes:
 * Telemetry for Common Editor and Core Engine is collected automatically via the EditorTelemetry plugin.
 * Telemetry Sessions are started and ended automatically with the plugin initialization and shutdown. As such telemetry will not be captured prior to the plugin initialization.
 * Developers are encouraged to add their own telemetry via this API or to intercept the event recording via the supplied callback on the SetRecordEventCallback API below.
 * It is strongly recommended that developers implement their own IAnalyticsProviderModule where custom recording of telemetry events is desired.
 * Custom AnalyticsProviders can be added to the plugin via the .ini. See FAnalyticsProviderLog or FAnalyticsProviderET for example.
 * Telemetry events are recored to all registered IAnalyticsProviders supplied in the .ini file using the FAnalyticsProviderMulticast provider, except where specifically recorded with the RecordEvent(ProviderName,.. ) API below
 */
class FStudioTelemetry : public IModuleInterface
{
public:

	typedef TFunction<void(const FString& EventName, const TArray<FAnalyticsEventAttribute>& Attrs)> OnRecordEventCallback;

	/** Check whether the module is available*/
	static STUDIOTELEMETRY_API bool IsAvailable() { return FModuleManager::Get().IsModuleLoaded("StudioTelemetry"); }

	/** Access to the module singleton*/
	static STUDIOTELEMETRY_API FStudioTelemetry& Get();

	/** Access to the a specific named analytics provider within the system*/
	STUDIOTELEMETRY_API TWeakPtr<IAnalyticsProvider> GetProvider(const FString& ProviderName);

	/** Access to the broadcast analytics provider for the system*/
	STUDIOTELEMETRY_API TWeakPtr<IAnalyticsProvider> GetProvider();

	/** Access to the tracer for the system*/
	STUDIOTELEMETRY_API TWeakPtr<IAnalyticsTracer> GetTracer();
	
	/** Thread safe method to record an event to all registered analytics providers*/
	STUDIOTELEMETRY_API void RecordEvent(const FString& EventName, const TArray<FAnalyticsEventAttribute>& Attributes = {});

	/** Thread safe method to record an event to all registered analytics providers*/
	STUDIOTELEMETRY_API void RecordEvent(const FName CategoryName, const FString& EventName, const TArray<FAnalyticsEventAttribute>& Attributes = {});

	/** Thread safe method to record an event to the specifically named analytics provider */
	STUDIOTELEMETRY_API void RecordEventToProvider(const FString& ProviderName, const FString& EventName, const TArray<FAnalyticsEventAttribute>& Attributes = {});

	/** Start a new span specifying the parent*/
	STUDIOTELEMETRY_API TSharedPtr<IAnalyticsSpan> StartSpan(const FName Name, const TArray<FAnalyticsEventAttribute>& AdditionalAttributes = {});

	/** Start a new span specifying the parent*/
	STUDIOTELEMETRY_API TSharedPtr<IAnalyticsSpan> StartSpan(const FName Name, TSharedPtr<IAnalyticsSpan> ParentSpan, const TArray<FAnalyticsEventAttribute>& AdditionalAttributes = {});

	/** End an existing span*/
	STUDIOTELEMETRY_API bool EndSpan(TSharedPtr<IAnalyticsSpan> Span, const TArray<FAnalyticsEventAttribute>& AdditionalAttributes = {});

	/** End an existing span by name*/
	STUDIOTELEMETRY_API bool EndSpan(const FName Name, const TArray<FAnalyticsEventAttribute>& AdditionalAttributes = {});

	/** Get an active span by name, non active spans will not be available*/
	STUDIOTELEMETRY_API TSharedPtr<IAnalyticsSpan> GetSpan(const FName Name);

	/** Get the root session span*/
	STUDIOTELEMETRY_API TSharedPtr<IAnalyticsSpan> GetSessionSpan() const;

	/** Callback for interception of telemetry events recording that can be used by Developers to send telemetry events to their own back end, though it is recommended that Developers implement their own IAnalyticsProvider via their own IAnalyticsProviderModule*/
	STUDIOTELEMETRY_API void SetRecordEventCallback(OnRecordEventCallback);

	/** Delegates for event callbacks **/
	DECLARE_MULTICAST_DELEGATE(FOnStartSession);
	STUDIOTELEMETRY_API FOnStartSession& GetOnStartSession() { return OnStartSession; }

	DECLARE_MULTICAST_DELEGATE(FOnEndSession);
	STUDIOTELEMETRY_API FOnEndSession& GetOnEndSession() { return OnEndSession; }

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRecordEvent, const FString&, const TArray<FAnalyticsEventAttribute>&);
	STUDIOTELEMETRY_API FOnRecordEvent& GetOnRecordEvent() { return OnRecordEvent; }

	/** Scoped Span helper class **/
	class ScopedSpan
	{
	public:
		ScopedSpan(const FName Name, const TArray<FAnalyticsEventAttribute>& AdditionalAttributes = {} )
		{
			if (FStudioTelemetry::Get().IsAvailable())
			{
				Span = FStudioTelemetry::Get().StartSpan(Name, AdditionalAttributes);
			}
		}

		~ScopedSpan()
		{
			if (FStudioTelemetry::Get().IsAvailable())
			{
				FStudioTelemetry::Get().EndSpan(Span);
			}
		}
	private:

		TSharedPtr<IAnalyticsSpan> Span;
	};

private:

	/** IModuleInterface implementation */
	STUDIOTELEMETRY_API virtual void StartupModule()  final;
	STUDIOTELEMETRY_API virtual void ShutdownModule()  final;

	/** Starts a new analytics session*/
	void StartSession();

	/** Ends an existing analytics session*/
	void EndSession();

	/** Configure the plugin*/
	void LoadConfiguration();

	struct FConfig
	{
		bool bSendTelemetry = true; // Only send telemetry data if we have been requested to
		bool bSendUserData = false;  // Never send user data unless specifically asked to
		bool bSendHardwareData = true; // Always send hardware data unless specifically asked not to
		bool bSendOSData = true; // Always send operating system data unless specifically asked not to
	};
	
	FCriticalSection						CriticalSection;
	TSharedPtr<FAnalyticsProviderMulticast>	AnalyticsProvider;
	TSharedPtr<IAnalyticsTracer>			AnalyticsTracer;
	OnRecordEventCallback					RecordEventCallback;
	FGuid									SessionGUID;
	FConfig									Config;
	FOnStartSession							OnStartSession;
	FOnEndSession							OnEndSession;
	FOnRecordEvent							OnRecordEvent;
};

// Useful macros for scoped spans
#define STUDIO_TELEMETRY_SPAN_SCOPE(Name) FStudioTelemetry::ScopedSpan PREPROCESSOR_JOIN(ScopedSpan, __LINE__)(TEXT(#Name));
#define STUDIO_TELEMETRY_START_SPAN(Name) if (FStudioTelemetry::Get().IsAvailable()) { FStudioTelemetry::Get().StartSpan(TEXT(#Name));}
#define STUDIO_TELEMETRY_END_SPAN(Name) if (FStudioTelemetry::Get().IsAvailable()) { FStudioTelemetry::Get().EndSpan(TEXT(#Name));}
