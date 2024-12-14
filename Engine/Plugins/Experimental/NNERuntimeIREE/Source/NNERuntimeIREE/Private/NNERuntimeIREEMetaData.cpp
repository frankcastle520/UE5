// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeIREEMetaData.h"

#include "Containers/Array.h"
#include "Internationalization/Regex.h"
#include "NNE.h"
#include "NNERuntimeIREELog.h"
#include "Serialization/CustomVersion.h"

namespace UE::NNERuntimeIREE::ModuleMetaData::Private
{
	enum Version : uint32
	{
		V0 = 0, // Initial
		// New versions can be added above this line
		VersionPlusOne,
		Latest = VersionPlusOne - 1
	};
	const FGuid GUID(0x2f9ffd31, 0x12b817cd, 0x627855bf, 0x5e405720);
	FCustomVersionRegistration Version(GUID, Version::Latest, TEXT("NNERuntimeIREEModuleMetaDataVersion"));// Always save with the latest version

	ENNETensorDataType ConvertTypeString(const FString& TypeString)
	{
		if (TypeString.StartsWith("char"))
		{
			return ENNETensorDataType::Char;
		}
		if (TypeString.StartsWith("bool") || TypeString.StartsWith("i1"))
		{
			return ENNETensorDataType::Boolean;
		}
		else if (TypeString.StartsWith("half"))
		{
			return ENNETensorDataType::Half;
		}
		else if (TypeString.StartsWith("bf16"))
		{
			return ENNETensorDataType::BFloat16;
		}
		else if (TypeString.StartsWith("f"))
		{
			if (TypeString.StartsWith("f16"))
			{
				return ENNETensorDataType::Half;
			}
			else if (TypeString.StartsWith("float") || TypeString.StartsWith("f32"))
			{
				return ENNETensorDataType::Float;
			}
			else if (TypeString.StartsWith("f64"))
			{
				return ENNETensorDataType::Double;
			}
		}
		else if (TypeString.StartsWith("double"))
		{
			return ENNETensorDataType::Double;
		}
		else if (TypeString.StartsWith("i") || TypeString.StartsWith("si"))
		{
			if (TypeString.EndsWith("i8"))
			{
				return ENNETensorDataType::Int8;
			}
			else if (TypeString.EndsWith("i16"))
			{
				return ENNETensorDataType::Int16;
			}
			else if (TypeString.EndsWith("i32") || TypeString.EndsWith("int"))
			{
				return ENNETensorDataType::Int32;
			}
			else if (TypeString.EndsWith("i64"))
			{
				return ENNETensorDataType::Int64;
			}
		}
		else if (TypeString.StartsWith("ui"))
		{
			if (TypeString.EndsWith("i8"))
			{
				return ENNETensorDataType::UInt8;
			}
			else if (TypeString.EndsWith("i16"))
			{
				return ENNETensorDataType::UInt16;
			}
			else if (TypeString.EndsWith("i32"))
			{
				return ENNETensorDataType::UInt32;
			}
			else if (TypeString.EndsWith("i64"))
			{
				return ENNETensorDataType::UInt64;
			}
		}
		return ENNETensorDataType::None;
	}

	int32 FindCorrespondingClosingSymbol(const FString& String, int32 Offset, const FString& OpenSymbol, const FString& CloseSymbol)
	{
		int32 OpenedSymbols = 1;
		int32 NextClose = String.Find(CloseSymbol, ESearchCase::IgnoreCase, ESearchDir::FromStart, Offset);
		if (NextClose == INDEX_NONE)
		{
			return INDEX_NONE;
		}

		int32 NextOpen = String.Find(OpenSymbol, ESearchCase::IgnoreCase, ESearchDir::FromStart, Offset);
		while (OpenedSymbols > 0)
		{
			if (NextOpen == INDEX_NONE || NextClose < NextOpen)
			{
				OpenedSymbols--;
				if (OpenedSymbols > 0)
				{
					NextClose = String.Find(CloseSymbol, ESearchCase::IgnoreCase, ESearchDir::FromStart, NextClose + 1);
					if (NextClose == INDEX_NONE)
					{
						return INDEX_NONE;
					}
				}
			}
			else
			{
				OpenedSymbols++;
				NextOpen = String.Find(OpenSymbol, ESearchCase::IgnoreCase, ESearchDir::FromStart, NextOpen + 1);
			}
		}
		return NextClose;
	}

