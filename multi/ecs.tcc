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
#ifndef MONKERO_ECS_TCC
#define MONKERO_ECS_TCC
#include "ecs.hh"
#include <limits>

namespace monkero
{

ecs::ecs()
: id_counter(1), subscriber_counter(0), defer_batch(0)
{
}

ecs::~ecs()
{
    // This is called manually so that remove events are fired if necessary.
    clear_entities();
}

template<bool pass_id, typename... Components>
template<typename Component>
struct ecs::foreach_impl<pass_id, Components...>::iterator_wrapper<Component*>
{
    static constexpr bool required = false;
    typename component_container<std::decay_t<std::remove_pointer_t<std::decay_t<Component>>>>::iterator iter;
};

template<bool pass_id, typename... Components>
template<typename F>
void ecs::foreach_impl<pass_id, Components...>::foreach(ecs& ctx, F&& f)
{
    ctx.start_batch();

    std::tuple component_it(make_iterator<Components>(ctx)...);
#define monkero_apply_tuple(...) \
    std::apply([&](auto&... it){return (__VA_ARGS__);}, component_it)

    // Note that all checks based on it.required are compile-time, it's
    // constexpr!
    constexpr bool all_optional = (std::is_pointer_v<Components> && ...);

    if constexpr(sizeof...(Components) == 1)
    {
        // If we're only iterating one category, we can do it very quickly!
        auto& it = std::get<0>(component_it).iter;
        while(it)
        {
            auto [cur_id, ptr] = *it;
            call(std::forward<F>(f), cur_id, ptr);
            ++it;
        }
    }
    else if constexpr(all_optional)
    {
        // If all are optional, iteration logic has to differ a bit. The other
        // version would never quit as there would be zero finished required
        // iterators.
        while(monkero_apply_tuple((bool)it.iter || ...))
        {
            entity cur_id = monkero_apply_tuple(std::min({
                (it.iter ? it.iter.get_id() : std::numeric_limits<entity>::max())...
            }));
            monkero_apply_tuple(call(
                std::forward<F>(f),
                cur_id,
                (it.iter.get_id() == cur_id ? (*it.iter).second : nullptr)...
            ));
            monkero_apply_tuple(
                (it.iter && it.iter.get_id() == cur_id ? (++it.iter, void()) : void()), ...
            );
        }
    }
    else
    {
        // This is the generic implementation for when there's multiple
        // components where some are potentially optional.
        std::size_t min_length = monkero_apply_tuple(std::min({
            (it.required ?
                it.iter.get_container()->size() :
                std::numeric_limits<std::size_t>::max()
            )...
        }));

        component_container_entity_advancer advancer = {};
        monkero_apply_tuple(
            (it.required && it.iter.get_container()->size() == min_length ?
                (advancer = it.iter.get_advancer(), void()): void()), ...
        );

        while(advancer.current_entity != INVALID_ENTITY)
        {
            bool have_all_required = monkero_apply_tuple(
                (it.iter.try_advance(advancer.current_entity) || !it.required) && ...
            );
            if(have_all_required)
            {
                monkero_apply_tuple(call(
                    std::forward<F>(f), advancer.current_entity,
                    (it.iter.get_id() == advancer.current_entity ? (*it.iter).second : nullptr)...
                ));
            }
            advancer.advance();
        }
    }
#undef monkero_apply_tuple

    ctx.finish_batch();
}

template<bool pass_id, typename... Components>
template<typename Component>
struct ecs::foreach_impl<pass_id, Components...>::converter<Component*>
{
    template<typename T>
    static inline T* convert(T* val) { return val; }
};

template<bool pass_id, typename... Components>
template<typename Component>
template<typename T>
T& ecs::foreach_impl<pass_id, Components...>::converter<Component>::convert(T* val)
{
    return *val;
}

template<bool pass_id, typename... Components>
template<typename F>
void ecs::foreach_impl<pass_id, Components...>::call(
    F&& f,
    entity id,
    std::decay_t<std::remove_pointer_t<std::decay_t<Components>>>*... args
){
    if constexpr(pass_id) f(id, converter<Components>::convert(args)...);
    else f(converter<Components>::convert(args)...);
}

template<typename T, typename=void>
struct has_ensure_dependency_components_exist: std::false_type { };

template<typename T>
struct has_ensure_dependency_components_exist<
    T,
    decltype((void)
        T::ensure_dependency_components_exist(entity(), *(ecs*)nullptr), void()
    )
> : std::true_type { };

template<typename Component>
void ecs::try_attach_dependencies(entity id)
{
    (void)id;
    if constexpr(has_ensure_dependency_components_exist<Component>::value)
        Component::ensure_dependency_components_exist(id, *this);
}

template<typename F>
void ecs::foreach(F&& f)
{
    // This one little trick lets us know the argument types without
    // actually using the std::function wrapper at runtime!
    decltype(
        foreach_redirector(std::function(f))
    )::foreach(*this, std::forward<F>(f));
}

template<typename F>
void ecs::operator()(F&& f)
{
    foreach(std::forward<F>(f));
}

entity ecs::add()
{
    if(reusable_ids.size() > 0)
    {
        entity id = reusable_ids.back();
        reusable_ids.pop_back();
        return id;
    }
    else
    {
        if(id_counter == INVALID_ENTITY)
            return INVALID_ENTITY;
        return id_counter++;
    }
}

template<typename... Components>
entity ecs::add(Components&&... components)
{
    entity id = add();
    attach(id, std::forward<Components>(components)...);
    return id;
}

template<typename Component, typename... Args>
void ecs::emplace(entity id, Args&&... args)
{
    try_attach_dependencies<Component>(id);

    get_container<Component>().emplace(
        id, std::forward<Args>(args)...
    );
}

template<typename... Components>
void ecs::attach(entity id, Components&&... components)
{
    (try_attach_dependencies<Components>(id), ...);

    (
        get_container<Components>().insert(
            id, std::forward<Components>(components)
        ), ...
    );
}

void ecs::remove(entity id)
{
    for(auto& c: components)
        if(c) c->erase(id);
    if(defer_batch == 0)
        reusable_ids.push_back(id);
    else
        post_batch_reusable_ids.push_back(id);
}

template<typename Component>
void ecs::remove(entity id)
{
    get_container<Component>().erase(id);
}

void ecs::clear_entities()
{
    for(auto& c: components)
        if(c) c->clear();

    if(defer_batch == 0)
    {
        id_counter = 1;
        reusable_ids.clear();
        post_batch_reusable_ids.clear();
    }
}

void ecs::concat(
    ecs& other,
    std::map<entity, entity>* translation_table_ptr
){
    std::map<entity, entity> translation_table;

    for(auto& c: other.components)
        if(c) c->list_entities(translation_table);

    start_batch();
    for(auto& pair: translation_table)
        pair.second = add();

    for(auto& c: other.components)
        if(c) c->concat(*this, translation_table);
    finish_batch();

    if(translation_table_ptr)
        *translation_table_ptr = std::move(translation_table);
}

entity ecs::copy(ecs& other, entity other_id)
{
    entity id = add();

    for(auto& c: other.components)
        if(c) c->copy(*this, id, other_id);

    return id;
}

void ecs::start_batch()
{
    ++defer_batch;
    if(defer_batch == 1)
    {
        for(auto& c: components)
            c->start_batch();
    }
}

void ecs::finish_batch()
{
    if(defer_batch > 0)
    {
        --defer_batch;
        if(defer_batch == 0)
        {
            for(auto& c: components)
                c->finish_batch();

            reusable_ids.insert(
                reusable_ids.end(),
                post_batch_reusable_ids.begin(),
                post_batch_reusable_ids.end()
            );
            post_batch_reusable_ids.clear();
        }
    }
}

template<typename Component>
size_t ecs::count() const
{
    return get_container<Component>().size();
}

template<typename Component>
bool ecs::has(entity id) const
{
    return get_container<Component>().contains(id);
}

template<typename Component>
const Component* ecs::get(entity id) const
{
    return get_container<Component>()[id];
}

template<typename Component>
Component* ecs::get(entity id)
{
    return get_container<Component>()[id];
}

template<typename Component, typename... Args>
Component* ecs::find_component(Args&&... args)
{
    return get<Component>(
        find<Component>(std::forward<Args>(args)...)
    );
}

template<typename Component, typename... Args>
const Component* ecs::find_component(Args&&... args) const
{
    return get<Component>(
        find<Component>(std::forward<Args>(args)...)
    );
}

template<typename Component, typename... Args>
entity ecs::find(Args&&... args) const
{
    return get_container<Component>().find_entity(std::forward<Args>(args)...);
}

template<typename Component>
void ecs::update_search_index()
{
    return get_container<Component>().update_search_index();
}

void ecs::update_search_indices()
{
    for(auto& c: components)
        if(c) c->update_search_index();
}

template<typename EventType>
void ecs::emit(const EventType& event)
{
    size_t key = get_event_type_key<EventType>();
    if(event_handlers.size() <= key) return;

    for(event_handler& eh: event_handlers[key])
        eh.callback(*this, &event);
}

template<typename EventType>
size_t ecs::get_handler_count() const
{
    size_t key = get_event_type_key<EventType>();
    if(event_handlers.size() <= key) return 0;
    return event_handlers[key].size();
}

template<typename... F>
size_t ecs::add_event_handler(F&&... callbacks)
{
    size_t id = subscriber_counter++;
    (internal_add_handler(id, std::forward<F>(callbacks)), ...);
    return id;
}

template<class T, typename... F>
size_t ecs::bind_event_handler(T* userdata, F&&... callbacks)
{
    size_t id = subscriber_counter++;
    (internal_bind_handler(id, userdata, std::forward<F>(callbacks)), ...);
    return id;
}

void ecs::remove_event_handler(size_t id)
{
    for(std::vector<event_handler>& type_event_handlers: event_handlers)
    {
        for(
            auto it = type_event_handlers.begin();
            it != type_event_handlers.end();
            ++it
        ){
            if(it->subscription_id == id)
            {
                type_event_handlers.erase(it);
                break;
            }
        }
    }
}

template<typename... F>
event_subscription ecs::subscribe(F&&... callbacks)
{
    return event_subscription(
        this, add_event_handler(std::forward<F>(callbacks)...)
    );
}

template<typename... EventTypes>
void ecs::add_receiver(receiver<EventTypes...>& r)
{
    r.sub.ctx = this;
    r.sub.subscription_id = bind_event_handler(
        &r, &event_receiver<EventTypes>::handle...
    );
}

template<typename Component>
component_container<Component>& ecs::get_container() const
{
    size_t key = get_component_type_key<Component>();
    if(components.size() <= key) components.resize(key+1);
    auto& base_ptr = components[key];
    if(!base_ptr)
    {
        base_ptr.reset(new component_container<Component>(*const_cast<ecs*>(this)));
        if(defer_batch > 0)
            base_ptr->start_batch();
    }
    return *static_cast<component_container<Component>*>(base_ptr.get());
}

template<typename Component>
size_t ecs::get_component_type_key()
{
    static size_t key = component_type_key_counter++;
    return key;
}

template<typename Event>
size_t ecs::get_event_type_key()
{
    static size_t key = event_type_key_counter++;
    return key;
}

template<typename F>
void ecs::internal_add_handler(size_t id, F&& f)
{
    using T = decltype(event_handler_type_detector(f));
    size_t key = get_event_type_key<T>();
    if(event_handlers.size() <= key) event_handlers.resize(key+1);

    event_handler h;
    h.subscription_id = id;
    h.callback = [f = std::forward<F>(f)](ecs& ctx, const void* ptr){
        f(ctx, *(const T*)ptr);
    };
    event_handlers[key].push_back(std::move(h));
}

template<class C, typename F>
void ecs::internal_bind_handler(size_t id, C* c, F&& f)
{
    using T = decltype(event_handler_type_detector(f));

    size_t key = get_event_type_key<T>();
    if(event_handlers.size() <= key) event_handlers.resize(key+1);

    event_handler h;
    h.subscription_id = id;
    h.callback = [c = c, f = std::forward<F>(f)](ecs& ctx, const void* ptr){
        ((*c).*f)(ctx, *(const T*)ptr);
    };
    event_handlers[key].push_back(std::move(h));
}

template<typename... DependencyComponents>
void dependency_components<DependencyComponents...>::
ensure_dependency_components_exist(entity id, ecs& ctx)
{
    ((ctx.has<DependencyComponents>(id) ? void() : ctx.attach(id, DependencyComponents())), ...);
}

}

#endif
