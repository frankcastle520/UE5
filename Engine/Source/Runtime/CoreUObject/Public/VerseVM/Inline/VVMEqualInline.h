// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/Inline/VVMIntInline.h"
#include "VerseVM/Inline/VVMValueInline.h"
#include "VerseVM/VVMFloat.h"
#include "VerseVM/VVMOption.h"
#include "VerseVM/VVMValue.h"

namespace Verse
{
template <typename HandlePlaceholderFunction>
inline bool VValue::Equal(FAllocationContext Context, VValue Left, VValue Right, HandlePlaceholderFunction HandlePlaceholder)
{
	if (Left.IsPlaceholder() || Right.IsPlaceholder())
	{
		HandlePlaceholder(Left, Right);
		return true;
	}
	if (Left == Right)
	{
		return true;
	}
	if (Left.IsFloat() && Right.IsFloat())
	{
		return Left.AsFloat() == Right.AsFloat();
	}
	if (Left.IsInt() || Right.IsInt())
	{
		return Left.IsInt() && Right.IsInt() && VInt::Eq(Context, Left.AsInt(), Right.AsInt());
	}
	if (Left.IsLogic() || Right.IsLogic())
	{
		return Left.IsLogic() && Right.IsLogic() && Left.AsBool() == Right.AsBool();
	}
	if (Left.IsEnumerator() || Right.IsEnumerator())
	{
		checkSlow(Left != Right);
		return false;
	}
	if (Left.IsCell() && Right.IsCell())
	{
		VCell* LeftCell = &Left.AsCell();
		VCell* RightCell = &Right.AsCell();

		if (LeftCell->IsA<VOption>())
		{
			return RightCell->IsA<VOption>()
				&& Equal(Context, LeftCell->StaticCast<VOption>().GetValue(), RightCell->StaticCast<VOption>().GetValue(), HandlePlaceholder);
		}
		return LeftCell->Equal(Context, RightCell, HandlePlaceholder);
	}

	return false;
}

} // namespace Verse
#endif // WITH_VERSE_VM
