/*
The MIT License (MIT)

Copyright (c) 2020, 2021 Julius Ikkala

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
/** \mainpage MonkeroECS
 *
 * \section intro_sec Introduction
 *
 * MonkeroECS is a fairly small, header-only Entity Component System rescued
 * from a game engine. It aims for simple and terse usage and also contains an
 * event system.
 *
 * MonkeroECS is written in C++17 and the only dependency is the standard
 * library. Note that the code isn't pretty and has to do some pretty gnarly
 * trickery to make the usable as terse as it is.
 *
 * While performance is one of the goals of this ECS, it was more written with
 * flexibility in mind. It can iterate over large numbers of entities quickly
 * (this case was optimized for), but one-off lookups of individual components
 * is logarithmic. On the flipside, there is no maximum limit to the number of
 * entities other than the entity ID being 32-bit (just change it to 64-bit if
 * you are a madman and need more, it should work but consumes more memory).
 *
 * Adding the ECS to your project is simple; just copy the monkeroecs.hh yo
 * your codebase and include it!
 *
 * The name is a reference to the game the ECS was originally created for, but
 * it was unfortunately never finished or released despite the engine being in a
 * finished state.
 */
#ifndef MONKERO_ECS_HH
#define MONKERO_ECS_HH
#include <functional>
#include <unordered_map>
#include <map>
#include <algorithm>
#include <vector>
#include <memory>
#include <tuple>
#include <limits>
#include <any>

/** This namespace contains all of MonkeroECS. */
namespace monkero
{

/** The entity type, it's just an ID.
 * An entity alone will not take up memory in the ECS, only once components are
 * added does the entity truly use memory. You can change this to uint64_t if
 * you truly need over 4 billion entities and have tons of memory.
 */
using entity = uint32_t;
inline constexpr entity INVALID_ENTITY = std::numeric_limits<entity>::max();

/** A base class for components which need to have unchanging memory addresses.
 * Derive from this if you want to have an extra layer of indirection in the
 * storage of this component. What this allows is no move or copy constructor
 * calls and faster add & remove at the cost of iteration speed. Additionally,
 * the pointer to the component will not become outdated until that specific
 * component is removed.
 */
struct ptr_component {};
class ecs;

/** A built-in event emitted when a component is added to the ECS. */
template<typename Component>
struct add_component
{
    entity id; /**< The entity that got the component */
    Component* data; /**< A pointer to the component */
};

/** A built-in event emitted when a component is removed from the ECS.
 * The destructor of class ecs will emit remove_component events for all
 * components still left at that point.
 */
template<typename Component>
struct remove_component
{
    entity id; /**< The entity that lost the component */
    Component* data; /**< A pointer to the component (it's not destroyed yet) */
};

/** This class is used to receive events of the specified type(s).
 * Once it is destructed, no events will be delivered to the associated
 * callback function anymore.
 * \note Due to the callback nature, event subscriptions are immovable.
 * \see receiver
 */
class event_subscription
{
friend class ecs;
public:
    inline event_subscription(ecs* ctx = nullptr, size_t subscription_id = 0);
    inline explicit event_subscription(event_subscription&& other);
    event_subscription(const event_subscription& other) = delete;
    inline ~event_subscription();

    event_subscription& operator=(event_subscription&& other) = delete;
    event_subscription& operator=(const event_subscription& other) = delete;

private:
    ecs* ctx;
    size_t subscription_id;
};

// Provides event receiving facilities for one type alone. Derive
// from class receiver instead in your own code.
template<typename EventType>
class event_receiver
{
public:
    virtual ~event_receiver() = default;

    /** Receivers must implement this for each received event type.
     * It's called by emitters when an event of EventType is emitted.
     * \param ctx The ECS this receiver is part of.
     * \param event The event that occurred.
     */
    virtual void handle(ecs& ctx, const EventType& event) = 0;
};

/** Deriving from this class allows systems to receive events of the specified
 * type(s). Just give a list of the desired event types in the template
 * parameters and the system will start receiving them from all other systems
 * automatically.
 */
template<typename... ReceiveEvents>
class receiver: public event_receiver<ReceiveEvents>...
{
friend class ecs;
private:
    event_subscription sub;
};

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
     * \warn Don't save a pointer to the data unless the component derives from
     *  ptr_component, otherwise the address can change without notification to
     *  you.
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
    void update(ecs& e);

    // Don't copy this one though, or else you won't get some add_entity or
    // remove_entity calls.
    using empty_default_impl = void;
};

/** The primary class of the ECS.
 * Entities are created by it, components are attached throught it and events
 * are routed through it.
 */
class ecs
{
friend class event_subscription;
public:
    /** The constructor. */
    inline ecs();
    /** The destructor.
     * It ensures that all remove_component events are sent for the remainder
     * of the components before event handlers are cleared.
     */
    inline ~ecs();

    /** Calls a given function for all suitable entities.
     * The parameters of the function mandate how it is called. Batching is
     * enabled automatically so that removing and adding entities and components
     * during iteration is safe.
     * \param f The iteration callback.
     *   The first parameter of \p f must be #entity (the entity that the
     *   iteration concerns.) After that, further parameters must be either
     *   references or pointers to components. The function is only called when
     *   all referenced components are present for the entity; pointer
     *   parameters are optional and can be null if the component is not
     *   present.
     */
    template<typename F>
    inline void foreach(F&& f);

