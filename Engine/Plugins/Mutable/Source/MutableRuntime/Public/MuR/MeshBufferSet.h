// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Serialisation.h"
#include "Containers/Array.h"
#include "HAL/PlatformMath.h"
#include "MuR/MutableMemory.h"

#include "MuR/MemoryTrackingAllocationPolicy.h"

namespace mu::MemoryCounters
{
	struct MUTABLERUNTIME_API FMeshMemoryCounter
	{
		alignas(8) static inline std::atomic<SSIZE_T> Counter{0};
	};
}

namespace mu
{
	/** Supported formats for the elements in mesh buffers. **/
	typedef enum
	{

		MBF_NONE,
		MBF_FLOAT16,
		MBF_FLOAT32,

		MBF_UINT8,
		MBF_UINT16,
		MBF_UINT32,
		MBF_INT8,
		MBF_INT16,
		MBF_INT32,

		/** Integers interpreted as being in the range 0.0f to 1.0f */
		MBF_NUINT8,
		MBF_NUINT16,
		MBF_NUINT32,

		/** Integers interpreted as being in the range -1.0f to 1.0f */
		MBF_NINT8,
		MBF_NINT16,
		MBF_NINT32,

        /** Packed 1 to -1 value using multiply+add (128 is almost zero). Use 8-bit unsigned ints. */
        MBF_PACKEDDIR8,

        /** 
		 * Same as MBF_PACKEDDIR8, with the w component replaced with the sign of the determinant
         * of the vertex basis to define the orientation of the tangent space in UE4 format.
         * Use 8-bit unsigned ints.
		*/
        MBF_PACKEDDIR8_W_TANGENTSIGN,

        /** Packed 1 to -1 value using multiply+add (128 is almost zero). Use 8-bit signed ints. */
        MBF_PACKEDDIRS8,

        /** 
		 * Same as MBF_PACKEDDIRS8, with the w component replaced with the sign of the determinant
         * of the vertex basis to define the orientation of the tangent space in UE4 format.
         * Use 8-bit signed ints.
		 */
        MBF_PACKEDDIRS8_W_TANGENTSIGN,

		MBF_FLOAT64,
		MBF_UINT64,
		MBF_INT64,
		MBF_NUINT64,
		MBF_NINT64,

		MBF_COUNT,

		_MBF_FORCE32BITS = 0xFFFFFFFF

	} EMeshBufferFormat;

	/** */
	struct FMeshBufferFormatData
	{
		/** Size per component in bytes. */
		uint8 SizeInBytes;

		/** log 2 of the max value if integer. */
		uint8 MaxValueBits;
	};

	MUTABLERUNTIME_API const FMeshBufferFormatData& GetMeshFormatData(EMeshBufferFormat Format);


	/** Semantics of the mesh buffers */
	typedef enum
	{

		MBS_NONE,

		/** For index buffers, and mesh morphs */
		MBS_VERTEXINDEX,

		/** Standard vertex semantics */
		MBS_POSITION,
		MBS_NORMAL,
		MBS_TANGENT,
		MBS_BINORMAL,
		MBS_TEXCOORDS,
		MBS_COLOUR,
		MBS_BONEWEIGHTS,
		MBS_BONEINDICES,

		/**
		 * Internal semantic indicating what layout block each vertex belongs to.
		 * It can be safely ignored if present in meshes returned by the system.
		 * It will never be in the same buffer that other vertex semantics.
		 */
		MBS_LAYOUTBLOCK,

		MBS_CHART_DEPRECATED,

		/** 
		 * To let users define channels with semantics unknown to the system.
		 * These channels will never be transformed, and the per-vertex or per-index data will be
		 * simply copied.
		 */
		MBS_OTHER,

        /** Sign to define the orientation of the tangent space. **/
        MBS_TANGENTSIGN_DEPRECATED,

		/** Semantics usefule for mesh binding. */
		MBS_TRIANGLEINDEX,
		MBS_BARYCENTRICCOORDS,
		MBS_DISTANCE,

		/** Semantics useful for alternative skin weight profiles. */
		MBS_ALTSKINWEIGHT,

		/** Utility */
		MBS_COUNT,

        _MBS_FORCE32BITS = 0xFFFFFFFF
	} EMeshBufferSemantic;


	/** */
	struct FMeshBufferChannel
	{
		FMeshBufferChannel()
		{
			Semantic = MBS_NONE;
			Format = MBF_NONE;
			SemanticIndex = 0;
			Offset = 0;
			ComponentCount = 0;
		}

		EMeshBufferSemantic Semantic;

		EMeshBufferFormat Format;

		/** Index of the semantic, in case there are more than one of this type. */
		int32 SemanticIndex;

		/** Offset in bytes from the begining of a buffer element */
		uint16 Offset;

		/** Number of components of the type in Format for every value in the channel */
		uint16 ComponentCount;

		inline bool operator==(const FMeshBufferChannel& Other) const
		{
			return (Semantic == Other.Semantic) &&
				(Format == Other.Format) &&
				(SemanticIndex == Other.SemanticIndex) &&
				(Offset == Other.Offset) &&
				(ComponentCount == Other.ComponentCount);
		}

	};


