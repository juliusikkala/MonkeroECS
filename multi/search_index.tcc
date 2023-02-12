/*
The MIT License (MIT)

Copyright (c) 2020, 2021, 2022 Julius Ikkala

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#ifndef MONKERO_SEARCH_INDEX_TCC
#define MONKERO_SEARCH_INDEX_TCC
#include "search_index.hh"

namespace monkero
{

template<typename T>
constexpr bool search_index_is_empty_default(
    int,
    typename T::empty_default_impl const * = nullptr
){ return true; }

template<typename T>
constexpr bool search_index_is_empty_default(long)
{ return false; }

template<typename T>
constexpr bool search_index_is_empty_default()
 { return search_index_is_empty_default<T>(0); }

template<typename Component>
void search_index<Component>::add_entity(entity, const Component&) {}

template<typename Component>
void search_index<Component>::update(scene&) {}

template<typename Component>
void search_index<Component>::remove_entity(entity, const Component&) {}

}

#endif

