// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTextFilter.h"

#include "Algo/Compare.h"
#include "CollectionManagerModule.h"
#include "CollectionManagerTypes.h"
#include "Filters.h"
#include "ICollectionManager.h"
#include "String/ParseTokens.h"

namespace AssetTextFilter
{
/** Keys used by TestComplexExpression */
const FName NameKeyName("Name");
const FName PathKeyName("Path");
const FName ClassKeyName("Class");
const FName TypeKeyName("Type");
const FName CollectionKeyName("Collection");
const FName TagKeyName("Tag");

FRWLock CustomHandlerLock;
TArray<IAssetTextFilterHandler*> CustomHandlers;
} // namespace AssetTextFilter

FAssetTextFilter::FAssetTextFilter()
	: ReferencedDynamicCollections()
	, TextFilterExpressionEvaluator(ETextFilterExpressionEvaluatorMode::Complex)
{
	FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();

	// We need to watch for collection changes so that we can keep ReferencedDynamicCollections up-to-date
	ICollectionManager& Manager = CollectionManagerModule.Get();
	OnCollectionCreatedHandle = Manager.OnCollectionCreated().AddRaw(this, &FAssetTextFilter::HandleCollectionCreated);
	OnCollectionDestroyedHandle = Manager.OnCollectionDestroyed().AddRaw(this, &FAssetTextFilter::HandleCollectionDestroyed);
	OnCollectionRenamedHandle = Manager.OnCollectionRenamed().AddRaw(this, &FAssetTextFilter::HandleCollectionRenamed);
	OnCollectionUpdatedHandle = Manager.OnCollectionUpdated().AddRaw(this, &FAssetTextFilter::HandleCollectionUpdated);
}

FAssetTextFilter::~FAssetTextFilter()
{
	// Check IsModuleAvailable as we might be in the process of shutting down...
	if (FCollectionManagerModule::IsModuleAvailable())
	{
		FCollectionManagerModule& CollectionManagerModule = FCollectionManagerModule::GetModule();

		ICollectionManager& Manager = CollectionManagerModule.Get();
		Manager.OnCollectionCreated().Remove(OnCollectionCreatedHandle);
		Manager.OnCollectionDestroyed().Remove(OnCollectionDestroyedHandle);
		Manager.OnCollectionRenamed().Remove(OnCollectionRenamedHandle);
		Manager.OnCollectionUpdated().Remove(OnCollectionUpdatedHandle);
	}
}

bool FAssetTextFilter::IsEmpty() const
{
	return TextFilterExpressionEvaluator.GetFilterText().IsEmpty() && CustomTextFilters.IsEmpty();
}

TSharedPtr<FCompiledAssetTextFilter> FAssetTextFilter::Compile()
{
	TSharedPtr<FTextFilterExpressionEvaluator> CompiledEvaluator;
	TSharedPtr<const TArray<FCollectionNameType>> SharedReferencedDynamicCollections;
	if (CustomTextFilters.Num() > 0)
	{
		// Combine main filter and custom saved queries with AND semantics
		TStringBuilder<2048> Builder;
		if (!TextFilterExpressionEvaluator.GetFilterText().IsEmpty())
		{
			Builder << TEXTVIEW("(") << TextFilterExpressionEvaluator.GetFilterText().ToString() << TEXTVIEW(")");
		}
		for (const FText& Text : CustomTextFilters)
		{
			if (Builder.Len() > 0)
			{
				Builder << TEXTVIEW(" AND ");
			}
			Builder << TEXTVIEW("(") << Text.ToString() << TEXTVIEW(")");
		}
		CompiledEvaluator = MakeShared<FTextFilterExpressionEvaluator>(ETextFilterExpressionEvaluatorMode::Complex);
		CompiledEvaluator->SetFilterText(FText::FromStringView(Builder.ToView()));
	
		TArray<FCollectionNameType> CombinedReferencedDynamicCollections;
		TextFilterExpressionEvaluator.TestTextFilter(FFrontendFilter_GatherDynamicCollectionsExpressionContext(CombinedReferencedDynamicCollections));
		if (CombinedReferencedDynamicCollections.Num())
		{
			SharedReferencedDynamicCollections = MakeShared<const TArray<FCollectionNameType>>(MoveTemp(CombinedReferencedDynamicCollections));
		}
	}
	else
	{
		CompiledEvaluator = MakeShared<FTextFilterExpressionEvaluator>(TextFilterExpressionEvaluator);
		if (bReferencedDynamicCollectionsDirty)
		{
			ReferencedDynamicCollections.Reset();
			TextFilterExpressionEvaluator.TestTextFilter(FFrontendFilter_GatherDynamicCollectionsExpressionContext(ReferencedDynamicCollections));
		}
		if (ReferencedDynamicCollections.Num())
		{
			SharedReferencedDynamicCollections = MakeShared<const TArray<FCollectionNameType>>(ReferencedDynamicCollections);
		}
	}

	// "Include" flags were always propagated to FFrontendFilter_CustomText, we could in theory save that as part of the data, which would mean we would need separate evaluators.
	return MakeShared<FCompiledAssetTextFilter>(FCompiledAssetTextFilter::FPrivateToken(), CompiledEvaluator.ToSharedRef(), SharedReferencedDynamicCollections,
		bIncludeClassName, bIncludeAssetPath, bIncludeCollectionNames);
}

