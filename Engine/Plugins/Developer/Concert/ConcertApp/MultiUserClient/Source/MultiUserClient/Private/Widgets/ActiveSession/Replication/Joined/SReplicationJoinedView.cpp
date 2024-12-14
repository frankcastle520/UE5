// Copyright Epic Games, Inc. All Rights Reserved.

#include "SReplicationJoinedView.h"

#include "IConcertSyncClient.h"
#include "Replication/MultiUserReplicationManager.h"
#include "Widgets/ActiveSession/Replication/Client/Multi/SAllClientsView.h"

namespace UE::MultiUserClient::Replication
{
	void SReplicationJoinedView::Construct(
		const FArguments& InArgs,
		const TSharedRef<FMultiUserReplicationManager>& InReplicationManager,
		const TSharedRef<IConcertSyncClient>& InClient
		)
	{
		ChildSlot
		[
			SNew(SAllClientsView, InClient->GetConcertClient(), *InReplicationManager)
		];
	}
}