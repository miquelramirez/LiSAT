#include "successor_generator.h"

#include "../action.h"
#include "../action_schema.h"
#include "../states/state.h"
#include "../task.h"
#include "../database/table.h"

#include <cassert>
#include <vector>

using namespace std;

SuccessorGenerator::SuccessorGenerator(const Task &task)  :
    static_information(task.get_static_info())
{
    obj_per_type.resize(task.type_names.size());
    for (const Object &obj : task.objects) {
        for (int type : obj.getTypes()) {
            obj_per_type[type].push_back(obj.getIndex());
        }
    }
    is_predicate_static.reserve(static_information.get_relations().size());
    for (const auto &r : static_information.get_relations()) {
        is_predicate_static.push_back(!r.tuples.empty());
    }
}


/**
 * Generate successors states for a given state
 *
 * @details For each action schema, we first check if the nullary preconditions
 * are satisfied in the current state. If they are, we instantiate them using
 * the successor generator passed as command line parameter. Then we check if
 * there is any instantiation of the action schema in the given state. If there
 * is none, then two cases are possible:
 *    1. The action schema is not applicable. In this case, we just proceed to
 *    instantiate the next schema; or
 *    2. The action schema is ground. In this case, we simply proceed to check
 *    if the preconditions are satisfied and, if so, apply the ground action. We
 *    need to check applicability here because, if there is no parameter, then
 *    the join in the successor generator was never performed.
 * If there are instantiations, then we simply apply the action effects, since
 * we know the actions are applicable.
 *
 * @attention A lot of duplication in this code. :-)
 *
 * @param actions: list of actions
 * @param state: state being evaluated
 * @param staticInformation: static information of the task
 * @return vector of pairs <State, Action> where state is the successor state and
 * action is the ground action generating it from the current state
 */
vector<LiftedOperatorId>
SuccessorGenerator::get_applicable_actions(const std::vector<ActionSchema> &actions,
                                           const DBState &state) {
    vector<LiftedOperatorId> applicable_operators;
    // Duplicate code from generic join implementation
    for (const ActionSchema &action : actions) {
        if (is_trivially_inapplicable(state, action)) {
            continue;
        }

        if (action.is_ground()) {
            bool applicable = is_ground_action_applicable(action, state);
            if (applicable)
                applicable_operators.emplace_back(action.get_index(), vector<int>());
        } else {
            Table instantiations = instantiate(action, state);
            if (instantiations.tuples.empty()) {
                // No applicable action, skip this action schema;
                continue;
            }
            vector<int> free_var_indices;
            vector<int> map_indices_to_position;
            compute_map_indices_to_table_positions(instantiations,
                                                   free_var_indices,
                                                   map_indices_to_position);
            for (const vector<int> &tuple_with_const : instantiations.tuples) {
                vector<int> ordered_tuple(free_var_indices.size());
                order_tuple_by_free_variable_order(free_var_indices, map_indices_to_position,
                                                   tuple_with_const, ordered_tuple);
                applicable_operators.emplace_back(action.get_index(), move(ordered_tuple));
            }
        }
    }

    return applicable_operators;
}

DBState SuccessorGenerator::generate_successors(
    const LiftedOperatorId &op,
    const ActionSchema& action,
    const DBState &state) {

    vector<bool> new_nullary_atoms(state.get_nullary_atoms());
    vector<Relation> new_relation(state.get_relations());
    apply_nullary_effects(action, new_nullary_atoms);

    if (action.is_ground()) {
        apply_ground_action_effects(action, new_relation);
    }
    else {
        apply_lifted_action_effects(action, op.get_instantiation(), new_relation);
    }

    return DBState(move(new_relation), move(new_nullary_atoms));
}

void SuccessorGenerator::order_tuple_by_free_variable_order(const vector<int> &free_var_indices,
                                                            const vector<int> &map_indices_to_position,
                                                            const vector<int> &tuple_with_const,
                                                            vector<int> &ordered_tuple) const {
    for (size_t i = 0; i < free_var_indices.size(); ++i) {
        ordered_tuple[free_var_indices[i]] = tuple_with_const[map_indices_to_position[i]];
    }
}

void SuccessorGenerator::compute_map_indices_to_table_positions(const Table &instantiations,
                                                                vector<int> &free_var_indices,
                                                                vector<int> &map_indices_to_position) const {
    for (size_t j = 0; j < instantiations.tuple_index.size(); ++j) {
        if (instantiations.index_is_variable(j)) {
            free_var_indices.push_back(instantiations.tuple_index[j]);
            map_indices_to_position.push_back(j);
        }
    }
}

bool SuccessorGenerator::is_trivially_inapplicable(const DBState &state, const ActionSchema &action) const {
    const auto& positive_precond = action.get_positive_nullary_precond();
    const auto& negative_precond = action.get_negative_nullary_precond();
    const auto& nullary_atoms = state.get_nullary_atoms();
    for (size_t i = 0; i < positive_precond.size(); ++i) {
        if ((positive_precond[i] and !nullary_atoms[i]) or
            (negative_precond[i] and nullary_atoms[i])) {
            return true;
        }
    }
    return false;
}

