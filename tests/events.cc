#include "test.hh"
#include <random>
#include <algorithm>

struct test_event_1
{
    int count;
};

struct test_event_2
{
    double distance;
};

struct test_event_3
{
    int something;
};

struct test_component_tag {};
struct test_component_normal { int a; };
struct test_component_ptr: ptr_component { int a; };

struct test_system_1: receiver<test_event_1>
{
    int sum = 0;
    void handle(ecs&, const test_event_1& e)
    {
        sum += e.count;
    }
};

struct test_system_2: receiver<test_event_1, test_event_2>
{
    int sum = 0;
    double sum_d = 0;

    void handle(ecs&, const test_event_1& e)
    {
        sum += e.count;
    }

    void handle(ecs&, const test_event_2& e)
    {
        sum_d += e.distance;
    }
};

struct lifetime_tester: receiver<
    add_component<test_component_tag>,
    add_component<test_component_normal>,
    add_component<test_component_ptr>,
    remove_component<test_component_tag>,
    remove_component<test_component_normal>,
    remove_component<test_component_ptr>
> {
    int tag_count = 0;
    int normal_count = 0;
    int ptr_count = 0;
    entity expected_id = INVALID_ENTITY;

    void handle(ecs&, const add_component<test_component_tag>& e) override
    {
        tag_count++;
        test(e.id == expected_id);
        test(e.data != nullptr);
    }

    void handle(ecs&, const add_component<test_component_normal>& e) override
    {
        normal_count++;
        test(e.id == expected_id);
        test(e.data != nullptr);
    }

    void handle(ecs&, const add_component<test_component_ptr>& e) override
    {
        ptr_count++;
        test(e.id == expected_id);
        test(e.data != nullptr);
    }

    void handle(ecs&, const remove_component<test_component_tag>& e) override
    {
        tag_count--;
        test(e.id == expected_id);
        test(e.data != nullptr);
    }

    void handle(ecs&, const remove_component<test_component_normal>& e) override
    {
        normal_count--;
        test(e.id == expected_id);
        test(e.data != nullptr);
    }

    void handle(ecs&, const remove_component<test_component_ptr>& e) override
    {
        ptr_count--;
        test(e.id == expected_id);
        test(e.data != nullptr);
    }
};


int something_count = 0;
void handle_event_3(ecs&, const test_event_3& e)
{
    something_count += e.something;
}

int main()
{
    ecs e;

    // Emit without any listeners
    e.emit(test_event_1{1024});
    e.emit(test_event_2{1024.0});

    {
        // Emit with 1 listener
        test_system_1 ts1;
        e.add_receiver(ts1);

        e.emit(test_event_1{123});
        e.emit(test_event_2{456.0});

        test(ts1.sum == 123);

        // Emit with 2 listeners
        test_system_2 ts2;
        e.add_receiver(ts2);

        e.emit(test_event_1{789});
        e.emit(test_event_2{101112.0});

        test(e.get_handler_count<test_event_1>() == 2);
        test(e.get_handler_count<test_event_2>() == 1);
        test(ts1.sum == 123+789);
        test(ts2.sum == 789);
        test(ts2.sum_d == 101112.0);
    }
    test(e.get_handler_count<test_event_1>() == 0);
    test(e.get_handler_count<test_event_2>() == 0);

    // Add free-function event handler (members are tested through add_receiver)
    size_t handler_id = e.add_event_handler(handle_event_3);
    e.emit(test_event_3{42});
    e.emit(test_event_3{64});
    test(e.get_handler_count<test_event_3>() == 1);
    e.remove_event_handler(handler_id);
    test(e.get_handler_count<test_event_3>() == 0);
    test(something_count == 42+64);

    // Test subscription
    {
        event_subscription sub(e.subscribe(handle_event_3));
        e.emit(test_event_3{1});
        test(something_count == 42+64+1);
        test(e.get_handler_count<test_event_3>() == 1);
    }
    test(e.get_handler_count<test_event_3>() == 0);

    // Emit without any listeners, but again after all receivers are removed.
    e.emit(test_event_1{1024});
    e.emit(test_event_2{1024.0});

    // Test add_component & remove_component events
    lifetime_tester lt;
    e.add_receiver(lt);

    for(int attempt = 0; attempt < 3; ++attempt)
    {
        constexpr size_t N = 1000;
        std::vector<entity> ids;
        if(attempt == 1 || attempt == 2) e.start_batch();
        for(size_t i = 0; i < N; ++i)
        {
            entity id = lt.expected_id = e.add();
            e.attach(id, test_component_tag{});
            e.attach(id, test_component_normal{1});
            e.attach(id, test_component_ptr{{}, 1});
            ids.push_back(id);
        }
        if(attempt == 1) e.finish_batch();
        test(lt.tag_count == N);
        test(lt.normal_count == N);
        test(lt.ptr_count == N);

        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(ids.begin(), ids.end(), g);
        if(attempt == 1) e.start_batch();
        for(entity id: ids)
        {
            lt.expected_id = id;
            e.remove<test_component_tag>(id);
            e.remove<test_component_normal>(id);
            e.remove<test_component_ptr>(id);
        }
        if(attempt == 1 || attempt == 2) e.finish_batch();
        test(lt.tag_count == 0);
        test(lt.normal_count == 0);
        test(lt.ptr_count == 0);
    }


    return 0;
}
