#pragma once
#include "common_defines.hpp"

#ifdef new
#undef new
#endif

#include <boost/scoped_array.hpp>
using boost::scoped_array;

#ifdef DEBUG_NEW
#define new DEBUG_NEW
#endif