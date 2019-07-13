#ifndef SEARCH_HEURISTIC_FACTORY_H
#define SEARCH_HEURISTIC_FACTORY_H

#include <iostream>

#include <boost/algorithm/string.hpp>

#include "heuristic.h"
#include "blind_heuristic.h"
#include "goalcount.h"

class HeuristicFactory {
public:
    static Heuristic *new_heuristic(const std::string& method) {
        std::cout << "Creating search factory..." << std::endl;
        if (boost::iequals(method, "blind")) {
            return new BlindHeuristic;
        }
        else if (boost::iequals(method, "goalcount")) {
            return new Goalcount;
        }
        else {
            return nullptr;
        }
    }
};

#endif //SEARCH_HEURISTIC_FACTORY_H