#include "ecs.hh"
//#include "../monkeroecs.hh"
#include <iostream>
#include <cstdlib>
#include <chrono>
#include <random>

struct small
{
    int data;
};

struct large
{
    int data;
    int pad[99];
};

struct tag {};

void test_random_access()
{
    monkero::ecs ecs;
    std::default_random_engine rng(0);
    std::uniform_int_distribution<int> dist(0, 10);

    // Populate ECS.
    size_t N = 1<<16;
    std::vector<monkero::entity> ids;
    for(size_t i = 0; i < N; ++i)
    {
        monkero::entity id = ecs.add();
        if(dist(rng)==0) ecs.attach(id, tag{});
        if(dist(rng)==0) ecs.attach(id, small{});
        if(dist(rng)==0) ecs.attach(id, large{});
        ids.push_back(id);
    }

    // Try random access. (random ids are precalculated first!)
    size_t M = 100;
    std::vector<monkero::entity> shuffled_ids;

    for(size_t i = 0; i < M; ++i)
    {
        std::shuffle(ids.begin(), ids.end(), rng);
        shuffled_ids.insert(shuffled_ids.end(), ids.begin(), ids.end());
    }

    auto start = std::chrono::high_resolution_clock::now();
    int total = 0;
    for(size_t i = 0; i < M*N; ++i)
    {
        tag* t = ecs.get<tag>(shuffled_ids[i]);
        total += t != nullptr ? 1 : 0;
    }
    auto finish = std::chrono::high_resolution_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::duration<float>>(finish-start).count();
    printf("tag random access %f (count: %d)\n", diff, total);

    start = std::chrono::high_resolution_clock::now();
    total = 0;
    for(size_t i = 0; i < M*N; ++i)
    {
        small* t = ecs.get<small>(shuffled_ids[i]);
        total += t != nullptr ? 1 : 0;
    }
    finish = std::chrono::high_resolution_clock::now();
    diff = std::chrono::duration_cast<std::chrono::duration<float>>(finish-start).count();
    printf("small random access %f (count: %d)\n", diff, total);

    start = std::chrono::high_resolution_clock::now();
    total = 0;
    for(size_t i = 0; i < M*N; ++i)
    {
        large* t = ecs.get<large>(shuffled_ids[i]);
        total += t != nullptr ? 1 : 0;
    }
    finish = std::chrono::high_resolution_clock::now();
    diff = std::chrono::duration_cast<std::chrono::duration<float>>(finish-start).count();
    printf("large random access %f (count: %d)\n", diff, total);
}

void test_iteration()
{
    monkero::ecs ecs;
    std::default_random_engine rng(0);
    std::uniform_int_distribution<int> dist1(0, 1);
    std::uniform_int_distribution<int> dist2(0, 1000);

    // Populate ECS.
    size_t N = 1<<22;
    std::vector<monkero::entity> ids;
    for(size_t i = 0; i < N; ++i)
    {
        monkero::entity id = ecs.add();
        if(dist2(rng)==0) ecs.attach(id, tag{});
        if(dist1(rng)==0) ecs.attach(id, small{2});
        if(dist1(rng)==0) ecs.attach(id, large{2, {}});
        ids.push_back(id);
    }

    // Try iteration.
    size_t M = 100;
    auto start = std::chrono::high_resolution_clock::now();
    size_t total = 1;
    for(size_t i = 0; i < M; ++i)
    {
        ecs([&](tag& t){
            total <<= 1;
        });
    }
    auto finish = std::chrono::high_resolution_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::duration<float>>(finish-start).count();
    printf("tag iteration %f (count: %lu, rubbish: %lu)\n", diff, M * ecs.count<tag>(), total);

    start = std::chrono::high_resolution_clock::now();
    total = 1;
    for(size_t i = 0; i < M; ++i)
    {
        ecs([&](small& t){
            total *= t.data;
        });
    }
    finish = std::chrono::high_resolution_clock::now();
    diff = std::chrono::duration_cast<std::chrono::duration<float>>(finish-start).count();
    printf("small iteration %f (count: %lu, rubbish: %lu)\n", diff, M * ecs.count<small>(), total);

    start = std::chrono::high_resolution_clock::now();
    total = 1;
    for(size_t i = 0; i < M; ++i)
    {
        ecs([&](large& t){
            total *= t.data;
        });
    }
    finish = std::chrono::high_resolution_clock::now();
    diff = std::chrono::duration_cast<std::chrono::duration<float>>(finish-start).count();
    printf("large iteration %f (count: %lu, rubbish: %lu)\n", diff, M * ecs.count<large>(), total);

    start = std::chrono::high_resolution_clock::now();
    total = 1;
    size_t sum = 0;
    for(size_t i = 0; i < M; ++i)
    {
        ecs([&](tag& t1, small& t2, large& t3){
            total *= t2.data;
            total *= t3.data;
            sum++;
        });
    }
    finish = std::chrono::high_resolution_clock::now();
    diff = std::chrono::duration_cast<std::chrono::duration<float>>(finish-start).count();
    printf("combo iteration %f (count: %lu, rubbish: %lu)\n", diff, sum, total);
}

int main()
{
    test_random_access();
    test_iteration();

    return 0;
}

