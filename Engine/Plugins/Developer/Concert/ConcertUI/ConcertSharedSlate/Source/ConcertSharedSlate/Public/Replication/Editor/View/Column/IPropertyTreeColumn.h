// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IReplicationTreeColumn.h"
#include "ReplicationColumnInfo.h"
#include "Replication/Editor/Model/Data/PropertyData.h"

namespace UE::ConcertSharedSlate
{
	/** Holds information relevant to a row. */
	struct FPropertyTreeRowContext
	{
		/** The data that is stored in the row */
		FPropertyData RowData;
	};
	
	using IPropertyTreeColumn = IReplicationTreeColumn<FPropertyTreeRowContext>;
	using FPropertyColumnEntry = TReplicationColumnEntry<FPropertyTreeRowContext>;
}
