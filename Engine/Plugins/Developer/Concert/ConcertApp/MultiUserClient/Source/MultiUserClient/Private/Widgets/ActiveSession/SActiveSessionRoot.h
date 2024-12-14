// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FSpawnTabArgs;
class FTabManager;
class FWorkspaceItem;
class IConcertSyncClient;
class SDockTab;
class SWidgetSwitcher;
class SWindow;

namespace UE::MultiUserClient::Replication { class FMultiUserReplicationManager; }

namespace UE::MultiUserClient
{
	/**
	 * Displayed when the client is connected to an active session.
	 * Manages the child content in tabs.
	 */
	class SActiveSessionRoot : public SCompoundWidget
	{
	public:
		
		static const FName SessionOverviewTabId;
		static const FName ReplicationTabId;

		SLATE_BEGIN_ARGS(SActiveSessionRoot)
		{}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, TSharedPtr<IConcertSyncClient> InConcertSyncClient, TSharedRef<Replication::FMultiUserReplicationManager> InReplicationManager);

	private:

		/** This switches "tabs" when a button in the "tab" area is changed. */
		TSharedPtr<SWidgetSwitcher> TabSwitcher;

		TSharedRef<SWidget> CreateTabArea();
	};
}