void FAssetTextFilter::SetCustomTextFilters(TArray<FText> InQueries)
{
	if (Algo::Compare(InQueries, CustomTextFilters, [](const FText& A, const FText& B) { return A.EqualTo(B); }) == false)
	{
		CustomTextFilters = MoveTemp(InQueries);
		BroadcastChangedEvent(); // This is likely not strictly necessary as these queries will come from the content browser in the first place 
	}
}

FText FAssetTextFilter::GetRawFilterText() const
{
	return TextFilterExpressionEvaluator.GetFilterText();
}

void FAssetTextFilter::SetRawFilterText(const FText& InFilterText)
{
	if (TextFilterExpressionEvaluator.SetFilterText(InFilterText))
	{
		bReferencedDynamicCollectionsDirty = true;

		// Will trigger a re-filter with the new text
		BroadcastChangedEvent();
	}
}

FText FAssetTextFilter::GetFilterErrorText() const
{
	return TextFilterExpressionEvaluator.GetFilterErrorText();
}

void FAssetTextFilter::SetIncludeClassName(const bool InIncludeClassName)
{
	if (bIncludeClassName != InIncludeClassName)
	{
		bIncludeClassName = InIncludeClassName;

		// Will trigger a re-filter with the new setting
		BroadcastChangedEvent();
	}
}

void FAssetTextFilter::SetIncludeAssetPath(const bool InIncludeAssetPath)
{
	if (bIncludeAssetPath != InIncludeAssetPath)
	{
		bIncludeAssetPath = InIncludeAssetPath;

		// Will trigger a re-filter with the new setting
		BroadcastChangedEvent();
	}
}

bool FAssetTextFilter::GetIncludeAssetPath() const
{
	return bIncludeAssetPath;
}

void FAssetTextFilter::SetIncludeCollectionNames(const bool InIncludeCollectionNames)
{
	if (bIncludeCollectionNames != InIncludeCollectionNames)
	{
		bIncludeCollectionNames = InIncludeCollectionNames;

		// Will trigger a re-filter with the new collections
		BroadcastChangedEvent();
	}
}

bool FAssetTextFilter::GetIncludeCollectionNames() const
{
	return bIncludeCollectionNames;
}

void FAssetTextFilter::HandleCollectionCreated(const FCollectionNameType& Collection)
{
	bReferencedDynamicCollectionsDirty = true;

	// Will trigger a re-filter with the new collections
	BroadcastChangedEvent();
}

void FAssetTextFilter::HandleCollectionDestroyed(const FCollectionNameType& Collection)
{
	if (ReferencedDynamicCollections.Contains(Collection))
	{
		bReferencedDynamicCollectionsDirty = true;

		// Will trigger a re-filter with the new collections
		BroadcastChangedEvent();
	}
}

void FAssetTextFilter::HandleCollectionRenamed(
	const FCollectionNameType& OriginalCollection, const FCollectionNameType& NewCollection)
{
	int32 FoundIndex = INDEX_NONE;
	if (ReferencedDynamicCollections.Find(OriginalCollection, FoundIndex))
	{
		ReferencedDynamicCollections[FoundIndex] = NewCollection;
	}
}