    /** Same as foreach(), just syntactic sugar.
     * \see foreach()
     */
    template<typename F>
    inline void operator()(F&& f);

    /** Reserves space for components.
     * Using this function isn't mandatory, but it can be used to improve
     * performance when about to add a known number of components.
     * \tparam Component The type of component to reserve memory for.
     * \param count The number of components to reserve memory for.
     */
    template<typename Component>
    void reserve(size_t count);

    /** Adds an entity without components.
     * No memory is reserved, as the operation literally just increments a
     * counter.
     * \return The new entity ID.
     */
    inline entity add();

    /** Adds an entity with initial components.
     * Takes a list of component instances. Note that they must be moved in, so
     * you should create them during the call or use std::move().
     * \param components All components that should be included.
     * \return The new entity ID.
     */
    template<typename... Components>
    entity add(Components&&... components);

    /** Adds components to an existing entity.
     * Takes a list of component instances. Note that they must be moved in, so
     * you should create them during the call or use std::move().
     * \param id The entity that components are added to.
     * \param components All components that should be attached.
     */
    template<typename... Components>
    void attach(entity id, Components&&... components);

    /** Removes all components related to the entity.
     * Unlike the component-specific remove() call, this also releases the ID to
     * be reused by another entity.
     * \param id The entity whose components to remove.
     */
    inline void remove(entity id);

    /** Removes a component of an entity.
     * \tparam Component The type of component to remove from the entity.
     * \param id The entity whose component to remove.
     */
    template<typename Component>
    void remove(entity id);

    /** Removes all components of all entities.
     * It also resets the entity counter, so this truly invalidates all
     * previous entities!
     */
    inline void clear_entities();

    /** Copies entities from another ECS to this one.
     * \param other the other ECS whose entities and components to copy to this.
     * \param translation_table if not nullptr, will be filled in with the
     * entity ID correspondence from the old ECS to the new.
     * \warn event handlers are not copied, only entities and components. Entity
     * IDs will also change.
     * \warn You should finish batching on the other ECS before calling this.
     */
    inline void concat(
        ecs& other,
        std::map<entity, entity>* translation_table = nullptr
    );

    /** Copies one entity from another ECS to this one.
     * \param other the other ECS whose entity to copy to this.
     * \param other_id the ID of the entity to copy in the other ECS.
     * \return entity ID of the created entity.
     * \warn You should finish batching on the other ECS before calling this.
     */
    inline entity copy(ecs& other, entity other_id);

    /** Starts batching behaviour for add/remove.
     * If you know you are going to do a lot of modifications to existing
     * entities (i.e. attaching new components to old entities or removing
     * components in any case), you can call start_batch() before that and
     * finish_batch() after to gain a lot of performance. If there's only a
     * couple of modifications, don't bother. Also, if you are within a foreach
     * loop, batching will already be applied.
     */
    inline void start_batch();

    /** Finishes batching behaviour for add/remove and applies the changes.
     * Some batched changes take place immediately, but many are not. After
     * calling finish_batch(), all functions act like you would expect.
     */
    inline void finish_batch();

    /** Counts instances of entities with a specified component.
     * \tparam Component the component type to count instances of.
     * \return The number of entities with the specified component.
     * \note This count is only valid when not batching. It is stuck to the
     * value before batching started.
     */
    template<typename Component>
    size_t count() const;

    /** Checks if an entity has the given component.
     * \tparam Component the component type to check.
     * \param id The id of the entity whose component is checked.
     * \return true if the entity has the given component, false otherwise.
     */
    template<typename Component>
    bool has(entity id) const;

    /** Returns the desired component of an entity.
     * Const version.
     * \tparam Component the component type to get.
     * \param id The id of the entity whose component to fetch.
     * \return A pointer to the component if present, null otherwise.
     */
    template<typename Component>
    const Component* get(entity id) const;

    /** Returns the desired component of an entity.
     * \tparam Component the component type to get.
     * \param id The id of the entity whose component to fetch.
     * \return A pointer to the component if present, null otherwise.
     */
    template<typename Component>
    Component* get(entity id);

    /** Returns the Nth entity of those that have a given component.
     * This is primarily useful for picking an arbitrary entity out of many,
     * like picking a random entity etc.
     * \tparam Component The component type whose entity list is used.
     * \param index The index of the entity id to return.
     * \return The Nth entity id with \p Component.
     * \warning There is no bounds checking. Use count() to be safe.
     * \note This function is only valid when not batching, but it is safe to
     * use with count() during batching as well. You may encounter entities that
     * are pending for removal and will not find entities whose addition is
     * pending.
     */
    template<typename Component>
    entity get_entity(size_t index) const;

    /** Uses search_index<Component> to find the desired component.
     * \tparam Component the component type to search for.
     * \tparam Args search argument types.
     * \param args arguments for search_index<Component>::find().
     * \return A pointer to the component if present, null otherwise.
     * \see update_search_index()
     */
    template<typename Component, typename... Args>
    Component* find_component(Args&&... args);

    /** Uses search_index<Component> to find the desired component.
     * Const version.
     * \tparam Component the component type to search for.
     * \tparam Args search argument types.
     * \param args arguments for search_index<Component>::find().
     * \return A pointer to the component if present, null otherwise.
     * \see update_search_index()
     */
    template<typename Component, typename... Args>
    const Component* find_component(Args&&... args) const;

