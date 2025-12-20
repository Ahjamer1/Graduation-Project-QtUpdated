#include "helperFunctions.h"

pair<double, double> chooseProbabilityToGeneratePKT() {
    std::mt19937_64 randEngine(seed);

    // Define the discrete possible values
    vector<double> range1 = {0.1, 0.2, 0.3};
    vector<double> range2 = {0.6, 0.7, 0.8};

    // vector<double> range1 = {0, 0, 0};
    // vector<double> range2 = {0, 0, 0};

    uniform_int_distribution<> dist1(0, range1.size() - 1);
    uniform_int_distribution<> dist2(0, range2.size() - 1);

    double num1 = range1[dist1(randEngine)];
    double num2 = range2[dist2(randEngine)];

    return {num1, num2};
}

vector<unsigned int> selectRandomValues(vector<unsigned int>& values, int numSelections) {
    if (values.size() < numSelections) {
        throw invalid_argument("Not enough elements in the input vector.");
    }

    vector<unsigned int> selected(numSelections);
    sample(values.begin(), values.end(), selected.begin(), numSelections, randEngine);
    if (values.size() == numSelections)
    {  // When selecting all elements, just shuffle
        selected = values;
        shuffle(selected.begin(), selected.end(), randEngine);
    }
    return selected;
}

// Function to choose a single random value between 0 and max_number (FOR SHIFT CHOOSING)
unsigned int selectRandomValue(unsigned int max_number) {
    // uniform_int_distribution ensures every number has an EQUAL probability
    // The range is [0, max_number] (inclusive)
    uniform_int_distribution<unsigned int> distribution(0, max_number);

    return distribution(randEngine);
}


void printVector(vector<unsigned int> vec, string msg) {
    cout << msg << endl;
    for (int i = 0; i < vec.size(); i++) {
        cout << vec[i] << ", ";
    }
    cout << endl;
}

void printVector(vector<double> vec, string msg) {
    cout << msg << endl;
    for (int i = 0; i < vec.size(); i++) {
        cout << vec[i] << ", ";
    }
    cout << endl;
}


void printDeQueue(deque<unsigned int> q) {
    while (!q.empty()) {
        cout << q.front() << " ";
        q.pop_front();
    }
    cout << endl;
}

// Bernoulli Chance
bool randomCoinFlipper(double probability){

    const double p = probability;
    bernoulli_distribution flip {p}; //probability% chance to succeed
    return flip(randEngine); //if(flip(randEngine) === true return true else return false);
}


