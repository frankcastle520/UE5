// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodePrivate.h"
#include "MuT/NodeProjector.h"

#include "MuT/NodeRange.h"
#include "MuR/MutableMath.h"
#include "MuR/ParametersPrivate.h"


namespace mu
{


	class NodeProjector::Private : public Node::Private
	{
	};


	class NodeProjectorConstant::Private : public NodeProjector::Private
	{
	public:

		static FNodeType s_type;

        PROJECTOR_TYPE m_type = PROJECTOR_TYPE::PLANAR;
		FVector3f m_position;
		FVector3f m_direction;
		FVector3f m_up;
		FVector3f m_scale;
        float m_projectionAngle = 0.0f;
	};


	class NodeProjectorParameter::Private : public NodeProjectorConstant::Private
	{
	public:

		static FNodeType s_type;

		FString m_name;
		FString m_uid;

		TArray<Ptr<NodeRange>> m_ranges;
	};


}
