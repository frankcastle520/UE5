// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Serialisation.h"
#include "Containers/Array.h"
#include "HAL/PlatformMath.h"
#include "Math/Transform.h"
#include "Math/VectorRegister.h"
#include "Misc/AssertionMacros.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"

// Remove when removin deprecated data
#include <string>


namespace mu
{	
	// Forward references
    class Skeleton;

    typedef Ptr<Skeleton> SkeletonPtr;
    typedef Ptr<const Skeleton> SkeletonPtrConst;

	// Bone name identifier
	struct FBoneName
	{
		FBoneName() {};
		FBoneName(uint32 InID) : Id(InID) {};

		// Hash built from the bone name (FString)
		uint32 Id = 0;

		inline void Serialise(OutputArchive& arch) const
		{
			arch << Id;
		}

		inline void Unserialise(InputArchive& arch)
		{
			arch >> Id;
		}

		//!
		inline bool operator==(const FBoneName& Other) const
		{
			return Id == Other.Id;
		}
	};

	inline uint32 GetTypeHash(const FBoneName& Bone)
	{
		return Bone.Id;
	}


    //! \brief Skeleton object.
	//! \ingroup runtime
    class MUTABLERUNTIME_API Skeleton : public RefCounted
	{
	public:

		//-----------------------------------------------------------------------------------------
		// Life cycle
		//-----------------------------------------------------------------------------------------

		Skeleton() {}

        //! Deep clone this skeleton.
        Ptr<Skeleton> Clone() const;

		//! Serialisation
        static void Serialise( const Skeleton* p, OutputArchive& arch );
        static Ptr<Skeleton> StaticUnserialise( InputArchive& arch );


		//-----------------------------------------------------------------------------------------
		// Own interface
		//-----------------------------------------------------------------------------------------

        //! @return - Number of bones in the Skeleton
		int32 GetBoneCount() const;
        void SetBoneCount(int32 c);

		//! @return - FName of the bone at 'Index'. Only valid in the editor
		const FName GetDebugName(int32 Index) const;
		void SetDebugName(const int32 Index, const FName BoneName);

        //! Get and set the parent bone of each bone. The parent can be -1 if the bone is a root.
        int32 GetBoneParent(int32 boneIndex) const;
        void SetBoneParent(int32 boneIndex, int32 parentBoneIndex);

		//! @return - BoneName of the Bone at 'Index'.
		const FBoneName& GetBoneName(int32 Index) const;
		void SetBoneName(int32 Index, const FBoneName& BoneName);

		//! @return - Index in the Skeleton. INDEX_NONE if not found.
		int32 FindBone(const FBoneName& BoneName) const;
		

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~Skeleton() {}

	public:

		//! Deprecated
		TArray<std::string> m_bones_DEPRECATED;
		TArray<FTransform3f> m_boneTransforms_DEPRECATED;

		//! DEBUG. FNames of the bones. Only valid in the editor. Do not serialize.
		TArray<FName> DebugBoneNames;

		//! Array of bone identifiers. 
		TArray<FBoneName> BoneIds;

		//! For each bone, index of the parent bone in the bone vectors. -1 means no parent.
		//! This array must have the same size than the m_bones array.
		TArray<int16> BoneParents;

		//!
		void Serialise(OutputArchive& arch) const;

		//!
		void Unserialise(InputArchive& arch);

		//!
		inline bool operator==(const Skeleton& o) const
		{
			return BoneIds == o.BoneIds
				&& BoneParents == o.BoneParents;
		}
	};

}