    /** Uses search_index<Component> to find the desired entity.
     * \tparam Component the component type to search for.
     * \tparam Args search argument types.
     * \param args arguments for search_index<Component>::find().
     * \return An entity id if found, INVALID_ENTITY otherwise.
     * \see update_search_index()
     */
    template<typename Component, typename... Args>
    entity find(Args&&... args) const;

    /** Calls search_index<Component>::update() for the component type.
     * \tparam Component the component type whose search index to update.
     */
    template<typename Component>
    void update_search_index();

    /** Updates search indices of all component types.
     */
    inline void update_search_indices();

    /** Calls all handlers of the given event type.
     * \tparam EventType the type of the event to emit.
     * \param event The event to emit.
     */
    template<typename EventType>
    void emit(const EventType& event);

    /** Returns how many handlers are present for the given event type.
     * \tparam EventType the type of the event to check.
     * \return the number of event handlers for this EventType.
     */
    template<typename EventType>
    size_t get_handler_count() const;

    /** Adds event handler(s) to the ECS.
     * \tparam F Callable types, with signature void(ecs& ctx, const EventType& e).
     * \param callbacks The event handler callbacks.
     * \return ID of the "subscription"
     * \see subscribe() for RAII handler lifetime.
     * \see bind_event_handler() for binding to member functions.
     */
    template<typename... F>
    size_t add_event_handler(F&&... callbacks);

    /** Adds member functions of an object as event handler(s) to the ECS.
     * \tparam T Type of the object whose members are being bound.
     * \tparam F Member function types, with signature
     * void(ecs& ctx, const EventType& e).
     * \param userdata The class to bind to each callback.
     * \param callbacks The event handler callbacks.
     * \return ID of the "subscription"
     * \see subscribe() for RAII handler lifetime.
     * \see add_event_handler() for free-standing functions.
     */
    template<class T, typename... F>
    size_t bind_event_handler(T* userdata, F&&... callbacks);

    /** Removes event handler(s) from the ECS
     * \param id ID of the "subscription"
     * \see subscribe() for RAII handler lifetime.
     */
    inline void remove_event_handler(size_t id);

    /** Adds event handlers for a receiver object.
     * \tparam EventTypes Event types that are being received.
     * \param r The receiver to add handlers for.
     */
    template<typename... EventTypes>
    void add_receiver(receiver<EventTypes...>& r);

    /** Adds event handlers with a subscription object that tracks lifetime.
     * \tparam F Callable types, with signature void(ecs& ctx, const EventType& e).
     * \param callbacks The event handler callbacks.
     * \return The subscription object that removes the event handler on its
     * destruction.
     */
    template<typename... F>
    event_subscription subscribe(F&&... callbacks);

private:
    class component_container_base
    {
    public:
        virtual ~component_container_base() = default;

        inline virtual void resolve_pending() = 0;
        inline virtual void remove(ecs& ctx, entity id) = 0;
        inline virtual void clear(ecs& ctx) = 0;
        inline virtual size_t count() const = 0;
        inline virtual void update_search_index(ecs& ctx) = 0;
        inline virtual void list_entities(
            std::map<entity, entity>& translation_table
        ) = 0;
        inline virtual void concat(
            ecs& ctx,
            const std::map<entity, entity>& translation_table
        ) = 0;
        inline virtual void copy(
            ecs& target,
            entity result_id,
            entity original_id
        ) = 0;
    };

    template<typename Component>
    struct foreach_iterator_base;

    template<typename Component>
    struct foreach_iterator;

    template<typename Component>
    class component_container: public component_container_base
    {
        template<typename> friend struct foreach_iterator_base;
        template<typename> friend struct foreach_iterator;
    public:
        struct component_tag
        {
            component_tag();
            component_tag(Component&& c);

            Component* get();
        };

        struct component_payload
        {
            component_payload();
            component_payload(Component&& c);
            Component c;

            Component* get();
        };

        struct component_indirect
        {
            component_indirect();
            component_indirect(Component* c);
            std::unique_ptr<Component> c;

            Component* get();
        };

        static constexpr bool use_indirect =
            std::is_base_of_v<ptr_component, Component> ||
            std::is_polymorphic_v<Component>;

        static constexpr bool use_empty =
            std::is_empty_v<Component> && !use_indirect;

        struct dummy_container
        {
            void resize(size_t size);
            void reserve(size_t size);
            void clear();
            component_tag* begin();
            void erase(component_tag*);
            component_tag& emplace_back(component_tag&&);
            component_tag* emplace(component_tag*, component_tag&&);
            component_tag& operator[](size_t i) const;
        };

        using component_data = std::conditional_t<
            use_indirect, component_indirect,
            std::conditional_t<
                use_empty,
                component_tag,
                component_payload
            >
        >;

        using component_storage = std::conditional_t<
            use_empty, dummy_container, std::vector<component_data>
        >;

        Component* get(entity id);
        entity get_entity(size_t index) const;
        template<typename... Args>
        entity find_entity(Args&&... args) const;

        void native_add(
            ecs& ctx,
            entity id,
            std::conditional_t<use_indirect, Component*, Component&&> c
        );
        void add(ecs& ctx, entity id, Component* c);
        void add(ecs& ctx, entity id, Component&& c);
        void add(ecs& ctx, entity id, const Component& c);
        void remove(ecs& ctx, entity id) override;
        void reserve(size_t count);
        void resolve_pending() override;
        void clear(ecs& ctx) override;
        size_t count() const override;
        void update_search_index(ecs& ctx) override;

