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
#ifndef MONKERO_EVENT_HH
#define MONKERO_EVENT_HH
#include "entity.hh"

namespace monkero
{

class scene;

/** A built-in event emitted when a component is added to the ECS. */
template<typename Component>
struct add_component
{
    entity id; /**< The entity that got the component */
    Component* data; /**< A pointer to the component */
};

/** A built-in event emitted when a component is removed from the ECS.
 * The destructor of class scene will emit remove_component events for all
 * components still left at that point.
 */
template<typename Component>
struct remove_component
{
    entity id; /**< The entity that lost the component */
    Component* data; /**< A pointer to the component (it's not destroyed quite yet) */
};

/** This class is used to receive events of the specified type(s).
 * Once it is destructed, no events will be delivered to the associated
 * callback function anymore.
 * \note Due to the callback nature, event subscriptions are immovable.
 * \see receiver
 */
class event_subscription
{
friend class scene;
public:
    inline event_subscription(scene* ctx = nullptr, std::size_t subscription_id = 0);
    inline explicit event_subscription(event_subscription&& other);
    event_subscription(const event_subscription& other) = delete;
    inline ~event_subscription();

    event_subscription& operator=(event_subscription&& other) = delete;
    event_subscription& operator=(const event_subscription& other) = delete;

private:
    scene* ctx;
    std::size_t subscription_id;
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
    virtual void handle(scene& ctx, const EventType& event) = 0;
};

/** Deriving from this class allows systems to receive events of the specified
 * type(s). Just give a list of the desired event types in the template
 * parameters and the system will start receiving them from all other systems
 * automatically.
 */
template<typename... ReceiveEvents>
class receiver: public event_receiver<ReceiveEvents>...
{
friend class scene;
private:
    event_subscription sub;
};

}

#endif
