// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraitCore/ITraitInterface.h"
#include "TraitCore/TraitInterfaceUID.h"

namespace UE::AnimNext
{
	/**
	 * FTraitInterfaceRegistry
	 * 
	 * A global registry of all existing trait interfaces that can be used in animation graph traits.
	 * 
	 * @see FTraitInterface
	 */
	struct ANIMNEXT_API FTraitInterfaceRegistry final
	{
		// Access the global registry
		static FTraitInterfaceRegistry& Get();

		// Finds and returns the trait interface associated with the provided trait interface UID.
		// If the trait interface is not registered, nullptr is returned.
		const ITraitInterface* Find(FTraitInterfaceUID InterfaceUID) const;

		// Registers a trait interface dynamically
		void Register(const TSharedPtr<ITraitInterface>& TraitInterface);

		// Unregisters a trait intrerface dynamically
		void Unregister(const TSharedPtr<ITraitInterface>& TraitInterface);

		// Returns a list of all registered trait interfaces
		TArray<const ITraitInterface*> GetTraitInterfaces() const;

		// Returns the number of registered trait interfaces
		uint32 GetNum() const;

	private:
		FTraitInterfaceRegistry() = default;
		FTraitInterfaceRegistry(const FTraitInterfaceRegistry&) = delete;
		FTraitInterfaceRegistry(FTraitInterfaceRegistry&&) = default;
		FTraitInterfaceRegistry& operator=(const FTraitInterfaceRegistry&) = delete;
		FTraitInterfaceRegistry& operator=(FTraitInterfaceRegistry&&) = default;

		// Static init lifetime functions
		static void StaticRegister(const TSharedPtr<ITraitInterface>& TraitInterface);
		static void StaticUnregister(const TSharedPtr<ITraitInterface>& TraitInterface);

		// Module lifetime functions
		static void Init();
		static void Destroy();

		// Holds information for each registered trait
		struct FRegistryEntry
		{
			// A pointer to the trait
			TSharedPtr<ITraitInterface> TraitInterface = nullptr;
		};

		TMap<FTraitInterfaceUIDRaw, FRegistryEntry>	TraitInterfaceUIDToEntryMap;

		friend class FAnimNextModuleImpl;
		friend struct FTraitInterfaceStaticInitHook;
	};
}