void SuccessorGenerator::apply_nullary_effects(const ActionSchema &action,
                                               vector<bool> &new_nullary_atoms) const
{
    /*
     * Loop over positive and negative nullary effects and apply them accordingly
     * to the state.
     */
    for (size_t i = 0; i < action.get_negative_nullary_effects().size(); ++i) {
        if (action.get_negative_nullary_effects()[i])
            new_nullary_atoms[i] = false;
    }
    for (size_t i = 0; i < action.get_positive_nullary_effects().size(); ++i) {
        if (action.get_positive_nullary_effects()[i])
            new_nullary_atoms[i] = true;
    }
}

void SuccessorGenerator::apply_ground_action_effects(const ActionSchema &action,
                                                     vector<Relation> &new_relation) const
{
    for (const Atom &eff : action.get_effects()) {
        GroundAtom ga;
        for (const Argument &a : eff.arguments) {
            // Create ground atom for each effect given the instantiation
            assert(a.constant);
            ga.push_back(a.index);
        }
        assert(eff.predicate_symbol == new_relation[eff.predicate_symbol].predicate_symbol);
        if (eff.negated) {
            // If ground effect is negated, remove it from relation
            new_relation[eff.predicate_symbol].tuples.erase(ga);
        }
        else {
            // If ground effect is not in the state, we add it
            new_relation[eff.predicate_symbol].tuples.insert(ga);
        }
    }
}

void SuccessorGenerator::apply_lifted_action_effects(const ActionSchema &action,
                                                     const vector<int> &tuple,
                                                     vector<Relation> &new_relation)
{
    for (const Atom &eff : action.get_effects()) {
        const GroundAtom &ga = tuple_to_atom(tuple, eff);
        assert(eff.predicate_symbol == new_relation[eff.predicate_symbol].predicate_symbol);
        if (eff.negated) {
            // Remove from relation
            new_relation[eff.predicate_symbol].tuples.erase(ga);
        }
        else {
            if (find(new_relation[eff.predicate_symbol].tuples.begin(),
                     new_relation[eff.predicate_symbol].tuples.end(),
                     ga) == new_relation[eff.predicate_symbol].tuples.end()) {
                // If ground atom is not in the state, we add it
                new_relation[eff.predicate_symbol].tuples.insert(ga);
            }
        }
    }
}

/**
 *    This action generates the ground atom produced by an atomic effect given an instantiation of
 *    its parameters.
 *
 *    @details First, we rearrange the indices. Then, we create the atom based on whether the
 * argument is a constant or not. If it is, then we simply pass the constant value; otherwise we use
 *    the instantiation that we found.
 */
const GroundAtom &SuccessorGenerator::tuple_to_atom(const vector<int> &tuple, const Atom &eff)
{

    ground_atom.clear();
    ground_atom.reserve(eff.arguments.size());
    for (size_t i = 0; i < eff.arguments.size(); i++) {
        if (!eff.arguments[i].constant)
            ground_atom.push_back(tuple[eff.arguments[i].index]);
        else
            ground_atom.push_back(eff.arguments[i].index);
    }

    // Sanity check: check that all positions of the tuple were initialized
    assert(find(ground_atom.begin(), ground_atom.end(), -1) == ground_atom.end());

    return ground_atom;
}

/*
 * Check the applicability of an already ground action (given grounded in the
 * PDDL). We just need to check applicability for completely ground actions
 * because the successor generations find only applicable actions for lifted
 * ones.
 *
 * In this case, the parameter type is slightly misleading, but the parameter
 * 'action' is a ground action here.
 */
bool SuccessorGenerator::is_ground_action_applicable(const ActionSchema &action,
                                                     const DBState &state)
{
    for (const Atom &precond : action.get_precondition()) {
        int index = precond.predicate_symbol;
        vector<int> tuple;
        tuple.reserve(precond.arguments.size());
        for (const Argument &arg : precond.arguments) {
            assert(arg.constant);
            tuple.push_back(arg.index);  // Index of a constant is the obj index
        }
        const auto& tuples_in_relation = state.get_tuples_of_relation(index);
        const auto& it_end_tuples_in_relation = tuples_in_relation.end();
        const auto& static_tuples = get_tuples_from_static_relation(index);
        const auto& it_end_static_tuples = static_tuples.end();
        if (!tuples_in_relation.empty()) {
            if (precond.negated) {
                if (tuples_in_relation.find(tuple) != it_end_tuples_in_relation)
                    return false;
            }
            else {
                if (tuples_in_relation.find(tuple) == it_end_tuples_in_relation)
                    return false;
            }
        }
        else if (!static_tuples.empty()) {
            if (precond.negated) {
                if (static_tuples.find(tuple) != it_end_static_tuples)
                    return false;
            }
            else {
                if (static_tuples.find(tuple) == it_end_static_tuples)
                    return false;
            }
        }
        else {
            return false;
        }
    }
    return true;
}
const std::unordered_set<GroundAtom, TupleHash> &
SuccessorGenerator::get_tuples_from_static_relation(size_t i) const
{
    return static_information.get_tuples_of_relation(i);
}
