#include <cstring>
#include "StdString.h"
#include "Platform.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <malloc.h>

stdstr::stdstr()
{
}

stdstr::stdstr(const std::string & str) :
    std::string(str)
{
}

stdstr::stdstr(const stdstr & str) :
    std::string((const std::string &)str)
{
}

stdstr::stdstr(const char * str) :
    std::string(str ? str : "")
{
}

strvector stdstr::Tokenize(const char * delimiter) const
{
    strvector tokens;

    stdstr::size_type lastPos = find_first_not_of(delimiter, 0);
    stdstr::size_type pos = find_first_of(delimiter, lastPos);
    size_t DelLen = strlen(delimiter);
    while (stdstr::npos != pos)
    {
        tokens.push_back(substr(lastPos, pos - lastPos));
        lastPos = pos + DelLen;
        pos = find_first_of(delimiter, lastPos);
    }
    if (stdstr::npos != lastPos)
    {
        tokens.push_back(substr(lastPos));
    }
    return tokens;
}

strvector stdstr::Tokenize(char delimiter) const
{
    strvector tokens;

    stdstr::size_type lastPos = find_first_not_of(delimiter, 0);
    stdstr::size_type pos = find_first_of(delimiter, lastPos);
    while (stdstr::npos != pos)
    {
        tokens.push_back(substr(lastPos, pos - lastPos));
        lastPos = pos + 1;
        pos = find_first_of(delimiter, lastPos);
    }
    if (stdstr::npos != lastPos)
    {
        tokens.push_back(substr(lastPos));
    }
    return tokens;
}

void stdstr::ArgFormat(const char * strFormat, va_list & args)
{
#pragma warning(push)
#pragma warning(disable : 4996)

    size_t nlen = _vscprintf(strFormat, args) + 1;
    char * buffer = (char *)alloca(nlen * sizeof(char));
    buffer[nlen - 1] = 0;
    if (buffer != nullptr)
    {
        vsprintf(buffer, strFormat, args);
        *this = buffer;
    }
#pragma warning(pop)
}

void stdstr::Format(const char * strFormat, ...)
{
    va_list args;
    va_start(args, strFormat);
    ArgFormat(strFormat, args);
    va_end(args);
}

stdstr & stdstr::ToLower(void)
{
    std::transform(begin(), end(), begin(), (char (*)(int))tolower);
    return *this;
}

stdstr & stdstr::ToUpper(void)
{
    std::transform(begin(), end(), begin(), (char (*)(int))toupper);
    return *this;
}

void stdstr::Replace(const char search, const char replace)
{
    std::string & str = *this;
    std::string::size_type pos = str.find(search);
    while (pos != std::string::npos)
    {
        str.replace(pos, 1, &replace, 1);
        pos = str.find(search, pos + 1);
    }
}

void stdstr::Replace(const char * search, const char replace)
{
    std::string & str = *this;
    std::string::size_type pos = str.find(search);
    size_t SearchSize = strlen(search);
    while (pos != std::string::npos)
    {
        str.replace(pos, SearchSize, &replace);
        pos = str.find(search, pos + 1);
    }
}

void stdstr::Replace(const std::string & search, const std::string & replace)
{
    std::string & str = *this;
    std::string::size_type pos = str.find(search);
    size_t SearchSize = search.size();
    while (pos != std::string::npos)
    {
        str.replace(pos, SearchSize, replace);
        pos = str.find(search, pos + replace.length());
    }
}

stdstr & stdstr::TrimLeft(const char * chars2remove)
{
    if (!empty())
    {
        std::string::size_type pos = find_first_not_of(chars2remove);
        if (pos != std::string::npos)
        {
            erase(0, pos);
        }
        else
        {
            erase(begin(), end()); // Make empty
        }
    }
    return *this;
}

stdstr & stdstr::TrimRight(const char * chars2remove)
{
    if (!empty())
    {
        std::string::size_type pos = find_last_not_of(chars2remove);
        if (pos != std::string::npos)
        {
            erase(pos + 1);
        }
        else
        {
            erase(begin(), end()); // Make empty
        }
    }
    return *this;
}

