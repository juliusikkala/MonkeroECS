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
#ifndef MONKERO_SEARCH_INDEX_HH
#define MONKERO_SEARCH_INDEX_HH
#include "entity.hh"

namespace monkero
{
class scene;

/** Specializing this class for your component type and implementing the given
 * functions allows for accelerated entity searching based on any parameter you
 * want to define. The default does not define any searching operations.
 */
template<typename Component>
class search_index
{
public:
    // It's up to you how you want to do this searching. You should return
    // INVALID_ENTITY if there was no entity matching the search parameters.
    // You can also have multiple find() overloads for the same component type.

    // entity find(some parameters here) const;

    /** Called automatically when an entity of this component type is added.
     * \param id ID of the entity whose component is being added.
     * \param data the component data itself.
     */
    void add_entity(entity id, const Component& data);

    /** Called automatically when an entity of this component type is removed.
     * \param id ID of the entity whose component is being removed.
     * \param data the component data itself.
     */
    void remove_entity(entity id, const Component& data);

    /** Manual full search index refresh.
     * Never called automatically, the ECS has a refresh_search_index() function
     * that the user must call that then calls this.
     * \param e the ECS context.
     */
    void update(scene& e);

    // Don't copy this one though, or else you won't get some add_entity or
    // remove_entity calls.
    using empty_default_impl = void;
};

}

#endif
