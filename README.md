MonkeroECS
==========

A compact header-only, C++17 ECS (Entity Component System) gutted from a game
engine project.

Features:
- No entity count limits (other than 32-bit entity index)
- Only standard library 
- Events
- Dependencies
  - Components can depend on systems and other components which will be added
    automatically if not present already
- Flexible but simple-to-use foreach
  - Iterates over one or more component types simultaneously, some can be
    optional
- Generally linear iteration time
- Memory-efficient handling of tag components
- Batched modification
- Only depends on the standard library

In the interest of saving your time, here's a list of common game dev deal-breakers:
- No tests included (for now)
- Uses RTTI (not in components though) & STL & lots of templates
- Lots of potentially slow-to-include standard library headers
- Looking up individual components by entity id is logarithmic
- Thread-oblivious

## Integration

Just copy monkeroecs.hh over and include it wherever you need ECS stuff.

The namespace name is quite long, so you may want to [alias it to something
shorter](https://en.cppreference.com/w/cpp/language/namespace_alias) or put
"using namespace monkero;" inside your own namespace.

## Building docs & examples

To build the Doxygen documentation and examples:
```sh
meson build
ninja -C build # for examples
ninja -C build docs # for docs
```

## Usage

1. Create your component types. Any type can be used as a component!
```cpp
struct health
{
    int hit_points;
};
```

2. Create some event types. Any type can be used as an event!
```cpp
struct hit_event
{
    monkero::entity damaged_entity;
    int damage;
};
```

3. Create a system & list emitted and received events!
```cpp
class health_system:
    public monkero::system,
    public monkero::receiver<hit_event>
{
public:
    void handle(monkero::ecs& ecs, const hit_event& e)
    {
        ecs.get<health>(e.damaged_entity)->hit_points -= e.damage;
    }
};

class poison_gas_system:
    public monkero::system,
    public monkero::emitter<hit_event>
{
public:
    void tick(monkero::ecs& ecs)
    {
        // Iterate all entities with health component
        ecs([&](monkero::entity id, health&){
            emit(ecs, hit_event{id, 10});
        });
    }
};
```

4. Add entities, components and systems to the ECS instance!
```cpp
monkero::ecs ecs;

ecs.add_system<health_system>();
poison_gas_system& gas = ecs.add_system<poison_gas_system>();

// Adds entity with component health
ecs.add(health{30});
ecs.add(health{40});
ecs.add(health{50});

while(main_loop)
{
    //...
    gas.tick(ecs);
    //...
}
```

Advanced usage examples of components, systems and events can be found in the
`examples` folder along with a complete usage example of the system as a whole.
