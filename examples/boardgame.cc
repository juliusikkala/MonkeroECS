// This example is chess-themed, but doesn't implement any real chess logic.
// Please start reading from main(), check out the systems last.
#include "monkeroecs.hh"
#include <iostream>
#include <cstdlib>

// Anything can be a component, and you don't even have to derive from anything!
struct position
{
    int x, y;
};

// Even enums can be components as-is.
enum piece
{
    PIECE_PAWN,
    PIECE_KNIGHT,
    PIECE_BISHOP,
    PIECE_ROOK,
    PIECE_QUEEN,
    PIECE_KING
};

// These two are tag components; they are used to "tag" entities with a feature
// simply with the existence of the component.
struct white_side {};
struct black_side {};

// This struct is used as an event. Note that it too has no requirements to
// derive or contain anything, so you can just use anything as an event type.
struct move_event
{
    monkero::entity id;
    position to;
};

// This system removes the opponent's pieces when a move captures one.
class piece_remover:
    // The receiver template takes a list of event types this system is
    // interested in. They will be automatically fed to the system when they
    // are emitted.
    public monkero::receiver<move_event>
{
public:
    // Because we specified that we want move_events, we have to implement a
    // handler for when that event occurs.
    void handle(monkero::ecs& ecs, const move_event& move)
    {
        // Figure out which side the moving piece belongs to.
        bool is_white = ecs.has<white_side>(move.id);

        // You can call the ECS object itself or use ecs::foreach in order to
        // iterate over entities. The parameter given to it is usually a lambda
        // function, whose first parameter is an entity id. This function is
        // called for every entity which has the components specified in the
        // rest of the function's parameters.
        //
        // Reference parameters are required (entities which don't have all
        // referenced components are skipped), pointers are optional (null if
        // the entity doesn't have that component).
        //
        // In this example, we iterate over all entities that have a position.
        // The side is optional, black pieces won't have a 'white_side' tag.
        ecs([&](
            monkero::entity id, const position& p, const white_side* w
        ){
            if(is_white != (bool)w && p.x == move.to.x && p.y == move.to.y)
            {
                // You can safely remove components and entities while looping
                // over entities. The component destructors will be called only
                // after the outermost loop, but ecs::get() will already start
                // pretending that the components no longer exist.
                ecs.remove(id);
            }
        });
    }

};

// This system moves chess pieces randomly like a toddler, but stays within the
// board.
class players
{
public:
    void play_turn(monkero::ecs& ecs)
    {
        // Pick a random piece for this side
        monkero::entity id;
        if(current_turn % 2 == 0)
        {
            // Pick a random white piece
            id = ecs.get_entity<white_side>(
                rand() % ecs.count<white_side>()
            );
        } else {
            // Pick a random black piece
            id = ecs.get_entity<black_side>(
                rand() % ecs.count<black_side>()
            );
        }

        // Pick a random direction that stays on the board
        position& pos = *ecs.get<position>(id);
        position steps[8] = {
            {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}, {0, 1}
        };

        int dir = rand()%8;
        for(int i = 0; i < 8; ++i)
        {
            position to = {pos.x + steps[dir].x, pos.y + steps[dir].y};
            // Check if move is on board
            if(to.x >= 0 && to.x < 8 && to.y >= 0 && to.y < 8)
            {
                // Let's move the piece!
                pos = to;
                ecs.emit(move_event{id, pos});
                break;
            }
            dir = (dir + 1) % 8;
        }

        // Prepare the next turn.
        current_turn++;
    }

private:
    int current_turn = 0;
};

// This system is responsible for checking the win condition. In our game, the
// winner is the side that has pieces left at the end.
class win_condition_checker:
    // add_component and remove_component are built-in events. They're sent
    // whenever a component of the specified type is added (or removed).
    public monkero::receiver<
        monkero::add_component<black_side>,
        monkero::remove_component<black_side>,
        monkero::add_component<white_side>,
        monkero::remove_component<white_side>
    >
{
public:
    void handle(monkero::ecs&, const monkero::add_component<black_side>&)
    { black_pieces_left++; }
    void handle(monkero::ecs&, const monkero::remove_component<black_side>&)
    { black_pieces_left--; }
    void handle(monkero::ecs&, const monkero::add_component<white_side>&)
    { white_pieces_left++; }
    void handle(monkero::ecs&, const monkero::remove_component<white_side>&)
    { white_pieces_left--; }

    const char* get_winner() const
    {
        return white_pieces_left == 0 ? "black" : "white";
    }

    bool is_game_over() const
    {
        return white_pieces_left == 0 || black_pieces_left == 0; 
    }

private:
    int white_pieces_left = 0;
    int black_pieces_left = 0;
};

int main()
{
    monkero::ecs ecs;

    // Seed the random (not related to MonkeroECS, just game logic)
    srand(time(NULL));

    // Let's add the systems first.
    piece_remover remover;
    ecs.add_receiver(remover);
    players p;
    win_condition_checker w;
    ecs.add_receiver(w);

    // Then, let's populate the chess board with entities.
    const piece pieces[8] = {
        PIECE_ROOK, PIECE_KNIGHT, PIECE_BISHOP, PIECE_QUEEN,
        PIECE_KING, PIECE_BISHOP, PIECE_KNIGHT, PIECE_ROOK
    };
    for(int i = 0; i < 8; ++i)
    {
        // Each call adds one entity with the given values as components
        ecs.add(position{i, 0}, piece{pieces[i]}, white_side{});
        ecs.add(position{i, 1}, PIECE_PAWN, white_side{});
        ecs.add(position{i, 6}, PIECE_PAWN, black_side{});
        ecs.add(position{i, 7}, piece{pieces[i]}, black_side{});
    }

    // It's time to play a little game of absolutely-not-chess.
    // Let's run until one side wins.
    while(!w.is_game_over())
    {
        p.play_turn(ecs);
    }
    std::cout << w.get_winner() << " won!" << std::endl;
    return 0;
}
