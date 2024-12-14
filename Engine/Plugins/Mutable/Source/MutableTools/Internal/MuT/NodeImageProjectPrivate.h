// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeImageProject.h"

#include "MuT/NodeImagePrivate.h"
#include "MuT/NodeScalar.h"
#include "MuT/NodeColour.h"
#include "MuT/NodeMesh.h"
#include "MuT/NodeProjector.h"
#include "MuR/MutableMath.h"

namespace mu
{

	class NodeImageProject::Private : public NodeImage::Private
	{
	public:

		static FNodeType s_type;

		Ptr<NodeProjector> m_pProjector;
		Ptr<NodeMesh> m_pMesh;
		Ptr<NodeScalar> m_pAngleFadeStart;
		Ptr<NodeScalar> m_pAngleFadeEnd;
		Ptr<NodeImage> m_pImage;
		Ptr<NodeImage> m_pMask;
		FUintVector2 m_imageSize;
		uint8 m_layout = 0;
		bool bIsRGBFadingEnabled = true;
		bool bIsAlphaFadingEnabled = true;
		bool bEnableTextureSeamCorrection = true;
		ESamplingMethod SamplingMethod = ESamplingMethod::Point;
		EMinFilterMethod MinFilterMethod = EMinFilterMethod::None;
	};


}