	bool ParseArgumentType(const FString& ArgumentType, UE::NNE::FSymbolicTensorShape& Shape, ENNETensorDataType& Type)
	{
		int32 ShapeStart = ArgumentType.Find("<");
		if (ShapeStart != INDEX_NONE)
		{
			int32 ShapeEnd = ArgumentType.Find(">", ESearchCase::IgnoreCase, ESearchDir::FromStart, ShapeStart);
			check(ShapeStart != INDEX_NONE);
			FString ShapeString = ArgumentType.Mid(ShapeStart + 1, ShapeEnd - ShapeStart - 1).TrimStartAndEnd();
			TArray<FString> ShapeList;
			ShapeString.ParseIntoArray(ShapeList, TEXT(","));
			if (ShapeList.Num() < 2)
			{
				ShapeString.ParseIntoArray(ShapeList, TEXT("x"));
			}
			TArray<int32> ShapeArray;
			for (int32 i = 0; i < ShapeList.Num() - 1; i++)
			{
				if (ShapeList[i].Contains("?"))
				{
					ShapeArray.Emplace(-1);
				}
				else
				{
					// Match any integer possibly starting with a sign
					// [-+]?: Matches 0 or 1 or optional times plus or minus
					// \\d+: Matches one or more digits
					const FRegexPattern Pattern(TEXT("[-+]?\\d+"));
					FRegexMatcher Matcher(Pattern, ShapeList[i]);
					if (Matcher.FindNext())
					{
						int32 IntegerStart = Matcher.GetMatchBeginning();
						int32 IntegerEnd = Matcher.GetMatchEnding();
						check(IntegerStart != INDEX_NONE && IntegerEnd != INDEX_NONE);
						ShapeArray.Emplace(FCString::Atoi(*ShapeList[i].Mid(IntegerStart, IntegerEnd - IntegerStart)));
					}
				}

			}
			Shape = UE::NNE::FSymbolicTensorShape::Make(ShapeArray);
			Type = ConvertTypeString(ShapeList[ShapeList.Num() - 1]);
		}
		else
		{
			Type = ConvertTypeString(ArgumentType);
		}
		return true;
	}

	bool ParseArguments(const FString& Arguments, TArray<UE::NNE::FTensorDesc>& TensorDescs)
	{
		FString ReducedArguments = "";
		int32 LastStart = 0;
		int32 NextOpenSymbol = Arguments.Find("{", ESearchCase::IgnoreCase, ESearchDir::FromStart, LastStart);
		while (NextOpenSymbol != INDEX_NONE)
		{
			ReducedArguments += Arguments.Mid(LastStart, NextOpenSymbol - LastStart);
			LastStart = FindCorrespondingClosingSymbol(Arguments, NextOpenSymbol + 1, "{", "}") + 1;
			check(LastStart != INDEX_NONE);
			NextOpenSymbol = Arguments.Find("{", ESearchCase::IgnoreCase, ESearchDir::FromStart, LastStart);
		}
		ReducedArguments += Arguments.Mid(LastStart);

		FString FinalArguments = "";
		LastStart = 0;
		NextOpenSymbol = ReducedArguments.Find("(", ESearchCase::IgnoreCase, ESearchDir::FromStart, LastStart);
		while (NextOpenSymbol != INDEX_NONE)
		{
			FinalArguments += ReducedArguments.Mid(LastStart, NextOpenSymbol - LastStart);
			LastStart = FindCorrespondingClosingSymbol(ReducedArguments, NextOpenSymbol + 1, "(", ")") + 1;
			check(LastStart != INDEX_NONE);
			NextOpenSymbol = ReducedArguments.Find("(", ESearchCase::IgnoreCase, ESearchDir::FromStart, LastStart);
		}
		FinalArguments += ReducedArguments.Mid(LastStart);

		TArray<FString> ArgumentList;
		int32 LastIndex = 0;
		int32 InsideShape = 0;
		for (int32 i = 0; i < FinalArguments.Len(); i++)
		{
			if (FinalArguments[i] == *TEXT(",") && InsideShape == 0)
			{
				ArgumentList.Add(FinalArguments.Mid(LastIndex, i - LastIndex).TrimStartAndEnd());
				LastIndex = i + 1;
			}
			else if (FinalArguments[i] == *TEXT("<"))
			{
				InsideShape++;
			}
			else if (FinalArguments[i] == *TEXT(">"))
			{
				InsideShape--;
			}
		}
		ArgumentList.Add(FinalArguments.Mid(LastIndex).TrimStartAndEnd());

		for (const FString& Argument : ArgumentList)
		{
			TArray<FString> ArgumentPartList;
			Argument.ParseIntoArray(ArgumentPartList, TEXT(":"));

			FString Name;
			UE::NNE::FSymbolicTensorShape Shape;
			ENNETensorDataType Type = ENNETensorDataType::None;
			if (ArgumentPartList.Num() > 1)
			{
				Name = ArgumentPartList[0].TrimStartAndEnd();
				if (!ParseArgumentType(ArgumentPartList[1].TrimStartAndEnd(), Shape, Type))
				{
					return false;
				}
			}
			else
			{
				if (!ParseArgumentType(ArgumentPartList[0].TrimStartAndEnd(), Shape, Type))
				{
					return false;
				}
			}
			TensorDescs.Add(UE::NNE::FTensorDesc::Make(Name, Shape, Type));
		}

		return true;
	}
} // UE::NNERuntimeIREE::ModuleMetaData::Private

