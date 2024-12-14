// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAllClientsView.h"

#include "Selection/AllOnlineClientsSelectionModel.h"
#include "Replication/MultiUserReplicationManager.h"
#include "SMultiClientView.h"

namespace UE::MultiUserClient::Replication
{
	void SAllClientsView::Construct(const FArguments&, TSharedRef<IConcertClient> InConcertClient, FMultiUserReplicationManager& InMultiUserReplicationManager)
	{
		AllOnlineClientsModel = MakeUnique<FAllOnlineClientsSelectionModel>(*InMultiUserReplicationManager.GetOnlineClientManager());
		AllOfflineClientsModel = MakeUnique<FAllOfflineClientsSelectionModel>(*InMultiUserReplicationManager.GetOfflineClientManager());
		
		ChildSlot
		[
			SNew(SMultiClientView, InConcertClient, InMultiUserReplicationManager, *AllOnlineClientsModel, *AllOfflineClientsModel)
		];
	}
}
