#include "test.hh"

int main()
{
    ecs e;

    // Add so many entities that we reach the error state
    for(uint64_t i = 0; i < std::numeric_limits<entity>::max(); ++i)
        test(e.add() != INVALID_ENTITY);
    for(uint64_t i = 0; i < 10; ++i)
        test(e.add() == INVALID_ENTITY);

    e.clear_entities();

    // Test entity ID reuse, we shouldn't hit entity limit this way
    entity es[3];
    for(uint64_t i = 0; i < std::numeric_limits<entity>::max()/2; ++i)
    {
        for(uint64_t j = 0; j < 3; ++j)
        {
            es[j] = e.add();
            test(es[j] != INVALID_ENTITY);
        }
        for(uint64_t j = 0; j < 3; ++j)
            e.remove(es[j]);
    }

    e.clear_entities();

    return 0;
}
