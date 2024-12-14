// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuR/Layout.h"

#include "HAL/LowLevelMemTracker.h"
#include "Math/IntPoint.h"
#include "MuR/MutableMath.h"
#include "MuR/SerialisationPrivate.h"


namespace mu {

	MUTABLE_IMPLEMENT_POD_SERIALISABLE(FLayoutBlock);
	MUTABLE_IMPLEMENT_POD_VECTOR_SERIALISABLE(FLayoutBlock);

	//---------------------------------------------------------------------------------------------
	Layout::Layout()
	{
	}


	//---------------------------------------------------------------------------------------------
	void Layout::Serialise( const Layout* p, OutputArchive& arch )
	{
		arch << *p;
	}


	//---------------------------------------------------------------------------------------------
	Ptr<Layout> Layout::StaticUnserialise( InputArchive& arch )
	{
		LLM_SCOPE_BYNAME(TEXT("MutableRuntime"));
		Ptr<Layout> pResult = new Layout();
		arch >> *pResult;
		return pResult;
	}


	//---------------------------------------------------------------------------------------------
	Ptr<Layout> Layout::Clone() const
	{
		LLM_SCOPE_BYNAME(TEXT("MutableRuntime"));
		Ptr<Layout> pResult = new Layout();
		pResult->Size = Size;
		pResult->MaxSize = MaxSize;
		pResult->Blocks = Blocks;
		pResult->Strategy = Strategy;
		pResult->ReductionMethod = ReductionMethod;
		return pResult;
	}


	//---------------------------------------------------------------------------------------------
	bool Layout::operator==( const Layout& o ) const
	{
		return (Size == o.Size) &&
			(MaxSize == o.MaxSize) &&
			(Blocks == o.Blocks) &&
			(Strategy == o.Strategy) &&
			// maybe this is not needed
			(ReductionMethod==o.ReductionMethod);
	}


	//---------------------------------------------------------------------------------------------
	int32 Layout::GetDataSize() const
	{
		return sizeof(Layout) + Blocks.GetAllocatedSize();
	}


	//---------------------------------------------------------------------------------------------
	FIntPoint Layout::GetGridSize() const
	{
		return FIntPoint(Size[0], Size[1]);
	}


	//---------------------------------------------------------------------------------------------
	void Layout::SetGridSize( int32 sizeX, int32 sizeY )
	{
		check( sizeX>=0 && sizeY>=0 );
		Size[0] = (uint16)sizeX;
		Size[1] = (uint16)sizeY;
	}


	//---------------------------------------------------------------------------------------------
	void Layout::GetMaxGridSize(int32* SizeX, int32* SizeY) const
	{
		check(SizeX && SizeY);

		if (SizeX && SizeY)
		{
			*SizeX = MaxSize[0];
			*SizeY = MaxSize[1];
		}
	}


	//---------------------------------------------------------------------------------------------
	void Layout::SetMaxGridSize(int32 sizeX, int32 sizeY)
	{
		check(sizeX >= 0 && sizeY >= 0);
		MaxSize[0] = (uint16)sizeX;
		MaxSize[1] = (uint16)sizeY;
	}


	//---------------------------------------------------------------------------------------------
	int32 Layout::GetBlockCount() const
	{
		return Blocks.Num();
	}


	//---------------------------------------------------------------------------------------------
	void Layout::SetBlockCount( int32 n )
	{
		check( n>=0 );
		LLM_SCOPE_BYNAME(TEXT("MutableRuntime"));
		Blocks.SetNum( n );
	}


	//---------------------------------------------------------------------------------------------
	void Layout::SetLayoutPackingStrategy(EPackStrategy InStrategy)
	{
		Strategy = InStrategy;
	}


	//---------------------------------------------------------------------------------------------
	EPackStrategy Layout::GetLayoutPackingStrategy() const
	{
		return Strategy;
	}


	//---------------------------------------------------------------------------------------------
	void Layout::Serialise(OutputArchive& arch) const
	{
		arch << Size;
		arch << Blocks;

		arch << MaxSize;
		arch << uint32(Strategy);
		arch << uint32(ReductionMethod);
	}

	
	//---------------------------------------------------------------------------------------------
	void Layout::Unserialise(InputArchive& arch)
	{
		arch >> Size;
		arch >> Blocks;
		arch >> MaxSize;

		uint32 Temp;
		arch >> Temp;
		Strategy = EPackStrategy(Temp);

		arch >> Temp;
		ReductionMethod = EReductionMethod(Temp);
	}


	//---------------------------------------------------------------------------------------------
	bool Layout::IsSimilar(const Layout& o) const
	{
		if (Size != o.Size || MaxSize != o.MaxSize ||
			Blocks.Num() != o.Blocks.Num() || Strategy != o.Strategy)
			return false;

		for (int32 i = 0; i < Blocks.Num(); ++i)
		{
			if (!Blocks[i].IsSimilar(o.Blocks[i])) return false;
		}

		return true;

	}


	//---------------------------------------------------------------------------------------------
	int32 Layout::FindBlock(uint64 Id) const
	{
		for (int32 i = 0; i < Blocks.Num(); ++i)
		{
			if (Blocks[i].Id == Id)
			{
				return i;
			}
		}

		return -1;
	}


	//---------------------------------------------------------------------------------------------
	bool Layout::IsSingleBlockAndFull() const
	{
		if (Blocks.Num() == 1
			&& Blocks[0].Min == FIntVector2(0, 0)
			&& Blocks[0].Size == Size)
		{
			return true;
		}
		return false;
	}


}

