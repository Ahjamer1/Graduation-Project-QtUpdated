#ifndef HELPERFUNCTIONS_H
#define HELPERFUNCTIONS_H

#include <vector>
#include <deque>
#include <utility>
#include <random>
#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <string>

using namespace std;

// Random number globals
extern unsigned int seed;
extern std::mt19937_64 randEngine;

// Helper function declarations
pair<double, double> chooseProbabilityToGeneratePKT();
vector<unsigned int> selectRandomValues(vector<unsigned int>& values, int numSelections);

void printVector(vector<unsigned int> vec, string msg);
void printVector(vector<double> vec, string msg);
bool randomCoinFlipper(double probability);
class Band;   // forward declaration for PUInitMarkov
class Packet; // forward declaration for printQueue
void printDeQueue(deque<unsigned int> q);

#endif


