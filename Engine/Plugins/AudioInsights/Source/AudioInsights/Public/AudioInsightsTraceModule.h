// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Map.h"
#include "IAudioInsightsTraceModule.h"
#include "Templates/SharedPointer.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "UObject/NameTypes.h"

namespace UE::Audio::Insights
{
	class AUDIOINSIGHTS_API FTraceModule : public IAudioInsightsTraceModule
	{
	public:
		FTraceModule();
		virtual ~FTraceModule() = default;

		//~ Begin TraceServices::IModule interface
		virtual void GetModuleInfo(TraceServices::FModuleInfo& OutModuleInfo) override;
		virtual void OnAnalysisBegin(TraceServices::IAnalysisSession& Session) override;
		virtual void GetLoggers(TArray<const TCHAR *>& OutLoggers) override;
		virtual void GenerateReports(const TraceServices::IAnalysisSession& Session, const TCHAR* CmdLine, const TCHAR* OutputDirectory) override;
		virtual const TCHAR* GetCommandLineArgument() override { return TEXT("audiotrace"); }
		//~ End TraceServices::IModule interface

		template <typename TraceProviderType>
		TSharedPtr<TraceProviderType> FindAudioTraceProvider() const
		{
			return StaticCastSharedPtr<TraceProviderType>(TraceProviders.FindRef(TraceProviderType::GetName_Static()));
		}

		virtual void AddTraceProvider(TSharedPtr<FTraceProviderBase> TraceProvider) override;

		virtual TSharedRef<FTraceChannelManager> GetChannelManager() override;

		void StartTraceAnalysis() const override;
		bool IsTraceAnalysisActive() const override;
		void StopTraceAnalysis() const override;

		void SetFirstTimeStamp(double InFirstTimeStamp) { FirstTimeStamp = InFirstTimeStamp; }
		double GetFirstTimeStamp() const { return FirstTimeStamp; }

	private:
		static const FName GetName();

		static void DisableAllTraceChannels();
		static void EnableAudioInsightsTraceChannels();

		TSharedRef<FTraceChannelManager> ChannelManager;
		TMap<FName, TSharedPtr<FTraceProviderBase>> TraceProviders;

		double FirstTimeStamp = -TNumericLimits<double>::Min();
	};
} // namespace UE::Audio::Insights
