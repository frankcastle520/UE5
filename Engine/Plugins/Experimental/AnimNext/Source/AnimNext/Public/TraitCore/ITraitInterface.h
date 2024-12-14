// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraitCore/TraitInterfaceUID.h"

namespace UE::AnimNext
{
	struct ITraitInterface;
}

// Helper macros

// In a trait interface struct declaration, this macro declares the necessary boilerplate we require
#define DECLARE_ANIM_TRAIT_INTERFACE(InterfaceName, InterfaceNameHash) \
	/* Globally unique UID for this interface */ \
	static constexpr UE::AnimNext::FTraitInterfaceUID InterfaceUID = UE::AnimNext::FTraitInterfaceUID(InterfaceNameHash, TEXT(#InterfaceName)); \
	virtual UE::AnimNext::FTraitInterfaceUID GetInterfaceUID() const override { return InterfaceUID; }

// Allows a trait interface to auto-register and unregister within the current execution scope
// The trait interface must be found in the current scope without a namespace qualification
#define AUTO_REGISTER_ANIM_TRAIT_INTERFACE(Interface) \
		UE::AnimNext::FTraitInterfaceStaticInitHook Interface##Hook(MakeShared<Interface>());


namespace UE::AnimNext
{
	struct FExecutionContext;	// Derived types will have functions that accept the execution context

	/**
	 * ITraitInterface
	 * 
	 * Base type for all trait interfaces. Used for type safety.
	 */
	struct ANIMNEXT_API ITraitInterface
	{
		virtual ~ITraitInterface() {}

		// The globally unique UID for this interface
		// Derived types will have their own InterfaceUID member that hides/aliases/shadows this one
		// @see DECLARE_ANIM_TRAIT_INTERFACE
		static constexpr FTraitInterfaceUID InterfaceUID = FTraitInterfaceUID(0xcdc27733, TEXT("ITraitInterface"));

		// Returns the globally unique UID for this interface
		virtual FTraitInterfaceUID GetInterfaceUID() const { return InterfaceUID; };

#if WITH_EDITOR
		// Internal interfaces are only displayed in the Traits Editor in the Advanced View
		virtual bool IsInternal() const { return false; }

		// Human readable interface names, in long and short format
		virtual const FText& GetDisplayName() const { ensure(false); return FText::GetEmpty(); }
		// Human readable interface names, in short format (ideally 3 or 4 letters, due to space restrictions in the editor)
		virtual const FText& GetDisplayShortName() const { ensure(false); return FText::GetEmpty(); }
#endif // WITH_EDITOR
	};


	/**
	 * Trait Stack Propagation
	 * 
	 * This enum can be used to signal whether an interface call should be forwarded to its parent
	 * on the trait stack or not.
	 */
	enum class ETraitStackPropagation
	{
		// Forward the call to our parent on the trait stack (if we have one)
		Continue,

		// Do not forward the call to our parent, execution stops
		Stop,
	};

	/**
	* FTraitInterfaceStaticInitHook
	*
	* Allows trait interfaces to automatically register/unregister within the current scope.
	* This can be used during static init.
	* See AUTO_REGISTER_ANIM_TRAIT_INTERFACE
	*/
	struct ANIMNEXT_API FTraitInterfaceStaticInitHook final
	{
		explicit FTraitInterfaceStaticInitHook(const TSharedPtr<UE::AnimNext::ITraitInterface>& InTraitInterface);
		~FTraitInterfaceStaticInitHook();

	private:
		TSharedPtr<ITraitInterface> TraitInterface = nullptr;
	};
}
