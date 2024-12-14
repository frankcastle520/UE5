// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeMatrix.h"
#include "Math/Matrix.h"

namespace mu
{

	class MUTABLETOOLS_API NodeMatrixConstant : public NodeMatrix
	{
	public:
		FMatrix44f Value;

		// Node interface
		const FNodeType* GetType() const override { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		virtual ~NodeMatrixConstant() override = default;

	private:

		static FNodeType StaticType;
	};

}
