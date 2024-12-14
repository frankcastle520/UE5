// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IClientEnumerator.h"
#include "IStreamEnumerator.h"

namespace UE::ConcertSyncServer::Replication
{
	class IRegistrationEnumerator : public IStreamEnumerator, public IClientEnumerator
	{};
}