stdstr & stdstr::Trim(const char * chars2remove)
{
    if (!empty())
    {
        std::string::size_type pos = find_first_not_of(chars2remove);
        if (pos != std::string::npos)
        {
            erase(0, pos);
        }
        else
        {
            erase(begin(), end()); // Make empty
        }

        pos = find_last_not_of(chars2remove);
        if (pos != std::string::npos)
        {
            erase(pos + 1);
        }
        else
        {
            erase(begin(), end()); // Make empty
        }
    }
    return *this;
}

// Unlike Windows (wchar_t is always UTF-16), glibc's wchar_t is UTF-32/UCS-4,
// so this is a straight Unicode-scalar-value <-> UTF-8 codec rather than a
// UTF-16 one; the name is kept for API compatibility with the Windows side
// and because callers already treat it as "the platform wide-string type
// produced by ToUTF16 below, converted to/from UTF-8".
stdstr & stdstr::FromUTF16(const wchar_t * UTF16Source, bool * bSuccess)
{
    bool bConverted = false;
    std::string result;

    if (UTF16Source == nullptr)
    {
        result.clear();
        bConverted = true;
    }
    else
    {
        bConverted = true;
        for (const wchar_t * p = UTF16Source; *p != 0; ++p)
        {
            uint32_t cp = (uint32_t)*p;
            if (cp <= 0x7F)
            {
                result += (char)cp;
            }
            else if (cp <= 0x7FF)
            {
                result += (char)(0xC0 | (cp >> 6));
                result += (char)(0x80 | (cp & 0x3F));
            }
            else if (cp <= 0xFFFF)
            {
                result += (char)(0xE0 | (cp >> 12));
                result += (char)(0x80 | ((cp >> 6) & 0x3F));
                result += (char)(0x80 | (cp & 0x3F));
            }
            else if (cp <= 0x10FFFF)
            {
                result += (char)(0xF0 | (cp >> 18));
                result += (char)(0x80 | ((cp >> 12) & 0x3F));
                result += (char)(0x80 | ((cp >> 6) & 0x3F));
                result += (char)(0x80 | (cp & 0x3F));
            }
            else
            {
                bConverted = false;
            }
        }
    }
    *this = result;
    if (bSuccess)
    {
        *bSuccess = bConverted;
    }
    return *this;
}

std::wstring stdstr::ToUTF16(unsigned int /*CodePage*/, bool * bSuccess) const
{
    bool bConverted = true;
    std::wstring res;

    const unsigned char * s = (const unsigned char *)this->c_str();
    while (*s != 0)
    {
        uint32_t cp = 0;
        int extraBytes = 0;
        if ((*s & 0x80) == 0x00)
        {
            cp = *s;
            extraBytes = 0;
        }
        else if ((*s & 0xE0) == 0xC0)
        {
            cp = *s & 0x1F;
            extraBytes = 1;
        }
        else if ((*s & 0xF0) == 0xE0)
        {
            cp = *s & 0x0F;
            extraBytes = 2;
        }
        else if ((*s & 0xF8) == 0xF0)
        {
            cp = *s & 0x07;
            extraBytes = 3;
        }
        else
        {
            // Invalid leading byte; skip it rather than desync forever.
            bConverted = false;
            ++s;
            continue;
        }
        ++s;
        bool bValidSequence = true;
        for (int i = 0; i < extraBytes; i++)
        {
            if ((*s & 0xC0) != 0x80)
            {
                bValidSequence = false;
                break;
            }
            cp = (cp << 6) | (*s & 0x3F);
            ++s;
        }
        if (!bValidSequence)
        {
            bConverted = false;
            continue;
        }
        res += (wchar_t)cp;
    }
    if (bSuccess)
    {
        *bSuccess = bConverted;
    }
    return res;
}

stdstr_f::stdstr_f(const char * strFormat, ...)
{
    va_list args;
    va_start(args, strFormat);
    ArgFormat(strFormat, args);
    va_end(args);
}

