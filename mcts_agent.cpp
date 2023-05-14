#include <iostream>
#include <limits>
#include <memory>
#include <cmath>
#include <random>
#include <cassert>
#include <iomanip>
#include <thread>
#include <mutex>
#include <sstream> 
#include "mcts_agent.h"

Mcts_agent::Mcts_agent(double exploration_factor,
    std::chrono::milliseconds max_decision_time, bool is_parallelized, bool is_verbose) : exploration_factor(exploration_factor),
    max_decision_time(max_decision_time), is_verbose(is_verbose), random_generator(random_device()) {
    if (is_parallelized && is_verbose)
    {
        throw std::logic_error("Concurrent playouts and verbose mode do not make sense together.");
    }
}

Mcts_agent::Node::Node(Cell_state player, std::pair<int, int> move,
    std::shared_ptr<Node> parent_node) :
    win_count(0),
    visit_count(0),
    move(move),
    player(player),
    child_nodes(),
    parent_node(parent_node) {}

std::pair<int, int> Mcts_agent::choose_move(const Board& board, Cell_state player)
{
    if (is_verbose)
    {
        std::cout << "\n-------------MCTS VERBOSE START - " << player << " to move-------------\n" << std::endl;
    }
    root = std::make_shared<Node>(player, std::make_pair(-1, -1), nullptr);
    std::vector<std::thread> threads;
    unsigned int number_of_threads;
    if (is_parallelized) {
        number_of_threads = std::thread::hardware_concurrency();
    }
    int mcts_iteration_counter = 0;
    expand_node(root, board);
    auto start_time = std::chrono::high_resolution_clock::now();
    auto end_time = start_time + max_decision_time;
    while (std::chrono::high_resolution_clock::now() < end_time)
    {   
        if (is_verbose)
        {
			std::cout << "\n------------------STARTING SIMULATION " << mcts_iteration_counter + 1 << "------------------" << std::endl;
		}
        std::shared_ptr<Node> chosen_child = select_child(root);
        if (is_parallelized) {
            std::vector<Cell_state> results(number_of_threads);
            for (unsigned int thread_index = 0; thread_index < number_of_threads; thread_index++) {
                threads.push_back(std::thread([&, thread_index]() {
                    results[thread_index] = simulate_random_playout(chosen_child, board);
                    }));
            }
            for (auto& thread : threads) {
                thread.join();
            }
            threads.clear();
            for (Cell_state playout_winner : results) {
                backpropagate(chosen_child, playout_winner);
            }
        }
        else {
            Cell_state playout_winner = simulate_random_playout(chosen_child, board);
            backpropagate(chosen_child, playout_winner);
        }
        if (is_verbose)
        {
            std::cout << "\nAFTER BACKPROPAGATION, root node has " << root->visit_count << " visits, " << root->win_count << " wins, and " << root->child_nodes.size() << " child nodes. Their details are:\n";
            for (const auto& child : root->child_nodes)
            {
                std::ostringstream node_info_stream;
                node_info_stream << "Child node " << child->move.first << "," << child->move.second << ": Wins: " << child->win_count << ", Visits: " << child->visit_count << ". Win ratio: ";

                if (child->visit_count) {
                    node_info_stream << std::fixed << std::setprecision(2) << static_cast<double> (child->win_count) / child->visit_count;
                }
                else {
                    node_info_stream << "N/A (no visits yet)";
                }

                std::cout << node_info_stream.str() << std::endl;
            }
        }

        mcts_iteration_counter++;
    }
    if (is_verbose)
    {
        std::cout << "\nTIMER RAN OUT. " << mcts_iteration_counter << " iterations completed. CHOOSING A MOVE FROM ROOT'S CHILDREN:" << std::endl;
    }
    double max_win_ratio = -1.;
    std::shared_ptr<Node> best_child;
    for (const auto& child : root->child_nodes)
    {
        double win_ratio = static_cast<double> (child->win_count) / child->visit_count;
        if (is_verbose)
        {
            std::ostringstream win_ratio_stream;
            if (child->visit_count > 0) {
                win_ratio_stream << std::fixed << std::setprecision(2) << static_cast<double> (child->win_count) / child->visit_count;
            }
            else {
                win_ratio_stream << "N/A (no visits yet)";
            }

            std::cout << "Child " << child->move.first << "," << child->move.second << " has a win ratio of " << win_ratio_stream.str() << std::endl;
        }

        if (win_ratio > max_win_ratio)
        {
            max_win_ratio = win_ratio;
            best_child = child;
        }
    }
    if (!best_child)
    {
        throw std::runtime_error("Statistics are not sufficient to find the best child.");
    }
    else if (is_verbose)
    {
        std::cout << "\nAfter " << mcts_iteration_counter << " iterations, choose child " << best_child->move.first << ", " << best_child->move.second << " with win ratio " << std::setprecision(4) << max_win_ratio << std::endl;
        std::cout << "\n--------------------MCTS VERBOSE END--------------------\n" << std::endl;
    }
    return best_child->move;
}

