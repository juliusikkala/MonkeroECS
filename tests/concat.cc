#include "test.hh"

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
    }

    for(int i = 0; i < 10000; ++i)
    {
        entity id = primary.add();
        if(rand()%5)
            primary.attach(id, test_component_tag{rand()});
        if(rand()%4)
            primary.attach(id, test_component_normal{rand()});
        if(rand()%3)
            primary.attach(id, test_component_ptr{rand()});
        if(rand()%2)
            primary.attach(id, test_component_uncopiable{rand()});
    }

    size_t secondary_tag = secondary.count<test_component_tag>();
    size_t secondary_normal = secondary.count<test_component_normal>();
    size_t secondary_ptr = secondary.count<test_component_ptr>();
    //size_t secondary_uncopiable = secondary.count<test_component_uncopiable>();

    size_t primary_tag = primary.count<test_component_tag>();
    size_t primary_normal = primary.count<test_component_normal>();
    size_t primary_ptr = primary.count<test_component_ptr>();
    size_t primary_uncopiable = primary.count<test_component_uncopiable>();

    primary.concat(secondary);

    test(primary.count<test_component_tag>() == primary_tag + secondary_tag);
    test(primary.count<test_component_normal>() == primary_normal + secondary_normal);
    test(primary.count<test_component_ptr>() == primary_ptr + secondary_ptr);
    test(primary.count<test_component_uncopiable>() == primary_uncopiable);

    std::map<entity, entity> translation_table;
    primary.start_batch();
    primary.concat(secondary, &translation_table);
    primary.finish_batch();

    test(primary.count<test_component_tag>() == primary_tag + secondary_tag*2);
    test(primary.count<test_component_normal>() == primary_normal + secondary_normal*2);
    test(primary.count<test_component_ptr>() == primary_ptr + secondary_ptr*2);
    test(primary.count<test_component_uncopiable>() == primary_uncopiable);

    secondary.foreach([&](
        entity id,
        test_component_normal* normal,
        test_component_ptr* ptr
    ){
        if(normal)
            test(primary.get<test_component_normal>(translation_table[id])->a == normal->a);
        if(ptr)
            test(primary.get<test_component_ptr>(translation_table[id])->a == ptr->a);
    });

    return 0;
}

