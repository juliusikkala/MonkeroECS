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
#ifndef MONKERO_CONTAINER_HH
#define MONKERO_CONTAINER_HH
#include "entity.hh"
#include "search_index.hh"
#include <limits>
#include <utility>
#include <type_traits>
#include <algorithm>
#include <map>
//#define MONKERO_CONTAINER_DEALLOCATE_BUCKETS
//#define MONKERO_CONTAINER_DEBUG_UTILS

namespace monkero
{

class scene;

template<typename T, typename=void>
struct has_bucket_exp_hint: std::false_type { };

template<typename T>
struct has_bucket_exp_hint<
    T,
    decltype((void)
        T::bucket_exp_hint, void()
    )
> : std::is_integral<decltype(T::bucket_exp_hint)> { };

/** Provides bucket size choice for the component container.
 * To change the number of entries in a bucket for your component type, you have
 * two options:
 * A (preferred when you can modify the component type):
 *     Add a `static constexpr std::uint32_t bucket_exp_hint = N;`
 * B (needed when you cannot modify the component type):
 *     Specialize component_bucket_exp_hint for your type and provide a
 *     `static constexpr std::uint32_t value = N;`
 * Where the bucket size will then be 2**N.
 */
template<typename T>
struct component_bucket_exp_hint
{
    static constexpr std::uint32_t value = []{
        if constexpr (has_bucket_exp_hint<T>::value)
        {
            return T::bucket_exp_hint;
        }
        else
        {
            uint32_t i = 6;
            // Aim for 65kb buckets
            while((std::max(sizeof(T), 4lu)<<i) < 65536lu)
                ++i;
            return i;
        }
    }();
};

class component_container_base
{
public:
    virtual ~component_container_base() = default;

    inline virtual void start_batch() = 0;
    inline virtual void finish_batch() = 0;
    inline virtual void erase(entity id) = 0;
    inline virtual void clear() = 0;
    inline virtual std::size_t size() const = 0;
    inline virtual void update_search_index() = 0;
    inline virtual void list_entities(
        std::map<entity, entity>& translation_table
    ) = 0;
    inline virtual void concat(
        scene& target,
        const std::map<entity, entity>& translation_table
    ) = 0;
    inline virtual void copy(
        scene& target,
        entity result_id,
        entity original_id
    ) = 0;
};

struct component_container_entity_advancer
{
public:
    inline void advance();

    std::uint32_t bucket_mask;
    std::uint32_t bucket_exp;
    entity*** bucket_jump_table;
    std::uint32_t current_bucket;
    entity current_entity;
    entity* current_jump_table;
};

template<typename T>
class component_container: public component_container_base
{
public:
    using bitmask_type = std::uint64_t;
    static constexpr uint32_t bitmask_bits = 64;
    static constexpr uint32_t bitmask_shift = 6; // 64 = 2**6
    static constexpr uint32_t bitmask_mask = 0x3F;
    static constexpr uint32_t initial_bucket_count = 16u;
    static constexpr bool tag_component = std::is_empty_v<T>;
    static constexpr std::uint32_t bucket_exp =
        component_bucket_exp_hint<T>::value;
    static constexpr std::uint32_t bucket_mask = (1u<<bucket_exp)-1;
    static constexpr std::uint32_t bucket_bitmask_units =
        std::max(1u, (1u<<bucket_exp)>>bitmask_shift);

    component_container(scene& ctx);
    component_container(component_container&& other) = delete;
    component_container(const component_container& other) = delete;
    ~component_container();

    T* operator[](entity e);
    const T* operator[](entity e) const;

    void insert(entity id, T&& value);

    template<typename... Args>
    void emplace(entity id, Args&&... value);

    void erase(entity id) override;

    void clear() override;

    bool contains(entity id) const;

    void start_batch() override;
    void finish_batch() override;

    class iterator
    {
    friend class component_container<T>;
    public:
        using component_type = T;

        iterator() = delete;
        iterator(const iterator& other) = default;

        iterator& operator++();
        iterator operator++(int);
        std::pair<entity, T*> operator*();
        std::pair<entity, const T*> operator*() const;

        bool operator==(const iterator& other) const;
        bool operator!=(const iterator& other) const;

        bool try_advance(entity id);

        operator bool() const;
        entity get_id() const;
        component_container<T>* get_container() const;
        component_container_entity_advancer get_advancer();

    private:
        iterator(component_container& from, entity e);

        component_container* from;
        entity current_entity;
        std::uint32_t current_bucket;
        entity* current_jump_table;
        T* current_components;
    };

    iterator begin();
    iterator end();
    std::size_t size() const override;

    void update_search_index() override;

    void list_entities(
        std::map<entity, entity>& translation_table
    ) override;
    void concat(
        scene& target,
        const std::map<entity, entity>& translation_table
    ) override;
    void copy(
        scene& target,
        entity result_id,
        entity original_id
    ) override;

    template<typename... Args>
    entity find_entity(Args&&... args) const;

#ifdef MONKERO_CONTAINER_DEBUG_UTILS
    bool test_invariant() const;
    void print_bitmask() const;
    void print_jump_table() const;
#endif

private:
    T* get_unsafe(entity e);
    void destroy();
    void jump_table_insert(entity id);
    void jump_table_erase(entity id);
    std::size_t get_top_bitmask_size() const;
    bool bitmask_empty(std::uint32_t bucket_index) const;
    void bitmask_insert(entity id);
    // Returns a hint to whether the whole bucket should be removed or not.
    bool bitmask_erase(entity id);

    template<typename... Args>
    void bucket_insert(entity id, Args&&... args);
    void bucket_erase(entity id, bool signal);
    void bucket_self_erase(std::uint32_t bucket_index);
    void try_jump_table_bucket_erase(std::uint32_t bucket_index);
    void ensure_bucket_space(entity id);
    void ensure_bitmask(std::uint32_t bucket_index);
    void ensure_jump_table(std::uint32_t bucket_index);
    bool batch_change(entity id);
    entity find_previous_entity(entity id);
    void signal_add(entity id, T* data);
    void signal_remove(entity id, T* data);
    static unsigned bitscan_reverse(std::uint64_t mt);
    static bool find_bitmask_top(
        bitmask_type* bitmask,
        std::uint32_t count,
        std::uint32_t& top_index
    );
    static bool find_bitmask_previous_index(
        bitmask_type* bitmask,
        std::uint32_t index,
        std::uint32_t& prev_index
    );

    struct alignas(T) t_mimicker { std::uint8_t pad[sizeof(T)]; };

    // Bucket data
    std::uint32_t entity_count;
    std::uint32_t bucket_count;
    bitmask_type** bucket_bitmask;
    bitmask_type* top_bitmask;
    entity** bucket_jump_table;
    T** bucket_components;

    // Batching data
    bool batching;
    std::uint32_t batch_checklist_size;
    std::uint32_t batch_checklist_capacity;
    entity* batch_checklist;
    bitmask_type** bucket_batch_bitmask;

    // Search index (kinda separate, but handy to keep around here.)
    scene* ctx;
    search_index<T> search;
};

}

#endif