        void list_entities(
            std::map<entity, entity>& translation_table
        ) override;
        void concat(
            ecs& ctx,
            const std::map<entity, entity>& translation_table
        ) override;
        void copy(
            ecs& target,
            entity result_id,
            entity original_id
        ) override;

    private:
        void signal_add(ecs& ctx, entity id, Component* data);
        void signal_remove(ecs& ctx, entity id, Component* data);

        search_index<Component> search;
        std::vector<entity> ids;
        component_storage components;
        std::vector<entity> pending_removal_ids;
        std::vector<entity> pending_addition_ids;
        component_storage pending_addition_components;
    };

    template<typename Component>
    struct foreach_iterator_base
    {
        using Type = std::decay_t<Component>;
        using Container = component_container<Type>;

        foreach_iterator_base(Container& c);

        bool finished();
        inline void advance_up_to(entity id);
        entity get_id() const;

        Container* c;
        size_t i;
    };

    template<typename Component>
    struct foreach_iterator<Component&>: foreach_iterator_base<Component>
    {
        static constexpr bool required = true;
        using foreach_iterator_base<Component>::foreach_iterator_base;
        using Type = typename foreach_iterator_base<Component>::Type;

        Type& get(entity id);
    };

    template<typename Component>
    struct foreach_iterator<Component*>: foreach_iterator_base<Component>
    {
        static constexpr bool required = false;
        using foreach_iterator_base<Component>::foreach_iterator_base;
        using Type = typename foreach_iterator_base<Component>::Type;

        Type* get(entity id);
    };

    template<bool pass_id, typename... Components>
    struct foreach_impl
    {
        template<typename F>
        static inline void call(ecs& ctx, F&& f);
    };

    template<typename... Components>
    foreach_impl<true, Components...>
    foreach_redirector(const std::function<void(entity id, Components...)>&);

    template<typename... Components>
    foreach_impl<false, Components...>
    foreach_redirector(const std::function<void(Components...)>&);

    template<typename T>
    T event_handler_type_detector(const std::function<void(ecs&, const T&)>&);

    template<typename T>
    T event_handler_type_detector(void (*)(ecs&, const T&));

    template<typename T, typename U>
    U event_handler_type_detector(void (T::*)(ecs&, const U&));

    template<typename Component>
    void try_attach_dependencies(entity id);

    template<typename Component>
    component_container<Component>& get_container() const;

    inline void resolve_pending();

    template<typename Component>
    static size_t get_component_type_key();
    inline static size_t component_type_key_counter = 0;

    template<typename Event>
    static size_t get_event_type_key();
    inline static size_t event_type_key_counter = 0;

    template<typename F>
    void internal_add_handler(size_t id, F&& f);

    template<class C, typename F>
    void internal_bind_handler(size_t id, C* c, F&& f);

    entity id_counter;
    std::vector<entity> reusable_ids;
    size_t subscriber_counter;
    int defer_batch;
    mutable std::vector<std::unique_ptr<component_container_base>> components;

    struct event_handler
    {
        size_t subscription_id;

        // TODO: Once we have std::function_ref, see if this can be optimized.
        // The main difficulty we have to handle are pointers to member
        // functions, which have been made unnecessarily unusable in C++.
        std::function<void(ecs& ctx, const void* event)> callback;
    };
    std::vector<std::vector<event_handler>> event_handlers;
};

/** Components may derive from this class to require other components.
 * The other components are added to the entity along with this one if they
 * are not yet present.
 */
template<typename... DependencyComponents>
class dependency_components
{
friend class ecs;
public:
    static void ensure_dependency_components_exist(entity id, ecs& ctx);
};

//==============================================================================
// Implementation
//==============================================================================

inline size_t lower_bound(std::vector<entity>& ids, entity id)
{
    // TODO: More optimal implementation than binary search is possible! Our
    // list has the further limitation of no duplications. Also, should use SSE.
    return std::lower_bound(ids.begin(), ids.end(), id)-ids.begin();
}

ecs::ecs()
: id_counter(0), defer_batch(0)
{
    components.reserve(64);
}

ecs::~ecs()
{
    // This is called manually so that remove events are fired if necessary.
    clear_entities();
}

template<typename Component>
ecs::foreach_iterator_base<Component>::foreach_iterator_base(Container& c)
: c(&c), i(0)
{
}

template<typename Component>
bool ecs::foreach_iterator_base<Component>::finished()
{
    return i == c->ids.size();
}

template<typename Component>
void ecs::foreach_iterator_base<Component>::advance_up_to(entity id)
{
    size_t last = i + std::min(entity(c->ids.size() - i), (id - c->ids[i]));
    i = std::lower_bound(c->ids.begin()+i+1, c->ids.begin()+last, id)-c->ids.begin();
}

template<typename Component>
entity ecs::foreach_iterator_base<Component>::get_id() const
{
    return c->ids[i];
}

template<typename Component>
auto ecs::foreach_iterator<Component&>::get(entity) -> Type&
{
    return *this->c->components[this->i].get();
}

template<typename Component>
auto ecs::foreach_iterator<Component*>::get(entity id) -> Type*
{
    if(this->finished())
        return nullptr;

    if(this->c->ids[this->i] != id) return nullptr;
    return this->c->components[this->i].get();
}

