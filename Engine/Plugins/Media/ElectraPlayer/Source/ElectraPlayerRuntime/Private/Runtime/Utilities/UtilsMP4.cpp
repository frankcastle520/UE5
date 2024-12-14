// Copyright Epic Games, Inc. All Rights Reserved.

#include "Utilities/UtilsMP4.h"
#include "HTTP/HTTPResponseCache.h"
#include <Misc/Base64.h>


namespace Electra
{

namespace FMetadataTags
{
static const TCHAR * const Title = TEXT("Title");
static const TCHAR * const Artist = TEXT("Artist");
static const TCHAR * const Genre = TEXT("Genre");
static const TCHAR * const Date = TEXT("Date");						// Content creation date
static const TCHAR * const Description = TEXT("Description");
static const TCHAR * const LongDescription = TEXT("LongDescription");
static const TCHAR * const Album = TEXT("Album");
static const TCHAR * const Encoder = TEXT("Encoder");
}

UtilsMP4::FMetadataParser::FMetadataParser()
{
	NumTotalItems = 0;

	WellKnownItems.Emplace(ElectraDecodersUtil::MakeMP4Atom(0xa9,'n','a','m'), FMetadataTags::Title);
	WellKnownItems.Emplace(ElectraDecodersUtil::MakeMP4Atom(0xa9,'A','R','T'), FMetadataTags::Artist);
	WellKnownItems.Emplace(ElectraDecodersUtil::MakeMP4Atom(0xa9,'g','e','n'), FMetadataTags::Genre);
	WellKnownItems.Emplace(ElectraDecodersUtil::MakeMP4Atom(0xa9,'d','a','y'), FMetadataTags::Date);
	WellKnownItems.Emplace(ElectraDecodersUtil::MakeMP4Atom('d','e','s','c'), FMetadataTags::Description);
	WellKnownItems.Emplace(ElectraDecodersUtil::MakeMP4Atom('l','d','e','s'), FMetadataTags::LongDescription);
	WellKnownItems.Emplace(ElectraDecodersUtil::MakeMP4Atom(0xa9,'a','l','b'), FMetadataTags::Album);
	WellKnownItems.Emplace(ElectraDecodersUtil::MakeMP4Atom(0xa9,'t','o','o'), FMetadataTags::Encoder);
}

FString UtilsMP4::FMetadataParser::PrintableBoxAtom(const uint32 InAtom)
{
	FString Out;
	// Not so much just printable as alphanumeric.
	for(uint32 i=0, Atom=InAtom; i<4; ++i, Atom<<=8)
	{
		int32 v = Atom >> 24;
		if ((v>='A' && v<='Z') || (v>='a' && v<='z') || (v>='0' && v<='9') || v=='_')
		{
			Out.AppendChar(v);
		}
		else
		{
			// Not alphanumeric, return it as a hex string.
			return FString::Printf(TEXT("%08x"), InAtom);
		}
	}
	return Out;
}

void UtilsMP4::FMetadataParser::AddItem(const FString& InType, const FString& InValue)
{
	// Add a string item.
	TSharedPtr<FItem, ESPMode::ThreadSafe> Item = MakeShared<FItem, ESPMode::ThreadSafe>();
	Item->Type = 1;
	Item->Value = FVariant(InValue);
	++NumTotalItems;
	Items.FindOrAdd(InType).Emplace(MoveTemp(Item));
}

void UtilsMP4::FMetadataParser::AddItem(const FString& InType, const FString& InMimeType, const TArray<uint8>& InValue)
{
	if (InMimeType.Equals(TEXT("image/jpeg")))
	{
		TSharedPtr<FItem, ESPMode::ThreadSafe> Item = MakeShared<FItem, ESPMode::ThreadSafe>();
		Item->MimeType = InMimeType;
		Item->Type = 13;
		Item->Value = FVariant(InValue);
		++NumTotalItems;
		Items.FindOrAdd(InType).Emplace(MoveTemp(Item));
	}
	else if (InMimeType.Equals(TEXT("image/png")))
	{
		TSharedPtr<FItem, ESPMode::ThreadSafe> Item = MakeShared<FItem, ESPMode::ThreadSafe>();
		Item->MimeType = InMimeType;
		Item->Type = 14;
		Item->Value = FVariant(InValue);
		++NumTotalItems;
		Items.FindOrAdd(InType).Emplace(MoveTemp(Item));
	}
}


UtilsMP4::FMetadataParser::EResult UtilsMP4::FMetadataParser::Parse(uint32 InHandler, uint32 InHandlerReserved0, const TArray<UtilsMP4::FMetadataParser::FBoxInfo>& InBoxes)
{
	// We only support the Apple iTunes metadata at the moment.
	if (InHandler != ElectraDecodersUtil::MakeMP4Atom('m','d','i','r'))
	{
		return UtilsMP4::FMetadataParser::EResult::NotSupported;
	}
	/*
		As per ISO/IEC 14496-12:2015 section 8.11.1.2 the 'meta' box must contain a handler ('hdlr'), followed by nothing but optional boxes.
		Of these optional boxes the ones recognized and defined by the document are:
		'pitm' - 8.11.4 Primary Item Box
		'dinf' - 8.7.1 Data Information Box
		'iloc' - 8.11.3 The Item Location Box
		'ipro' - 8.11.5 Item Protection Box
		'iinf' - 8.11.6 Item Information Box
		 ....  - IPMPControlBox (8.12 Support for Protected Streams)
		'iref' - 8.11.12 Item Reference Box
		'idat' - 8.11.11 Item Data Box

		followed by any number of other boxes.

		Presently the boxes mentioned above are not processed since they are apparently not used by any of the tools
		that allow insertion of metadata. It appears that the way iTunes handles metadata has become the de facto standard
		in which there is
		    'meta'
			   'hdlr'
			   'ilst'
			     ....               // See: https://developer.apple.com/library/archive/documentation/QuickTime/QTFF/Metadata/Metadata.html#//apple_ref/doc/uid/TP40000939-CH1-SW26
				   'data'			// See: https://developer.apple.com/library/archive/documentation/QuickTime/QTFF/Metadata/Metadata.html#//apple_ref/doc/uid/TP40000939-CH1-SW27
	*/

	// By some definition the items are contained inside an 'ilst' box, so we look for that one.
	bool bHaveILST = false;
	for(int32 i=0; i<InBoxes.Num(); ++i)
	{
		if (InBoxes[i].Type == ElectraDecodersUtil::MakeMP4Atom('i','l','s','t'))
		{
			// We will actually parse all 'ilst' boxes if for some reason the metadata has
			// been scattered across several.
			bHaveILST = true;
			Parse(InBoxes[i]);
		}
	}
	return bHaveILST ? UtilsMP4::FMetadataParser::EResult::Success : UtilsMP4::FMetadataParser::EResult::MissingBox;
}

void UtilsMP4::FMetadataParser::Parse(const UtilsMP4::FMetadataParser::FBoxInfo& InBox)
{
	if (!InBox.Data || !InBox.Size)
	{
		return;
	}

	ElectraDecodersUtil::FMP4AtomReader dr(InBox.Data, InBox.Size);
	while(dr.GetNumBytesRemaining() > 8)
	{
		int32 BoxSize;
		uint32 BoxType;
		dr.Read(BoxSize);
		dr.Read(BoxType);
		if (WellKnownItems.Contains(BoxType))
		{
			ParseBoxDataList(WellKnownItems[BoxType], dr.GetCurrentDataPointer(), BoxSize-8);
		}
		else switch(BoxType)
		{
			case ElectraDecodersUtil::MakeMP4Atom('-','-','-','-'):
			{
				ParseBoxDataiTunes(dr.GetCurrentDataPointer(), BoxSize-8);
				break;
			}
			default:
			{
				ParseBoxDataList(FString(TEXT("qt."))+PrintableBoxAtom(BoxType), dr.GetCurrentDataPointer(), BoxSize-8);
				break;
			}
		}
		dr.ReadBytes(nullptr, BoxSize - 8);
	}
}


void UtilsMP4::FMetadataParser::ParseBoxDataList(const FString& AsCategory, const uint8* InBoxData, uint32 InBoxSize)
{
	if (!InBoxData || InBoxSize <= 8)
	{
		return;
	}

	ElectraDecodersUtil::FMP4AtomReader dr(InBoxData, InBoxSize);

#define RETURN_IF_READERROR(expr)	\
	if (!(expr))					\
	{								\
		return;						\
	}								\

	for(int32 CurrentOffset=dr.GetCurrentOffset(), BoxSize=0; dr.GetNumBytesRemaining() > 8; dr.SetCurrentOffset(CurrentOffset), dr.ReadBytes(nullptr, BoxSize - 8))
	{
		uint32 BoxType;
		RETURN_IF_READERROR(dr.Read(BoxSize));
		RETURN_IF_READERROR(dr.Read(BoxType));
		CurrentOffset = dr.GetCurrentOffset();
		if (BoxType != ElectraDecodersUtil::MakeMP4Atom('d','a','t','a'))
		{
			continue;
		}
		uint32 TypeIndicatorAndWellKnownType = 0;
		RETURN_IF_READERROR(dr.Read(TypeIndicatorAndWellKnownType));
		if ((TypeIndicatorAndWellKnownType >> 24) != 0)
		{
			continue;
		}
		uint16 CountryIndicator=0;
		RETURN_IF_READERROR(dr.Read(CountryIndicator));
		uint16 LanguageIndicator=0;
		RETURN_IF_READERROR(dr.Read(LanguageIndicator));
		int32 NumRemainingDataBytes = BoxSize - 8 - (dr.GetCurrentOffset() - CurrentOffset);

		TSharedPtr<FItem, ESPMode::ThreadSafe> Item = MakeShared<FItem, ESPMode::ThreadSafe>();

		// We do not handle the country indicator at the moment.
		// Likewise, the language indicator must be a directly specified language code since we do not handle
		// language tables.
		if (LanguageIndicator >= 1 && LanguageIndicator <= 255)
		{
			// This would be an index into the language table (which is a 'lang' box somewhere under the 'meta' box).
			continue;
		}
		if (LanguageIndicator)
		{
			char Lang[3];
			Lang[0] = (char)(0x60 + ((LanguageIndicator & 0x7c00) >> 10));
			Lang[1] = (char)(0x60 + ((LanguageIndicator & 0x03e0) >> 5));
			Lang[2] = (char)(0x60 + (LanguageIndicator & 0x001f));
			Item->Language = FString::ConstructFromPtrSize(Lang, 3);
		}

		bool bSet = true;
		int32 WKT = (int32)(TypeIndicatorAndWellKnownType & 0x00ffffffU);
		switch(WKT)
		{
			// UTF-8 string
			case 1:
			{
				FString String;
				if ((bSet = dr.ReadStringUTF8(String, NumRemainingDataBytes)) == true)
				{
					Item->Value = String;
					Item->Type = 1;
				}
				break;
			}
/*
			// UTF-16 string
			case 2:
			{
				FString String;
				if ((bSet = dr.ReadStringUTF16(String, NumRemainingDataBytes)) == true)
				{
					Item->Value = String;
					Item->Type = 1;
				}
				break;
			}
*/
			case 13:	// JPEG image
			case 14:	// PNG image
			case 27:	// BMP image
			{
				TArray<uint8> Image(dr.GetCurrentDataPointer(), NumRemainingDataBytes);
				Item->Value = MoveTemp(Image);
				Item->Type = WKT;
				if (WKT == 13)
				{
					Item->MimeType = TEXT("image/jpeg");
				}
				else if (WKT == 14)
				{
					Item->MimeType = TEXT("image/png");
				}
				else if (WKT == 27)
				{
					Item->MimeType = TEXT("image/bmp");
				}
				break;
			}

			// Float
			case 23:
			{
				float Flt;
				if ((bSet = dr.ReadAsNumber(Flt)) == true)
				{
					// Set as double!
					Item->Value = (double)Flt;
					Item->Type = 24;
				}
				break;
			}
			// Double
			case 24:
			{
				double Dbl;
				if ((bSet = dr.ReadAsNumber(Dbl)) == true)
				{
					Item->Value = Dbl;
					Item->Type = 24;
				}
				break;
			}
			// Signed integers
			case 65:	// int8
			case 66:	// int16
			case 67:	// int32
			case 74:	// int64
			case 21:	// 1-4 byte signed integer
			{
				int64 Number;
				if ((bSet = dr.ReadAsNumber(Number, WKT==65?1:WKT==66?2:WKT==67?4:WKT==74?8:NumRemainingDataBytes)) == true)
				{
					Item->Value = Number;
					Item->Type = 74;
				}
				break;
			}
			// Unsigned integers
			case 75:	// uint8
			case 76:	// uint16
			case 77:	// uint32
			case 78:	// uint64
			case 22:	// 1-4 byte unsigned integer
			{
				uint64 Number;
				if ((bSet = dr.ReadAsNumber(Number, WKT==75?1:WKT==76?2:WKT==77?4:WKT==78?8:NumRemainingDataBytes)) == true)
				{
					Item->Value = Number;
					Item->Type = 78;
				}
				break;
			}

			// Not handled here.
			default:
			{
				bSet = false;
				break;
			}
		}

		if (bSet)
		{
			++NumTotalItems;
			Items.FindOrAdd(AsCategory).Emplace(MoveTemp(Item));
		}
	}
#undef RETURN_IF_READERROR
}

void UtilsMP4::FMetadataParser::ParseBoxDataiTunes(const uint8* InBoxData, uint32 InBoxSize)
{
	if (!InBoxData || InBoxSize <= 8)
	{
		return;
	}

	ElectraDecodersUtil::FMP4AtomReader dr(InBoxData, InBoxSize);

#define RETURN_IF_READERROR(expr)	\
	if (!(expr))					\
	{								\
		return;						\
	}								\

	FString Name;
	for(int32 CurrentOffset=dr.GetCurrentOffset(), BoxSize=0; dr.GetNumBytesRemaining() > 8; dr.SetCurrentOffset(CurrentOffset), dr.ReadBytes(nullptr, BoxSize - 8))
	{
		uint32 BoxType;
		RETURN_IF_READERROR(dr.Read(BoxSize));
		RETURN_IF_READERROR(dr.Read(BoxType));
		CurrentOffset = dr.GetCurrentOffset();
		if (BoxType == ElectraDecodersUtil::MakeMP4Atom('m','e','a','n'))
		{
			// 4 reserved bytes follow
			RETURN_IF_READERROR(dr.ReadBytes(nullptr, 4));
			FString Meaning;
			int32 NumRemainingDataBytes = BoxSize - 8 - (dr.GetCurrentOffset() - CurrentOffset);
			RETURN_IF_READERROR(dr.ReadStringUTF8(Meaning, NumRemainingDataBytes));
			if (!Meaning.Equals(TEXT("com.apple.iTunes")))
			{
				return;
			}
		}
		else if (BoxType == ElectraDecodersUtil::MakeMP4Atom('n','a','m','e'))
		{
			// 4 reserved bytes follow
			RETURN_IF_READERROR(dr.ReadBytes(nullptr, 4));
			int32 NumRemainingDataBytes = BoxSize - 8 - (dr.GetCurrentOffset() - CurrentOffset);
			RETURN_IF_READERROR(dr.ReadStringUTF8(Name, NumRemainingDataBytes));
		}
		else if (BoxType == ElectraDecodersUtil::MakeMP4Atom('d','a','t','a'))
		{
			ParseBoxDataList(FString(TEXT("iTunes."))+Name, dr.GetCurrentDataPointer()-8, BoxSize);
		}
	}
#undef RETURN_IF_READERROR
}

bool UtilsMP4::FMetadataParser::IsDifferentFrom(const UtilsMP4::FMetadataParser& Other)
{
	// Different number of items or map entries means the metadata can't be identical.
	if (NumTotalItems != Other.NumTotalItems || Items.Num() != Other.Items.Num())
	{
		return true;
	}

	// Check if the maps keys or items are different.
	for(auto& Key : Items)
	{
		const TArray<TSharedPtr<FItem, ESPMode::ThreadSafe>>* OtherKeyItems = Other.Items.Find(Key.Key);
		if (!OtherKeyItems || Key.Value.Num() != OtherKeyItems->Num())
		{
			return true;
		}
		// Expensive item by item test :-(
		for(auto& Item : Key.Value)
		{
			if (!OtherKeyItems->ContainsByPredicate([Item](const TSharedPtr<FItem, ESPMode::ThreadSafe>& CompItem)
			{
				return *Item == *CompItem;
			}))
			{
				return true;
			}
		}
	}
	return false;
}


TSharedPtr<TMap<FString, TArray<TSharedPtr<IMediaStreamMetadata::IItem, ESPMode::ThreadSafe>>>, ESPMode::ThreadSafe> UtilsMP4::FMetadataParser::GetMediaStreamMetadata() const
{
	TSharedPtr<TMap<FString, TArray<TSharedPtr<IMediaStreamMetadata::IItem, ESPMode::ThreadSafe>>>, ESPMode::ThreadSafe> NewMeta(new TMap<FString, TArray<TSharedPtr<IMediaStreamMetadata::IItem, ESPMode::ThreadSafe>>>);
	for(auto& Item : Items)
	{
		const TArray<TSharedPtr<FItem, ESPMode::ThreadSafe>>& SrcList = Item.Value;
		if (SrcList.Num())
		{
			TArray<TSharedPtr<IMediaStreamMetadata::IItem, ESPMode::ThreadSafe>> DstList;
			for(int32 i=0; i<SrcList.Num(); ++i)
			{
				DstList.Emplace(SrcList[i]);
			}
			NewMeta->Emplace(Item.Key, MoveTemp(DstList));
		}
	}
	return NewMeta;
}


FString UtilsMP4::FMetadataParser::GetAsJSON() const
{
	FString JSON(TEXT("{"));
	bool bFirstItem = true;
	for(auto& Item : Items)
	{
		bool bFirstItemValue = true;
		const TArray<TSharedPtr<FItem, ESPMode::ThreadSafe>>& List=Item.Value;
		FString ItemJSON;
		for(int32 i=0; i<List.Num(); ++i)
		{
			const TSharedPtr<FItem, ESPMode::ThreadSafe>& it = List[i];
			FString ItemValue = it->ToJSONValue();
			if (ItemValue.Len())
			{
				if (!bFirstItemValue)
				{
					ItemJSON.AppendChar(TCHAR(','));
				}
				bFirstItemValue = false;
				ItemJSON.Append(FString::Printf(TEXT("\"%s\":"), *(it->Language)));
				ItemJSON.Append(MoveTemp(ItemValue));
			}
		}
		if (ItemJSON.Len())
		{
			if (!bFirstItem)
			{
				JSON.AppendChar(TCHAR(','));
			}
			bFirstItem = false;

			JSON.Append(FString::Printf(TEXT("\"%s\":{"), *Item.Key));
			JSON.Append(MoveTemp(ItemJSON));
			JSON.Append(TEXT("}"));
		}
	}
	JSON.Append(TEXT("}"));
	return JSON;
}

FString UtilsMP4::FMetadataParser::FItem::ToJSONValue() const
{
	switch(Type)
	{
		// String
		case 1:
		{
			return FString::Printf(TEXT("\"%s\""), *Value.GetValue<FString>().ReplaceCharWithEscapedChar(&CharsToEscapeInJSON));
		}
		// Double
		case 24:
		{
			return FString::Printf(TEXT("%f"), Value.GetValue<double>());
		}
		// Signed integer
		case 74:
		{
			int64 v = Value.GetValue<int64>();
			if (v >= -(1LL<<53) && v <= (1LL<<53)-1)
			{
				return FString::Printf(TEXT("%lld"), (long long int)v);
			}
		}
		// Unsigned integer
		case 78:
		{
			uint64 v = Value.GetValue<uint64>();
			if (v <= (1LL<<53)-1)
			{
				return FString::Printf(TEXT("%lld"), (long long int)v);
			}
		}
		// JPEG image
		case 13:
		{
			FString js(TEXT("{\"mimetype\":\"image/jpeg\",\"data\":\""));
			js.Append(FBase64::Encode(Value.GetValue<TArray<uint8>>()));
			js.Append(TEXT("\"}"));
			return js;
		}
		// PNG image
		case 14:
		{
			FString js(TEXT("{\"mimetype\":\"image/png\",\"data\":\""));
			js.Append(FBase64::Encode(Value.GetValue<TArray<uint8>>()));
			js.Append(TEXT("\"}"));
			return js;
		}
		default:
		{
			break;
		}
	}
	return FString();
}
TArray<TCHAR> UtilsMP4::FMetadataParser::FItem::CharsToEscapeInJSON = { TCHAR('\n'), TCHAR('\r'), TCHAR('\t'), TCHAR('\\'), TCHAR('\"') };




UtilsMP4::FMP4RootBoxLocator::~FMP4RootBoxLocator()
{
}

bool UtilsMP4::FMP4RootBoxLocator::LocateRootBoxes(TArray<FBoxInfo>& OutBoxInfos, const TSharedPtrTS<IElectraHttpManager>& InHTTPManager, const FString& InURL, const TArray<uint32>& InFirstBoxes, const TArray<uint32>& InStopAfterBoxes, const TArray<uint32>& InReadDataOfBoxes, FCancellationCheckDelegate InCheckCancellationDelegate)
{
	FMediaEvent ReadCompleted;
	FString URL(InURL);
	volatile bool bAbort = false;
	FileSize = -1;
	bHasErrored = false;

	// Create a HTTP response cache to avoid unnecessary requests.
	TSharedPtrTS<IHTTPResponseCache> LocalCache = IHTTPResponseCache::Create(1024*1024, 32, nullptr);

	TSharedPtrTS<IElectraHttpManager::FProgressListener> ProgressListener = MakeSharedTS<IElectraHttpManager::FProgressListener>();
	ProgressListener->ProgressDelegate = IElectraHttpManager::FProgressListener::FProgressDelegate::CreateLambda([&](const IElectraHttpManager::FRequest* InRequest)->int32
	{
		bAbort = InCheckCancellationDelegate.Execute();
		return bAbort ? 1 : 0;
	});
	ProgressListener->CompletionDelegate = IElectraHttpManager::FProgressListener::FCompletionDelegate::CreateLambda([&](const IElectraHttpManager::FRequest* InRequest)
	{
		bool bFailed = InRequest->ConnectionInfo.StatusInfo.ErrorDetail.IsError();
		ConnectionInfo = InRequest->ConnectionInfo;
		if (!bFailed)
		{
			// Set the size of the resource if we don't have it yet.
			if (FileSize < 0)
			{
				ElectraHTTPStream::FHttpRange crh;
				if (crh.ParseFromContentRangeResponse(InRequest->ConnectionInfo.ContentRangeHeader))
				{
					FileSize = crh.GetDocumentSize();
				}
			}

			if (ConnectionInfo.EffectiveURL.Len())
			{
				URL = ConnectionInfo.EffectiveURL;
			}
		}
		bHasErrored = bFailed;
		ReadCompleted.Signal();
	});


	auto CreateReadRequestAndBuffer = [&](TSharedPtrTS<FWaitableBuffer>& OutReceiveBuffer, int64 InFromOffset, int64 InNumBytes) -> TSharedPtrTS<IElectraHttpManager::FRequest>
	{
		TSharedPtrTS<IElectraHttpManager::FRequest> Req = MakeSharedTS<IElectraHttpManager::FRequest>();
		Req->Parameters.URL = URL;
		Req->Parameters.Range.SetStart(InFromOffset);
		int64 LastByte = InFromOffset + InNumBytes - 1;
		if (FileSize >= 0 && LastByte > FileSize-1)
		{
			LastByte = FileSize - 1;
		}
		Req->Parameters.Range.SetEndIncluding(LastByte);
		Req->Parameters.ConnectTimeout = FTimeValue().SetFromMilliseconds(1000 * 8);
		Req->Parameters.NoDataTimeout = FTimeValue().SetFromMilliseconds(1000 * 6);
		OutReceiveBuffer = MakeSharedTS<FWaitableBuffer>();
		OutReceiveBuffer->Reserve(InNumBytes);
		Req->ReceiveBuffer = OutReceiveBuffer;
		Req->ProgressListener = ProgressListener;
		Req->ResponseCache = LocalCache;
		return Req;
	};

	const int32 MinRequiredReadSize = 32; // uint32(size) + uint32(type) + uint64(largesize) + uuid
	const int32 ChunkSize = 4096;
	int64 StartOffset = 0;
	int64 CurrentEndOffset = -1;
	bool bSuccess = false;
	bool bIsFirst = true;
	TSharedPtrTS<IElectraHttpManager::FRequest> Request;
	TSharedPtrTS<FWaitableBuffer> ReceiveBuffer;
	while(1)
	{
		int64 SizeToRead = CurrentEndOffset < StartOffset+MinRequiredReadSize ? ChunkSize : MinRequiredReadSize;
		Request = CreateReadRequestAndBuffer(ReceiveBuffer, StartOffset, SizeToRead);
		InHTTPManager->AddRequest(Request, false);
		ReadCompleted.WaitAndReset();
		if (bAbort || bHasErrored)
		{
			break;
		}
		TSharedPtrTS<FWaitableBuffer> DataBuffer(MoveTemp(ReceiveBuffer));
		InHTTPManager->RemoveRequest(Request, false);
		Request.Reset();

		if (DataBuffer.IsValid() && DataBuffer->Num() >= 8)
		{
			const int64 End = StartOffset + DataBuffer->Num();
			CurrentEndOffset = CurrentEndOffset < End ? End : CurrentEndOffset;

			FBoxInfo bi;
			const uint32* Data = reinterpret_cast<const uint32*>(DataBuffer->GetLinearReadData());
			bi.Size = (int64) MEDIA_FROM_BIG_ENDIAN(Data[0]);
			bi.Type = MEDIA_FROM_BIG_ENDIAN(Data[1]);
			bi.Offset = StartOffset;
			uint32 BoxInternalOffset = 8;
			if (bIsFirst)
			{
				// The way we read the file we now need to know its actual size.
				// An open ended chunked transfer will not work here.
				if (FileSize < 0)
				{
					ErrorMsg = TEXT("Invalid mp4 file: Unknown file size. Cannot parse the file.");
					break;
				}

				if (InFirstBoxes.Num() && !InFirstBoxes.Contains(bi.Type))
				{
					ErrorMsg = TEXT("Invalid mp4 file: First box is not of expected type");
					break;
				}
				bIsFirst = false;
			}

			// Check the box size value.
			if (bi.Size == 0)
			{
				// Zero size means "until the end of the file".
				bi.Size = FileSize - StartOffset;
			}
			else if (bi.Size == 1)
			{
				// A size of 1 indicates that the size is expressed as a 64 bit value following the box type.
				if (DataBuffer->Num() < 16)
				{
					ErrorMsg = TEXT("Invalid mp4 file: Box requiring 64 bit size value is truncated");
					break;
				}
				bi.Size = (int64) MEDIA_FROM_BIG_ENDIAN(reinterpret_cast<const uint64*>(Data)[1]);
				BoxInternalOffset += 8;
			}

			if (bi.Type == Make4CC('u','u','i','d'))
			{
				if (DataBuffer->Num() < BoxInternalOffset + 16)
				{
					ErrorMsg = TEXT("Invalid mp4 file: UUID box is truncated");
					break;
				}
				FMemory::Memcpy(bi.UUID, reinterpret_cast<const uint8*>(Data) + BoxInternalOffset, 16);
			}

			// Read this box?
			if (!bAbort && InReadDataOfBoxes.Contains(bi.Type))
			{
				Request = CreateReadRequestAndBuffer(bi.DataBuffer, bi.Offset, bi.Size);
				InHTTPManager->AddRequest(Request, false);
				ReadCompleted.WaitAndReset();
				if (bAbort || bHasErrored)
				{
					break;
				}
				InHTTPManager->RemoveRequest(Request, false);
				Request.Reset();
			}

			StartOffset += bi.Size;
			bool bStopNow = InStopAfterBoxes.Contains(bi.Type);
			OutBoxInfos.Emplace(MoveTemp(bi));

			// Done?
			if (bStopNow || StartOffset >= FileSize)
			{
				if (StartOffset > FileSize)
				{
					ErrorMsg = TEXT("Invalid mp4 file: File shorter than box sizes indicate");
					break;
				}
				else
				{
					bSuccess = true;
				}
				break;
			}
		}
		else
		{
			break;
		}
	}

	if (Request.IsValid())
	{
		InHTTPManager->RemoveRequest(Request, false);
		Request.Reset();
	}
	ProgressListener.Reset();
	ReceiveBuffer.Reset();

	return bSuccess;
}

TSharedPtrTS<FWaitableBuffer> UtilsMP4::FMP4ChunkLoader::LoadChunk(const int64 InOffset, const int64 InSize, const TSharedPtrTS<IElectraHttpManager>& InHTTPManager, const TSharedPtrTS<IHTTPResponseCache>& InHttpResponseCache, const FString& InURL, FCancellationCheckDelegate InCheckCancellationDelegate)
{
	FMediaEvent ReadCompleted;
	FString URL(InURL);
	volatile bool bAbort = false;
	FileSize = -1;
	bHasErrored = false;
	
	TSharedPtrTS<IElectraHttpManager::FProgressListener> ProgressListener = MakeSharedTS<IElectraHttpManager::FProgressListener>();
	ProgressListener->ProgressDelegate = IElectraHttpManager::FProgressListener::FProgressDelegate::CreateLambda([&](const IElectraHttpManager::FRequest* InRequest)->int32
	{
		bAbort = InCheckCancellationDelegate.Execute();
		return bAbort ? 1 : 0;
	});
	ProgressListener->CompletionDelegate = IElectraHttpManager::FProgressListener::FCompletionDelegate::CreateLambda([&](const IElectraHttpManager::FRequest* InRequest)
	{
		const bool bFailed = InRequest->ConnectionInfo.StatusInfo.ErrorDetail.IsError();
		ConnectionInfo = InRequest->ConnectionInfo;
		if (!bFailed)
		{
			// Set the size of the resource if we don't have it yet.
			if (FileSize < 0)
			{
				ElectraHTTPStream::FHttpRange crh;
				if (crh.ParseFromContentRangeResponse(InRequest->ConnectionInfo.ContentRangeHeader))
				{
					FileSize = crh.GetDocumentSize();
				}
			}

			if (ConnectionInfo.EffectiveURL.Len())
			{
				URL = ConnectionInfo.EffectiveURL;
			}
		}
		bHasErrored = bFailed;
		ReadCompleted.Signal();
	});

	auto CreateReadRequestAndBuffer = [&](TSharedPtrTS<FWaitableBuffer>& OutReceiveBuffer, int64 InFromOffset, int64 InNumBytes) -> TSharedPtrTS<IElectraHttpManager::FRequest>
	{
		TSharedPtrTS<IElectraHttpManager::FRequest> Req = MakeSharedTS<IElectraHttpManager::FRequest>();
		Req->Parameters.URL = URL;
		Req->Parameters.Range.SetStart(InFromOffset);
		int64 LastByte = InFromOffset + InNumBytes - 1;
		if (FileSize >= 0 && LastByte > FileSize-1)
		{
			LastByte = FileSize - 1;
		}
		Req->Parameters.Range.SetEndIncluding(LastByte);
		Req->Parameters.ConnectTimeout = FTimeValue().SetFromMilliseconds(1000 * 8);
		Req->Parameters.NoDataTimeout = FTimeValue().SetFromMilliseconds(1000 * 6);
		OutReceiveBuffer = MakeSharedTS<FWaitableBuffer>();
		OutReceiveBuffer->Reserve(InNumBytes);
		Req->ReceiveBuffer = OutReceiveBuffer;
		Req->ProgressListener = ProgressListener;
		Req->ResponseCache = InHttpResponseCache;
		return Req;
	};

	TSharedPtrTS<FWaitableBuffer> ReceiveBuffer;
	const TSharedPtrTS<IElectraHttpManager::FRequest> Request = CreateReadRequestAndBuffer(ReceiveBuffer, InOffset, InSize);
	InHTTPManager->AddRequest(Request, false);
	ReadCompleted.WaitAndReset();
	InHTTPManager->RemoveRequest(Request, false);

	return !(bAbort || bHasErrored) ? ReceiveBuffer : nullptr;
}

}


