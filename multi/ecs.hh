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
 * flexibility in mind. Particularly, random access and multi-component
 * iteration should be quite fast. All components have stable storage, that is,
 * they will not get moved around after their creation. Therefore, the ECS also
 * works with components that are not copyable or movable and pointers to them
 * will not be invalidated until the component is removed.
 *
 * Adding the ECS to your project is simple; just copy the monkeroecs.hh yo
 * your codebase and include it!
 *
 * The name is a reference to the game the ECS was originally created for, but
 * it was unfortunately never finished or released in any form despite the
 * engine being in a finished state.
 */
#ifndef MONKERO_ECS_HH
#define MONKERO_ECS_HH
#include "container.hh"
#include "event.hh"
#include <cstdint>
#include <map>
#include <functional>
#include <memory>
#include <vector>

/** This namespace contains all of MonkeroECS. */
namespace monkero
{

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

    /** Adds an entity without components.
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

    /** Adds a component to an existing entity, building it in-place.
     * \param id The entity that components are added to.
     * \param args Parameters for the constructor of the Component type.
     */
    template<typename Component, typename... Args>
    void emplace(entity id, Args&&... args);

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
     * Batching allows you to safely add and remove components while you iterate
     * over them, but comes with no performance benefit.
     */
    inline void start_batch();

    /** Finishes batching behaviour for add/remove and applies the changes.
     */
    inline void finish_batch();

    /** Counts instances of entities with a specified component.
     * \tparam Component the component type to count instances of.
     * \return The number of entities with the specified component.
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
    template<bool pass_id, typename... Components>
    struct foreach_impl
    {
        template<typename F>
        static void foreach(ecs& ctx, F&& f);

        template<typename Component>
        struct iterator_wrapper
        {
            static constexpr bool required = true;
            typename component_container<std::decay_t<std::remove_pointer_t<std::decay_t<Component>>>>::iterator iter;
        };

        template<typename Component>
        static inline auto make_iterator(ecs& ctx)
        {
            return iterator_wrapper<Component>{
                ctx.get_container<std::decay_t<std::remove_pointer_t<std::decay_t<Component>>>>().begin()
            };
        }

        template<typename Component>
        struct converter
        {
            template<typename T>
            static inline T& convert(T*);
        };

        template<typename F>
        static void call(
            F&& f,
            entity id,
            std::decay_t<std::remove_pointer_t<std::decay_t<Components>>>*... args
        );
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
    std::vector<entity> post_batch_reusable_ids;
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

}

#include "event.tcc"
#include "search_index.tcc"
#include "container.tcc"
#include "ecs.tcc"

#endif

