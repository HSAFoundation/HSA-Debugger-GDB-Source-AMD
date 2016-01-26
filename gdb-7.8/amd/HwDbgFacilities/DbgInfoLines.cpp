//==============================================================================
// Copyright (c) 2012-2015 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Description: Contains the implementation of FileLocation which is a popular implementation of a LineType
//==============================================================================
/// Local:
#include <DbgInfoLines.h>

/// STL:
#include <sstream>

using namespace HwDbg;

#define DBGINFO_IS_STRP_EMPTY(pstr) ((nullptr == pstr) || (pstr->empty()))
#define DBGINFO_IS_STRP_NOT_EMPTY(pstr) ((nullptr != pstr) && (!pstr->empty()))

/////////////////////////////////////////////////////////////////////////////////////////////////////
/// FileLocation
/// \brief Description: Constructor
/// \param[in]          fullPath - Full path
/// \param[in]          lineNum - line number
/////////////////////////////////////////////////////////////////////////////////////////////////////
FileLocation::FileLocation(const std::string& fullPath, HwDbgUInt64 lineNum)
    : m_fullPath(nullptr), m_lineNum(lineNum)
{
    if (!fullPath.empty())
    {
        m_fullPath = new std::string(fullPath);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
/// FileLocation
/// \brief Description: Copy Constructor
/// \param[in]          other - copy from
/////////////////////////////////////////////////////////////////////////////////////////////////////
FileLocation::FileLocation(const FileLocation& other)
    : m_fullPath(nullptr), m_lineNum(other.m_lineNum)
{
    if (DBGINFO_IS_STRP_NOT_EMPTY(other.m_fullPath))
    {
        m_fullPath = new std::string(*other.m_fullPath);
    }
}
#if (__cplusplus >= 201103L) || (_MSC_VER >= 1800)
/////////////////////////////////////////////////////////////////////////////////////////////////////
/// FileLocation
/// \brief Description: Move Constructor
/// \param[in]          other - move from
/////////////////////////////////////////////////////////////////////////////////////////////////////
FileLocation::FileLocation(FileLocation&& other)
    : m_fullPath(other.m_fullPath), m_lineNum(other.m_lineNum)
{
    other.m_fullPath = nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
/// FileLocation
/// \brief Description: Move operator
/// \param[in]          other - move from
/////////////////////////////////////////////////////////////////////////////////////////////////////
FileLocation& FileLocation::operator= (FileLocation&& other)
{
    if (this != &other)
    {
        delete m_fullPath;
        m_fullPath = other.m_fullPath;
        other.m_fullPath = nullptr;
        m_lineNum = other.m_lineNum;
    }

    return *this;
}
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////
/// ~FileLocation
/// \brief Description: Destructor
/////////////////////////////////////////////////////////////////////////////////////////////////////
FileLocation::~FileLocation()
{
    delete m_fullPath;
    m_fullPath = nullptr;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
/// AsString
/// \brief Description: Static function which dumps to string
/// \param[in]          fileLoc - The member to dump
/// \param[out]         o_outputString - the output string
/////////////////////////////////////////////////////////////////////////////////////////////////////
void FileLocation::AsString(const FileLocation& fileLoc, std::string& o_outputString)
{
    std::ostringstream oss;
    std::string fileNameString = "<>";

    if (DBGINFO_IS_STRP_NOT_EMPTY(fileLoc.m_fullPath))
    {
        fileNameString = *fileLoc.m_fullPath;
    }

    oss << fileNameString << ':' << fileLoc.m_lineNum;
    o_outputString = oss.str();
}

bool FileLocation::operator< (const FileLocation& other) const
{
    bool retVal = false;

    if (DBGINFO_IS_STRP_EMPTY(m_fullPath))
    {
        retVal = DBGINFO_IS_STRP_NOT_EMPTY(other.m_fullPath) || (m_lineNum < other.m_lineNum);
    }
    else if (DBGINFO_IS_STRP_NOT_EMPTY(other.m_fullPath))
    {
        retVal = (*m_fullPath < *other.m_fullPath) || ((*m_fullPath == *other.m_fullPath) && (m_lineNum < other.m_lineNum));
    }

    return retVal;
}

bool FileLocation::operator== (const FileLocation& other) const
{
    bool retVal = false;

    if (DBGINFO_IS_STRP_EMPTY(m_fullPath))
    {
        retVal = DBGINFO_IS_STRP_EMPTY(other.m_fullPath);
    }
    else if (DBGINFO_IS_STRP_NOT_EMPTY(other.m_fullPath))
    {
        retVal = (*m_fullPath == *other.m_fullPath);
    }

    retVal = retVal && (m_lineNum == other.m_lineNum);
    return retVal;
}

FileLocation& FileLocation::operator= (const FileLocation& other)
{
    if (this != &other)
    {
        if (DBGINFO_IS_STRP_NOT_EMPTY(other.m_fullPath))
        {
            if (nullptr == this->m_fullPath)
            {
                this->m_fullPath = new std::string(*other.m_fullPath);
            }
            else
            {
                *this->m_fullPath = *other.m_fullPath;
            }
        }
        else
        {
            delete this->m_fullPath;
            this->m_fullPath = nullptr;
        }
    }

    this->m_lineNum = other.m_lineNum;

    return *this;
}

FileLocation& FileLocation::operator++ ()
{
    m_lineNum++;

    return *this;
}

FileLocation FileLocation::operator++ (int)
{
    FileLocation retVal(*this);

    ++*this;

    return retVal;
}

FileLocation& FileLocation::operator-- ()
{
    if (m_lineNum > 0)
    {
        m_lineNum--;
    }

    return *this;
}

FileLocation FileLocation::operator-- (int)
{
    FileLocation retVal(*this);

    --*this;

    return retVal;
}

FileLocation::operator bool() const
{
    return (m_lineNum > 0);
}

FileLocation::operator HwDbgUInt64() const
{
    return (m_lineNum);
}

FileLocation& FileLocation::operator= (const HwDbgUInt64& other)
{
    delete this->m_fullPath;
    this->m_fullPath = nullptr;
    this->m_lineNum = other;
    return (*this);
}
