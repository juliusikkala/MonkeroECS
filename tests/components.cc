#include "test.hh"
#include <random>
#include <algorithm>

struct test_component_tag { test_component_tag(int = 123){} };
struct test_component_normal { test_component_normal(int a = 123): a(a) {} int a; };
struct test_component_dependency_tag:
    dependency_components<
        test_component_tag, test_component_normal
    >
{
    test_component_dependency_tag(int) {}
};

struct test_component_dependency_normal:
    dependency_components<
        test_component_tag, test_component_normal
    >
{
    test_component_dependency_normal(int a = 123): a(a) {}
    int a;
};

template<typename Component>
void test_sum(ecs& e, size_t expected)
{
    if constexpr(
        !std::is_same_v<Component, test_component_tag> && 
        !std::is_same_v<Component, test_component_dependency_tag>
    ){
        size_t sum = 0;
        e.foreach([&](Component& c){sum += c.a;});
        test(expected == sum);
    }
}

template<typename Component>
void run_tests(ecs& e)
{
    constexpr int N = 10000;
    for(int batching = 0; batching <= 1; ++batching)
    {
        // Check regular addition
        size_t real_sum = 0;
        std::vector<entity> ids;
        std::vector<Component*> ptrs;

        if(batching) e.start_batch();
        for(int i = 0; i < N; ++i)
        {
            real_sum += i;
            ids.push_back(e.add(Component(i)));
            ptrs.push_back(e.get<Component>(ids.back()));
        }
        if(batching) e.finish_batch();

        test(e.count<Component>() == N);
        test_sum<Component>(e, real_sum);

        if(batching) e.start_batch();
        // Check attach
        for(int i = 0; i < N; ++i)
        {
            test(e.has<Component>(ids[i]));

            real_sum += i;
            entity id = e.add();
            e.attach(id, Component(i));
            ids.push_back(id);
            ptrs.push_back(e.get<Component>(id));
        }
        if(batching) e.finish_batch();
        test(e.count<Component>() == 2*N);
        test_sum<Component>(e, real_sum);

        if(batching) e.start_batch();
        // Check re-attach and adding elsewhere than end
        for(int i = 0; i < N; ++i)
        {
            real_sum -= i;
            e.attach(ids[i], Component(0));
            ptrs[i] = e.get<Component>(ids[i]);
        }
        if(batching) e.finish_batch();
        test(e.count<Component>() == 2*N);
        test_sum<Component>(e, real_sum);

        // Make sure dependencies were actually added
        if constexpr(
            std::is_same_v<Component, test_component_dependency_tag> ||
            std::is_same_v<Component, test_component_dependency_normal>
        ){
            test(e.count<test_component_tag>() == 2*N);
            test(e.count<test_component_normal>() == 2*N);
        }

        // Check that pointers are still valid.
        size_t i = 0;
        e.foreach([&](Component& c){test(&c == ptrs[i]); ++i;});

        if(batching) e.start_batch();

        // Test removal
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(ids.begin(), ids.end(), g);
        for(int i = 0; i < N; ++i)
        {
            if constexpr(
                !std::is_same_v<Component, test_component_tag> && 
                !std::is_same_v<Component, test_component_dependency_tag>
            ){
                real_sum -= e.get<Component>(ids[i])->a;
            }
            e.remove<Component>(ids[i]);
        }
        if(batching) e.finish_batch();

        test(e.count<Component>() == N);
        test_sum<Component>(e, real_sum);

        // Component-wise removal should not remove dependencies.
        if constexpr(
            std::is_same_v<Component, test_component_dependency_tag> ||
            std::is_same_v<Component, test_component_dependency_normal>
        ){
            test(e.count<test_component_tag>() == 2*N);
            test(e.count<test_component_normal>() == 2*N);
        }

        if(batching) e.start_batch();
        // Add some more post-removal
        for(int i = 0; i < N/2; ++i)
        {
            e.add(Component(i));
            real_sum += i;
        }
        if(batching) e.finish_batch();

        test(e.count<Component>() == N+N/2);
        test_sum<Component>(e, real_sum);

        if(batching) e.start_batch();
        // Do full entity removals
        for(int i = 0; i < N; ++i)
        {
            e.remove(ids[i+N]);
        }
        if(batching) e.finish_batch();

        test(e.count<Component>() == N-N/2);

        // Full removal should remove dependencies too.
        if constexpr(
            std::is_same_v<Component, test_component_dependency_tag> ||
            std::is_same_v<Component, test_component_dependency_normal>
        ){
            test(e.count<test_component_tag>() == 2*N-N/2);
            test(e.count<test_component_normal>() == 2*N-N/2);
        }

        // Finally, clear should remove everything.
        e.clear_entities();
        test(e.count<Component>() == 0);
        if constexpr(
            std::is_same_v<Component, test_component_dependency_tag> ||
            std::is_same_v<Component, test_component_dependency_normal>
        ){
            test(e.count<test_component_tag>() == 0);
            test(e.count<test_component_normal>() == 0);
        }
    }
}

int main()
{
    ecs e;

    run_tests<test_component_tag>(e);
    run_tests<test_component_normal>(e);
    run_tests<test_component_dependency_tag>(e);
    run_tests<test_component_dependency_normal>(e);

    return 0;
}

