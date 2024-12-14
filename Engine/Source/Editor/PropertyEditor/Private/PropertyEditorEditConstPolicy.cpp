// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertyEditorEditConstPolicy.h"
#include "PropertyNode.h"

namespace PropertyEditorPolicy
{
	void RegisterEditConstPolicy(IEditConstPolicy* Policy)
	{
		FPropertyNode::RegisterEditConstPolicy(Policy);
	}

	void UnregisterEditConstPolicy(IEditConstPolicy* Policy)
	{
		FPropertyNode::UnregisterEditConstPolicy(Policy);
	}

	bool IsPropertyEditConst(const FEditPropertyChain& PropertyChain, UObject* Object)
	{
		return FPropertyNode::IsPropertyEditConst(PropertyChain, Object);
	}

	bool IsPropertyEditConst(const FProperty* Property, UObject* Object)
	{
		return FPropertyNode::IsPropertyEditConst(Property, Object);
	}
}