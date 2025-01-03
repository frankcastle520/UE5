#include "StdAfx.h"
#include "TextEncoder.h"
#include <String.h>

TextEncoder::TextEncoder(void)
{
}

TextEncoder::~TextEncoder(void)
{
}

void TextEncoder::SmokeTest(void)
{
    // test mode fo Unicode transcoding
    //wchar_t * wTestStr = L"The € quick ¢ brown fox jumped the fence <АБВГ>";
    wchar_t * wTestStr = L"\xD852\xDF62 The € quick ¢ brown fox jumped the fence <АБВГ>";
    //wchar_t * wTestStr = L"\xD852\xDF62 T €<АБВГ>";

    unsigned long uniCodeLong = 0x00024B62; // a Unicode point that requires a surrogate pair to encode in UTF-16
        
    SurrogatePair sp =  TextEncoder::UnicodePointToSurrogatePair(uniCodeLong);
    unsigned long uniCodeTest = TextEncoder::SurrogatePairToUnicodePoint(sp);

    wprintf(L"UTF-16 String: %s\r\n", wTestStr);

    char * aTestStr = TextEncoder::Utf16ToUtf8(wTestStr);
    printf("UTF-8 Translation: %s\r\n", aTestStr);

    wchar_t * wResultStr = TextEncoder::Utf8ToUtf16(aTestStr);

    wprintf(L"UTF-16 Retranslation: %s\r\n", wResultStr);
}

char * TextEncoder::CopyStr(const char *s)
{
    int cnt = static_cast<int>(strlen(s));
    char * newStr = new char[cnt+1];
    for (int idx = 0; idx < cnt; idx++)
        newStr[idx] = s[idx];
    newStr[cnt] = '\0';
    return newStr;
}

wchar_t * TextEncoder::CopyWStr(const wchar_t *s)
{
    int cnt = static_cast<int>(wcslen(s));
    wchar_t * newStr = new wchar_t[cnt+1];
    for (int idx = 0; idx < cnt; idx++)
        newStr[idx] = s[idx];
    newStr[cnt] = '\0';
    return newStr;
}

SurrogatePair TextEncoder::UnicodePointToSurrogatePair(unsigned long codePoint)
{
    SurrogatePair val;
    val.first = 0;
    val.second = 0;

    // need to encode an up to 21 bit code point into a two wchar surrogate pair.
        
    codePoint -= 0x10000; //this gives us a 20 bit number, yyyyyyyyyyxxxxxxxxxx

    // yyyyyyyyyyxxxxxxxxxx -> the two surrogate pair using this transfermation
    //              110110yyyyyyyyyy (0xD800 + yyyyyyyyyyy) 110111xxxxxxxxxx (0xDC00 +yyyyyyyyyy)

    val.first = 0xD800 | ((codePoint >> 10) & 0x03FF); //yyyyyyyyyy goes in the first of the pair

    val.second = 0xDC00 | (codePoint & 0x03FF); //xxxxxxxxxx goes in the second of the pair

    return val;
}

unsigned long TextEncoder::SurrogatePairToUnicodePoint(wchar_t first, wchar_t second)
{
    unsigned long val = 0;

    // get the 20 bit value from the pair
    //  110110yyyyyyyyyy 110111xxxxxxxxxx --> 000000000000YYYY yyyyyyxxxxxxxxxx (32 bit number, need only 20 bits)
    val = ((unsigned long)(0x03FF & first)) << 10; // get yyyyyyyyyy in the right place
    val |= ((unsigned long)(0x03FF & second)); //get xxxxxxxxxx

    val += 0x10000;

    return val;
}

unsigned long TextEncoder::SurrogatePairToUnicodePoint(SurrogatePair surrogates)
{
    return SurrogatePairToUnicodePoint(surrogates.first, surrogates.second);
}

