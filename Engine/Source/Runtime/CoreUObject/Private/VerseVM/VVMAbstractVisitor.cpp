// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_VERSE_VM || defined(__INTELLISENSE__)

#include "VerseVM/VVMAbstractVisitor.h"
#include "VerseVM/VVMPlaceholder.h"
#include "VerseVM/VVMRestValue.h"

namespace Verse
{
void FAbstractVisitor::VisitNonNull(VCell*& InCell, const TCHAR* ElementName)
{
}

void FAbstractVisitor::VisitNonNull(UObject*& InObject, const TCHAR* ElementName)
{
}

void FAbstractVisitor::VisitAuxNonNull(void* InAux, const TCHAR* ElementName)
{
}

void FAbstractVisitor::Visit(bool& bValue, const TCHAR* ElementName)
{
}

void FAbstractVisitor::Visit(FString& Value, const TCHAR* ElementName)
{
}

void FAbstractVisitor::Visit(uint64& Value, const TCHAR* ElementName)
{
}

void FAbstractVisitor::Visit(int64& Value, const TCHAR* ElementName)
{
}

void FAbstractVisitor::Visit(uint32& Value, const TCHAR* ElementName)
{
}

void FAbstractVisitor::Visit(int32& Value, const TCHAR* ElementName)
{
}

void FAbstractVisitor::Visit(uint16& Value, const TCHAR* ElementName)
{
}

void FAbstractVisitor::Visit(int16& Value, const TCHAR* ElementName)
{
}

void FAbstractVisitor::Visit(uint8& Value, const TCHAR* ElementName)
{
}

void FAbstractVisitor::Visit(int8& Value, const TCHAR* ElementName)
{
}

void FAbstractVisitor::Visit(VFloat&, const TCHAR*)
{
}

void FAbstractVisitor::BeginArray(const TCHAR* ElementName, uint64& NumElements)
{
}

void FAbstractVisitor::EndArray()
{
}

void FAbstractVisitor::BeginString(const TCHAR* ElementName, uint64& NumElements)
{
	BeginArray(ElementName, NumElements);
}

void FAbstractVisitor::EndString()
{
	EndArray();
}

void FAbstractVisitor::BeginSet(const TCHAR* ElementName, uint64& NumElements)
{
}

void FAbstractVisitor::EndSet()
{
}

void FAbstractVisitor::BeginMap(const TCHAR* ElementName, uint64& NumElements)
{
}

void FAbstractVisitor::EndMap()
{
}

void FAbstractVisitor::BeginOption()
{
}

void FAbstractVisitor::EndOption()
{
}

void FAbstractVisitor::VisitBulkData(void* Data, uint64 DataSize, const TCHAR* ElementName)
{
}

void FAbstractVisitor::VisitEmergentType(const VEmergentType* InEmergentType)
{
	VCell* Scratch = const_cast<VEmergentType*>(InEmergentType);
	VisitNonNull(Scratch, TEXT("EmergentType"));
}

void FAbstractVisitor::VisitObject(const TCHAR* ElementName, FUtf8StringView TypeName, TFunctionRef<void()> VisitBody)
{
	VisitBody();
}

void FAbstractVisitor::VisitPair(TFunctionRef<void()> VisitBody)
{
	VisitObject(TEXT(""), VisitBody);
}

void FAbstractVisitor::VisitClass(FUtf8StringView ClassName, TFunctionRef<void()> VisitBody)
{
	VisitBody();
}

void FAbstractVisitor::VisitFunction(FUtf8StringView FunctionName, TFunctionRef<void()> VisitBody)
{
	VisitBody();
}

void FAbstractVisitor::VisitConstrainedInt(TFunctionRef<void()> VisitBody)
{
	VisitBody();
}

void FAbstractVisitor::VisitConstrainedFloat(TFunctionRef<void()> VisitBody)
{
	VisitBody();
}

void FAbstractVisitor::Visit(VCell*& InCell, const TCHAR* ElementName)
{
	if (InCell != nullptr)
	{
		VisitNonNull(InCell, ElementName);
	}
}

void FAbstractVisitor::Visit(UObject*& InObject, const TCHAR* ElementName)
{
	if (InObject != nullptr)
	{
		VisitNonNull(InObject, ElementName);
	}
}

void FAbstractVisitor::VisitAux(void* InAux, const TCHAR* ElementName)
{
	if (InAux != nullptr)
	{
		VisitAuxNonNull(InAux, ElementName);
	}
}

void FAbstractVisitor::Visit(VValue& Value, const TCHAR* ElementName)
{
	if (Value.IsCell())
	{
		VCell* Cell = &Value.AsCell();
		VisitNonNull(Cell, ElementName);
	}
	if (Value.IsPlaceholder())
	{
		Visit(Value.AsPlaceholder(), ElementName);
	}
	else if (Value.IsUObject())
	{
		UObject* Object = Value.AsUObject();
		Visit(Object, ElementName);
	}
	else if (Value.IsInt32())
	{
		int32 Int = Value.AsInt32();
		Visit(Int, ElementName);
	}
	else if (Value.IsChar())
	{
		uint8 Char = static_cast<uint8>(Value.AsChar());
		Visit(Char, ElementName);
	}
	else if (Value.IsChar32())
	{
		uint32 Char32 = static_cast<uint32>(Value.AsChar32());
		Visit(Char32, ElementName);
	}
	else if (Value.IsFloat())
	{
		VFloat Float = Value.AsFloat();
		Visit(Float, ElementName);
	}
}

void FAbstractVisitor::Visit(VPlaceholder& Value, const TCHAR* ElementName)
{
	VCell* Cell = &Value;
	VisitNonNull(Cell, ElementName);
}

void FAbstractVisitor::Visit(VRestValue& Value, const TCHAR* ElementName)
{
	Value.Visit(*this, ElementName);
}

FArchive* FAbstractVisitor::GetUnderlyingArchive()
{
	return nullptr;
}

bool FAbstractVisitor::IsLoading()
{
	return false;
}

bool FAbstractVisitor::IsTextFormat()
{
	return false;
}

FAccessContext FAbstractVisitor::GetLoadingContext()
{
	V_DIE("Subclass must implement GetLoadingContext when loading");
}

} // namespace Verse
#endif // WITH_VERSE_VM || defined(__INTELLISENSE__)