void Mcts_agent::expand_node(const std::shared_ptr<Node>& node, const Board& board)
{
    std::vector<std::pair<int, int>> valid_moves = board.get_valid_moves();
    for (const auto& move : valid_moves)
    {
        std::shared_ptr<Node> new_child =
            std::make_shared<Node>(node->player, move, node);
        node->child_nodes.push_back(new_child);
        if (is_verbose)
        {
            std::cout << "EXPANDED ROOT'S CHILD: " << move.first << "," << move.second << std::endl;
        }
    }
}

double Mcts_agent::calculate_uct_score(const std::shared_ptr<Node>& child_node, const std::shared_ptr<Node>& parent_node) {
    if (child_node->visit_count == 0) {
        return std::numeric_limits<double>::max();
    }
    else {
        return static_cast<double> (child_node->win_count) / child_node->visit_count +
            exploration_factor * std::sqrt(std::log(parent_node->visit_count) / child_node->visit_count);
    }
}

std::shared_ptr<Mcts_agent::Node > Mcts_agent::select_child(const std::shared_ptr<Node>& parent_node)
{
    // Initialize best_child as the first child and calculate its UCT score
    std::shared_ptr<Node> best_child = parent_node->child_nodes[0];
    double max_score = calculate_uct_score(best_child, parent_node);
    // Start loop from the second child
    for (auto iterator = std::next(parent_node->child_nodes.begin()); iterator != parent_node->child_nodes.end(); ++iterator)
    {
        const auto& child = *iterator;
        double uct_score = calculate_uct_score(child, parent_node);

        if (uct_score > max_score)
        {
            max_score = uct_score;
            best_child = child;
        }
    }
    if (is_verbose)
    {
        std::cout << "\nSELECTED CHILD " << best_child->move.first << ", " << best_child->move.second << " with UCT of ";
        if (max_score == std::numeric_limits<double>::max()) {
            std::cout << "infinity" << std::endl;
        }
        else {
            std::cout << std::setprecision(4) << max_score << std::endl;
        }
    }

    return best_child;
}

Cell_state Mcts_agent::simulate_random_playout(const std::shared_ptr<Node>& node, Board board)
{
    Cell_state current_player = node->player;
    board.make_move(node->move.first, node->move.second, current_player);
    if (is_verbose)
    {
        std::cout << "\nSIMULATING A RANDOM PLAYOUT from node " << node->move.first << ", " << node->move.second << ". Simulation board is in state:\n" << board;
    }

    while (board.check_winner() == Cell_state::Empty)
    {
        current_player = (current_player == Cell_state::Blue) ? Cell_state::Red : Cell_state::Blue;
        std::vector<std::pair<int, int>> valid_moves = board.get_valid_moves();
        std::uniform_int_distribution < > distribution(0, static_cast<int> (valid_moves.size() - 1));
        std::pair<int, int> random_move = valid_moves[distribution(random_generator)];
        if (is_verbose)
        {
            std::cout << "Current player in simulation is " << current_player << " in Board state:\n" << board;
            std::cout << current_player << " makes random move " << random_move.first << "," << random_move.second << ". ";
        }

        board.make_move(random_move.first, random_move.second, current_player);
        if (board.check_winner() != Cell_state::Empty)
        {
            if (is_verbose)
            {
                std::cout << "DETECTED WIN for player " << current_player << " in Board state:\n" << board << "\n";
            }

            break;	// If the game has ended, break the loop.
        }
    }

    return current_player;
}

//in the current implementation traverses the tree chosen child
//to its root (1 level), but is suitable for traversing the whole tree.
void Mcts_agent::backpropagate(std::shared_ptr<Node>& node, Cell_state winner)
{
    std::shared_ptr<Node> current_node = node;
    while (current_node != nullptr)
    {
        std::lock_guard<std::mutex> lock(current_node->node_mutex); // Lock the node's mutex
        current_node->visit_count += 1;
        if (winner == current_node->player)
        {
            current_node->win_count += 1;
        }

        if (is_verbose)
        {
            std::cout << "BACKPROPAGATED result to node " << current_node->move.first << ", " << current_node->move.second << ". It currently has " << current_node->win_count << " wins and " << current_node->visit_count << " visits." << std::endl;
        }

        current_node = current_node->parent_node;
    }
}