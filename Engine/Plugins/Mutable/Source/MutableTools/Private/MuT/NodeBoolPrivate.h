// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodePrivate.h"

#include "MuT/NodeBool.h"
#include "MuT/NodeImage.h"
#include "MuT/NodeRange.h"

namespace mu
{


	class NodeBool::Private : public Node::Private
	{
	};


	class NodeBoolConstant::Private : public NodeBool::Private
	{
	public:

		static FNodeType s_type;

		bool m_value;
	};


	class NodeBoolParameter::Private : public NodeBool::Private
	{
	public:

		static FNodeType s_type;

		bool m_defaultValue;
		FString m_name;
		FString m_uid;

        TArray<Ptr<NodeRange>> m_ranges;
	};


	class NodeBoolNot::Private : public NodeBool::Private
	{
	public:

		static FNodeType s_type;

		Ptr<NodeBool> m_pSource;
	};


	class NodeBoolAnd::Private : public NodeBool::Private
	{
	public:

		static FNodeType s_type;

		Ptr<NodeBool> m_pA;
		Ptr<NodeBool> m_pB;
	};

}
