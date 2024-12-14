// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Iris/ReplicationSystem/ReplicationFragment.h"

namespace UE::Net::Private
{
	struct FFragmentRegistrationContextPrivateAccessor
	{
		static const FReplicationFragments& GetReplicationFragments(const FFragmentRegistrationContext& Context) { return Context.Fragments; }
		static const Private::FReplicationStateDescriptorRegistry* GetReplicationStateRegistry(const FFragmentRegistrationContext& Context) { return Context.ReplicationStateRegistry; }
		static Private::FReplicationStateDescriptorRegistry* GetReplicationStateRegistry(FFragmentRegistrationContext& Context) { return Context.ReplicationStateRegistry; }
		static UReplicationSystem* GetReplicationSystem(const FFragmentRegistrationContext& Context) { return Context.ReplicationSystem; }
	};
}
