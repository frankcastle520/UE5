// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeImagePrivate.h"
#include "MuT/NodeImageNormalComposite.h"


namespace mu
{

	class NodeImageNormalComposite::Private : public NodeImage::Private
	{
	public:

		static FNodeType s_type;

		NodeImagePtr m_pBase;
		NodeImagePtr m_pNormal;
		float m_power;
		ECompositeImageMode m_mode;

	};

}