void FAssetTextFilter::HandleCollectionUpdated(const FCollectionNameType& Collection)
{
	bReferencedDynamicCollectionsDirty = true;

	// Will trigger a re-filter with the new collections
	BroadcastChangedEvent();
}

FCompiledAssetTextFilter::FCompiledAssetTextFilter(
	FCompiledAssetTextFilter::FPrivateToken,
	TSharedRef<const FTextFilterExpressionEvaluator> InSharedEvaluator,
	TSharedPtr<const TArray<FCollectionNameType>> InSharedReferencedDynamicCollections,
	bool InIncludeClassName,
	bool InIncludeAssetPath,
	bool InIncludeCollectionNames)
	: Evaluator(MoveTemp(InSharedEvaluator))
	, ReferencedDynamicCollections(MoveTemp(InSharedReferencedDynamicCollections))
	, bIncludeClassName(InIncludeClassName)
	, bIncludeAssetPath(InIncludeAssetPath)
	, bIncludeCollectionNames(InIncludeCollectionNames)
{
	CollectionManager = &FCollectionManagerModule::GetModule().Get();
	TextBuffer.Reserve(2048);
}

FCompiledAssetTextFilter FCompiledAssetTextFilter::CloneForThreading()
{
	return FCompiledAssetTextFilter(*this);
}

bool FCompiledAssetTextFilter::PassesFilter(FAssetFilterType InItem)
{
	using namespace AssetTextFilter;
	FReadScopeLock Guard(CustomHandlerLock);

	AssetPtr = &InItem;
    TextBuffer.Reset();
	AssetCollectionNames.Reset();

	int32 DisplayNameLen = 0;
	int32 AssetPathLen = 0;
	int32 ExportTextPathLen = 0;

    TextBuffer.Append(AssetPtr->GetDisplayName().ToString());
	DisplayNameLen = TextBuffer.Len();

	if (bIncludeAssetPath)
	{
		int32 TextBufferLenBefore = TextBuffer.Len();
		// Get the full asset path, and also split it so we can compare each part in the filter
		AssetPtr->GetVirtualPath().AppendString(TextBuffer);
        
        // chop off item name if necessary
		{
			int32 LastSlashIndex = INDEX_NONE;
			int32 LastDotIndex = INDEX_NONE;
			if (TextBuffer.FindLastChar(TEXT('/'), LastSlashIndex) && TextBuffer.FindLastChar(TEXT('.'), LastDotIndex)
				&& LastDotIndex > LastSlashIndex && LastSlashIndex >= DisplayNameLen)
			{
				TextBuffer.LeftInline(LastDotIndex, EAllowShrinking::No);
				checkf(TextBuffer.Len() >= TextBufferLenBefore, TEXT("If TextBuffer is smaller than it used to be, then DisplayNameLen is now invalid. That should never happen."));
			}
		}
		AssetPathLen = TextBuffer.Len() - TextBufferLenBefore;

		if (bIncludeClassName && !AssetPtr->IsFolder())
		{
			TextBufferLenBefore = TextBuffer.Len();
			// Get the full export text path as people sometimes search by copying this (requires class and asset path
			// search to be enabled in order to match)
			// TODO: this allocates a temporary FString inside the backends
			AssetPtr->AppendItemReference(TextBuffer);
			ExportTextPathLen = TextBuffer.Len() - TextBufferLenBefore;
		}
	}

	if (CollectionManager && bIncludeCollectionNames)
	{
		FSoftObjectPath ItemCollectionId;
		if (AssetPtr->TryGetCollectionId(ItemCollectionId))
		{
			CollectionManager->GetCollectionsContainingObject(
				ItemCollectionId,
				ECollectionShareType::CST_All,
				AssetCollectionNames,
				ECollectionRecursionFlags::SelfAndChildren);

			if (ReferencedDynamicCollections.IsValid())
			{
				// Test the dynamic collections from the active query against the current asset
				// We can do this as a flat list since FFrontendFilter_GatherDynamicCollectionsExpressionContext has already
				// taken care of processing the recursion
				for (const FCollectionNameType& DynamicCollection : *ReferencedDynamicCollections)
				{
					bool bPassesCollectionFilter = false;
					CollectionManager->TestDynamicQuery(
						DynamicCollection.Name, DynamicCollection.Type, *this, bPassesCollectionFilter);
					if (bPassesCollectionFilter)
					{
						AssetCollectionNames.AddUnique(DynamicCollection.Name);
					}
				}
			}
		}
	}
    
    // Convert entire text buffer to uppercase
    // Assumes that for a TCHAR buffer this won't change number of code points 
	TextBuffer.ToUpperInline();
	
	const TCHAR* Cursor = *TextBuffer;
	AssetDisplayName = FStringView(Cursor, DisplayNameLen);
	Cursor += DisplayNameLen;
	AssetFullPath = FStringView(Cursor, AssetPathLen);
	Cursor += AssetPathLen;
	AssetExportTextPath  = FStringView(Cursor, ExportTextPathLen);
	AssetExportTextPath.TrimStartAndEndInline(); // Backends try to separate export text paths with newlines 
	Cursor += ExportTextPathLen;

	const bool bMatched = Evaluator->TestTextFilter(*this);
	AssetPtr = nullptr;
	return bMatched;
}

