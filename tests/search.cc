#include "test.hh"
#include <unordered_map>
#include <string>

struct test_component
{
    std::string name;
};

size_t add_count = 0;
size_t remove_count = 0;

template<>
class monkero::search_index<test_component>
{
public:
    entity find(const std::string& name) const
    {
        auto it = name_to_id.find(name);
        if(it == name_to_id.end())
            return INVALID_ENTITY;
        return it->second;
    }

    void add_entity(entity id, const test_component& data)
    {
        name_to_id[data.name] = id;
        id_to_name[id] = data.name;
        add_count++;
    }

    void remove_entity(entity id, const test_component&)
    {
        auto it = id_to_name.find(id);
        name_to_id.erase(it->second);
        id_to_name.erase(it);
        remove_count++;
    }

    void update(ecs& e)
    {
        for(auto& pair: name_to_id)
        {
            pair.second = INVALID_ENTITY;
        }

        e([&](entity id, const test_component& data){
            if(id_to_name[id] != data.name)
                id_to_name[id] = data.name;
            name_to_id[data.name] = id;
        });
    }

private:
    std::unordered_map<std::string, entity> name_to_id;
    std::unordered_map<entity, std::string> id_to_name;
};

int main()
{
    {
        ecs e;

        entity monkero = e.add(test_component{"Monkero"});
        entity tankero = e.add(test_component{"Tankero"});
        entity punkero = e.add(test_component{"Punkero"});
        entity antero = e.add(test_component{"Antero"});

        test(add_count == 4);

        entity id = e.find<test_component>("Punkero");
        test(id == punkero);
        id = e.find<test_component>("Antero");
        test(id == antero);
        id = e.find<test_component>("Monkero");
        test(id == monkero);
        id = e.find<test_component>("Tankero");
        test(id == tankero);

        e.get<test_component>(monkero)->name = "Bonito";
        id = e.find<test_component>("Monkero");
        test(id == monkero);

        e.update_search_index<test_component>();
        id = e.find<test_component>("Monkero");
        test(id == INVALID_ENTITY);
        id = e.find<test_component>("Bonito");
        test(id == monkero);

        e.get<test_component>(monkero)->name = "Monkero";
        e.update_search_indices();
        id = e.find<test_component>("Monkero");
        test(id == monkero);
        id = e.find<test_component>("Bonito");
        test(id == INVALID_ENTITY);

        test(e.find_component<test_component>("Antero")->name == "Antero");
    }
    test(remove_count == 4);

    return 0;
}
