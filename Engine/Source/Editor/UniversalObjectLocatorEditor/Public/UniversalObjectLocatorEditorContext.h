// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

class UObject;
class IPropertyHandle;

namespace UE::UniversalObjectLocator
{

class UNIVERSALOBJECTLOCATOREDITOR_API ILocatorFragmentEditorContext : public TSharedFromThis<ILocatorFragmentEditorContext>
{
public:
	virtual ~ILocatorFragmentEditorContext() = default;

	// Get a context object that can be used to resolve partial fragments at edit time
	virtual UObject* GetContext(const IPropertyHandle& InPropertyHandle) const = 0;

	// Check whether the supplied locator fragment is supported in this context
	virtual bool IsFragmentAllowed(FName InFragmentName) const { return true; }
};

} // namespace UE::UniversalObjectLocator