template<bool pass_id, typename... Components>
template<typename F>
void ecs::foreach_impl<pass_id, Components...>::call(ecs& ctx, F&& f)
{
    ctx.start_batch();

    std::tuple<foreach_iterator<Components>...>
        component_it(ctx.get_container<
            std::decay_t<std::remove_pointer_t<std::decay_t<Components>>>
        >()...);
#define monkero_apply_tuple(...) \
    std::apply([&](auto&... it){return (__VA_ARGS__);}, component_it)

    // Note that all checks based on it.required are compile-time, it's
    // constexpr!
    constexpr bool all_optional = monkero_apply_tuple(!it.required && ...);

    if constexpr(sizeof...(Components) == 1)
    {
        // If we're only iterating one category, we can do it very quickly!
        auto& iter = std::get<0>(component_it);
        while(!iter.finished())
        {
            entity cur_id = iter.get_id();
            if constexpr(pass_id) f(cur_id, iter.get(cur_id));
            else f(iter.get(cur_id));
            ++iter.i;
        }
    }
    else if constexpr(all_optional)
    {
        // If all are optional, iteration logic has to differ a bit. The other
        // version would never quit as there would be zero finished required
        // iterators.
        while(monkero_apply_tuple(!it.finished() || ...))
        {
            entity cur_id = monkero_apply_tuple(std::min({
                (it.finished() ?
                 std::numeric_limits<entity>::max() : it.get_id())...
            }));
            if constexpr(pass_id) monkero_apply_tuple(f(cur_id, it.get(cur_id)...));
            else monkero_apply_tuple(f(it.get(cur_id)...));
            monkero_apply_tuple(
                (!it.finished() && it.get_id() == cur_id
                 ? (++it.i, void()) : void()), ...
            );
        }
    }
    else
    {
        // This is the generic implementation for when there's multiple
        // components where some are potentially optional.
        while(monkero_apply_tuple((!it.finished() || !it.required) && ...))
        {
            entity cur_id = monkero_apply_tuple(
                std::max({(it.required ? it.get_id() : 0)...})
            );
            // Check if all entries have the same id. For each entry that
            // doesn't, advance to the next id.
            bool all_required_equal = monkero_apply_tuple(
                (it.required ?
                    (it.get_id() == cur_id ?
                        true : (it.advance_up_to(cur_id), false)) :
                    (it.finished() || it.get_id() >= cur_id ?
                        true : (it.advance_up_to(cur_id), true))) && ...
            );
            if(all_required_equal)
            {
                if constexpr(pass_id)
                    monkero_apply_tuple(f(cur_id, it.get(cur_id)...));
                else monkero_apply_tuple(f(it.get(cur_id)...));
                monkero_apply_tuple((it.required ? ++it.i, void(): void()), ...);
            }
        }
    }
#undef monkero_apply_tuple

    ctx.finish_batch();
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
    )::call(*this, std::forward<F>(f));
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

template<typename... Components>
void ecs::attach(entity id, Components&&... components)
{
    (try_attach_dependencies<Components>(id), ...);

    (
        get_container<Components>().add(
            *this, id, std::forward<Components>(components)
        ), ...
    );
}

void ecs::start_batch()
{
    ++defer_batch;
}

void ecs::finish_batch()
{
    if(defer_batch > 0)
    {
        --defer_batch;
        if(defer_batch == 0)
            resolve_pending();
    }
}

template<typename Component>
size_t ecs::count() const
{
    return get_container<Component>().count();
}

template<typename Component>
bool ecs::has(entity id) const
{
    return get<Component>(id) != nullptr;
}

template<typename Component>
const Component* ecs::get(entity id) const
{
    return get_container<Component>().get(id);
}

template<typename Component>
Component* ecs::get(entity id)
{
    return get_container<Component>().get(id);
}

template<typename Component>
entity ecs::get_entity(size_t index) const
{
    return get_container<Component>().get_entity(index);
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
    return get_container<Component>().update_search_index(*this);
}

void ecs::update_search_indices()
{
    for(auto& c: components)
        if(c) c->update_search_index(*this);
}

template<typename T, typename=void>
struct is_receiver: std::false_type { };

template<typename T>
struct is_receiver<
    T,
    decltype((void)
        std::declval<ecs>().add_receiver(*(T*)nullptr), void()
    )
> : std::true_type { };

void ecs::remove(entity id)
{
    for(auto& c: components)
        if(c) c->remove(*this, id);
    reusable_ids.push_back(id);
}

template<typename Component>
void ecs::remove(entity id)
{
    get_container<Component>().remove(*this, id);
}