bool FCompiledAssetTextFilter::TestBasicStringExpression(
	const FTextFilterString& InValue, const ETextFilterTextComparisonMode InTextComparisonMode) const
{
	using namespace AssetTextFilter;
	for (IAssetTextFilterHandler* Handler : CustomHandlers)
	{
		bool bIsHandlerMatch = false;
		if (Handler->HandleTextFilterValue(*AssetPtr, InValue, InTextComparisonMode, bIsHandlerMatch))
		{
			return bIsHandlerMatch;
		}
	}

	if (InValue.CompareName(AssetPtr->GetItemName(), InTextComparisonMode))
	{
		return true;
	}

	if (InValue.CompareFStringView(AssetDisplayName, InTextComparisonMode))
	{
		return true;
	}

	if (bIncludeAssetPath)
	{
		if (InValue.CompareFStringView(AssetFullPath, InTextComparisonMode))
		{
			return true;
		}

		bool bSuccess = false;
		UE::String::ParseTokens(AssetFullPath, TEXTVIEW("/"), [&bSuccess, &InValue, InTextComparisonMode](FStringView Element){
			bSuccess = bSuccess || InValue.CompareFStringView(Element, InTextComparisonMode);
		});
		if (bSuccess)
		{
			return true;
		}
	}

	if (bIncludeClassName)
	{
		const FContentBrowserItemDataAttributeValue ClassValue = AssetPtr->GetItemAttribute(NAME_Class);
		if (ClassValue.IsValid() && InValue.CompareName(ClassValue.GetValue<FName>(), InTextComparisonMode))
		{
			return true;
		}
	}

	if (bIncludeClassName && bIncludeAssetPath)
	{
		// Only test this if we're searching the class name and asset path too, as the exported text contains the type
		// and path in the string
		if (InValue.CompareFStringView(AssetExportTextPath, InTextComparisonMode))
		{
			return true;
		}
	}

	for (const FName& AssetCollectionName : AssetCollectionNames)
	{
		if (InValue.CompareName(AssetCollectionName, InTextComparisonMode))
		{
			return true;
		}
	}

	return false;
}

