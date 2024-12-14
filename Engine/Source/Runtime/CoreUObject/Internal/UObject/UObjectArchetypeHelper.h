// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreFwd.h"
#include "UObject/Object.h"
#include "UObject/UObjectAnnotation.h"

class FObjectArchetypeHelper
{
public:
	class IObjectArchetypePolicy
	{
#if WITH_EDITOR
	public:
		virtual UObject* GetArchetype(const UObject* InObject) const = 0;
#endif
	};

#if WITH_EDITOR
private:
	// Restrict usage to FPropertyNode
	friend class FPropertyNode;
#endif
	COREUOBJECT_API static UObject* GetArchetype(const UObject* InObject, const IObjectArchetypePolicy* InPolicy);
};

#if WITH_EDITOR

struct FCacheArchetype
{
	bool IsDefault() const
	{
		return CachedArchetype == nullptr;
	}

	/** Archetype is cache when it is or its archetype is about to be replaced **/
	TObjectPtr<UObject> CachedArchetype = nullptr;
};

class FEditorCacheArchetypeManager
{
public:
	/**
	 * @return the static instance managing the reinstantiation */
	COREUOBJECT_API static FEditorCacheArchetypeManager& Get();

	/**
	 * Caches the archetype for this object during reinstantiation has we rename object in that phase.
	 * This is only a utility method to ensure the archetype is not lost during reinstantiation
	 * as it renames and changes outers which makes impossible to retrieve it during the process.
	 * @param Object top cache its archetype
	 * @param Archetype to use as a cache, if null it will use GetArchetype on the owner of the specified object */
	void CacheArchetype(gsl::not_null<const UObject*> Object, UObject* Archetype = nullptr)
	{
		FCacheArchetype Annotation = ObjectCachedArchetypeAnnotations.GetAnnotation(Object);
		if (Annotation.CachedArchetype == nullptr)
		{
			Annotation.CachedArchetype = Archetype ? Archetype : Object->GetArchetype();
			ObjectCachedArchetypeAnnotations.AddAnnotation(Object, Annotation);
		}
	}

	void ResetCacheArchetype(gsl::not_null<const UObject*> Object)
	{
		ObjectCachedArchetypeAnnotations.RemoveAnnotation(Object);
	}

	/**
	 * Gets the archetype that was cached when the CacheArchetype method was called
	 * @return the cached Archetype if any */
	UObject* GetCachedArchetype(gsl::not_null<const UObject*> Object)
	{
		return ObjectCachedArchetypeAnnotations.GetAnnotation(Object).CachedArchetype;
	}

private:
	FUObjectAnnotationSparse<FCacheArchetype, true/*bAutoRemove*/> ObjectCachedArchetypeAnnotations;
};

#endif // WITH_EDITOR
