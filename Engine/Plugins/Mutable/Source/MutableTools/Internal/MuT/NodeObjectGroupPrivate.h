// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeObjectPrivate.h"

#include "MuT/NodeObjectGroup.h"
#include "MuT/NodeLayout.h"


namespace mu
{
	MUTABLE_DEFINE_ENUM_SERIALISABLE(NodeObjectGroup::CHILD_SELECTION)


	class NodeObjectGroup::Private : public NodeObject::Private
	{
	public:

		static FNodeType s_type;

		FString Name;
		FString Uid;

		CHILD_SELECTION m_type;

		//! Set the child selection type
		void SetSelectionType( CHILD_SELECTION );

		TArray<NodeObjectPtr> m_children;
		int32 DefaultValue;

 	};

}
