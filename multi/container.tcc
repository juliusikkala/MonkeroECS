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
#ifndef MONKERO_CONTAINER_TCC
#define MONKERO_CONTAINER_TCC
#include "container.hh"
#include "ecs.hh"
#include <cstring>
#ifdef MONKERO_CONTAINER_DEBUG_UTILS
#include <iostream>
#include <bitset>
#endif

namespace monkero
{

template<typename T>
component_container<T>::component_container(scene& ctx)
:   entity_count(0), bucket_count(0),
    bucket_bitmask(nullptr), top_bitmask(nullptr),
    bucket_jump_table(nullptr), bucket_components(nullptr), batching(false),
    batch_checklist_size(0), batch_checklist_capacity(0),
    batch_checklist(nullptr), bucket_batch_bitmask(nullptr), ctx(&ctx)
{
}

template<typename T>
component_container<T>::~component_container()
{
    destroy();
}

template<typename T>
T* component_container<T>::operator[](entity e)
{
    if(!contains(e)) return nullptr;
    return get_unsafe(e);
}

template<typename T>
const T* component_container<T>::operator[](entity e) const
{
    return const_cast<component_container<T>*>(this)->operator[](e);
}

template<typename T>
void component_container<T>::insert(entity id, T&& value)
{
    emplace(id, std::move(value));
}

template<typename T>
template<typename... Args>
void component_container<T>::emplace(entity id, Args&&... args)
{
    if(id == INVALID_ENTITY)
        return;

    ensure_bucket_space(id);
    if(contains(id))
    { // If we just replace something that exists, life is easy.
        bucket_erase(id, true);
        bucket_insert(id, std::forward<Args>(args)...);
    }
    else if(batching)
    {
        entity_count++;
        if(!batch_change(id))
        {
            // If there was already a change, that means that there was an
            // existing batched erase. That means that we can replace an
            // existing object instead.
            bucket_erase(id, true);
        }
        bucket_insert(id, std::forward<Args>(args)...);
    }
    else
    {
        entity_count++;
        bitmask_insert(id);
        jump_table_insert(id);
        bucket_insert(id, std::forward<Args>(args)...);
    }
}

template<typename T>
void component_container<T>::erase(entity id)
{
    if(!contains(id))
        return;
    entity_count--;

    if(batching)
    {
        if(!batch_change(id))
        {
            // If there was already a change, that means that there was an
            // existing batched add. Because it's not being iterated, we can
            // just destroy it here.
            bucket_erase(id, true);
        }
        else signal_remove(id, get_unsafe(id));
    }
    else
    {
        bool erase_bucket = bitmask_erase(id);
        jump_table_erase(id);
        bucket_erase(id, true);
        if(erase_bucket)
        {
            bucket_self_erase(id >> bucket_exp);
            try_jump_table_bucket_erase(id >> bucket_exp);
        }
    }
}

template<typename T>
void component_container<T>::clear()
{
    if(batching)
    { // Uh oh, this is super suboptimal :/ pls don't clear while iterating.
        for(auto it = begin(); it != end(); ++it)
        {
            erase((*it).first);
        }
    }
    else
    {
        // Clear top bitmask
        std::uint32_t top_bitmask_count = get_top_bitmask_size();
        for(std::uint32_t i = 0; i< top_bitmask_count; ++i)
            top_bitmask[i] = 0;

        // Destroy all existing objects
        if(
            ctx->get_handler_count<remove_component<T>>() ||
            !search_index_is_empty_default<decltype(search)>()
        ){
            for(auto it = begin(); it != end(); ++it)
            {
                auto pair = *it;
                signal_remove(pair.first, pair.second);
                pair.second->~T();
            }
        }
        else
        {
            for(auto it = begin(); it != end(); ++it)
                (*it).second->~T();
        }

        // Release all bucket pointers
        for(std::uint32_t i = 0; i < bucket_count; ++i)
        {
            if(bucket_bitmask[i])
            {
                delete [] bucket_bitmask[i];
                bucket_bitmask[i] = nullptr;
            }
            if(bucket_batch_bitmask[i])
            {
                delete [] bucket_batch_bitmask[i];
                bucket_batch_bitmask[i] = nullptr;
            }
            if(bucket_jump_table[i])
            {
                delete [] bucket_jump_table[i];
                bucket_jump_table[i] = nullptr;
            }
            if constexpr(!tag_component)
            {
                if(bucket_components[i])
                {
                    delete [] reinterpret_cast<t_mimicker*>(bucket_components[i]);
                    bucket_components[i] = nullptr;
                }
            }
        }
    }
    entity_count = 0;
}

template<typename T>
bool component_container<T>::contains(entity id) const
{
    entity hi = id >> bucket_exp;
    if(id == INVALID_ENTITY || hi >= bucket_count) return false;
    entity lo = id & bucket_mask;
    if(batching)
    {
        bitmask_type bitmask = bucket_bitmask[hi] ?
            bucket_bitmask[hi][lo>>bitmask_shift] : 0;
        if(bucket_batch_bitmask[hi])
            bitmask ^= bucket_batch_bitmask[hi][lo>>bitmask_shift];
        return (bitmask >> (lo&bitmask_mask))&1;
    }
    else
    {
        bitmask_type* bitmask = bucket_bitmask[hi];
        if(!bitmask) return false;
        return (bitmask[lo>>bitmask_shift] >> (lo&bitmask_mask))&1;
    }
}

template<typename T>
void component_container<T>::start_batch()
{
    batching = true;
    batch_checklist_size = 0;
}

template<typename T>
void component_container<T>::finish_batch()
{
    if(!batching) return;
    batching = false;

    // Discard duplicate changes first.
    for(std::uint32_t i = 0; i < batch_checklist_size; ++i)
    {
        std::uint32_t ri = batch_checklist_size-1-i;
        entity& id = batch_checklist[ri];
        entity hi = id >> bucket_exp;
        entity lo = id & bucket_mask;
        bitmask_type bit = 1lu<<(lo&bitmask_mask);
        bitmask_type* bbit = bucket_batch_bitmask[hi];
        if(bbit && (bbit[lo>>bitmask_shift] & bit))
        { // Not a dupe, but latest state.
            bbit[lo>>bitmask_shift] ^= bit;
        }
        else id = INVALID_ENTITY;
    }

    // Now, do all changes for realzies. All IDs that are left are unique and
    // change the existence of an entity.
    for(std::uint32_t i = 0; i < batch_checklist_size; ++i)
    {
        entity& id = batch_checklist[i];
        if(id == INVALID_ENTITY) continue;

        entity hi = id >> bucket_exp;
        entity lo = id & bucket_mask;
        bitmask_type bit = 1lu<<(lo&bitmask_mask);
        if(bucket_bitmask[hi] && (bucket_bitmask[hi][lo>>bitmask_shift] & bit))
        { // Erase
            bitmask_erase(id);
            jump_table_erase(id);
            bucket_erase(id, false);
        }
        else
        { // Insert (in-place)
            bitmask_insert(id);
            jump_table_insert(id);
            // No need to add to bucket, that already happened due to batching
            // semantics.
        }
    }

    // Finally, check erased entries for if we can remove their buckets.
    for(std::uint32_t i = 0; i < batch_checklist_size; ++i)
    {
        entity& id = batch_checklist[i];
        if(id == INVALID_ENTITY) continue;

        entity hi = id >> bucket_exp;
        if(bucket_bitmask[hi] == nullptr) // Already erased!
            continue;

        entity lo = id & bucket_mask;
        if(bucket_bitmask[hi][lo>>bitmask_shift] == 0 && bitmask_empty(hi))
        { // This got erased, so check if the whole bucket is empty.
            bucket_self_erase(hi);
            try_jump_table_bucket_erase(id >> bucket_exp);
        }
    }
}

template<typename T>
typename component_container<T>::iterator component_container<T>::begin()
{
    if(entity_count == 0) return end();
    // The jump entry for INVALID_ENTITY stores the first valid entity index.
    // INVALID_ENTITY is always present, but doesn't cause allocation of a
    // component for itself.
    return iterator(*this, bucket_jump_table[0][0]);
}

template<typename T>
typename component_container<T>::iterator component_container<T>::end()
{
    return iterator(*this, INVALID_ENTITY);
}

template<typename T>
std::size_t component_container<T>::size() const
{
    return entity_count;
}

template<typename T>
void component_container<T>::update_search_index()
{
    search.update(*ctx);
}

template<typename T>
void component_container<T>::list_entities(
    std::map<entity, entity>& translation_table
){
    for(auto it = begin(); it; ++it)
        translation_table[(*it).first] = INVALID_ENTITY;
}

template<typename T>
void component_container<T>::concat(
    scene& target,
    const std::map<entity, entity>& translation_table
){
    if constexpr(std::is_copy_constructible_v<T>)
    {
        for(auto it = begin(); it; ++it)
        {
            auto pair = *it;
            target.emplace<T>(translation_table.at(pair.first), *pair.second);
        }
    }
}

template<typename T>
void component_container<T>::copy(
    scene& target,
    entity result_id,
    entity original_id
){
    if constexpr(std::is_copy_constructible_v<T>)
    {
        T* comp = operator[](original_id);
        if(comp) target.emplace<T>(result_id, *comp);
    }
}

template<typename T>
template<typename... Args>
entity component_container<T>::find_entity(Args&&... args) const
{
    return search.find(std::forward<Args>(args)...);
}

template<typename T>
T* component_container<T>::get_unsafe(entity e)
{
    if constexpr(tag_component)
    {
        // We can return basically anything, since tag components are just tags.
        // As long as it's not nullptr, that is.
        return reinterpret_cast<T*>(&bucket_components);
    }
    else return &bucket_components[e>>bucket_exp][e&bucket_mask];
}

template<typename T>
void component_container<T>::destroy()
{
    // Cannot batch while destroying.
    if(batching)
        finish_batch();

    // Destruct all entries
    clear();
#ifndef MONKERO_CONTAINER_DEALLOCATE_BUCKETS
    for(size_t i = 0; i < bucket_count; ++i)
    {
        if(bucket_bitmask[i])
            delete bucket_bitmask[i];
        if(bucket_jump_table[i])
            delete bucket_jump_table[i];
        if constexpr(!tag_component)
        {
            if(bucket_components[i])
                delete bucket_components[i];
        }
        if(bucket_batch_bitmask[i])
            delete bucket_batch_bitmask[i];
    }
#endif

    // Free top-level arrays (bottom-level arrays should have been deleted in
    // clear().
    if(bucket_bitmask)
        delete[] bucket_bitmask;
    if(top_bitmask)
        delete[] top_bitmask;
    if(bucket_jump_table)
    {
        // The initial jump table entry won't get erased otherwise, as it is a
        // special case due to INVALID_ENTITY.
        delete[] bucket_jump_table[0];
        delete[] bucket_jump_table;
    }
    if constexpr(!tag_component)
    {
        if(bucket_components)
            delete[] bucket_components;
    }
    if(batch_checklist)
        delete[] batch_checklist;
    if(bucket_batch_bitmask)
        delete[] bucket_batch_bitmask;
}

template<typename T>
void component_container<T>::jump_table_insert(entity id)
{
    // Assumes that the corresponding bitmask change has already been made.
    std::uint32_t cur_hi = id >> bucket_exp;
    std::uint32_t cur_lo = id & bucket_mask;
    ensure_jump_table(cur_hi);

    // Find the start of the preceding block.
    entity prev_start_id = find_previous_entity(id);
    std::uint32_t prev_start_hi = prev_start_id >> bucket_exp;
    std::uint32_t prev_start_lo = prev_start_id & bucket_mask;
    ensure_jump_table(prev_start_hi);
    entity& prev_start = bucket_jump_table[prev_start_hi][prev_start_lo];

    if(prev_start_id + 1 < id)
    { // Make preceding block's end point back to its start
        entity prev_end_id = id-1;
        std::uint32_t prev_end_hi = prev_end_id >> bucket_exp;
        std::uint32_t prev_end_lo = prev_end_id & bucket_mask;
        ensure_jump_table(prev_end_hi);
        entity& prev_end = bucket_jump_table[prev_end_hi][prev_end_lo];
        prev_end = prev_start_id;
    }

    if(id + 1 < prev_start)
    { // Make succeeding block's end point back to its start
        entity next_end_id = prev_start-1;
        std::uint32_t next_end_hi = next_end_id >> bucket_exp;
        std::uint32_t next_end_lo = next_end_id & bucket_mask;
        entity& next_end = bucket_jump_table[next_end_hi][next_end_lo];
        next_end = id;
    }

    bucket_jump_table[cur_hi][cur_lo] = prev_start;
    prev_start = id;
}

template<typename T>
void component_container<T>::jump_table_erase(entity id)
{
    std::uint32_t hi = id >> bucket_exp;
    std::uint32_t lo = id & bucket_mask;
    entity prev = id-1;
    std::uint32_t prev_hi = prev >> bucket_exp;
    std::uint32_t prev_lo = prev & bucket_mask;

    entity& prev_jmp = bucket_jump_table[prev_hi][prev_lo];
    entity& cur_jmp = bucket_jump_table[hi][lo];
    entity block_start = 0;
    if(prev_jmp == id)
    { // If previous existed
        // It should jump where the current entity would have jumped
        prev_jmp = cur_jmp;
        block_start = prev;
    }
    else
    { // If previous did not exist
        // Find the starting entry of this block from it.
        prev_hi = prev_jmp >> bucket_exp;
        prev_lo = prev_jmp & bucket_mask;
        // Update the starting entry to jump to our target.
        bucket_jump_table[prev_hi][prev_lo] = cur_jmp;
        block_start = prev_jmp;
    }

    // Ensure the skip block end knows to jump back as well.
    if(cur_jmp != 0)
    {
        entity block_end = cur_jmp-1;
        prev_hi = block_end >> bucket_exp;
        prev_lo = block_end & bucket_mask;
        bucket_jump_table[prev_hi][prev_lo] = block_start;
    }
}

template<typename T>
std::size_t component_container<T>::get_top_bitmask_size() const
{
    return bucket_count == 0 ? 0 : std::max(initial_bucket_count, bucket_count >> bitmask_shift);
}

template<typename T>
bool component_container<T>::bitmask_empty(std::uint32_t bucket_index) const
{
    if(bucket_bitmask[bucket_index] == nullptr)
        return true;
    for(unsigned j = 0; j < bucket_bitmask_units; ++j)
    {
        if(bucket_bitmask[bucket_index][j] != 0)
            return false;
    }
    return true;
}

template<typename T>
void component_container<T>::bitmask_insert(entity id)
{
    std::uint32_t hi = id >> bucket_exp;
    std::uint32_t lo = id & bucket_mask;
    ensure_bitmask(hi);
    bitmask_type& mask = bucket_bitmask[hi][lo>>bitmask_shift];
    if(mask == 0)
        top_bitmask[hi>>bitmask_shift] |= 1lu<<(hi&bitmask_mask);
    mask |= 1lu<<(lo&bitmask_mask);
}

template<typename T>
bool component_container<T>::bitmask_erase(entity id)
{
    std::uint32_t hi = id >> bucket_exp;
    std::uint32_t lo = id & bucket_mask;
    bucket_bitmask[hi][lo>>bitmask_shift] &= ~(1lu<<(lo&bitmask_mask));
    if(bucket_bitmask[hi][lo>>bitmask_shift] == 0 && bitmask_empty(hi))
    {
        top_bitmask[hi>>bitmask_shift] &= ~(1lu<<(hi&bitmask_mask));
        return true;
    }
    return false;
}

template<typename T>
template<typename... Args>
void component_container<T>::bucket_insert(entity id, Args&&... args)
{
    // This function assumes that there isn't an existing entity at the same
    // position.
    T* data = nullptr;
    if constexpr(tag_component)
    {
        data = reinterpret_cast<T*>(&bucket_components);
        new (&bucket_components) T(std::forward<Args>(args)...);
    }
    else
    {
        std::uint32_t hi = id >> bucket_exp;
        std::uint32_t lo = id & bucket_mask;

        // If this component container doesn't exist yet, create it.
        if(bucket_components[hi] == nullptr)
        {
            bucket_components[hi] = reinterpret_cast<T*>(
                new t_mimicker[1u<<bucket_exp]
            );
        }
        data = &bucket_components[hi][lo];
    }
    // Create the related component here.
    new (data) T(std::forward<Args>(args)...);
    signal_add(id, data);
}

template<typename T>
void component_container<T>::bucket_erase(entity id, bool signal)
{
    // This function assumes that the given entity exists.
    T* data = nullptr;
    if constexpr(tag_component)
    {
        data = reinterpret_cast<T*>(&bucket_components);
    }
    else
    {
        std::uint32_t hi = id >> bucket_exp;
        std::uint32_t lo = id & bucket_mask;
        data = &bucket_components[hi][lo];
    }
    if(signal) signal_remove(id, data);
    data->~T();
}

template<typename T>
void component_container<T>::bucket_self_erase(std::uint32_t i)
{
    (void)i;
#ifdef MONKERO_CONTAINER_DEALLOCATE_BUCKETS
    // If the bucket got emptied, nuke it.
    delete[] bucket_bitmask[i];
    bucket_bitmask[i] = nullptr;

    if(bucket_batch_bitmask[i])
    {
        delete[] bucket_batch_bitmask[i];
        bucket_batch_bitmask[i] = nullptr;
    }

    if constexpr(!tag_component)
    {
        delete[] reinterpret_cast<t_mimicker*>(bucket_components[i]);
        bucket_components[i] = nullptr;
    }
#endif
}

template<typename T>
void component_container<T>::try_jump_table_bucket_erase(std::uint32_t i)
{
    (void)i;
#ifdef MONKERO_CONTAINER_DEALLOCATE_BUCKETS
    // The first jump table bucket is always present, due to INVALID_ENTITY
    // being the iteration starting point.
    if(i == 0 || bucket_jump_table[i] == nullptr)
        return;

    // We can be removed if the succeeding bucket is also empty.
    if(i+1 >= bucket_count || bucket_bitmask[i+1] == nullptr)
    {
        delete[] bucket_jump_table[i];
        bucket_jump_table[i] = nullptr;
    }
#endif
}

template<typename T>
void component_container<T>::ensure_bucket_space(entity id)
{
    if((id>>bucket_exp) < bucket_count)
        return;

    std::uint32_t new_bucket_count = std::max(initial_bucket_count, bucket_count);
    while(new_bucket_count <= (id>>bucket_exp))
        new_bucket_count *= 2;

    bitmask_type** new_bucket_batch_bitmask = new bitmask_type*[new_bucket_count];
    memcpy(new_bucket_batch_bitmask, bucket_batch_bitmask,
        sizeof(bitmask_type*)*bucket_count);
    memset(new_bucket_batch_bitmask+bucket_count, 0,
        sizeof(bitmask_type*)*(new_bucket_count-bucket_count));
    delete [] bucket_batch_bitmask;
    bucket_batch_bitmask = new_bucket_batch_bitmask;

    bitmask_type** new_bucket_bitmask = new bitmask_type*[new_bucket_count];
    memcpy(new_bucket_bitmask, bucket_bitmask,
        sizeof(bitmask_type*)*bucket_count);
    memset(new_bucket_bitmask+bucket_count, 0,
        sizeof(bitmask_type*)*(new_bucket_count-bucket_count));
    delete [] bucket_bitmask;
    bucket_bitmask = new_bucket_bitmask;

    entity** new_bucket_jump_table = new entity*[new_bucket_count];
    memcpy(new_bucket_jump_table, bucket_jump_table,
        sizeof(entity*)*bucket_count);
    memset(new_bucket_jump_table+bucket_count, 0,
        sizeof(entity*)*(new_bucket_count-bucket_count));
    delete [] bucket_jump_table;
    bucket_jump_table = new_bucket_jump_table;

    // Create initial jump table entry.
    if(bucket_count == 0)
    {
        bucket_jump_table[0] = new entity[1 << bucket_exp];
        memset(bucket_jump_table[0], 0, sizeof(entity)*(1 << bucket_exp));
    }

    if constexpr(!tag_component)
    {
        T** new_bucket_components = new T*[new_bucket_count];
        memcpy(new_bucket_components, bucket_components,
            sizeof(T*)*bucket_count);
        memset(new_bucket_components+bucket_count, 0,
            sizeof(T*)*(new_bucket_count-bucket_count));
        delete [] bucket_components;
        bucket_components = new_bucket_components;
    }

    std::uint32_t top_bitmask_count = get_top_bitmask_size();
    std::uint32_t new_top_bitmask_count = std::max(
        initial_bucket_count,
        new_bucket_count >> bitmask_shift
    );
    if(top_bitmask_count != new_top_bitmask_count)
    {
        bitmask_type* new_top_bitmask = new bitmask_type[new_top_bitmask_count];
        memcpy(new_top_bitmask, top_bitmask,
            sizeof(bitmask_type)*top_bitmask_count);
        memset(new_top_bitmask+top_bitmask_count, 0,
            sizeof(bitmask_type)*(new_top_bitmask_count-top_bitmask_count));
        delete [] top_bitmask;
        top_bitmask = new_top_bitmask;
    }

    bucket_count = new_bucket_count;
}

template<typename T>
void component_container<T>::ensure_bitmask(std::uint32_t bucket_index)
{
    if(bucket_bitmask[bucket_index] == nullptr)
    {
        bucket_bitmask[bucket_index] = new bitmask_type[bucket_bitmask_units];
        std::memset(
            bucket_bitmask[bucket_index], 0,
            sizeof(bitmask_type)*bucket_bitmask_units
        );
    }
}

template<typename T>
void component_container<T>::ensure_jump_table(std::uint32_t bucket_index)
{
    if(!bucket_jump_table[bucket_index])
    {
        bucket_jump_table[bucket_index] = new entity[1 << bucket_exp];
        memset(bucket_jump_table[bucket_index], 0, sizeof(entity)*(1 << bucket_exp));
    }
}

template<typename T>
bool component_container<T>::batch_change(entity id)
{
    std::uint32_t hi = id >> bucket_exp;
    std::uint32_t lo = id & bucket_mask;
    if(bucket_batch_bitmask[hi] == nullptr)
    {
        bucket_batch_bitmask[hi] = new bitmask_type[bucket_bitmask_units];
        std::memset(
            bucket_batch_bitmask[hi], 0,
            sizeof(bitmask_type)*bucket_bitmask_units
        );
    }
    bitmask_type& mask = bucket_batch_bitmask[hi][lo>>bitmask_shift];
    bitmask_type bit = 1lu<<(lo&bitmask_mask);
    mask ^= bit;
    if(mask & bit)
    { // If there will be a change, add this to the list.
        if(batch_checklist_size == batch_checklist_capacity)
        {
            std::uint32_t new_batch_checklist_capacity = std::max(
                initial_bucket_count,
                batch_checklist_capacity * 2
            );
            entity* new_batch_checklist = new entity[new_batch_checklist_capacity];
            memcpy(new_batch_checklist, batch_checklist,
                sizeof(entity)*batch_checklist_capacity);
            memset(new_batch_checklist + batch_checklist_capacity, 0,
                sizeof(entity)*(new_batch_checklist_capacity-batch_checklist_capacity));
            delete [] batch_checklist;
            batch_checklist = new_batch_checklist;
            batch_checklist_capacity = new_batch_checklist_capacity;
        }
        batch_checklist[batch_checklist_size] = id;
        batch_checklist_size++;
        return true;
    }
    return false;
}

template<typename T>
entity component_container<T>::find_previous_entity(entity id)
{
    std::uint32_t hi = id >> bucket_exp;
    std::uint32_t lo = id & bucket_mask;

    // Try to find in the current bucket.
    std::uint32_t prev_index = 0;
    if(find_bitmask_previous_index(bucket_bitmask[hi], lo, prev_index))
        return (hi << bucket_exp) + prev_index;

    // If that failed, search from the top bitmask.
    std::uint32_t bucket_index = 0;
    if(!find_bitmask_previous_index(top_bitmask, hi, bucket_index))
        return INVALID_ENTITY;

    // Now, find the highest bit in the bucket that was found.
    find_bitmask_top(
        bucket_bitmask[bucket_index],
        bucket_bitmask_units,
        prev_index
    );
    return (bucket_index << bucket_exp) + prev_index;
}


template<typename T>
void component_container<T>::signal_add(entity id, T* data)
{
    search.add_entity(id, *data);
    ctx->emit(add_component<T>{id, data});
}

template<typename T>
void component_container<T>::signal_remove(entity id, T* data)
{
    search.remove_entity(id, *data);
    ctx->emit(remove_component<T>{id, data});
}

template<typename T>
unsigned component_container<T>::bitscan_reverse(std::uint64_t mt)
{
#if defined(__GNUC__)
    return 63 - __builtin_clzll(mt);
#elif defined(_MSC_VER)
    unsigned long index = 0;
    _BitScanReverse64(&index, mt);
    return index;
#else
    unsigned r = (mt > 0xFFFFFFFFF) << 5;
    mt >>= r;
    unsigned shift = (mt > 0xFFFF) << 4;
    mt >>= shift;
    r |= shift;
    shift = (mt > 0xFF) << 3;
    mt >>= shift;
    r |= shift;
    shift = (mt > 0xF) << 2;
    mt >>= shift;
    r |= shift;
    shift = (mt > 0x3) << 1;
    mt >>= shift;
    r |= shift;
    return r | (mt >> 1);
#endif
}

template<typename T>
bool component_container<T>::find_bitmask_top(
    bitmask_type* bitmask,
    std::uint32_t count,
    std::uint32_t& top_index
){
    for(std::uint32_t j = 0, i = count-1; j < count; ++j, --i)
    {
        if(bitmask[i] != 0)
        {
            std::uint32_t index = bitscan_reverse(bitmask[i]);
            top_index = (i << bitmask_shift) + index;
            return true;
        }
    }

    return false;
}

template<typename T>
bool component_container<T>::find_bitmask_previous_index(
    bitmask_type* bitmask,
    std::uint32_t index,
    std::uint32_t& prev_index
){
    if(!bitmask)
        return false;

    std::uint32_t bm_index = index >> bitmask_shift;
    bitmask_type bm_mask = (1lu<<(index&bitmask_mask))-1;
    bitmask_type cur_mask = bitmask[bm_index] & bm_mask;
    if(cur_mask != 0)
    {
        std::uint32_t index = bitscan_reverse(cur_mask);
        prev_index = (bm_index << bitmask_shift) + index;
        return true;
    }

    return find_bitmask_top(bitmask, bm_index, prev_index);
}

template<typename T>
component_container<T>::iterator::iterator(component_container& from, entity e)
:   from(&from), current_entity(e), current_bucket(e>>bucket_exp)
{
    if(current_bucket < from.bucket_count)
    {
        current_jump_table = from.bucket_jump_table[current_bucket];
        if constexpr(!tag_component)
        {
            current_components = from.bucket_components[current_bucket];
        }
    }
}

void component_container_entity_advancer::advance()
{
    current_entity = current_jump_table[current_entity&bucket_mask];
    std::uint32_t next_bucket = current_entity >> bucket_exp;
    if(next_bucket != current_bucket)
    {
        current_bucket = next_bucket;
        current_jump_table = (*bucket_jump_table)[current_bucket];
    }
}

template<typename T>
typename component_container<T>::iterator& component_container<T>::iterator::operator++()
{
    current_entity = current_jump_table[current_entity&bucket_mask];
    std::uint32_t next_bucket = current_entity >> bucket_exp;
    if(next_bucket != current_bucket)
    {
        current_bucket = next_bucket;
        current_jump_table = from->bucket_jump_table[current_bucket];
        if constexpr(!tag_component)
        {
            current_components = from->bucket_components[current_bucket];
        }
    }
    return *this;
}

template<typename T>
typename component_container<T>::iterator component_container<T>::iterator::operator++(int)
{
    iterator it(*this);
    ++it;
    return it;
}

template<typename T>
std::pair<entity, T*> component_container<T>::iterator::operator*()
{
    if constexpr(tag_component)
    {
        return {
            current_entity,
            reinterpret_cast<T*>(&from->bucket_components)
        };
    }
    else
    {
        return {
            current_entity,
            &current_components[current_entity&bucket_mask]
        };
    }
}

template<typename T>
std::pair<entity, const T*> component_container<T>::iterator::operator*() const
{
    if constexpr(tag_component)
    {
        return {
            current_entity,
            reinterpret_cast<const T*>(&from->bucket_components)
        };
    }
    else
    {
        return {
            current_entity,
            &current_components[current_entity&bucket_mask]
        };
    }
}

template<typename T>
bool component_container<T>::iterator::operator==(const iterator& other) const
{
    return other.current_entity == current_entity;
}

template<typename T>
bool component_container<T>::iterator::operator!=(const iterator& other) const
{
    return other.current_entity != current_entity;
}

template<typename T>
bool component_container<T>::iterator::try_advance(entity id)
{
    if(current_entity == id)
        return true;

    std::uint32_t next_bucket = id >> bucket_exp;
    std::uint32_t lo = id & bucket_mask;
    if(
        id < current_entity ||
        next_bucket >= from->bucket_count ||
        !from->bucket_bitmask[next_bucket] ||
        !(from->bucket_bitmask[next_bucket][lo>>bitmask_shift] & (1lu << (lo&bitmask_mask)))
    ) return false;

    current_entity = id;
    if(next_bucket != current_bucket)
    {
        current_bucket = next_bucket;
        current_jump_table = from->bucket_jump_table[current_bucket];
        if constexpr(!tag_component)
            current_components = from->bucket_components[current_bucket];
    }
    return true;
}

template<typename T>
component_container<T>::iterator::operator bool() const
{
    return current_entity != INVALID_ENTITY;
}

template<typename T>
entity component_container<T>::iterator::get_id() const
{
    return current_entity;
}

template<typename T>
component_container<T>* component_container<T>::iterator::get_container() const
{
    return from;
}

template<typename T>
component_container_entity_advancer component_container<T>::iterator::get_advancer()
{
    return component_container_entity_advancer{
        bucket_mask,
        bucket_exp,
        &from->bucket_jump_table,
        current_bucket,
        current_entity,
        current_jump_table
    };
}

#ifdef MONKERO_CONTAINER_DEBUG_UTILS
template<typename T>
bool component_container<T>::test_invariant() const
{
    // Check bitmask internal validity
    std::uint32_t top_bitmask_count = get_top_bitmask_size();
    std::uint32_t top_index = 0;
    bool found = top_bitmask && find_bitmask_top(
        top_bitmask,
        top_bitmask_count,
        top_index
    );
    std::uint32_t bitmask_entity_count = 0;
    if(found && top_index >= bucket_count && !batching)
    {
        std::cout << "Top bitmask has a higher bit than bucket count!\n";
        return false;
    }

    for(std::uint32_t i = 0; i < bucket_count; ++i)
    {
        int present = (top_bitmask[i>>bitmask_shift] >> (i&bitmask_mask))&1;
        if(present && !bucket_bitmask[i] && !batching)
        {
            std::cout << "Bitmask bucket that should exist is null instead!\n";
            return false;
        }
        bool found = bucket_bitmask[i] && find_bitmask_top(
            bucket_bitmask[i],
            bucket_bitmask_units,
            top_index
        );
        if(present && !found && !batching)
        {
            std::cout << "Empty bitmask bucket marked as existing in the top-level!\n";
            return false;
        }
        else if(!present && found && !batching)
        {
            std::cout << "Non-empty bitmask bucket marked as nonexistent in the top-level!\n";
            return false;
        }
        if(bucket_bitmask[i])
        {
            for(std::uint32_t j = 0; j < bucket_bitmask_units; ++j)
            {
                bitmask_entity_count += __builtin_popcountll(bucket_bitmask[i][j]);
            }
        }
    }

    if(!batching && bitmask_entity_count != entity_count)
    {
        std::cout << "Number of entities in bitmask does not match tracked number!\n";
        return false;
    }

    // Check jump table internal validity
    std::uint32_t jump_table_entity_count = 0;
    if(entity_count != 0)
    {
        entity prev_id = 0;
        entity id = bucket_jump_table[0][0];
        while(id != 0)
        {
            bitmask_type* bm = bucket_bitmask[id>>bucket_exp];
            bool present = false;
            if(bm)
            {
                entity lo = id & bucket_mask;
                present = (bm[lo>>bitmask_shift] >> (lo&bitmask_mask))&1;
            }
            if(!present && !batching)
            {
                std::cout << "Jump table went to a non-existent entity!\n";
                return false;
            }

            entity preceding_id = id-1;
            entity prec_next_id = bucket_jump_table[preceding_id>>bucket_exp][preceding_id&bucket_mask];
            if(prec_next_id != id && prec_next_id != prev_id)
            {
                std::cout << "Jump table preceding entry has invalid target id!\n";
                return false;
            }

            jump_table_entity_count++;
            prev_id = id;
            entity next_id = bucket_jump_table[id>>bucket_exp][id&bucket_mask];
            if(next_id != 0 && next_id <= id)
            {
                std::cout << "Jump table did not jump forward!\n";
                return false;
            }
            id = next_id;
        }
    }

    if(jump_table_entity_count != entity_count && !batching)
    {
        std::cout << "Number of entities in jump table does not match tracked number!\n";
        return false;
    }
    return true;
}

template<typename T>
void component_container<T>::print_bitmask() const
{
    for(std::uint32_t i = 0; i < bucket_count; ++i)
    {
        int present = (top_bitmask[i>>bitmask_shift] >> (i&bitmask_mask))&1;
        std::cout << "bucket " << i << " ("<< (present ? "present" : "empty") << "): ";
        if(!bucket_bitmask[i])
            std::cout << "(null)\n";
        else
        {
            for(std::uint32_t j = 0; j < bucket_bitmask_units; ++j)
            {
                for(std::uint32_t k = 0; k < 64; ++k)
                {
                    std::cout << ((bucket_bitmask[i][j]>>k)&1);
                }
                std::cout << " ";
            }
            std::cout << "\n";
        }
    }
}

template<typename T>
void component_container<T>::print_jump_table() const
{
    std::uint32_t k = 0;
    for(std::uint32_t i = 0; i < bucket_count; ++i)
    {
        if(bucket_jump_table[i] == nullptr)
        {
            std::uint32_t k_start = k;
            std::uint32_t i_start = i;
            for(; i < bucket_count && bucket_jump_table[i] == nullptr; ++i)
                k += 1<<bucket_exp;
            --i;
            std::uint32_t k_end = k-1;
            std::uint32_t i_end = i;
            if(i_start == i_end)
                std::cout << "bucket " << i_start << ": " << k_start << " to " << k_end;
            else
                std::cout << "buckets " << i_start << " to " << i_end << ": " << k_start << " to " << k_end;
        }
        else
        {
            std::cout << "bucket " << i << ":\n";

            std::cout << "\tindices: |";

            for(int j = 0; j < (1<<bucket_exp); ++j, ++k)
                std::cout << " " << k << " |";

            std::cout << "\n\tdata:    |";
            for(int j = 0; j < (1<<bucket_exp); ++j)
            {
                std::cout << " " << bucket_jump_table[i][j] << " |";
            }
        }
        std::cout << "\n";
    }
}
#endif

}
#endif
