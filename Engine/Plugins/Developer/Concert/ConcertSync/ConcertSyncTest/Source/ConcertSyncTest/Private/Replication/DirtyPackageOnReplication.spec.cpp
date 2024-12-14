// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Util/ConcertClientReplicationBridgeMock.h"
#include "Util/Spec/ObjectTestReplicator.h"
#include "Util/Spec/ReplicationClient.h"
#include "Util/Spec/ReplicationServer.h"

namespace UE::ConcertSyncTests::Replication::UI
{
	BEGIN_DEFINE_SPEC(FDirtyPackageOnReplication, "Editor.Concert.Replication.DirtyPackageOnReplication", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
		UPackage* Package;
		TUniquePtr<FObjectTestReplicator> Replicator;
		TUniquePtr<FReplicationServer> Server;
	
		FReplicationClient* Sender;
		FReplicationClient* Receiver;

		const FGuid StreamId = FGuid::NewGuid();
	END_DEFINE_SPEC(FDirtyPackageOnReplication);

	/**
	 * This tests that packages are dirtied whenever anything is replicated in them.
	 * This is important so Multi-User reverts the package back to its original state when leaving replication.
	 */
	void FDirtyPackageOnReplication::Define()
	{
		BeforeEach([this]
		{
			Package = NewObject<UPackage>(nullptr, *FString::Printf(TEXT("/Engine/Transient/%s"), *FGuid::NewGuid().ToString()), RF_Transient);
			Replicator = MakeUnique<FObjectTestReplicator>(Package);
			Server = MakeUnique<FReplicationServer>(*this);
			Sender = &Server->ConnectClient();
			Receiver = &Server->ConnectClient();

			Sender->JoinReplication(Replicator->CreateSenderArgs(StreamId));
			Receiver->JoinReplicationAsListener({ Replicator->TestObject });
			Sender->GetBridgeMock().InjectAvailableObject(*Replicator->TestObject);
			Sender->GetClientReplicationManager().TakeAuthorityOver({ Replicator->TestObject });

			Package->ClearDirtyFlag();
			TestTrue(TEXT("Test set-up correctly"), !Package->IsDirty());
		});
		AfterEach([this]
		{
			Server.Reset();
		});

		It("Package is marked dirty when replicated", [this]
		{
			Replicator->SimulateSendObjectToReceiver(*this, { *Sender, *Server, *Receiver }, { StreamId });
			Replicator->TestValuesWereReplicated(*this);

			TestTrue(TEXT("IsDirty()"), Package->IsDirty());
		});

		It("Package is re-marked dirty when saved in between replication", [this]
		{
			Replicator->SimulateSendObjectToReceiver(*this, { *Sender, *Server, *Receiver }, { StreamId });
			Replicator->TestValuesWereReplicated(*this);

			// Simulate a save
			Package->ClearDirtyFlag();

			Replicator->SimulateSendObjectToReceiver(*this, { *Sender, *Server, *Receiver }, { StreamId });
			Replicator->TestValuesWereReplicated(*this);
			
			TestTrue(TEXT("IsDirty()"), Package->IsDirty());
		});
	}
}
