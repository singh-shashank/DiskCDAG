/* TimestampVector.hxx - part of the DDGVec project
 *
 * Copyright (c) 2012, The Ohio State University
 *
 * This file is distributed under the terms described in LICENSE.TXT in the
 * root directory.
 */

#ifndef TIMESTAMPVECTOR_HXX
#define TIMESTAMPVECTOR_HXX

#include <algorithm>
#include <cstdlib>

namespace ddg
{

class TimestampVector
{
private:
  struct Tag
  {
    int count;
  };

public:
  TimestampVector() : array(0), length(0)
  {
  }

  void resize(int n)
  {
    if (n) {
      void *mem = malloc(sizeof(Tag) + n*sizeof(int));
      Tag *tag = (Tag*)mem;
      tag->count = 1;
      array = reinterpret_cast<int*>(tag+1);
      std::fill_n(array, n, 0);
      length = n;
    }
  }

  int size()
  {
    return length;
  }

  int& operator[](int idx)
  {
    return array[idx];
  }

  void swap(TimestampVector &other)
  {
    std::swap(array, other.array);
    std::swap(length, other.length);
  }

  ~TimestampVector()
  {
    if (length) {
      Tag *tag = getTag();
      tag->count--;
      if (tag->count == 0) {
        free(tag);
      }
    }
  }

  TimestampVector(const TimestampVector& other) : array(other.array), length(other.length)
  {
    if (length) {
      getTag()->count++;
    }
  }

  TimestampVector& operator=(const TimestampVector& other)
  {
    array = other.array;
    length = other.length;
    if (length) {
      getTag()->count++;
    }
    return *this;
  }

private:
  Tag* getTag() const
  {
    return reinterpret_cast<Tag*>(array)-1;
  }

private:
  int *array;
  int length;
};

void swap(ddg::TimestampVector &v1, ddg::TimestampVector &v2);

};

#endif