void TextEncoder::RecodeUtf16CharInUtf8(const wchar_t * pSrc, char * pDest)
{
    // check two see if this is a sing UTF-16 wchar or a surrogate pair
    if (( *pSrc < 0xD800 ) || ( *pSrc > 0xDFFF ))
    {
        //single wchar 
        if (*pSrc < 128)
        {
            // single byte ASCII UTF16 character
            *pDest = (char) *pSrc;
            return;
        }
        if (*pSrc < 0x0800)
        {
            // two byte UTF8 character
            // 00000yyy XXxxxxxx --> 110yyyXX 10xxxxxx
            
            // first char
            char c = 0xC0 | (char)(((wchar_t)(*pSrc >> 6)) & 0x1F); 
            *pDest = c;

            //second char
            c = 0x80 | (char)(*pSrc & 0x3F);
            *(pDest + 1) = c;
            return;
        }
        else
        {
            // three byte UTF8 character
            // YYYYyyyy XXxxxxxx --> 1110YYYY 10yyyyXX 10xxxxxx
            
            // first char
            char c = 0xE0 | (((wchar_t)(*pSrc >> 12)) & 0x0F); 
            *pDest = c;

            //second char
            c = 0x80 | (char)(((wchar_t)(*pSrc >> 6)) & 0x3F);
            *(pDest + 1) = c;

            //third char
            c = 0x80 | (char)(*pSrc & 0x3F);
            *(pDest + 2) = c;
            return;
        }
    }

    // this is a two wchar surrogate pair and it's going to take 4 bytes

    // get the 20 bit value from the pair
    //  110110YYYYyyyyyy 110111xxxxxxxxxx --> 000000000000YYYY yyyyyyxxxxxxxxxx +0x10000 (32 bit number, need only 21 bits)
    unsigned long uCodePoint = SurrogatePairToUnicodePoint(*pSrc, *(pSrc + 1));

    // encode the now bits into 4 UTF-8 bytes
    // 000ZZZzz YYYYyyyy XXxxxxxx --> 11110ZZZ 10zzYYYY 10yyyyXX 10xxxxxx

    // first char
    char c = 0xF0 | (char)((unsigned long)(((uCodePoint >> 18)) & 0x07)); 
    *pDest = c;

    //second char
    c = 0x80 | (char)((unsigned long)(((uCodePoint >> 12)) & 0x3F));
    *(pDest + 1) = c;

    //third char
    c = 0x80 | (char)((unsigned long)(((uCodePoint >> 6)) & 0x3F));
    *(pDest + 2) = c;

    //fourth char
    c = 0x80 | (char)((unsigned long)(uCodePoint & 0x3F));
    *(pDest + 3) = c;

    return;
}

