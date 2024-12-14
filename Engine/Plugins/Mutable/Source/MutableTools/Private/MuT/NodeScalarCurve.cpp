// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeScalarCurve.h"

#include "Misc/AssertionMacros.h"
#include "MuR/ParametersPrivate.h"
#include "MuT/NodePrivate.h"
#include "MuT/NodeScalar.h"
#include "MuT/NodeScalarCurvePrivate.h"


namespace mu
{

    FNodeType NodeScalarCurve::Private::s_type = FNodeType(Node::EType::ScalarCurve, NodeScalar::GetStaticType() );

    MUTABLE_IMPLEMENT_NODE( NodeScalarCurve )

}

