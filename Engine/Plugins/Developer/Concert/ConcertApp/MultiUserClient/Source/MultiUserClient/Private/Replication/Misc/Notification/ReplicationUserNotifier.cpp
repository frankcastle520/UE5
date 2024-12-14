// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReplicationUserNotifier.h"

namespace UE::MultiUserClient::Replication
{
	FReplicationUserNotifier::FReplicationUserNotifier(
		IConcertClient& InClient,
		FOnlineClientManager& InReplicationClientManager,
		FMuteStateManager& InMuteManager
		)
		: SubmissionNotifier(InReplicationClientManager)
		, MutingNotifier(InMuteManager)
		, DuplicateClientNameNotifier(InClient, InReplicationClientManager)
	{}
}
