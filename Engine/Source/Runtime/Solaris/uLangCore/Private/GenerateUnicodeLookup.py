#
# Python 3!
# 
# This utility scripts generates lookup tables for determining if a unicode code point >= 128 is in certain categories
# Each table lists all code points that are either 
# 1. the first match in a sequence of matches
# 2. the first non-match in a sequence of non-matches
# If a code point is a match can then be easily determined by finding the lower bound of a given code point in the table
# Iff the index of such lower bound is even, the code point is a match

import unicodedata 

def GenerateTable(Categories, ExtraCodePoints, Range, Format, MaxPerLine):
    print("    ", end="")
    CurPerLine = 0
    WasMatch = False
    for CodePoint in Range:
        IsMatch = (unicodedata.category(chr(CodePoint)) in Categories) or (CodePoint in ExtraCodePoints)
        if IsMatch != WasMatch:
            print(Format % CodePoint, end="")
            WasMatch = IsMatch
            CurPerLine += 1
            if CurPerLine == MaxPerLine:
                print("\n    ", end="")
                CurPerLine = 0

# Implementing Javascript identifier convention here (sans the $) which is:
# An identifier must start with $, _, or any character in the Unicode categories 
# “Uppercase letter (Lu)”, “Lowercase letter (Ll)”, “Titlecase letter (Lt)”, “Modifier letter (Lm)”, “Other letter (Lo)”, or “Letter number (Nl)”.
# The rest of the string can contain the same characters, plus 
# any U+200C zero width non-joiner characters, U+200D zero width joiner characters, and characters in the Unicode categories “Non-spacing mark (Mn)”, “Spacing combining mark (Mc)”, “Decimal digit number (Nd)”, or “Connector punctuation (Pc)”.

IdentifierStartCategories = [ 'Lu', 'Ll', 'Lt', 'Lo', 'Lm', 'Nl' ] # Unicode categories an identifier may start with
print("const uint16_t UnicodeIdentifierStartLookup16[] =\n{")
GenerateTable(IdentifierStartCategories, [], range(0x80, 0x10000), '0x%04x,', 24)
print("\n};\n")
print("const uint32_t UnicodeIdentifierStartLookup32[] =\n{")
GenerateTable(IdentifierStartCategories, [], range(0x10000, 0x110000), '0x%05x,', 21)
print("\n};\n")

IdentifierContinueCategories = [ 'Lu', 'Ll', 'Lt', 'Lo', 'Lm', 'Nl', 'Mn', 'Mc', 'Nd', 'Pc' ] # Unicode categories an identifier may continue with
print("const uint16_t UnicodeIdentifierContinueLookup16[] =\n{")
GenerateTable(IdentifierContinueCategories, [0x200C, 0x200D], range(0x80, 0x10000), '0x%04x,', 24)
print("\n};\n")
print("const uint32_t UnicodeIdentifierContinueLookup32[] =\n{")
GenerateTable(IdentifierContinueCategories, [0x200C, 0x200D], range(0x10000, 0x110000), '0x%05x,', 21)
print("\n};\n")