void UNNERuntimeIREEModuleMetaData::Serialize(FArchive& Ar)
{
	// Store the asset version (no effect in load)
	Ar.UsingCustomVersion(UE::NNERuntimeIREE::ModuleMetaData::Private::GUID);

	if (Ar.IsSaving() || Ar.IsCountingMemory()) 
	{
		int32 NumItems = FunctionMetaData.Num();
		Ar << NumItems;
		for (int32 i = 0; i < NumItems; i++)
		{
			Ar << FunctionMetaData[i].Name;

			int32 NumInputs = FunctionMetaData[i].InputDescs.Num();
			Ar << NumInputs;
			for (int32 j = 0; j < NumInputs; j++)
			{
				FString Name = FunctionMetaData[i].InputDescs[j].GetName();
				Ar << Name;

				ENNETensorDataType Type = FunctionMetaData[i].InputDescs[j].GetDataType();
				Ar << Type;

				TArray<int32> Shape = (TArray<int32>)FunctionMetaData[i].InputDescs[j].GetShape().GetData();
				Ar << Shape;
			}

			int32 NumOutputs = FunctionMetaData[i].OutputDescs.Num();
			Ar << NumOutputs;
			for (int32 j = 0; j < NumOutputs; j++)
			{
				FString Name = FunctionMetaData[i].OutputDescs[j].GetName();
				Ar << Name;

				ENNETensorDataType Type = FunctionMetaData[i].OutputDescs[j].GetDataType();
				Ar << Type;

				TArray<int32> Shape = (TArray<int32>)FunctionMetaData[i].OutputDescs[j].GetShape().GetData();
				Ar << Shape;
			}
		}
	}
	else
	{
		int32 NumItems = 0;
		int32 NumInputs = 0;
		int32 NumOutputs = 0;
		UE::NNERuntimeIREE::FFunctionMetaData MetaData;
		FString Name;
		ENNETensorDataType Type;
		TArray<int32> Shape;

		switch (Ar.CustomVer(UE::NNERuntimeIREE::ModuleMetaData::Private::GUID))
		{
		case UE::NNERuntimeIREE::ModuleMetaData::Private::Version::V0:
			Ar << NumItems;
			FunctionMetaData.SetNum(NumItems, EAllowShrinking::Yes);
			for (int32 i = 0; i < NumItems; i++)
			{
				Ar << MetaData.Name;

				Ar << NumInputs;
				MetaData.InputDescs.Empty();
				for (int32 j = 0; j < NumInputs; j++)
				{
					Ar << Name;
					Ar << Type;
					Ar << Shape;
					MetaData.InputDescs.Add(UE::NNE::FTensorDesc::Make(Name, UE::NNE::FSymbolicTensorShape::Make(Shape), Type));
				}

				Ar << NumOutputs;
				MetaData.OutputDescs.Empty();
				for (int32 j = 0; j < NumOutputs; j++)
				{
					Ar << Name;
					Ar << Type;
					Ar << Shape;
					MetaData.OutputDescs.Add(UE::NNE::FTensorDesc::Make(Name, UE::NNE::FSymbolicTensorShape::Make(Shape), Type));
				}

				FunctionMetaData[i] = MetaData;
			}
			break;
		default:
			UE_LOG(LogNNERuntimeIREE, Error, TEXT("UNNERuntimeIREEModuleMetaData: Unknown asset version %d: Deserialisation failed, please reimport the original model."), Ar.CustomVer(UE::NNERuntimeIREE::ModuleMetaData::Private::GUID));
			break;
		}
	}
}

