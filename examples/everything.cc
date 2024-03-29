// This example just showcases all features without any reasonable logic, the
// resulting program isn't cohesive at all. Maybe useful for copypasta?
#include "ecs.hh"
#include <cstdlib>
#include <iostream>

// Any type can be used as a component (structs, classes, enums are fine!)
struct mycomponent
{
    int value =1;
};

// Tag components are empty structs used to mark states in the ECS. They're very
// compact because no memory is reserved for the component.
struct tagcomponent {};

// If your component requires the existence of another for some reason, just
// derive from dependency_components and give the list of dependency components
// as the template parameters. The dependencies are created for the entity when
// this component is added if they don't exist already.
struct dependent: public monkero::dependency_components<mycomponent, tagcomponent>
{
    int value2 = 3;
};

// This one is used as an event. There's nothing special about it either, no
// need to derive from anything.
struct myevent
{
    int thishappened;
};

// Enums are okay to use as events too.
enum yourevent
{
    COOL_EVENT,
    UNCOOL_EVENT
};

struct thirdevent {};

// Systems actually exits a bit separate from the ECS object. Like components,
// there are exactly zero requirements for them, but optional base classes
// are available.
class minimalsystem
{
public:
};

// Systems usually want to communicate with other systems. This is done through
// events. You can to derive from receiver to mark which event types your class
// deals with. You'll have to connect it to an ECS with ecs::add_receiver().
class mysystem: public monkero::receiver<myevent>
{
public:
    // Each event type listed in receiver must have a corresponding handler.
    // This is called when any system emits this event type.
    void handle(monkero::scene& ecs, const myevent& ev)
    {
        // This sends an event to all interested systems.
        // You can only emit types that you have listed in emitter.
        ecs.emit(ev.thishappened > 3 ? COOL_EVENT : UNCOOL_EVENT);
    }

    void callme(monkero::scene& ecs)
    {
        // You can iterate entities in the ECS by calling the object itself or
        // using ecs::foreach (they're equivalent). You would usually give it a
        // lambda function as a parameter. This function gets called for every
        // entity that has the specified components.
        ecs([&](monkero::entity id, tagcomponent&, mycomponent& mc){
            // The entity 'id' has both a tagcomponent and mycomponent.
            mc.value = 32;
            if(rand() % 32 == 0)
            {
                // You can safely remove entities and components while you are
                // looping over them. The destructors are called only after the
                // outermost loop has exited.
                ecs.remove(id);
            }

            if(rand() % 64 == 0)
            {
                // Adding in a loop is fine too, but the entity will not be
                // iterated over during that loop.
                ecs.add(tagcomponent{});
            }
        });

        // If you mark a parameter with a pointer instead of a reference, that
        // component becomes optional. If an optional component is missing, null
        // is given instead. If all parameters are optional, at least one of
        // them is non-null.
        ecs([&](monkero::entity id, tagcomponent&, dependent* d){
            if(d) d->value2 = 16;

            // You will commonly emit in loops like this.
            ecs.emit(thirdevent{});
        });
    }
};

// This system track the count of tagcomponents available.
class tagtracker:
    public monkero::receiver<
        monkero::add_component<tagcomponent>,
        monkero::remove_component<tagcomponent>
    >
{
public:
    void handle(monkero::scene& ecs, const monkero::add_component<tagcomponent>& ev)
    {
        // ev->id contains the id of the entity that got this component.
        // ev->data would have a pointer to the component data, but our
        // tagcomponent has nothing so it's pointless.
        tags++;
    }

    void handle(monkero::scene& ecs, const monkero::remove_component<tagcomponent>& ev)
    {
        tags--;
    }

private:
    int tags = 0;
};


int main()
{
    monkero::scene ecs;

    // Let's add a system. This one tracks a specific component type as
    // explained above. It must therefore exist before the entities so that it
    // gets all those add_component events.
    tagtracker tt;
    // To make this system receive events from our ECS, we use add_receiver().
    ecs.add_receiver(tt);

    mysystem sys;
    ecs.add_receiver(sys);

    // ecs::add() creates an entity. This operation does not reserve any memory.
    monkero::entity first = ecs.add();

    // You can add components to it with ecs::attach() at any time. Note that
    // the component must be moved in, so if you don't construct it in-place,
    // use std::move().  Multiple components can be added with one call like
    // below.
    ecs.attach(first, tagcomponent{}, mycomponent{});

    // You can also create an entity with the desired components immediately:
    monkero::entity second = ecs.add(tagcomponent{}, dependent{});

    // You can fetch a component related to an entity with ecs::get()
    mycomponent* m = ecs.get<mycomponent>(first);
    // The returned value is nullptr if that entity did not have the specified
    // component type.
    if(m) m->value = 42;

    // Let's add a bunch of entities!
    for(int i = 0; i < 1000; ++i)
    {
        monkero::entity id = ecs.add(mycomponent{});
        if(rand()%2 == 0) ecs.attach(id, tagcomponent{});
    }

    // 'count' can be used to count the number of entities with a specified
    // component. You can only count by component, there is no way to count the
    // number of entities (that operation wouldn't be very useful if you think
    // about it for a while).
    std::cout
        << ecs.count<mycomponent>() << " entities with mycomponent, "
        << ecs.count<tagcomponent>() << " entities with tagcomponent."
        << std::endl;

    // I don't want the first entity to have a tag anymore. Let's remove it!
    ecs.remove<tagcomponent>(first);
    // On a second thought, I don't want the first entity at all. This operation
    // removes _all_ components related to the entity, so it no longer takes up
    // any memory.
    ecs.remove(first);

    // Let's call our own system now.
    sys.callme(ecs);

    // You can remove all entities like this.
    ecs.clear_entities();

    // That's it, folks!
    return 0;
}
