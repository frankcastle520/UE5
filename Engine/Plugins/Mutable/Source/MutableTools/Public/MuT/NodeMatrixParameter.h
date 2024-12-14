// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeMatrix.h"

#include "MuT/NodeRange.h"
#include "Math/Matrix.h"


namespace mu
{

	class MUTABLETOOLS_API NodeMatrixParameter : public NodeMatrix
	{
	public:
		FMatrix44f DefaultValue;
		FString Name;
		FString Uid;

		TArray<Ptr<NodeRange>> Ranges;
		
		// Node interface
		const FNodeType* GetType() const override { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:
		//! Forbidden. Manage with the Ptr<> template.
		virtual ~NodeMatrixParameter() override = default;

	private:

		static FNodeType StaticType;
	};

}
