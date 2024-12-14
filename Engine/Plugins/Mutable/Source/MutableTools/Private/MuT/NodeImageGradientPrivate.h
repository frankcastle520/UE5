// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeColour.h"
#include "MuT/NodeImagePrivate.h"
#include "MuT/NodeImageGradient.h"
#include "MuR/MutableMath.h"

namespace mu
{


	class NodeImageGradient::Private : public NodeImage::Private
	{
	public:

		static FNodeType s_type;

		NodeColourPtr m_pColour0;
		NodeColourPtr m_pColour1;
		FIntVector2 m_size = { 256,1 };		

	};


}
