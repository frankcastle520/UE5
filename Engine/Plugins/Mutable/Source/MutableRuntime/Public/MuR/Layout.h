// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuR/Serialisation.h"
#include "Containers/Array.h"
#include "Misc/AssertionMacros.h"
#include "Math/IntVector.h"
#include "Math/NumericLimits.h"

namespace mu
{

	//! Types of layout packing strategies 
	enum class EPackStrategy : uint32
	{
		Resizeable,
		Fixed,
		Overlay
	};

	//! Types of layout reduction methods 
	enum class EReductionMethod : uint32
	{
		Halve,	// Divide axis by 2
		Unitary	// Reduces 1 block the axis 
	};

	/** */
	struct FLayoutBlock
	{
		static constexpr uint64 InvalidBlockId = TNumericLimits<uint64>::Max();

		FIntVector2 Min = { 0, 0 };
		FIntVector2 Size = { 0, 0 };

		//! Absolute id used to control merging of various layouts
		uint64 Id;

		//! Priority value to control the shrink texture layout strategy
		int32 Priority;

		//! Value to control the method to reduce the block
		uint32 bReduceBothAxes : 1;

		//! Value to control if a block has to be reduced by two in an unitary reduction strategy
		uint32 bReduceByTwo : 1;

		/** Explicit padding to prevent uninitialized memory in this POD. */
		uint32 UnusedPadding : 30;

		/** */
		FLayoutBlock(FIntVector2 InMin = {}, FIntVector2 InSize = {})
		{
			Min = InMin;
			Size = InSize;
			Id = InvalidBlockId;
			Priority = 0;
			bReduceBothAxes = false;
			bReduceByTwo = false;
			UnusedPadding = 0;
		}

		//!
		inline bool operator==(const FLayoutBlock& o) const
		{
			return (Id == o.Id) && IsSimilar(o);
		}

		inline bool IsSimilar(const FLayoutBlock& o) const
		{
			// All but ids
			return (Min == o.Min) &&
				(Size == o.Size) &&
				(Priority == o.Priority) &&
				(bReduceBothAxes == o.bReduceBothAxes) &&
				(bReduceByTwo == o.bReduceByTwo);
		}
	};


    //! \brief Image block layout class.
    //!
    //! It contains the information about what blocks are defined in a texture layout (texture
    //! coordinates set from a mesh).
    //! It is usually not necessary to use this objects, except for some advanced cases.
	//! \ingroup runtime
    class MUTABLERUNTIME_API Layout : public Resource
	{
	public:

		//!
		FIntVector2 Size = FIntVector2(0, 0);

		/** Maximum size in layout blocks that this layout can grow to. From there on, blocks will shrink to fit. 
		* If 0,0 then no maximum size applies.
		*/
		FIntVector2 MaxSize = FIntVector2(0, 0);

		//!
		TArray<FLayoutBlock> Blocks;

		//! Packing strategy
		EPackStrategy Strategy = EPackStrategy::Resizeable;

		EReductionMethod ReductionMethod = EReductionMethod::Halve;

	public:

		//-----------------------------------------------------------------------------------------
		// Life cycle
		//-----------------------------------------------------------------------------------------

		Layout();

		//! Deep clone this layout.
		Ptr<Layout> Clone() const;

		//! Serialisation
		static void Serialise( const Layout* p, OutputArchive& arch );
		static Ptr<Layout> StaticUnserialise( InputArchive& arch );

		//! Full compare
		bool operator==( const Layout& other ) const;

		// Resource interface
		int32 GetDataSize() const override;

		//-----------------------------------------------------------------------------------------
		// Own interface
		//-----------------------------------------------------------------------------------------

		//! Get the resolution of the grid where the blocks are defined.
		FIntPoint GetGridSize() const;

		//! Get the resolution of the grid where the blocks are defined. It must be bigger than 0
		//! on each axis.
		//! \param sizeX width of the grid.
		//! \param sizeY height of the grid.
		void SetGridSize( int32 SizeX, int32 SizeY );

		//! Get the maximum resolution of the grid where the blocks are defined.
		//! \param[out] pSizeX The integer pointed by this will be set to the width of the grid.
		//! \param[out] pSizeY The integer pointed by this will be set to the height of the grid.
		void GetMaxGridSize(int32* SizeX, int32* SizeY) const;

		//! Get the maximum resolution of the grid where the blocks are defined. It must be bigger than 0
		//! on each axis.
		//! \param sizeX width of the grid.
		//! \param sizeY height of the grid.
		void SetMaxGridSize(int32 SizeX, int32 SizeY);

		//! Return the number of blocks in this layout.
		int32 GetBlockCount() const;

		//! Set the number of blocks in this layout.
		//! The existing blocks will be kept as much as possible. The new blocks will be undefined.
		void SetBlockCount( int32 );

		//! Set the texture layout packing strategy
		//! By default the texture layout packing strategy is set to resizable layout
		void SetLayoutPackingStrategy(EPackStrategy);

		//! Set the texture layout packing strategy
		EPackStrategy GetLayoutPackingStrategy() const;

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~Layout() {}

	public:


		//!
		void Serialise(OutputArchive& arch) const;

		//!
		void Unserialise(InputArchive& arch);

		//!
		bool IsSimilar(const Layout& o) const;

		/** Find a block by id. This converts the "absolute" id to a relative index to the layout blocks. Return -1 if not found. */
		int32 FindBlock(uint64 Id) const;

		//! Return true if the layout is a single block filling all area.
		bool IsSingleBlockAndFull() const;
	};

	MUTABLE_DEFINE_POD_SERIALISABLE(FLayoutBlock);
	MUTABLE_DEFINE_POD_VECTOR_SERIALISABLE(FLayoutBlock);

}