bool FCompiledAssetTextFilter::TestComplexExpression(
	const FName& InKey,
	const FTextFilterString& InValue,
	const ETextFilterComparisonOperation InComparisonOperation,
	const ETextFilterTextComparisonMode InTextComparisonMode) const
{
	using namespace AssetTextFilter;
	for (IAssetTextFilterHandler* Handler : CustomHandlers)
	{
		bool bIsHandlerMatch = false;
		if (Handler->HandleTextFilterKeyValue(*AssetPtr, InKey, InValue, InComparisonOperation, InTextComparisonMode, bIsHandlerMatch))
		{
			return bIsHandlerMatch;
		}
	}

	// Special case for the asset name, as this isn't contained within the asset registry meta-data
	if (InKey == NameKeyName)
	{
		// Names can only work with Equal or NotEqual type tests
		if (InComparisonOperation != ETextFilterComparisonOperation::Equal
			&& InComparisonOperation != ETextFilterComparisonOperation::NotEqual)
		{
			return false;
		}

		const bool bIsMatch = TextFilterUtils::TestBasicStringExpression(AssetPtr->GetItemName(), InValue, InTextComparisonMode);
		return (InComparisonOperation == ETextFilterComparisonOperation::Equal) ? bIsMatch : !bIsMatch;
	}

	// Special case for the asset path, as this isn't contained within the asset registry meta-data
	if (InKey == PathKeyName)
	{
		// Paths can only work with Equal or NotEqual type tests
		if (InComparisonOperation != ETextFilterComparisonOperation::Equal
			&& InComparisonOperation != ETextFilterComparisonOperation::NotEqual)
		{
			return false;
		}

		// If the comparison mode is partial, then we only need to test the ObjectPath as that contains the other two as
		// sub-strings
		bool bIsMatch = false;
		if (InTextComparisonMode == ETextFilterTextComparisonMode::Partial)
		{
			bIsMatch =
				TextFilterUtils::TestBasicStringExpression(AssetPtr->GetVirtualPath(), InValue, InTextComparisonMode);
		}
		else
		{
			bIsMatch =
				TextFilterUtils::TestBasicStringExpression(AssetPtr->GetVirtualPath(), InValue, InTextComparisonMode)
				|| (!AssetFullPath.IsEmpty()
					&& TextFilterUtils::TestBasicStringExpression(FString(AssetFullPath), InValue, InTextComparisonMode));
		}
		return (InComparisonOperation == ETextFilterComparisonOperation::Equal) ? bIsMatch : !bIsMatch;
	}

	// Special case for the asset type, as this isn't contained within the asset registry meta-data
	if (InKey == ClassKeyName || InKey == TypeKeyName)
	{
		// Class names can only work with Equal or NotEqual type tests
		if (InComparisonOperation != ETextFilterComparisonOperation::Equal
			&& InComparisonOperation != ETextFilterComparisonOperation::NotEqual)
		{
			return false;
		}

		const FContentBrowserItemDataAttributeValue ClassValue = AssetPtr->GetItemAttribute(NAME_Class);
		const bool bIsMatch =
			ClassValue.IsValid()
			&& TextFilterUtils::TestBasicStringExpression(ClassValue.GetValue<FName>(), InValue, InTextComparisonMode);
		return (InComparisonOperation == ETextFilterComparisonOperation::Equal) ? bIsMatch : !bIsMatch;
	}

	// Special case for collections, as these aren't contained within the asset registry meta-data
	if (InKey == CollectionKeyName || InKey == TagKeyName)
	{
		// Collections can only work with Equal or NotEqual type tests
		if (InComparisonOperation != ETextFilterComparisonOperation::Equal
			&& InComparisonOperation != ETextFilterComparisonOperation::NotEqual)
		{
			return false;
		}

		bool bFoundMatch = false;
		for (const FName& AssetCollectionName : AssetCollectionNames)
		{
			if (TextFilterUtils::TestBasicStringExpression(AssetCollectionName, InValue, InTextComparisonMode))
			{
				bFoundMatch = true;
				break;
			}
		}

		return (InComparisonOperation == ETextFilterComparisonOperation::Equal) ? bFoundMatch : !bFoundMatch;
	}

	// Generic handling for anything in the asset meta-data
	{
		const FContentBrowserItemDataAttributeValue AttributeValue = AssetPtr->GetItemAttribute(InKey);
		if (AttributeValue.IsValid())
		{
			return TextFilterUtils::TestComplexExpression(
				AttributeValue.GetValue<FString>(),
				InValue,
				InComparisonOperation,
				InTextComparisonMode);
		}
	}

	return false;
}

void IAssetTextFilterHandler::RegisterHandler()
{
	using namespace AssetTextFilter;
	FWriteScopeLock Guard(CustomHandlerLock);
	CustomHandlers.Add(this);
}

void IAssetTextFilterHandler::UnregisterHandler()
{
	using namespace AssetTextFilter;
	FWriteScopeLock Guard(CustomHandlerLock);
	CustomHandlers.Remove(this);
}