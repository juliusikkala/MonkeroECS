//#include "ecs.hh"
#include "../monkeroecs.hh"
#include <iostream>
#include <cstdlib>

struct age
{
    int years;
};

struct alive {};
struct dead {};

class aging_system
{
public:
    void step(monkero::scene& ecs)
    {
        ecs([&](monkero::entity id, age& a, alive&){
            a.years++;
            if(a.years > 40 && rand()%10 == 0)
            {
                ecs.remove<alive>(id);
                ecs.attach(id, dead{});
            }
        });
    }
};

class breeding_system
{
public:
    void step(monkero::scene& ecs)
    {
        ecs([&](const age& a, alive&){
            if(a.years >= 20 && a.years < 40 && rand()%10 == 0)
            {
                ecs.add(age{0}, alive{});
            }
        });
    }
};

int main()
{
    monkero::scene ecs;

    // Seed the random (not related to MonkeroECS, just game logic)
    srand(0);

    aging_system aging;
    breeding_system breeding;

    for(int i = 0; i < 10; ++i)
    {
        ecs.add(age{0}, alive{});
    }

    for(int i = 0; i < 550; ++i)
    {
        aging.step(ecs);
        breeding.step(ecs);

        size_t alive_count = ecs.count<alive>();
        std::cout
            << "Generation: " << i << "\n"
            << "Alive: " << alive_count  << "\n"
            << "Dead: " << ecs.count<dead>() << std::endl;
        if(alive_count == 0) break;
    }
    return 0;
}

