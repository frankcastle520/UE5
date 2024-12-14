// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Common/Text/CppMangling.h"
#include "uLang/Common/Text/UTF8String.h"
#include "uLang/Common/Text/UTF8StringBuilder.h"
#include "uLang/Common/Text/UTF8StringView.h"

// NOTE: This method is a duplicate of Verse::Names::Private::EncodeName.  This method can be eliminated when VNI compiler is eliminated.
uLang::CUTF8String uLang::CppMangling::Mangle(const uLang::CUTF8StringView& StringView)
{
    uLang::CUTF8StringBuilder ResultBuilder;
	
    CUTF8StringView ResidualUnmangledName = StringView;
    bool bIsFirstChar = true;
    while(ResidualUnmangledName.IsFilled())
    {
        const UTF8Char Char = ResidualUnmangledName.PopFirstByte();

        if ( (Char >= 'a' && Char <= 'z')
          || (Char >= 'A' && Char <= 'Z')
          || (Char >= '0' && Char <= '9' && !bIsFirstChar))
        {
            ResultBuilder.Append(Char);
        }
        else if (Char == '[' && ResidualUnmangledName.IsFilled() && ResidualUnmangledName[0] == ']')
        {
            ResidualUnmangledName.PopFirstByte();
            ResultBuilder.Append("_K");
        }
        else if (Char == '-' && ResidualUnmangledName.IsFilled() &&ResidualUnmangledName[0] == '>')
        {
            ResidualUnmangledName.PopFirstByte();
            ResultBuilder.Append("_T");
        }
        else if (Char == '_') { ResultBuilder.Append("__"); }
        else if (Char == '(') { ResultBuilder.Append("_L"); }
        else if (Char == ',') { ResultBuilder.Append("_M"); }
        else if (Char == ':') { ResultBuilder.Append("_N"); }
        else if (Char == '^') { ResultBuilder.Append("_P"); }
        else if (Char == '?') { ResultBuilder.Append("_Q"); }
        else if (Char == ')') { ResultBuilder.Append("_R"); }
        else if (Char == '\''){ ResultBuilder.Append("_U"); }
        else
        {
            ResultBuilder.AppendFormat("_%.2x", uint8_t(Char));
        }

        bIsFirstChar = false;
    }

    return ResultBuilder.MoveToString();
}

// NOTE: This method is a duplicate of Verse::Names::Private::DecodeName.  This method can be eliminated when VNI compiler is eliminated.
uLang::CUTF8String uLang::CppMangling::Demangle(const uLang::CUTF8StringView& StringView)
{
    uLang::CUTF8StringBuilder ResultBuilder;
	
	CUTF8StringView ResidualMangledName = StringView;
	while(ResidualMangledName.IsFilled())
	{
		const UTF8Char Char = ResidualMangledName[0];
		if (Char != '_' || ResidualMangledName.ByteLen() < 2)
		{
			ResultBuilder.Append(Char);
            ResidualMangledName = ResidualMangledName.SubViewTrimBegin(1);
		}
		else
		{
			// Handle escape codes prefixed by underscore.
			struct FEscapeCode
			{
				UTF8Char Escaped;
				uLang::CUTF8String Unescaped;
			};
			static const FEscapeCode EscapeCodes[] =
			{
				{'_', "_"},
				{'K', "[]"},
				{'L', "("},
				{'M', ","},
				{'N', ":"},
				{'P', "^"},
				{'Q', "?"},
				{'R', ")"},
				{'T', "->"},
                {'U', "\'"},
			};
			bool bHandledEscapeCode = false;
			for (const FEscapeCode& EscapeCode : EscapeCodes)
			{
				if (ResidualMangledName[1] == EscapeCode.Escaped)
				{
					ResultBuilder.Append(EscapeCode.Unescaped);
                    ResidualMangledName = ResidualMangledName.SubViewTrimBegin(2);
					bHandledEscapeCode = true;
					break;
				}
			}
			if (!bHandledEscapeCode)
			{
				// Handle hexadecimal escapes.
				if (ResidualMangledName.ByteLen() < 3)
				{
					ResultBuilder.Append(ResidualMangledName);
					ResidualMangledName.Reset();
				}
				else
				{
					auto ParseHexit = [](const UTF8Char Hexit) -> int8_t
					{
						return (Hexit >= '0' && Hexit <= '9') ? (Hexit - '0')
							 : (Hexit >= 'a' && Hexit <= 'f') ? (Hexit - 'a' + 10)
							 : (Hexit >= 'A' && Hexit <= 'F') ? (Hexit - 'A' + 10)
							 : -1;
					};

					int8_t Hexit1 = ParseHexit(ResidualMangledName[1]);
					int8_t Hexit2 = ParseHexit(ResidualMangledName[2]);
					if (Hexit1 == -1 || Hexit2 == -1)
					{
						ResultBuilder.Append(ResidualMangledName[0]);
						ResultBuilder.Append(ResidualMangledName[1]);
						ResultBuilder.Append(ResidualMangledName[2]);
					}
					else
					{
						ResultBuilder.Append(UTF8Char(Hexit1 * 16 + Hexit2));
					}
                    
                    ResidualMangledName = ResidualMangledName.SubViewTrimBegin(3);
				}
			}
		}
	}

	return ResultBuilder.MoveToString();
}