void ecs::clear_entities()
{
    for(auto& c: components)
        if(c) c->clear(*this);
    id_counter = 0;
    reusable_ids.clear();
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


template<typename Component>
void ecs::reserve(size_t count)
{
    get_container<Component>().reserve(count);
}

template<typename Component>
ecs::component_container<Component>::component_tag::component_tag()
{
}

template<typename Component>
ecs::component_container<Component>::component_tag::component_tag(Component&&)
{
}

template<typename Component>
Component* ecs::component_container<Component>::component_tag::get()
{
    return reinterpret_cast<Component*>(this);
}

template<typename Component>
ecs::component_container<Component>::component_payload::component_payload()
{
}

template<typename Component>
ecs::component_container<Component>::component_payload::component_payload(
    Component&& c
): c(std::move(c))
{
}

template<typename Component>
Component* ecs::component_container<Component>::component_payload::get()
{
    return &c;
}

template<typename Component>
ecs::component_container<Component>::component_indirect::component_indirect()
{
}

template<typename Component>
ecs::component_container<Component>::component_indirect::component_indirect(
    Component* c
): c(c)
{
}

template<typename Component>
Component* ecs::component_container<Component>::component_indirect::get()
{
    return c.get();
}

template<typename Component>
typename ecs::component_container<Component>::component_tag&
ecs::component_container<Component>::dummy_container::operator[](size_t) const
{
    return *(component_tag*)this;
}

template<typename Component>
void ecs::component_container<Component>::dummy_container::resize(size_t)
{}

template<typename Component>
void ecs::component_container<Component>::dummy_container::reserve(size_t)
{}

template<typename Component>
void ecs::component_container<Component>::dummy_container::clear()
{}

template<typename Component>
typename ecs::component_container<Component>::component_tag*
ecs::component_container<Component>::dummy_container::begin()
{
    return (component_tag*)this;
}

template<typename Component>
void ecs::component_container<Component>::dummy_container::erase(component_tag*)
{}

template<typename Component>
typename ecs::component_container<Component>::component_tag&
ecs::component_container<Component>::dummy_container::emplace_back(component_tag&&)
{
    return operator[](0);
}

template<typename Component>
typename ecs::component_container<Component>::component_tag*
ecs::component_container<Component>::dummy_container::emplace(component_tag*, component_tag&&)
{
    return begin();
}

template<typename Component>
Component* ecs::component_container<Component>::get(entity id)
{
    // Check if this entity is pending for removal. If so, it doesn't really
    // exist anymore.
    size_t i = lower_bound(pending_removal_ids, id);
    if(i != pending_removal_ids.size() && pending_removal_ids[i] == id)
        return nullptr;

    // Check if pending_addition has it.
    i = lower_bound(pending_addition_ids, id);
    if(i != pending_addition_ids.size() && pending_addition_ids[i] == id)
        return pending_addition_components[i].get();

    // Finally, check the big components vector has it.
    i = lower_bound(ids, id);
    if(i != ids.size() && ids[i] == id)
        return components[i].get();

    return nullptr;
}

template<typename Component>
entity ecs::component_container<Component>::get_entity(size_t index) const
{
    return ids[index];
}

template<typename Component>
template<typename... Args>
entity ecs::component_container<Component>::find_entity(Args&&... args) const
{
    return search.find(std::forward<Args>(args)...);
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
void ecs::component_container<Component>::native_add(
    ecs& ctx,
    entity id,
    std::conditional_t<use_indirect, Component*, Component&&> c
){
    if(ctx.defer_batch)
    {
        // Check if this entity is already pending for removal. Remove from
        // that vector first if so.
        size_t i = lower_bound(pending_removal_ids, id);
        if(i != pending_removal_ids.size() && pending_removal_ids[i] == id)
            pending_removal_ids.erase(pending_removal_ids.begin() + i);

        // Then, add to pending_addition too, if not there yet.
        i = lower_bound(pending_addition_ids, id);
        if(i == pending_addition_ids.size() || pending_addition_ids[i] != id)
        {
            // Skip the search if nobody cares.
            if(
                ctx.get_handler_count<remove_component<Component>>() ||
                !search_index_is_empty_default<decltype(search)>()
            ){
                // If this entity already exists in the components, signal the
                // removal of the previous one.
                size_t j = lower_bound(ids, id);
                if(j != ids.size() && ids[j] == id)
                    signal_remove(ctx, id, components[j].get());
            }

            pending_addition_ids.emplace(pending_addition_ids.begin()+i, id);
            auto cit = pending_addition_components.emplace(
                pending_addition_components.begin()+i,
                std::move(c)
            );
            signal_add(ctx, id, cit->get());
        }
        else
        {
            signal_remove(ctx, id, pending_addition_components[i].get());
            pending_addition_components[i] = component_data(std::move(c));
            signal_add(ctx, id, pending_addition_components[i].get());
        }
    }
    else
    {
        // If we can take the fast path of just dumping at the back, do it.
        if(ids.size() == 0 || ids.back() < id)
        {
            ids.emplace_back(id);
            auto& component = components.emplace_back(std::move(c));
            signal_add(ctx, id, component.get());
        }
        else
        {
            size_t i = lower_bound(ids, id);
            if(ids[i] != id)
            {
                ids.emplace(ids.begin() + i, id);
                auto cit = components.emplace(components.begin()+i, std::move(c));
                signal_add(
                    ctx, id, cit->get()
                );
            }
            else
            {
                signal_remove(ctx, id, components[i].get());
                components[i] = component_data(std::move(c));
                signal_add(ctx, id, components[i].get());
            }
        }
    }
}

template<typename Component>
void ecs::component_container<Component>::add(
    ecs& ctx, entity id, Component* c
){
    if constexpr(use_indirect)
        native_add(ctx, id, c);
    else
    {
        native_add(ctx, id, std::move(*c));
        delete c;
    }
}

template<typename Component>
void ecs::component_container<Component>::add(
    ecs& ctx, entity id, Component&& c
){
    if constexpr(use_indirect)
        native_add(ctx, id, new Component(std::move(c)));
    else
        native_add(ctx, id, std::move(c));
}

template<typename Component>
void ecs::component_container<Component>::add(
    ecs& ctx, entity id, const Component& c
){
    if constexpr(use_indirect)
        native_add(ctx, id, new Component(c));
    else
        native_add(ctx, id, Component(c));
}

template<typename Component>
void ecs::component_container<Component>::reserve(size_t count)
{
    ids.reserve(count);
    components.reserve(count);
    pending_removal_ids.reserve(count);
    pending_addition_ids.reserve(count);
    pending_addition_components.reserve(count);
}

template<typename Component>
void ecs::component_container<Component>::remove(ecs& ctx, entity id)
{
    bool do_emit = ctx.get_handler_count<remove_component<Component>>() ||
        !search_index_is_empty_default<decltype(search)>();

    if(ctx.defer_batch)
    {
        // Check if this entity is already pending for addition. Remove from
        // there first if so.
        size_t i = lower_bound(pending_addition_ids, id);
        if(i != pending_addition_ids.size() && pending_addition_ids[i] == id)
        {
            if(do_emit)
            {
                auto tmp = std::move(pending_addition_components[i]);
                pending_addition_ids.erase(pending_addition_ids.begin() + i);
                pending_addition_components.erase(pending_addition_components.begin() + i);
                signal_remove(ctx, id, tmp.get());
            }
            else
            {
                pending_addition_ids.erase(pending_addition_ids.begin() + i);
                pending_addition_components.erase(pending_addition_components.begin() + i);
            }
        }

        // Then, add to proper removal too, if not there yet.
        i = lower_bound(pending_removal_ids, id);
        if(i == pending_removal_ids.size() || pending_removal_ids[i] != id)
        {
            pending_removal_ids.insert(pending_removal_ids.begin() + i, id);
            if(do_emit)
            {
                size_t j = lower_bound(ids, id);
                if(j != ids.size() && ids[j] == id)
                    signal_remove(ctx, id, components[j].get());
            }
        }
    }
    else
    {
        size_t i = lower_bound(ids, id);
        if(i != ids.size() && ids[i] == id)
        {
            if(do_emit)
            {
                auto tmp = std::move(components[i]);
                ids.erase(ids.begin() + i);
                components.erase(components.begin() + i);
                signal_remove(ctx, id, tmp.get());
            }
            else
            {
                ids.erase(ids.begin() + i);
                components.erase(components.begin() + i);
            }
        }
    }
}

template<typename Component>
void ecs::component_container<Component>::resolve_pending()
{
    // Start by removing.
    if(pending_removal_ids.size() != 0)
    {
        size_t pi = 0;
        // Start iterating from the last element <= first id to be removed.
        // This could just be 0, but this should help with
        // performance in the common-ish case where most transient entities are
        // also the most recently added ones.
        size_t ri = lower_bound(ids, pending_removal_ids[pi]);
        size_t wi = ri;
        int removed_count = 0;

        while(pi != pending_removal_ids.size() && ri != ids.size())
        {
            // If this id is equal, this is the entry that should be removed.
            if(pending_removal_ids[pi] == ids[ri])
            {
                pi++;
                ri++;
                removed_count++;
            }
            // Skip all pending removals that aren't be in the list.
            else if(pending_removal_ids[pi] < ids[ri])
            {
                pi++;
            }
            else if(ids[ri] < pending_removal_ids[pi])
            {
                // Compact the vector.
                if(wi != ri)
                {
                    components[wi] = std::move(components[ri]);
                    ids[wi] = ids[ri];
                }

                // Advance to the next entry.
                ++wi;
                ++ri;
            }
        }

        if(removed_count != 0)
            while(ri != ids.size())
            {
                components[wi] = std::move(components[ri]);
                ids[wi] = ids[ri];
                ++wi;
                ++ri;
            }

        ids.resize(ids.size()-removed_count);
        components.resize(ids.size());
        pending_removal_ids.clear();
    }

    // There are two routes for addition, the fast one is for adding at the end,
    // which is the most common use case.
    if(
        pending_addition_ids.size() != 0 && (
            ids.size() == 0 || ids.back() < pending_addition_ids.front()
        )
    ){  // Fast route, only used when all additions are after the last
        // already-extant entity.
        size_t needed_size = ids.size() + pending_addition_ids.size();
        if(ids.capacity() < needed_size)
        {
            components.reserve(std::max(ids.capacity() * 2, needed_size));
            ids.reserve(std::max(ids.capacity() * 2, needed_size));
        }
        for(size_t i = 0; i < pending_addition_ids.size(); ++i)
        {
            components.emplace_back(std::move(pending_addition_components[i]));
            ids.emplace_back(pending_addition_ids[i]);
        }
        pending_addition_ids.clear();
        pending_addition_components.clear();
    }
    else if(pending_addition_ids.size() != 0)
    { // Slow route, handles duplicates and interleaved additions.
        // Handle duplicates first.
        {
            size_t pi = 0;
            size_t wi = 0;
            while(pi != pending_addition_ids.size() && wi != ids.size())
            {
                if(pending_addition_ids[pi] == ids[wi])
                {
                    components[wi] = std::move(pending_addition_components[pi]);
                    ids[wi] = pending_addition_ids[pi];
                    // TODO: We should probably avoid these erases, they're
                    // likely slow.
                    pending_addition_ids.erase(pending_addition_ids.begin() + pi);
                    pending_addition_components.erase(pending_addition_components.begin() + pi);
                    ++wi;
                }
                else if(pending_addition_ids[pi] < ids[wi]) ++pi;
                else ++wi;
            }
        }

        // If something is still left, actually perform additions.
        if(pending_addition_ids.size() != 0)
        {
            ids.resize(ids.size() + pending_addition_ids.size());
            components.resize(ids.size());

            size_t pi = 0;
            size_t wi = 0;
            size_t ri = wi+pending_addition_ids.size();

            while(pi != pending_addition_ids.size() && ri != ids.size())
            {
                size_t pir = pending_addition_ids.size()-1-pi;
                size_t rir = ids.size()-1-ri;
                size_t wir = ids.size()-1-wi;

                if(pending_addition_ids[pir] > ids[rir])
                {
                    components[wir] = std::move(pending_addition_components[pir]);
                    ids[wir] = pending_addition_ids[pir];
                    ++pi;
                }
                else
                {
                    components[wir] = std::move(components[rir]);
                    ids[wir] = ids[rir];
                    ++ri;
                }
                ++wi;
            }

            while(pi != pending_addition_ids.size())
            {
                size_t pir = pending_addition_ids.size()-1-pi;
                size_t wir = ids.size()-1-wi;
                components[wir] = std::move(pending_addition_components[pir]);
                ids[wir] = pending_addition_ids[pir];
                ++pi;
                ++wi;
            }
            pending_addition_ids.clear();
            pending_addition_components.clear();
        }
    }
}

template<typename Component>
void ecs::component_container<Component>::clear(ecs& ctx)
{
    bool do_emit = ctx.get_handler_count<remove_component<Component>>() ||
        !search_index_is_empty_default<decltype(search)>();

    if(ctx.defer_batch)
    {
        // If batching, we can't actually clear everything now. We will simply
        // have to queue everything for removal.
        while(pending_addition_ids.size() > 0)
            remove(ctx, pending_addition_ids.back());
        for(entity id: ids)
            remove(ctx, id);
    }
    else if(!do_emit)
    {
        // If we aren't going to emit anything and we don't batch, life is easy.
        ids.clear();
        components.clear();
        pending_removal_ids.clear();
        pending_addition_ids.clear();
        pending_addition_components.clear();
    }
    else
    {
        // The most difficult case, we don't batch but we still need to emit.
        auto tmp_ids(std::move(ids));
        auto tmp_components(std::move(components));
        ids.clear();
        components.clear();

        for(size_t i = 0; i < tmp_ids.size(); ++i)
            signal_remove(ctx, tmp_ids[i], tmp_components[i].get());
    }
}

template<typename Component>
size_t ecs::component_container<Component>::count() const
{
    return ids.size();
}

template<typename Component>
void ecs::component_container<Component>::update_search_index(ecs& ctx)
{
    search.update(ctx);
}

template<typename Component>
void ecs::component_container<Component>::list_entities(
    std::map<entity, entity>& translation_table
){
    for(entity id: ids)
        translation_table[id] = INVALID_ENTITY;
}

template<typename Component>
void ecs::component_container<Component>::concat(
    ecs& ctx,
    const std::map<entity, entity>& translation_table
){
    if constexpr(std::is_copy_constructible_v<Component>)
    {
        for(size_t i = 0; i < ids.size(); ++i)
            ctx.attach(translation_table.at(ids[i]), Component{*components[i].get()});
    }
}

template<typename Component>
void ecs::component_container<Component>::copy(
    ecs& target,
    entity result_id,
    entity original_id
){
    if constexpr(std::is_copy_constructible_v<Component>)
    {
        Component* comp = get(original_id);
        if(comp) target.attach(result_id, Component{*comp});
    }
}


template<typename Component>
void ecs::component_container<Component>::signal_add(ecs& ctx, entity id, Component* data)
{
    search.add_entity(id, *data);
    ctx.emit(add_component<Component>{id, data});
}

template<typename Component>
void ecs::component_container<Component>::signal_remove(ecs& ctx, entity id, Component* data)
{
    search.remove_entity(id, *data);
    ctx.emit(remove_component<Component>{id, data});
}

template<typename Component>
ecs::component_container<Component>& ecs::get_container() const
{
    size_t key = get_component_type_key<Component>();
    if(components.size() <= key) components.resize(key+1);
    auto& base_ptr = components[key];
    if(!base_ptr)
    {
        base_ptr.reset(new component_container<Component>());
    }
    return *static_cast<component_container<Component>*>(base_ptr.get());
}

void ecs::resolve_pending()
{
    for(auto& c: components)
        if(c) c->resolve_pending();
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

event_subscription::event_subscription(ecs* ctx, size_t subscription_id)
: ctx(ctx), subscription_id(subscription_id)
{
}

event_subscription::event_subscription(event_subscription&& other)
: ctx(other.ctx), subscription_id(other.subscription_id)
{
    other.ctx = nullptr;
}

event_subscription::~event_subscription()
{
    if(ctx)
        ctx->remove_event_handler(subscription_id);
}

template<typename Component>
void search_index<Component>::add_entity(entity, const Component&) {}

template<typename Component>
void search_index<Component>::update(ecs&) {}

template<typename Component>
void search_index<Component>::remove_entity(entity, const Component&) {}

}

#endif

