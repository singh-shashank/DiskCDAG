/*
 * Copyright (c) 2009-2010 Justin Holewinski
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#if !defined(GRAPH_ANALYSIS_HXX_INC)
#define GRAPH_ANALYSIS_HXX_INC 1

#include <cassert>

#include <iostream>
#include <fstream>
#include <map>
#include <set>
#include <stack>
#include <vector>
#include <list>
#include <stdexcept>

#include <boost/lexical_cast.hpp>
//#include <boost/algorithm/string.hpp>
#include <boost/cstdint.hpp>
#include <boost/foreach.hpp>

namespace GraphAnalysis
{
  typedef boost::uint64_t   uint64;
  typedef boost::uint32_t   uint32;
  typedef boost::uint16_t   uint16;
  typedef boost::uint8_t    uint8, byte;
  
  typedef boost::int64_t    int64;
  typedef boost::int32_t    int32;
  typedef boost::int16_t    int16;
  typedef boost::int8_t     int8;
}

#endif

