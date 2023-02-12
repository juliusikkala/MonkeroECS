#include "test.hh"
#include <random>
#include <cstdlib>
#include <algorithm>
#include <unordered_set>

struct test_component_tag { test_component_tag(int = 123){} };
struct test_component_normal { test_component_normal(int a = 123): a(a) {} int a; };
struct test_component_ptr { test_component_ptr(int a = 123): a(a) {} int a; };

int main()
{
    scene e;

    // Initialize some data for us
    constexpr size_t N = 1000000;
    std::unordered_set<entity> tag_ids;
    std::unordered_set<entity> normal_ids;
    std::unordered_set<entity> ptr_ids;
    size_t real_normal_sum = 0;
    size_t real_ptr_sum = 0;
    size_t real_and_sum = 0;
    size_t real_or_sum = 0;
    size_t all_count = 0;
    size_t any_count = 0;
    for(size_t i = 0; i < N; ++i)
    {
        entity id = e.add();
        unsigned count = 0;

        if((rand()%3) == 0)
        {
            e.attach(id, test_component_tag{});
            tag_ids.insert(id);
            count++;
        }
        if((rand()%4) == 0)
        {
            e.attach(id, test_component_normal(i));
            normal_ids.insert(id);
            real_normal_sum += i;
            real_or_sum += i;
            count++;
        }
        if((rand()%5) == 0)
        {
            e.attach(id, test_component_ptr(i));
            ptr_ids.insert(id);
            real_ptr_sum += i;
            real_or_sum += i;
            count++;
        }

        if(count == 3)
        {
            real_and_sum += i;
            all_count++;
        }

        if(count >= 1)
            any_count++;
    }

    // Test single-entry iteration
    size_t iter_count = 0;
    e([&](entity id, test_component_tag&){
        test(tag_ids.count(id) != 0);
        iter_count++;
    });
    test(iter_count = e.count<test_component_tag>());

    iter_count = 0;
    size_t normal_sum = 0;
    e.foreach([&](entity id, test_component_normal& n){
        test(normal_ids.count(id) != 0);
        iter_count++;
        normal_sum += n.a;
    });
    test(iter_count == e.count<test_component_normal>());
    test(normal_sum == real_normal_sum);

    iter_count = 0;
    size_t ptr_sum = 0;
    e.foreach([&](entity id, test_component_ptr& n){
        test(ptr_ids.count(id) != 0);
        iter_count++;
        ptr_sum += n.a;
    });
    test(iter_count = e.count<test_component_ptr>());
    test(ptr_sum == real_ptr_sum);

    // Test multi-entry iteration (all required)
    iter_count = 0;
    size_t and_sum = 0;
    e.foreach([&](
        entity id,
        test_component_tag&,
        test_component_normal& n,
        test_component_ptr& p
    ){
        test(tag_ids.count(id) != 0);
        test(normal_ids.count(id) != 0);
        test(ptr_ids.count(id) != 0);

        iter_count++;
        and_sum += (n.a+p.a)/2;
    });
    test(iter_count == all_count);
    test(and_sum == real_and_sum);

    // Test multi-entry iteration (all optional)
    iter_count = 0;
    size_t or_sum = 0;
    e.foreach([&](
        entity id,
        test_component_tag* t,
        test_component_normal* n,
        test_component_ptr* p
    ){
        test(!t || tag_ids.count(id) != 0);
        test(!n || normal_ids.count(id) != 0);
        test(!p || ptr_ids.count(id) != 0);
        test(t != nullptr || n != nullptr || p != nullptr);

        iter_count++;
        if(n) or_sum += n->a;
        if(p) or_sum += p->a;
    });
    test(iter_count == any_count);
    test(or_sum == real_or_sum);

    // Test multi-entry iteration (some required, some optional)
    iter_count = 0;
    normal_sum = 0;
    e.foreach([&](
        entity id,
        test_component_tag* t,
        test_component_normal& n,
        test_component_ptr* p
    ){
        test(!t || tag_ids.count(id) != 0);
        test(normal_ids.count(id) != 0);
        test(!p || ptr_ids.count(id) != 0);

        iter_count++;
        normal_sum += n.a;
    });
    test(iter_count == normal_ids.size());
    test(normal_sum == real_normal_sum);

    // Test id-less iteration
    iter_count = 0;
    e.foreach([&](
        test_component_tag&,
        test_component_normal*,
        test_component_ptr*
    ){
        iter_count++;
    });
    test(iter_count == tag_ids.size());

    // Remove during iteration
    e.foreach([&](
        entity id,
        test_component_tag&,
        test_component_normal& n,
        test_component_ptr* p
    ){
        if(p && rand()%2)
        {
            tag_ids.erase(id);
            normal_ids.erase(id);
            ptr_ids.erase(id);
            e.remove(id);
            real_or_sum -= n.a + p->a;
        }
    });
    test(e.count<test_component_tag>() == tag_ids.size());
    test(e.count<test_component_normal>() == normal_ids.size());
    test(e.count<test_component_ptr>() == ptr_ids.size());

    // Add during iteration
    e.foreach([&](
        entity id,
        test_component_tag&,
        test_component_normal* n,
        test_component_ptr&
    ){
        if(n)
        {
            normal_ids.insert(e.add(test_component_normal(id)));
            real_or_sum += id;
        }
    });
    test(e.count<test_component_normal>() == normal_ids.size());

    // Add & remove during iteration
    e.foreach([&](
        entity id,
        test_component_tag&,
        test_component_normal& n,
        test_component_ptr* p
    ){
        if(p)
        {
            if(rand()%2)
            {
                real_or_sum -= p->a;
                real_or_sum -= n.a;
                tag_ids.erase(id);
                normal_ids.erase(id);
                ptr_ids.erase(id);
                e.remove(id);
            }
            else
            {
                ptr_ids.insert(e.add(test_component_ptr(id)));
                real_or_sum += id;
            }
        }
    });
    test(e.count<test_component_tag>() == tag_ids.size());
    test(e.count<test_component_normal>() == normal_ids.size());
    test(e.count<test_component_ptr>() == ptr_ids.size());

    // Check the sum
    or_sum = 0;
    e.foreach([&](
        entity id,
        test_component_tag* t,
        test_component_normal* n,
        test_component_ptr* p
    ){
        test(!t || tag_ids.count(id) != 0);
        test(!n || normal_ids.count(id) != 0);
        test(!p || ptr_ids.count(id) != 0);
        test(t != nullptr || n != nullptr || p != nullptr);

        if(n) or_sum += n->a;
        if(p) or_sum += p->a;
    });
    test(or_sum == real_or_sum);

    return 0;
}


