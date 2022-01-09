#include "test.hh"
#include <random>
#include <unordered_map>

struct test_component_tag { test_component_tag(int = 123){} };
struct test_component_normal { test_component_normal(int a = 123): a(a) {} int a; };
struct test_component_ptr: ptr_component { test_component_ptr(int a = 123): a(a) {} int a; };

struct test_component_uncopiable: ptr_component
{
    test_component_uncopiable(int a = 123): a(a) {}
    test_component_uncopiable(const test_component_uncopiable& other) = delete;
    test_component_uncopiable(test_component_uncopiable&& other) = default;

    int a;
};

int main()
{
    ecs secondary;
    ecs primary;

    std::vector<entity> ids;
    for(int i = 0; i < 10000; ++i)
    {
        entity id = secondary.add();
        if(rand()%2)
            secondary.attach(id, test_component_tag{rand()});
        if(rand()%3)
            secondary.attach(id, test_component_normal{rand()});
        if(rand()%4)
            secondary.attach(id, test_component_ptr{rand()});
        if(rand()%5)
            secondary.attach(id, test_component_uncopiable{rand()});
        ids.push_back(id);
    }

    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(ids.begin(), ids.end(), g);
    std::unordered_map<entity, entity> equivalence;

    for(entity id: ids)
        equivalence[primary.copy(secondary, id)] = id;

    test(primary.count<test_component_tag>() == secondary.count<test_component_tag>());
    test(primary.count<test_component_normal>() == secondary.count<test_component_normal>());
    test(primary.count<test_component_ptr>() == secondary.count<test_component_ptr>());
    test(primary.count<test_component_uncopiable>() == 0);

    for(auto pair: equivalence)
    {
        test_component_tag* ptag_ptr = primary.get<test_component_tag>(pair.first);
        test_component_tag* stag_ptr = secondary.get<test_component_tag>(pair.second);
        test_component_normal* pnormal_ptr = primary.get<test_component_normal>(pair.first);
        test_component_normal* snormal_ptr = secondary.get<test_component_normal>(pair.second);
        test_component_ptr* pptr_ptr = primary.get<test_component_ptr>(pair.first);
        test_component_ptr* sptr_ptr = secondary.get<test_component_ptr>(pair.second);
        test(!!ptag_ptr == !!stag_ptr);
        test(!!pnormal_ptr == !!snormal_ptr);
        if(pnormal_ptr && snormal_ptr) test(pnormal_ptr->a == snormal_ptr->a);
        test(!!pptr_ptr == !!sptr_ptr);
        if(pptr_ptr && sptr_ptr) test(pptr_ptr->a == sptr_ptr->a);
    }

    return 0;
}