void TextEncoder::RecodeUtf8CharInUtf16(const char * pSrc, wchar_t * pDest)
{

    if ( !(*pSrc & 0x80) ) // ASCII character < 128
    {
        *pDest = (wchar_t) *pSrc;
        return;
    }
    unsigned int w1 = 0;
    unsigned long w2 = 0;

    if ( (*pSrc & 0xE0) == 0xC0) // 110xxxxx 2 bytes in UTF-8
    {
        // 110yyyXX 10xxxxxx UTF-8 -->  00000yyy XXxxxxxx UTF-16

        w1 = ((wchar_t)(*pSrc & 0x1C)) << 6; // puts the yyy in the lower bits of the upper byte where they belong
        w1 &= 0xFF00; // mask off the lower byte

        w1 |= ((*pSrc & 0x03)) << 6; // get the two XX bits from the first utf-8 character
                                   // into the right bits of the lower byte
        w1 |= (*(pSrc+1) & 0x3F);  // copy the remaining xxxxxx bits from the 2nd UTF-8 byte

        *pDest = (wchar_t) w1;

        return;
    }
    if ( (*pSrc & 0xF0) == 0xE0) // 1110xxxx 3 bytes in UTF-8
    {
        // 1110YYYY 10yyyyXX 10xxxxxx UTF-8 -->  YYYYyyyy XXxxxxxx UTF-16

        w1 = ((wchar_t)(*pSrc & 0x0F)) << 12; // puts the YYYY in the upper bits of the upper byte

        w1 |= ((wchar_t)(*(pSrc + 1) & 0x3C)) << 6; //puts yyyy in the lower bits of the upper byte

        w1 |= ((wchar_t)(*(pSrc + 1) & 0x03)) << 6; // get the two XX bits from the second utf-8 character
                                       // into the upper bits of the lower byte

        w1 |= (*(pSrc+2) & 0x2F);  // copy the remaining xxxxxx bits from the 3rd UTF-8 byte

        *pDest = (wchar_t) w1;

        return;
    }
    if ( (*pSrc & 0xF1) == 0xF0) // 11110xxx
    {
        // 11110ZZZ 10zzYYYY 10yyyyXX 10xxxxxx UTF-8 --> 000000 000ZZZzz YYYYyyyy XXxxxxxx UTF-16 (extended code plane)

        w2 = ((unsigned long)(*pSrc & 0x07)) << 2; // puts the ZZZ in the middle bits of the lower byte
        w2 |= ((unsigned long)(*(pSrc + 1) & 0x30)) >> 4; // puts the zz in the lower bits of the lower byte
        w2 <<= 16; //Shift the lower byte to the third byte

        w1 = ((unsigned long)(*(pSrc + 1) & 0x0F)) << 4; // puts the YYYY in the upper bits of the lower byte
        w1 |= ((unsigned long)(*(pSrc + 2) & 0x3C)) >> 2; //puts yyyy in the lower bits of the lower byte
        w1 <<= 8; // shift into the upper byte where they belong

        w1 |= ((unsigned long)(*(pSrc+2) & 0x03)) << 6; // get the two XX bits from the 3rd utf-8 character
                                       // into the right bits of the lower byte

        w1 |= ((unsigned long)(*(pSrc+3)) & 0x2F);  // copy the remaining xxxxxx bits from the 4th UTF-8 byte

        w2 |= w1; // put both bytes in w1 we now have up to a 21 bit number


        // need to encode the 21 bit Unicode point into a two wchar surrogate pair.
        SurrogatePair sp = UnicodePointToSurrogatePair(w2);

        *pDest = sp.first;

        *(pDest + 1) = sp.second;

        return;
    }
    // Not a valid Utf-8 leading byte

    *pDest = (wchar_t) 0xFFFD;

    return;

}
wchar_t * TextEncoder::Utf8ToUtf16(const char * pStr)
{
    int cnt = Utf8StrLen( pStr );

    // initially allow space for each character to take 4 bytes
    wchar_t * pCpy = new wchar_t[(2 * cnt) + 1];

    // copy each 1,2,3, or 4 byte utf-8 encoded character into the proper
    // sized (1 or 2 wchar_t) utf-16 encoded character
    int idx1 = 0;
    int idx2 = 0;
    for (int idx = 0; idx < cnt; idx++)
    {
        RecodeUtf8CharInUtf16(&pStr[idx2], &pCpy[idx1]);

        idx1 += Utf16CharSize(pCpy[idx1]);
        idx2 += Utf8CharSize(pStr[idx2]);
    }
    pCpy[idx1] = '\0';

    // copy into the 'right' sized array
    wchar_t * pRet = CopyWStr(pCpy);
    delete pCpy;

    return pRet;
}

wchar_t * TextEncoder::AsciiToUtf16(const char * pStr)
{
    int cnt = static_cast<int>(strlen( pStr ));

    wchar_t * pCpy = new wchar_t[cnt + 1]; 

    for (int idx = 0; idx < cnt; idx++)
    {
        pCpy[idx] = (wchar_t) pStr[idx];
    }
    pCpy[cnt] = '\0';
    return pCpy;

}