	struct FMeshBuffer
	{
		template<typename Type>
		using TMemoryTrackedArray = TArray<Type, FDefaultMemoryTrackingAllocator<MemoryCounters::FMeshMemoryCounter>>;

		TArray<FMeshBufferChannel> Channels;
		TMemoryTrackedArray<uint8> Data;
		uint32 ElementSize = 0;

		void Serialise(mu::OutputArchive& Arch) const;
		inline void Unserialise(mu::InputArchive& Arch);

		inline bool operator==(const FMeshBuffer& Other) const
		{
			bool bEqual = (Channels == Other.Channels);
			bEqual = bEqual && (ElementSize == Other.ElementSize);
			bEqual = bEqual && (Data == Other.Data);

			return bEqual;
		}

		/** Return true if the buffer has any channel with the passed semantic. */
		inline bool HasSemantic(EMeshBufferSemantic Semantic) const
		{
			for (const FMeshBufferChannel& Channel : Channels)
			{
				if (Channel.Semantic == Semantic)
				{
					return true;
				}
			}
			return false;
		}

		inline bool HasSameFormat(const FMeshBuffer& Other) const
		{
			return (Channels == Other.Channels && ElementSize == Other.ElementSize);
		}

		inline bool HasPadding() const
		{
			uint32 ActualElementSize = 0;
			for (const FMeshBufferChannel& Channel : Channels)
			{
				ActualElementSize += Channel.ComponentCount * GetMeshFormatData(Channel.Format).SizeInBytes;
			}
			check(ActualElementSize <= ElementSize);
			return ActualElementSize < ElementSize;
		}
	};


	/** Set of buffers storing mesh element data. Elements can be vertices, indices or faces. */
	class MUTABLERUNTIME_API FMeshBufferSet
	{
	public:

		uint32 ElementCount = 0;
		TArray<FMeshBuffer> Buffers;

		void Serialise(OutputArchive& Arch) const;
		void Unserialise(InputArchive& Arch);

		inline bool operator==(const FMeshBufferSet& Other) const
		{
			return ElementCount == Other.ElementCount && Buffers == Other.Buffers;
		}

	public:

		/** Get the number of elements in the buffers */
		int32 GetElementCount() const;

		/** 
		* Set the number of vertices in the mesh. This will resize the vertex buffers keeping the
		* previous data when possible. New data content is defined by MemoryInitPolicy.
		*/
		void SetElementCount(int32 Count, EMemoryInitPolicy MemoryInitPolicy = EMemoryInitPolicy::Uninitialized);

		/**
		* Get the size in bytes of a buffer element.
		* @param buffer index of the buffer from 0 to GetBufferCount()-1
		*/
		int32 GetElementSize(int32 Buffer) const;

		/** Get the number of vertex buffers in the mesh */
		int32 GetBufferCount() const;

		/** Set the number of vertex buffers in the mesh. */
		void SetBufferCount(int32 Count);

		/**
		* Get the number of channels in a vertex buffer.
		* \param buffer index of the vertex buffer from 0 to GetBufferCount()-1
		*/
		int32 GetBufferChannelCount(int32 BufferIndex) const;

		/**
		 * Get a channel of a buffer by index
		 * \param buffer index of the vertex buffer from 0 to GetBufferCount()-1
		 * \param channel index of the channel from 0 to GetBufferChannelCount( buffer )-1
		 * \param[out] pSemantic semantic of the channel
		 * \param[out] pSemanticIndex index of the semantic in case of having more than one of the
		 *				same type.
		 * \param[out] pFormat data format of the channel
		 * \param[out] pComponentCount components of an element of the channel
		 * \param[out] pOffset offset in bytes from the beginning of an element of the buffer
		 */
		void GetChannel(
				int32 BufferIndex,
				int32 ChannelIndex,
				EMeshBufferSemantic* SemanticPtr,
				int32* SemanticIndexPtr,
				EMeshBufferFormat* FormatPtr,
				int32* ComponentCountPtr,
				int32* OffsetPtr
			) const;

		/** 
		 * Set all the channels of a buffer
		 * \param buffer index of the buffer from 0 to GetBufferCount()-1
         * \param elementSize sizei n bytes of a vertex element in this buffer
         * \param channelCount number of channels to set in the buffer
         * \param pSemantics buffer of channelCount semantics
         * \param pSemanticIndices buffer of indices for the semantic of every channel
         * \param pFormats buffer of channelCount formats
         * \param pComponentCounts buffer of channelCount component counts
         * \param pOffsets offsets in bytes of every particular channel inside the buffer element
		 */ 
        void SetBuffer(
				int32 BufferIndex,
				int32 ElementSize,
				int32 ChannelCount,
				const EMeshBufferSemantic* SemanticsPtr = nullptr,
				const int32* SemanticIndicesPtr = nullptr,
				const EMeshBufferFormat* FormatsPtr = nullptr,
				const int32* ComponentCountsPtr = nullptr,
				const int32* OffsetsPtr = nullptr,
				EMemoryInitPolicy MemoryInitPolicy = EMemoryInitPolicy::Uninitialized);

