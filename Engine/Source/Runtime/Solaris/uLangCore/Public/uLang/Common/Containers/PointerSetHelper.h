// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Containers/Array.h"

namespace uLang
{

/**
 * Useful helper function for maintaining sets of pointers
 **/
template<typename ElementType, typename KeyType>
class TPointerSetHelper
{
public:

    using PointerStorageType = TArray<ElementType *>;

    // Merge other sorted array into this array
    static int32_t GetUpperBound(const PointerStorageType& This, const KeyType& Key)
    {
        int32_t Lo = 0;
        int32_t Hi = This.Num();
        ElementType const * const * Elements = This.GetData();
        if (Hi > 0)
        {
            while (true)
            {
                int32_t Mid = (Lo + Hi) >> 1;
                if (KeyType(*Elements[Mid]) < Key)
                {
                    if (Mid == Lo) break;
                    Lo = Mid;
                }
                else
                {
                    Hi = Mid;
                    if (Mid == Lo) break;
                }
            }
        }
        return Hi;
    }

    // Merge other sorted array into this array
    static void Merge(PointerStorageType& This, const PointerStorageType& Other)
    {
        // Begin from end and move backwards
        int32_t SrcIndexThis = This.Num() - 1;
        int32_t SrcIndexOther = Other.Num() - 1;
        This.ResizeTo(This.Num() + Other.Num());
        ElementType* ItemThis = SrcIndexThis >= 0 ? This[SrcIndexThis] : nullptr;
        ElementType* ItemOther = SrcIndexOther >= 0 ? Other[SrcIndexOther] : nullptr;
        for (int32_t DstIndex = This.Num() - 1; DstIndex >= 0; --DstIndex)
        {
            ElementType* Item;
            if (ItemThis && (!ItemOther || KeyType(*ItemThis) > KeyType(*ItemOther)))
            {
                Item = ItemThis;
                --SrcIndexThis;
                ItemThis = SrcIndexThis >= 0 ? This[SrcIndexThis] : nullptr;
            }
            else
            {
                Item = ItemOther;
                --SrcIndexOther;
                ItemOther = SrcIndexOther >= 0 ? Other[SrcIndexOther] : nullptr;
            }
            This[DstIndex] = Item;
        }
    }
};

}

