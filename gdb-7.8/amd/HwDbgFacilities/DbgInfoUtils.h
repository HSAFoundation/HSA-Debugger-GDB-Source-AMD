//==============================================================================
// Copyright (c) 2012-2015 Advanced Micro Devices, Inc. All rights reserved.
//
/// \author AMD Developer Tools
/// \file
/// \brief Description: General helper functions
//==============================================================================
#ifndef __DBGINFOUTILS_H
#define __DBGINFOUTILS_H

// Local:
#include <DbgInfoDefinitions.h>

// STL:
#include <string>

/// The HSA Device default name:
#define HSA_DEVICE_STRING "HSA"

#ifdef _DEBUG
    #include <assert.h>
    #define HWDBG_ASSERT(cond) {if (!(cond)) {std::printf(#cond); assert(cond);}}
    #define HWDBG_ASSERT_EX(cond, errMsg) {if (!(cond)) {std::printf(errMsg.c_str()); assert(cond);}}
#else
    #define HWDBG_ASSERT(cond) (void)(cond)
    #define HWDBG_ASSERT_EX(cond, errMsg) (void)(cond); (void)(errMsg)
#endif

namespace HwDbg
{
// Utility functions:
/// Helper function, prints a formatted string into an std::string:
DBGINF_API std::string string_format(const std::string fmt_str, ...);

/// Helper function, prepends a string to another string:
DBGINF_API std::string& string_prepend(std::string& str, std::string prefixStr);

/// Helper function, removes trailing characters:
DBGINF_API std::string& string_remove_trailing(std::string& str, char c);
}

#endif //__DBGINFOUTILS_H

