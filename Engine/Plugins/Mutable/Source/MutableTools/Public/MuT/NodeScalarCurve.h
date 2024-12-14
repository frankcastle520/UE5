// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeScalar.h"

#include "Curves/RichCurve.h"

namespace mu
{

    /** This node makes a new scalar value transforming another scalar value with a curve. */
    class MUTABLETOOLS_API NodeScalarCurve : public NodeScalar
	{
	public:

		FRichCurve Curve;
		Ptr<NodeScalar> CurveSampleValue;

	public:

        NodeScalarCurve();

		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

        const FNodeType* GetType() const override;
		static const FNodeType* GetStaticType();

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;

	protected:

		//! Forbidden. Manage with the Ptr<> template.
        ~NodeScalarCurve();

	private:

		Private* m_pD;

	};


}