		/**
		 * Set one  channels of a buffer
		 * \param buffer index of the buffer from 0 to GetBufferCount()-1
         * \param elementSize sizei n bytes of a vertex element in this buffer
         * \param channelIndex number of channels to set in the buffer
		 */
        void SetBufferChannel(
				int32 BufferIndex,
				int32 ChannelIndex,
				EMeshBufferSemantic Semantic,
				int32 SemanticIndex,
				EMeshBufferFormat Format,
				int32 ComponentCount,
				int32 Offset
			);

		/** 
		 * Get a pointer to the object-owned data of a buffer.
		 * Channel data is interleaved for every element and packed in the order it was set
		 * without any padding.
		 * \param buffer index of the buffer from 0 to GetBufferCount()-1
		 * \todo Add padding support for better alignment of buffer elements.
		*/
        uint8* GetBufferData(int32 Buffer);
		const uint8* GetBufferData(int32 Buffer) const;
		uint32 GetBufferDataSize(int32 Buffer) const;

		/** Utility methods */

		/** 
		 * Find the index of a buffer channel by semantic and relative index inside the semantic.
         * \param semantic Semantic of the channel we are searching.
         * \param semanticIndex Index of the semantic of the channel we are searching. e.g. if we
         *         want the second set of texture coordinates, it should be 1.
         * \param[out] pBuffer -1 if the channel is not found, otherwise it will contain the index
		 * of the buffer where the channel was found.
		 * \param[out] pChannel -1 if the channel is not found, otherwise it will contain the
		 * channel index of the channel inside the buffer returned at [buffer]
		 */
		void FindChannel(EMeshBufferSemantic Semantic, int32 SemanticIndex, int32* BufferPtr, int32* ChannelPtr) const;

		/**
		 * Get the offset in bytes of the data of this channel inside an element data.
		 * \param buffer index of the buffer from 0 to GetBufferCount()-1
		 * \param channel index of the channel from 0 to GetBufferChannelCount( buffer )-1
		 */ 
		int32 GetChannelOffset(int32 Buffer, int32 Channel) const;

		/**
		 * Add a new buffer by cloning a buffer from another set.
		 * The buffers must have the same number of elements.
		 */ 
		void AddBuffer( const FMeshBufferSet& Other, int32 BufferIndex);

		/** Return true if the formats of the two vertex buffers set match. **/
		bool HasSameFormat(const FMeshBufferSet& Other) const;

		/** 
		 * Remove the buffer at the specified position. This "invalidates" any buffer index that
		 * was referencing buffers after the removed one.
		 */ 
		void RemoveBuffer(int32 BufferIndex);

	public:

		/**
		 * Copy an element from one position to another, overwriting the other element.
		 * Both positions must be valid, buffer size won't change.
		 */ 
		void CopyElement(uint32 FromIndex, uint32 ToIndex);

		/** Compare the format of the two buffers at index buffer and return true if they match. **/
		bool HasSameFormat(int32 ThisBufferIndex, const FMeshBufferSet& pOther, int32 OtherBufferIndex) const;

		/** Get the total memory size of the buffers and this struct */
		int32 GetDataSize() const;

		/** */
		int32 GetAllocatedSize() const;

		/** 
		 * Compare the mesh buffer with another one, but ignore internal data like generated
		 * vertex indices.
		 */ 
		bool IsSpecialBufferToIgnoreInSimilar(const FMeshBuffer& Buffer) const;

		/** 
		 * Compare the mesh buffer with another one, but ignore internal data like generated
		 * vertex indices. Be aware this method compares the data byte by byte without checking
		 * if the data belong to the buffer components and could give false negatives if unset 
		 * padding data is present.
		 */ 
		bool IsSimilar(const FMeshBufferSet& Other) const;

		/** 
		 * Compare the mesh buffer with another one, but ignore internal data like generated
		 * vertex indices. This version compares the data component-wise, skipping any memory
		 * not specified in the buffer description.
		 */
		bool IsSimilarRobust(const FMeshBufferSet& Other, bool bCompareUVs) const;

		/** 
		 * Change the buffer descriptions so that all buffer indices start at 0 and are in the
		 * same order than memory.
		 */
		void ResetBufferIndices();

		/** */
		void UpdateOffsets(int32 BufferIndex);
		
		/** Check that all channels of a specific semantic use the provided format. */
		bool HasAnySemanticWithDifferentFormat(EMeshBufferSemantic Semantic, EMeshBufferFormat ExpectedFormat) const;
	};	

	
	MUTABLE_DEFINE_POD_SERIALISABLE(FMeshBufferChannel);
	MUTABLE_DEFINE_POD_VECTOR_SERIALISABLE(FMeshBufferChannel);
	MUTABLE_DEFINE_ENUM_SERIALISABLE(EMeshBufferFormat);
	MUTABLE_DEFINE_ENUM_SERIALISABLE(EMeshBufferSemantic);
}