int TextEncoder::Utf16StrLen(const wchar_t * pStr)  // length in characters, not bytes
{
    int cnt = 0;
    int idx = 0;
    while ( pStr[idx] != '\0')
     {
         idx += Utf16CharSize(pStr[idx]);
         cnt++;
    }
    return cnt;
}

int TextEncoder::Utf16StrBytes(const wchar_t * pStr)  // length in bytes, not characters
{
    int cnt = 0;
    while ( pStr[cnt] != '\0')
        cnt += 2;
    return cnt;
}

int TextEncoder::Utf16CharSize(const wchar_t c)  // How many wchar_t's in this character, 1 or 2?
{
    if (( c >= 0xD800 ) && ( c <= 0xDFFF ))
        return 2;
    return 1;
}

char * TextEncoder::Utf16ToUtf8(const wchar_t * pStr)
{
    int cnt = Utf16StrLen(pStr);

    // allow 4 bytes per character (max length possible)
    char * pCpy = new char[(4 * cnt) + 1];

    // copy each 1 or 2 wchar character into a 1,2,3, or 4 byte utf-8 encoded character
    int idx1 = 0;
    int idx2 = 0;
    for (int idx = 0; idx < cnt; idx++)
    {
        RecodeUtf16CharInUtf8(&pStr[idx2], &pCpy[idx1]);

        idx1 += Utf8CharSize(pCpy[idx1]);
        idx2 += Utf16CharSize(pStr[idx2]);
    }
    pCpy[idx1] = '\0';

    // copy into the 'right' sized array
    char * pRet = CopyStr(pCpy);
    delete pCpy;

    return pRet;
}
char * TextEncoder::AsciiToUtf8(const char * pStr)
{
    // An Ascii string is already utf-8, so just copy it
    return CopyStr(pStr);
}

// length in characters, not bytes
int TextEncoder::Utf8StrLen(const char* pStr)
{
    int cnt = 0;
    int idx = 0;
    while ( pStr[idx] != '\0')
     {
         idx += Utf8CharSize(pStr[idx]);
         cnt++;
    }
    return cnt;
}
 
// length in bytes, not characters
int TextEncoder::Utf8StrBytes(const char * pStr)
{
    int cnt = 0;
    while ( pStr[cnt] != '\0')
        cnt++;
    return cnt;
}

// How many bytes in this character, 1, 2, 3, or 4?
int TextEncoder::Utf8CharSize(char c)
{
    if ( !(c & 0x80) ) // ASCII character < 128
        return 1;
    if ( (c & 0xE0) == 0xC0) // 110xxxxx
        return 2;
    if ( (c & 0xF0) == 0xE0) // 1110xxxx
        return 3;
    if ( (c & 0xF1) == 0xF0) // 11110xxx
        return 4;

    // Not a valid Utf-8 leading byte
    return 1;
}

char * TextEncoder::Utf16ToAscii(const wchar_t * pStr)
{
    int cnt = Utf16StrLen(pStr);

    char * pCpy = new char[cnt + 1];

    int idx2 = 0;
    for (int idx = 0; idx < cnt; idx++)
    {
        if (pStr[idx2] < 128)
            pCpy[idx] = (char) pStr[idx2];
        else
            pCpy[idx] = '?'; // not mapable

        idx2 += Utf16CharSize(pStr[idx2]);
    }
    pCpy[cnt] = '\0';
    return pCpy;
}

char * TextEncoder::Utf8ToAscii(const char * pStr)
{
    int cnt = Utf8StrLen(pStr);

    char * pCpy = new char[cnt + 1];

    int idx2 = 0;
    for (int idx = 0; idx < cnt; idx++)
    {
        if (((unsigned char) pStr[idx2]) < 128)
            pCpy[idx] = (char) pStr[idx2];
        else
            pCpy[idx] = '?'; // not mapable

        idx2 += Utf8CharSize(pStr[idx2]);
    }
    pCpy[cnt] = '\0';
    return pCpy;
}
