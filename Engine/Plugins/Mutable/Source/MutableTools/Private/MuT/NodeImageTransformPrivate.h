// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "MuR/ImagePrivate.h"
#include "MuT/NodeImagePrivate.h"
#include "MuT/NodeImageTransform.h"
#include "MuT/NodeScalar.h"

namespace mu
{


	class NodeImageTransform::Private : public NodeImage::Private
	{
	public:

		static FNodeType s_type;

		NodeImagePtr m_pBase;
		NodeScalarPtr m_pOffsetX;
		NodeScalarPtr m_pOffsetY;
		NodeScalarPtr m_pScaleX;
		NodeScalarPtr m_pScaleY;
		NodeScalarPtr m_pRotation;

		EAddressMode AddressMode = EAddressMode::Wrap;
		uint32 SizeX = 0;
		uint32 SizeY = 0;
		
		bool bKeepAspectRatio = false;
		uint8 UnusedPadding[sizeof(NodeImagePtr) - sizeof(bool)] = {0}; 
		static_assert(sizeof(NodeImagePtr) - sizeof(bool) >= 1);

	};


}
