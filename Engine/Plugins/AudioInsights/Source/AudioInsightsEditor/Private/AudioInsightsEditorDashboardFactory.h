// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioDefines.h"
#include "Containers/Map.h"
#include "Engine/World.h"
#include "Framework/Docking/TabManager.h"
#include "IAudioInsightsDashboardFactory.h"
#include "Templates/SharedPointer.h"
#include "Views/DashboardViewFactory.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SComboBox.h"

namespace UE::Audio::Insights
{
	class FEditorDashboardFactory : public IDashboardFactory, public TSharedFromThis<FEditorDashboardFactory>
	{
	public:
		FEditorDashboardFactory() = default;
		virtual ~FEditorDashboardFactory() = default;

		TSharedRef<SDockTab> MakeDockTabWidget(const FSpawnTabArgs& Args);

		virtual void RegisterViewFactory(TSharedRef<IDashboardViewFactory> InFactory) override;
		virtual void UnregisterViewFactory(FName InDashboardName) override;
		virtual ::Audio::FDeviceId GetDeviceId() const override;

		DECLARE_MULTICAST_DELEGATE(FOnActiveAudioDeviceChanged);
		inline static FOnActiveAudioDeviceChanged OnActiveAudioDeviceChanged;

	private:
		void OnWorldRegisteredToAudioDevice(const UWorld* InWorld, ::Audio::FDeviceId InDeviceId);
		void OnWorldUnregisteredFromAudioDevice(const UWorld* InWorld, ::Audio::FDeviceId InDeviceId);
		void OnDeviceDestroyed(::Audio::FDeviceId InDeviceId);

		void OnPIEStarted(bool bSimulating);
		void OnPostPIEStarted(bool bSimulating);
		void OnPIEStopped(bool bSimulating);

		void RefreshDeviceSelector();
		void ResetDelegates();

		TSharedRef<SWidget> MakeMenuBarWidget();
		TSharedRef<SWidget> MakeMainToolbarWidget();

		void InitDelegates();
		TSharedRef<FTabManager::FLayout> GetDefaultTabLayout();

		void RegisterTabSpawners();
		void UnregisterTabSpawners();

		TSharedRef<FTabManager::FLayout> LoadLayoutFromConfig();
		void SaveLayoutToConfig();

		FDelegateHandle OnDeviceDestroyedHandle;
		FDelegateHandle OnWorldRegisteredToAudioDeviceHandle;
		FDelegateHandle OnWorldUnregisteredFromAudioDeviceHandle;

		FDelegateHandle OnPIEStartedHandle;
		FDelegateHandle OnPostPIEStartedHandle;
		FDelegateHandle OnPIEStoppedHandle;

		bool bStartWithPIE = true;
		bool bStopWithPIE = true;

		TSharedPtr<FTabManager> DashboardTabManager;
		TSharedPtr<FWorkspaceItem> DashboardWorkspace;

		TArray<TSharedPtr<::Audio::FDeviceId>> AudioDeviceIds;
		TSharedPtr<SComboBox<TSharedPtr<::Audio::FDeviceId>>> AudioDeviceComboBox;

		::Audio::FDeviceId ActiveDeviceId = INDEX_NONE;

		TMap<FName, TSharedPtr<IDashboardViewFactory>> DashboardViewFactories;
	};
} // namespace UE::Audio::Insights