bool UNNERuntimeIREEModuleMetaData::ParseFromString(const FString& ModuleString)
{
	using namespace UE::NNERuntimeIREE::ModuleMetaData::Private;

	TArray<UE::NNERuntimeIREE::FFunctionMetaData> Result;

	// Match func.func [access modifier] @<function_name> (
	// func\.func: Exact match of the word
	// [^@]*: Skip zero or more characters not being @
	// @: Have exactly one @
	// [^(]*: Skip zero or more characters not being (
	// (: Have exactly one (
	const FRegexPattern FunctionNamePattern(TEXT("func\\.func[^@]*@[^(]*\\("));
	FRegexMatcher FunctionNameMatcher(FunctionNamePattern, *ModuleString);
	while (FunctionNameMatcher.FindNext())
	{
		int32 FunctionStart = FunctionNameMatcher.GetMatchBeginning();
		int32 InputArgumentsStart = FunctionNameMatcher.GetMatchEnding();
		check(FunctionStart != INDEX_NONE && InputArgumentsStart != INDEX_NONE);

		FString MatchedFunctionPattern = ModuleString.Mid(FunctionStart, InputArgumentsStart - FunctionStart - 1);
		bool bIsPrivate = MatchedFunctionPattern.Find("private") != INDEX_NONE;
		bool bIsProtected = MatchedFunctionPattern.Find("protected") != INDEX_NONE;
		if (bIsPrivate || bIsProtected)
		{
			continue;
		}
		int32 FunctionNameStart = MatchedFunctionPattern.Find("@");
		check(FunctionNameStart != INDEX_NONE);

		UE::NNERuntimeIREE::FFunctionMetaData MetaData;
		MetaData.Name = MatchedFunctionPattern.Mid(FunctionNameStart+1).TrimStartAndEnd();
		check(MetaData.Name.Len() > 0);

		// Regex does not support recursive matching but mlir can contain paranthesis inside arguments
		int32 InputArgumentsEnd = FindCorrespondingClosingSymbol(ModuleString, InputArgumentsStart, "(", ")");
		check(InputArgumentsEnd != INDEX_NONE);

		FString InputArguments = ModuleString.Mid(InputArgumentsStart, InputArgumentsEnd - InputArgumentsStart);
		if (!ParseArguments(InputArguments, MetaData.InputDescs))
		{
			return false;
		}

		// Match [white spaces] -> [white spaces]
		// ^: Match start of the string to make sure that there are any non white space characters at the beginning
		// [\\s]*: Match zero or more white spaces
		// ->: Exact match of ->
		// [\\s]*: Match zero or more white spaces
		const FRegexPattern OutputArgumentsStartPattern(TEXT("^[\\s]*->[\\s]*"));
		FString Rest = ModuleString.Mid(InputArgumentsEnd+1);
		FRegexMatcher OutputArgumentsStartMatcher(OutputArgumentsStartPattern, Rest);
		if (OutputArgumentsStartMatcher.FindNext())
		{
			int32 OutputArgumentsStart = OutputArgumentsStartMatcher.GetMatchEnding();
			int32 OutputArgumentsEnd = INDEX_NONE;
			Rest = Rest.Mid(OutputArgumentsStart);
			if (Rest.StartsWith("("))
			{
				OutputArgumentsStart = 1;
				OutputArgumentsEnd = FindCorrespondingClosingSymbol(Rest, OutputArgumentsStart, "(", ")");
			}
			else
			{
				OutputArgumentsStart = 0;
				int32 ClosestParanthesis = Rest.Find("(");
				int32 ClosestBraces = Rest.Find("{");
				OutputArgumentsEnd = (ClosestParanthesis == INDEX_NONE || ClosestBraces < ClosestParanthesis) ? ClosestBraces : ClosestParanthesis;
			}
			check(OutputArgumentsEnd != INDEX_NONE);
			FString OutputArguments = Rest.Mid(OutputArgumentsStart, OutputArgumentsEnd - OutputArgumentsStart);
			if (!ParseArguments(OutputArguments, MetaData.OutputDescs))
			{
				return false;
			}
		}

		Result.Add(MetaData);
	}
	
	if (!Result.IsEmpty())
	{
		FunctionMetaData = Result;
		return true;
	}
	return false;
}