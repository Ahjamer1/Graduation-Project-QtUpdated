#include<random>
#include <algorithm>
#include <string>
#include <fstream>
#include <cmath>
#include <deque>
#include <iomanip>
#include <queue>
#include <filesystem>
#include <utility>
#include "helperFunctions.h"
using namespace std;
// parameters
const int numberOfSU = 20;
const double numberOfBands = 40;
// vector <double> numofBand = {5,10,25};
const int numberOfPU = numberOfBands;
const double numberOfTimeSlots = 5000;
const double durationOfTimeSlot = 0.01;
const int numberOfBandsPerSU = 1;
vector <double> PuActiveProb={0.2,0.4,0.5,0.6};
double probOffToOn = 0.01;//never change this
unsigned int seed = 123;
std::mt19937_64 randEngine(seed);
int checkPeriod = 20;
vector <unsigned int> StartingPositions;        //To choose random starting positions , change later for 100 bands
int collisionCounterHistoryPerSUSize = 10;
int SensedBandsSUPerspectiveHistorySize = checkPeriod;
int PuBehaviorHistorySize= checkPeriod;
const int offtime=5;
double DutyCycleDeterministic=0.4;
double MaxQueueSize = 50;
double PossiblePacketDroppedSize=20;
double DecayingTime=100;
double BandTrustFactor=10;
int ChooseTopKRandomly=2;
double utilizedBands=0;
class Parameters {
public:
    vector <double> AvgPerTimeSlot;
    vector <double> AvgPacketWaitingTime;
    vector <double> AvgPerBand;
    vector <double> AvgPerSU;
    Parameters() : AvgPerTimeSlot(numberOfTimeSlots,0),AvgPacketWaitingTime(numberOfSU,0),AvgPerBand(numberOfBands,0),AvgPerSU(numberOfSU,0){}
};
class Band {
public:
    bool PUState = 0;
    //this vector shows whether this band was used to transmit pkts or not
    deque <unsigned int> PuBehaviorHistory;
    double Weight;
    Band ():PuBehaviorHistory(PuBehaviorHistorySize,0){}
};
class Packet{
public:
    int pktGenerationTime;
    int pktWaitingTimeInQueue; // pktWaitingTimeInQueue = arrivalTime - generationTime
    int numberOfTriesToSend = 0;
    int pktArrivalTime;
    // int AckSequence; // we will assume that the SU receives an acknowledgment singal saying that the message is delivered
    // we will model this in the simulator by checking if the SU is alone on the band or not
};
class SecondaryUser{
public:

    bool AllowedToTransmit=false;
    // This is a deque of the vector called "bandsAsSeenBySU", which shows how SU sees all bands including his band.
    deque<vector<unsigned int>> SensedBandsSUPerspectiveHistory;
    //MURAD
    // deque <vector<unsigned int>> ExperiencedBands;

    // This deque shows the values of collision as 1 / no collision as zero experineced by each SU every 10 time slots
    // to calculate collisions according to this vector later
    deque <unsigned int> collisionCounterHistoryPerSU;

    vector<unsigned int> potentialBands;

    //Function(PUState) => fill potentialBands

    int urgency; // one of possible three values: delay-intolerant / delay tolerant uninterruptable (will lower datarate) / delay tolerant interruptable
    // Urgency possible values:
    // 0: Delay intolerant "URGENT"
    // 1: Delay tolerant uninterruptable "CAMERA"
    // 2: Delay tolerant interruptable "BEST EFFORT"
    int dataRateClass; // one of possible two values: realtime traffic (high data rate) / low dataRate
    // Urgency possible values:
    // 0: low data rate
    // 1: High data rate
    double NumOfPacketsDropped=0;
    double NumOfPacketsGenerated=0;
    double NumOfPacketsSent=0;
    int numberOfTimesDecreasedTXRATE = 0;
    int shift = 0;
    // {1, 2, 4, 6, 10, 12, 16, 18, 22};
    vector <unsigned int> periodsForBulky = {0,1,4,6};
    // periodsForBulky generates a packet every 1, 2, 5, 10, 15 (0,1,4,9,14 are set for counting purposes)

    // CHANGED: Fixed values to align with periodsForBulky (Shift by 1 fix)
    vector <unsigned int> TxRates = {0,1,4,6}; // the value "0" means "1" which means: every time slot(fastest) / the value "0" means "2" which means: ON OFF ON OFF
    // TxRates transmits a packet every 1, 2, 5, 10, 15 (0,1,4,9,19 are set for counting purposes)

    // CHANGED: Added indices to track current Quality Level
    int currentTxIndex = 0;
    int currentGenIndex = 0;

    int selectedBand=-1;
    vector <Packet> sentPackets;
    // Possible combinations of urgency and dataRateClass:
    // 1. 00 => summation = 0
    // 2. 11 => summation = 2
    // 3. 21 => summation  = 3
    deque <Packet> pktqueue;  // instantiate an empty queue of integers

    double pktGenerationRate;
    void fillpktGenerationRate(){
        if(this->urgency ==0 && this->dataRateClass == 0){ // alarm
            pktGenerationRate = chooseProbabilityToGeneratePKT().first;                           //geometric with low probability
        }else if(this->urgency ==1 && this->dataRateClass == 1){ // camera
            pktGenerationRate = -1;                                                             // periodic (to be chosenPeriod in each timeslot according to feedback)
        } else if(this->urgency ==2 && this->dataRateClass == 1){                             // best effort (internet traffic)
            pktGenerationRate = chooseProbabilityToGeneratePKT().second;                     //geometric with higher probability
        }
    }
    void generatePkt(unsigned int t){
        Packet pkt;
        pkt.pktGenerationTime = t;
        // ADDED FIXED SIZE QUEUE FOR PACKET DROPPING AFTER CERTAIN TIME WITHOUT TX.
        if( this->urgency ==1 && this->dataRateClass == 1 || this->urgency ==2 && this->dataRateClass == 1){
            if (this->pktqueue.size()< MaxQueueSize)
            {
                this->pktqueue.push_back(pkt);
                this->PossiblePacketsDropped.push_back(0);
            }


            else
            {
                this->pktqueue.push_back(pkt);
                this->pktqueue.pop_front();
                this->NumOfPacketsDropped++;
                this->PossiblePacketsDropped.push_back(1);

            }
            this->PossiblePacketsDropped.pop_front();
        }else{ // Urgent
            this->pktqueue.push_back(pkt);
        }
    }

    //**********************************************************
    //************* Handling GenerationRate Change *********
    //**********************************************************
    int counterPeriod=0; // CHANGED: Initialize to 0 so it uses index 0 logic immediately
    // int chosenPeriod =0; // REMOVED: Replaced by currentGenIndex

    // CHANGED: Helper to get the actual value for counters
    int getGenCounterVal() { return periodsForBulky[currentGenIndex]; }

    //**********************************************************
    //**********************************************************


    //**********************************************************
    //***************** Handling TXRate Change *************
    //**********************************************************
    int counterTxRate= 0; // is the value chosenPeriod from periodsForBulky;
    // int chosenTxRate = 0; // REMOVED: Replaced by currentTxIndex

    // CHANGED: Helper to get the actual value for counters
    int getTxCounterVal() { return TxRates[currentTxIndex]; }

    // CHANGED: NEW UNIFIED FUNCTION
    void adaptQuality(string actionToTake){    //actionToTake : "increaseQuality" || "decreaseQuality"

        int newTxIndex = currentTxIndex;
        vector <unsigned int> RatesToChooseFrom;

        // 1. DETERMINE NEW TX INDEX (Random jump logic)
        if(actionToTake == "increaseQuality" || actionToTake == "increaseTxRate"){
            if(newTxIndex > 0){
                for(int i=0; i< newTxIndex; i++){
                    RatesToChooseFrom.push_back(i);
                }
                if(RatesToChooseFrom.size() >0){
                    newTxIndex = selectRandomValues(RatesToChooseFrom,1)[0];
                }
            }
            // cout<< endl<< "INCREASED TXRATE"<< endl<< endl;
        }else if(actionToTake == "decreaseQuality" || actionToTake == "decreaseTxRate"){
            if(newTxIndex < this->TxRates.size()-1){
                for(int i=newTxIndex + 1; i< this->TxRates.size(); i++){ // changed limit to size()
                    RatesToChooseFrom.push_back(i);
                }
                if(RatesToChooseFrom.size()>0){
                    newTxIndex = selectRandomValues(RatesToChooseFrom,1)[0];
                }
            }
            // cout<< endl<< "DECREASED TXRATE"<< endl<< endl;
        }

        // 2. APPLY LOGIC BASED ON URGENCY (The Split)
        this->currentTxIndex = newTxIndex;

        if(this->urgency == 1){ // CAMERA: Lock Gen rate to Tx Rate
            this->currentGenIndex = newTxIndex;
        }
        // If BEST EFFORT (urgency 2): Do not touch currentGenIndex
    }

    //**********************************************************
    //**********************************************************

    vector <double> BandsRankingSeenByEachSu;


    //**********************************************************
    //******************* SUSensingBands *******************
    //**********************************************************
    vector <unsigned int> bandsAsSeenBySU;
    void fillbandsAsSeenBySU(vector<unsigned int> TXFREQARRAY){
        for (int i=0; i< TXFREQARRAY.size(); i++){
            if(i != this->selectedBand){
                if(TXFREQARRAY[i] >0){
                    this->bandsAsSeenBySU[i] = 1;
                }else{
                    this->bandsAsSeenBySU[i] = 0;
                }
            }else{ // it is the selectedBand by the SU
                if(this->counterTxRate >0){ // SU is not transmitting its packets yet
                    if(TXFREQARRAY[i] >0){
                        this->bandsAsSeenBySU[i] = 1;
                    }else{
                        this->bandsAsSeenBySU[i] = 0;
                    }
                }else if(this->counterTxRate ==0){ // SU is transmitting its packets
                    if(TXFREQARRAY[i] >1){
                        this->bandsAsSeenBySU[i] = 1;
                    }else{
                        this->bandsAsSeenBySU[i] = 0;
                    }
                }
            }
        }
        this->SensedBandsSUPerspectiveHistory.pop_front();

        this->SensedBandsSUPerspectiveHistory.push_back(bandsAsSeenBySU);

        // for(int i=0; i< this->SensedBandsSUPerspectiveHistory.size(); i++){
        //      string s =  "SU[" + to_string(i) + "] " + "SensedBandsSUPerspectiveHistory: ";
        //      printVector(SensedBandsSUPerspectiveHistory[i],s);
        // }
    }
    //**********************************************************
    //**********************************************************

    vector <double> weights;
    deque <unsigned int> PossiblePacketsDropped;
    //Su Specific Performance parameters//
    double CollisionsWeight;
    double QueueSizeWeight;
    double NumOfPacketsDroppedWeight;
    double AvgPacketWaitingTimeWeight;
    double RelinquishingTendency;
    vector <double> BandsExperienceHistory;


    SecondaryUser():
        collisionCounterHistoryPerSU(collisionCounterHistoryPerSUSize, 0),
        bandsAsSeenBySU(numberOfBands, 0),
        SensedBandsSUPerspectiveHistory(SensedBandsSUPerspectiveHistorySize, vector<unsigned int>(numberOfBands, 0)),
        weights(numberOfBands, 0),BandsRankingSeenByEachSu(numberOfBands,0),PossiblePacketsDropped(PossiblePacketDroppedSize,0),
        BandsExperienceHistory(numberOfBands,50)

    {
        // constructor body (optional)
    }
};
vector <Band> PU(numberOfBands);
vector <SecondaryUser> SU(numberOfSU);

//**********************************************************
void printQueue(vector<Packet> q) {
    for(int i=0; i< q.size(); i++){
        Packet p = q[i];
        cout<< "GenTime: " <<p.pktGenerationTime << endl;
        cout<< "WaitingTime: "<< p.pktWaitingTimeInQueue<< endl;
        cout<< "Arrival: "<< p.pktArrivalTime << endl;
    }
}
void printOnePacket(Packet q){
    Packet p = q;
    cout<< "GenTime: " <<p.pktGenerationTime << endl;
    cout<< "WaitingTime: "<< p.pktWaitingTimeInQueue<< endl;
    cout<< "Arrival: "<< p.pktArrivalTime << endl;
}
void PUInitMarkov (vector<Band>& PU)
{
    for(int i=0; i< PU.size(); i++){


        if(PU[i].PUState == false)
        {
            //flip a coin to choose between P01 and P00
            PU[i].PUState = randomCoinFlipper(0.01);
        }
        else if(PU[i].PUState == true)
        {
            //flip a coin to choose between P10 and P11
            PU[i].PUState = !(randomCoinFlipper(0.01));
        }
    }
}
void updateBandScore(SecondaryUser &su, int bandIndex, bool success) {
    double alpha = 0.2; // Learning rate (0.2 means we value new info by 20%)
    double reward = success ? 100.0 : 0.0;

    // EMA Formula: NewScore = (1 - alpha) * OldScore + alpha * Reward
    su.BandsExperienceHistory[bandIndex] =
        (1.0 - alpha) * su.BandsExperienceHistory[bandIndex] + (alpha * reward);
}
vector<unsigned int> AssignStartingPositions(int size, int lower, int upper) {

    uniform_int_distribution<int> dist(lower, upper);

    vector<unsigned int> values(size);
    for (int i = 0; i < size; ++i)
        values[i] = dist(randEngine);

    return values;
}

int PUInitDeterministic (vector<Band>& PU,int time,double DC)
{
    int ActiveTime=offtime*((DC/(1-DC)));
    int counter=0;
    cout<< "PUState: "<< endl;
    for (int i=0;i<PU.size();i++)
    { if  ((((time-StartingPositions[i])%(ActiveTime+offtime)<ActiveTime)&&time>=StartingPositions[i]))//variable for 10
        {
            PU[i].PUState=true;
            // counter++;
        }
        else{
            PU[i].PUState=false;
            counter++;
        }
       cout<<PU[i].PUState<<", ";
        PU[i].PuBehaviorHistory.pop_front();
        PU[i].PuBehaviorHistory.push_back(PU[i].PUState);
        double c=0;
        for (int j=0;j<PuBehaviorHistorySize;j++)
        {
            if (PU[i].PuBehaviorHistory[j]==0)
                c++;


        }
        PU[i].Weight=c/PuBehaviorHistorySize;
    }
    cout<< endl;



    return counter;
}
//**********************************************************
//***************** Performance Parameters *************
//**********************************************************
void CollisionCounter (int time,vector <double> &AvgPerTimeSlot,vector <unsigned int> &TXFreqArray)
{
    int counter=0;
    for (int i=0;i<TXFreqArray.size();i++)
    {
        if (TXFreqArray[i]>1)
            counter++;

    }
    AvgPerTimeSlot[time]=counter/numberOfBands;

}
void TotalPacketsCounter (int time,vector<SecondaryUser>& SU,vector <double> &AvgPerTimeSlot)
{
    int counter=0;
    for (int i=0;i<SU.size();i++)
    {
        counter=counter+SU[i].pktqueue.size();
    }
    AvgPerTimeSlot[time]=counter;
}
void WaitingTimeCalculator (vector <SecondaryUser> SU,vector <double> &AvgPacketWaitingTime)
{
    // cout<<SU[0].sentPackets.front().pktWaitingTimeInQueue;
    for (int i=0;i<SU.size();i++)
    {

        if (SU[i].sentPackets.size()>0)
        {
            double numofpkts=SU[i].sentPackets.size();
            double counter=0;
            for (int k=0;k<SU[i].sentPackets.size();k++)
            {
                counter=counter+SU[i].sentPackets[k].pktWaitingTimeInQueue;
            }
            AvgPacketWaitingTime[i]=counter/numofpkts;
        }
        else
            AvgPacketWaitingTime[i]=-1;


    }
}
void ThroughPutCalculator (int time,vector <unsigned int> &TXFreqArray,vector <double> &AvgPerTimeSlot,vector <double> &AvgPerBand)
{
    int counter=0;
    for (int i=0;i<TXFreqArray.size();i++)
    {
        if (TXFreqArray[i]==1)
        {
            AvgPerBand[i]=AvgPerBand[i]+1/numberOfTimeSlots;
            counter++;

        }
    }
    AvgPerTimeSlot[time]=counter/numberOfBands;
}
void UtilizationCalculator (int time,vector <unsigned int> &TXFreqArray,vector <double> &AvgPerTimeSlot,vector <double> &AvgPerBand)
{
    int counter=0;
    for (int i=0;i<TXFreqArray.size();i++)
    {
        if (TXFreqArray[i]>=1)
        {
            AvgPerBand[i]=AvgPerBand[i]+1/numberOfTimeSlots;
            counter++;
        }
    }
    AvgPerTimeSlot[time]=counter/numberOfBands;
}
void NumberOfPacketsDroppedCalculator(vector<SecondaryUser>& SU,vector <double>&AvgPerSU)
{
    for (int i=0;i<SU.size();i++)
    {
        AvgPerSU[i]=SU[i].NumOfPacketsDropped;
    }
}
void FairnessCalculator(vector<SecondaryUser>& SU,vector <double> &AvgPerSU)
{
    for (int i=0;i<SU.size();i++)
    {
        AvgPerSU[i]=SU[i].NumOfPacketsSent/SU[i].NumOfPacketsGenerated;
    }
}
//**********************************************************
//**********************************************************


void initializeSystem() {
    //initialize SU properties
    //Assume 10% of SU's are urgent
    //Assume 50% of SU's are bulky uninterruptable
    //Assume 50% of SU's are bulky interruptable

    //TO BE RANDOMIZED LATER ON
    for(int i=0; i <SU.size()*.1; i++){
        SU[i].urgency = 0;
        SU[i].dataRateClass = 0;
        SU[i].fillpktGenerationRate();
    }
    for(int i=SU.size()/10; i <SU.size()/2; i++){
        SU[i].urgency = 1;
        SU[i].dataRateClass = 1;
        SU[i].fillpktGenerationRate();
    }
    for(int i=SU.size()/2; i <SU.size(); i++){
        SU[i].urgency = 2;
        SU[i].dataRateClass = 1;
        SU[i].fillpktGenerationRate();
    }
}
void generatePKTS(vector <SecondaryUser> &SU, int t){
    for(int i=0; i< SU.size(); i++){
        // If a packet has been waiting too long, it "expires" and is dropped. This simulates latency sensitivity.
        if(SU[i].urgency == 1 && SU[i].dataRateClass == 1){ // CAMERA
            while(!SU[i].pktqueue.empty()){
                // Check the packet at the front (oldest)
                int waitingTime = t - SU[i].pktqueue.front().pktGenerationTime;

                // If waiting longer than allowed (DecayingTime), drop it
                if(waitingTime > DecayingTime){
                    SU[i].pktqueue.pop_front();
                    SU[i].NumOfPacketsDropped++;

                    // Track drop statistics (for your weighted param calculation)
                    SU[i].PossiblePacketsDropped.pop_front();
                    SU[i].PossiblePacketsDropped.push_back(1);
                    // cout<< "PKT DROPPED!!!!!"<< endl;
                } else {
                    // If the oldest packet is fine, the rest are fine too (ordered by time)
                    break;
                }
            }
        }

        // ***************************************************************
        // CHECK IF NEED TO GENERATE
        bool shouldGenerate = false;

        // Check Generation Rate first
        if(SU[i].pktGenerationRate == -1){ //Camera
            if(SU[i].counterPeriod > 0){
                SU[i].counterPeriod--;
            }else if(SU[i].counterPeriod== 0){
                shouldGenerate = true;
                SU[i].counterPeriod= SU[i].getGenCounterVal();
            }
        } else { // Urgent/Best Effort
            if(randomCoinFlipper(SU[i].pktGenerationRate)){
                shouldGenerate = true;
            }
        }
        if(shouldGenerate){
            SU[i].generatePkt(t);
            SU[i].NumOfPacketsGenerated++;
            // }
        }
    }
}

//************** Sub Allocation Funcions ****************//
void RankBandsAndChoooseTopK (vector<SecondaryUser>&SU,vector <unsigned int> &possibleBands,int SUIndex)
{
    vector <pair<double, int>> Ranks(possibleBands.size(), {0.0, 0});
    for (int i=0;i<possibleBands.size();i++)
    {
        Ranks[i].first=(SU[SUIndex].BandsExperienceHistory[possibleBands[i]]/100.0+PU[possibleBands[i]].Weight+SU[SUIndex].weights[possibleBands[i]])/3;
        Ranks[i].second=possibleBands[i];
    }

    sort(Ranks.begin(), Ranks.end(),
         [](const pair<double, int>& a, const pair<double, int>& b) {
             return a.first > b.first;
         });


    int topK = min((int)Ranks.size(), ChooseTopKRandomly);

    int chosenIndex = std::uniform_int_distribution<int>(0, topK-1)(randEngine);
    SU[SUIndex].selectedBand=Ranks[chosenIndex].second;
    cout<<"SU["<< SUIndex<< "].selectedBand = "<< SU[SUIndex].selectedBand<< endl;
    cout<< "SU["<< SUIndex<< "].queueSize= "<< SU[SUIndex].pktqueue.size()<< endl;


}
void chooseTxRateAfterAcquiring (int SuIndex)
{
    double availability = SU[SuIndex].weights[SU[SuIndex].selectedBand];

    if(availability <= 0.05)
        availability = 0.05;

    double minPeriod = 1.0 / availability; //
    double minWaitTime = minPeriod - 1.0;

    if(minWaitTime < 0) minWaitTime = 0; // Sanity check

    // 5. FILTER VALID TX RATES
    // Find all indices in TxRates that satisfy: TxRate >= minWaitTime
    vector<int> validTxIndices;
    for(int r = 0; r < SU[SuIndex].TxRates.size(); r++) {
        if(SU[SuIndex].TxRates[r] >= minWaitTime) {
            validTxIndices.push_back(r);
        }
    }
    if(validTxIndices.empty() || availability ==0) {
        validTxIndices.push_back(SU[SuIndex].TxRates.size() - 1);
    }
    int randomRateIdx = std::uniform_int_distribution<int>(0, validTxIndices.size() - 1)(randEngine);
    SU[SuIndex].currentTxIndex = validTxIndices[randomRateIdx];
    // Calculate the actual wait time chosen
    int chosenWaitTime = SU[SuIndex].TxRates[SU[SuIndex].currentTxIndex];

    // Assign random shift to desynchronize
    if(chosenWaitTime > 0){
        SU[SuIndex].shift = std::uniform_int_distribution<int>(0, chosenWaitTime)(randEngine);
    } else {
        SU[SuIndex].shift = 0;
    }

    // Reset the Tx Counter immediately based on new decision
    SU[SuIndex].counterTxRate = SU[SuIndex].getTxCounterVal();


}
void AssignBands (vector<SecondaryUser>&SU,vector <unsigned int> &possibleBands)
{
    for (int i=0;i<SU.size();i++)
    {
        if (SU[i].pktqueue.size() ==0)
        {
            continue;
        }
        else
        {
            if(possibleBands.size() >0)
            {
                if(SU[i].selectedBand==-1)
                {
                    RankBandsAndChoooseTopK (SU,possibleBands,i);
                    chooseTxRateAfterAcquiring(i);

                }

            }
        }
    }
}
void CheckTxPeriod  (vector<SecondaryUser>&SU,vector <unsigned int> &TXFreqArray)
{
    for (int i=0;i<SU.size();i++)
    {
        if (SU[i].selectedBand==-1)
            continue;
        else
        {

            if(SU[i].shift > 0){
                SU[i].shift--;
                if(SU[i].shift == 0){
                    if(SU[i].pktqueue.size()>0){
                    SU[i].counterTxRate = 0;
                    TXFreqArray[SU[i].selectedBand] += 1;
                    SU[i].AllowedToTransmit=true;
                    }
                }
            }
            // 2. Handle Periodic Transmission (Only if shift is done)
            else if(SU[i].shift == 0){
                // If the counter is still running, decrement it
                if(SU[i].counterTxRate > 0){
                    SU[i].counterTxRate--;
                }

                // CHANGE: Check for 0 separately.
                // This covers two cases:
                // A) We just decremented from 1 to 0 (Periodic transmission).
                // B) We forced it to 0 in the 'shift' block above (First transmission after shift).
                if(SU[i].counterTxRate == 0){
                    if(SU[i].pktqueue.size()>0){
                        TXFreqArray[SU[i].selectedBand] += 1;
                        SU[i].AllowedToTransmit=true;
                    }
                }
            }

        }
    }
}
void AttemptTransmission ( vector<SecondaryUser>&SU,vector <unsigned int> &TXFreqArray,int t)
{
    for(int i=0; i< SU.size(); i++)
    {
        SU[i].fillbandsAsSeenBySU(TXFreqArray);

        if(SU[i].selectedBand !=-1&&SU[i].pktqueue.size()>0)
        {
            if(TXFreqArray[SU[i].selectedBand] > 1 &&SU[i].AllowedToTransmit==true)
            {

                SU[i].collisionCounterHistoryPerSU.pop_front();
                SU[i].collisionCounterHistoryPerSU.push_back(1);
                SU[i].counterTxRate= SU[i].getTxCounterVal();
                updateBandScore(SU[i], SU[i].selectedBand, false); // false = collision (0 reward)
                // 1. Force a random delay (Backoff) to desynchronize
                // Randomly wait between 2 to 10 slots before trying again
                // int backoff = std::uniform_int_distribution<int>(2, 5)(randEngine);
                int backoff = 1;
                if(SU[i].pktqueue.size() > MaxQueueSize * 0.8) {
                    backoff = 1;
                    // SU[i].adaptQuality("increaseQuality");
                } else {
                    backoff = std::uniform_int_distribution<int>(1, 3)(randEngine);
                }
                SU[i].shift = backoff;
                SU[i].AllowedToTransmit=false;

            }
            else if (TXFreqArray[SU[i].selectedBand] == 1 && SU[i].AllowedToTransmit==true)
            {
                updateBandScore(SU[i], SU[i].selectedBand, true);  // true = success (100 reward)
                cout<<"Packet SENT!"<< endl;
                Packet poppedPacket = SU[i].pktqueue.front();
                SU[i].pktqueue.pop_front();
                SU[i].NumOfPacketsSent++;
                poppedPacket.pktArrivalTime = t;
                poppedPacket.pktWaitingTimeInQueue = poppedPacket.pktArrivalTime - poppedPacket.pktGenerationTime;
                SU[i].sentPackets.push_back(poppedPacket);
                // cout<< "PKT SENT ON BAND: "<< SU[i].selectedBand<< ", BY SU: "<< to_string(i)<< endl;
                // printOnePacket(poppedPacket);
                SU[i].collisionCounterHistoryPerSU.pop_front();
                SU[i].collisionCounterHistoryPerSU.push_back(0); //No collision
                // RESET THE COUNTER
                SU[i].counterTxRate= SU[i].getTxCounterVal();
                SU[i].AllowedToTransmit=false;
            }
            else
            {
                SU[i].collisionCounterHistoryPerSU.pop_front();
                SU[i].collisionCounterHistoryPerSU.push_back(0);
            }
        }
        else

        {
            SU[i].collisionCounterHistoryPerSU.pop_front();
            SU[i].collisionCounterHistoryPerSU.push_back(0);
        }
    }
}
//*******************************************************//
vector <unsigned int> allocationFunction(vector <Band> &PU, vector<SecondaryUser>&SU, int t)
{
    vector <unsigned int> possibleBands;
    vector <unsigned int> TXFreqArray(PU.size(), 0);
    for(int i=0; i< PU.size(); i++)
    {
        if(PU[i].PUState ==0){
            possibleBands.push_back(i);
        }
    }
    for (int i=0;i<SU.size();i++)
    {
        if (PU[SU[i].selectedBand].PUState==1)
        {
            SU[i].selectedBand=-1;
            SU[i].numberOfTimesDecreasedTXRATE=0;
        }
    }
    AssignBands(SU,possibleBands);
    CheckTxPeriod(SU,TXFreqArray);
    AttemptTransmission(SU,TXFreqArray,t);



    int counter=0;
    for (int i=0;i<TXFreqArray.size();i++)
    {
        if (TXFreqArray[i]>0)
            counter++;
    }
    utilizedBands=utilizedBands+counter;
    return TXFreqArray;


}
//************** SubUpdateParametersFunctions ****************//
void RelinquishingTendency (int t)
{
    for (int i=0;i<SU.size();i++)
    {

        for(int j = 0; j< numberOfBands; j++){
            double numberOfZeros = 0;
            for(int k = 0; k< SU[i].SensedBandsSUPerspectiveHistory.size(); k++){
                if(SU[i].SensedBandsSUPerspectiveHistory[k][j] ==0){

                    numberOfZeros++;
                }
            }
            SU[i].weights[j] = numberOfZeros/ SensedBandsSUPerspectiveHistorySize;
        }

        double counter=0;
        for (int j=0;j<SU[i].collisionCounterHistoryPerSU.size();j++)
        {
            if (SU[i].collisionCounterHistoryPerSU[j]==1)
                counter++;
        }
        int counter2=0;
        for (int k=0;k<SU[i].PossiblePacketsDropped.size();k++)
        {
            if (SU[i].PossiblePacketsDropped[k]==1)
                counter2++;
        }
        double counter3=0;
        for (int x=0;x<SU[i].pktqueue.size();x++)

        {
            counter3=counter3+min(t-SU[i].pktqueue[x].pktGenerationTime,int(MaxQueueSize));
        }
        SU[i].CollisionsWeight=counter/collisionCounterHistoryPerSUSize; //gives a higher weight if the su suffered more collision
        SU[i].QueueSizeWeight=SU[i].pktqueue.size()/MaxQueueSize;//Higher weight if the SU has more packets in the queue
        SU[i].NumOfPacketsDroppedWeight=counter2/PossiblePacketDroppedSize;//higher percentage of dropped packets gives higher weight,however we might have to make it more dynamic instead of accoutning for every single packet
        if(SU[i].pktqueue.size()>0){
            SU[i].AvgPacketWaitingTimeWeight=counter3/(MaxQueueSize*SU[i].pktqueue.size());
        }else{
            SU[i].AvgPacketWaitingTimeWeight=0;
        }
        SU[i].RelinquishingTendency=(SU[i].CollisionsWeight+SU[i].QueueSizeWeight+SU[i].NumOfPacketsDroppedWeight + SU[i].AvgPacketWaitingTimeWeight)/4;

    }
}
void SuBandExperienceUpdate ()
{
    double decayRate = 0.99;
    double neutralVal = 50;
    for (int i=0;i<SU.size();i++)
    {
        for (int k=0;k<numberOfBands;k++)
        {
            // 1. We skip the currently selected band (it gets updated by updateBandScore, not decay).
            // 2. We skip bands that are already at neutralVal (50).
            //    Since we fixed the constructor to init at 50, unvisited bands will hit this 'continue'
            //    and will NOT be touched.

            if(k == SU[i].selectedBand) {
                continue;
            }

            // Check if it is already 50 (or extremely close to it)
            if(abs(SU[i].BandsExperienceHistory[k] - neutralVal) < 0.001) {
                continue; // It is already neutral/unvisited, leave it alone.
            }

            // Apply Decay towards Neutral
            // If History is 100 -> moves to 99.5
            // If History is 0   -> moves to 0.5
            SU[i].BandsExperienceHistory[k] = (SU[i].BandsExperienceHistory[k] * decayRate) + (neutralVal * (1.0 - decayRate));
        }


    }

}
void candidateBandsWeights(){
    vector <double> weights;
    for(int i=0; i< SU.size(); i++){
        for(int j = 0; j< numberOfBands; j++){
            double numberOfZeros = 0;
            for(int k = 0; k< SU[i].SensedBandsSUPerspectiveHistory.size(); k++){
                if(SU[i].SensedBandsSUPerspectiveHistory[k][j] ==0){

                    numberOfZeros++;
                }
            }
            SU[i].weights[j] = numberOfZeros/ SensedBandsSUPerspectiveHistorySize;
        }
    }
}
//***********************************************************//
void UpdateParameters (int time)
{
    candidateBandsWeights();
    RelinquishingTendency(time);
    SuBandExperienceUpdate();

}
//************** Sub StayOrRelinquish Functions ****************//
void UrgentCheck(int SuIndex)
{
    if(SU[SuIndex].urgency == 0)
    {
        if (SU[SuIndex].RelinquishingTendency>0.4)
            SU[SuIndex].selectedBand=-1;
    }
}
void CameraCheck(int SuIndex)
{
    if(SU[SuIndex].dataRateClass == 1&&SU[SuIndex].urgency==1)
    {
        if (SU[SuIndex].RelinquishingTendency<0.4)
        {
            SU[SuIndex].adaptQuality("increaseQuality");
            SU[SuIndex].shift = selectRandomValue(SU[SuIndex].getTxCounterVal());
        }
        else
        {
            if (SU[SuIndex].numberOfTimesDecreasedTXRATE>3)
            {
                SU[SuIndex].selectedBand=-1;
                SU[SuIndex].numberOfTimesDecreasedTXRATE=0;
            }
            else
            {
                SU[SuIndex].adaptQuality("decreaseQuality");
                SU[SuIndex].numberOfTimesDecreasedTXRATE++;
                SU[SuIndex].shift = selectRandomValue(SU[SuIndex].getTxCounterVal());
            }

        }
    }
}
void BestEffortCheck(int SuIndex)
{
    if(SU[SuIndex].dataRateClass == 1&&SU[SuIndex].urgency==2)
    {
        if (SU[SuIndex].RelinquishingTendency<0.4)
        {
            SU[SuIndex].adaptQuality("increaseQuality");
            SU[SuIndex].shift = selectRandomValue(SU[SuIndex].getTxCounterVal());
        }
        else
        {
            if (SU[SuIndex].numberOfTimesDecreasedTXRATE>2)
            {
                SU[SuIndex].selectedBand=-1;
                SU[SuIndex].numberOfTimesDecreasedTXRATE=0;
            }
            else
            {
                SU[SuIndex].adaptQuality("decreaseQuality");
                SU[SuIndex].numberOfTimesDecreasedTXRATE++;
                SU[SuIndex].shift = selectRandomValue(SU[SuIndex].getTxCounterVal());
            }

        }
    }
}
//**************************************************************//
void StayOrRelinquish (int time)
{
    for (int i=0;i<SU.size();i++)
    {
        if (time%3==0)
        {
            UrgentCheck(i);
        }
        if (time%10==0)
        {
            CameraCheck(i);
            BestEffortCheck(i);
        }

    }
}




int main(){
    Parameters Collisions;
    Parameters TotalPackets;
    Parameters WaitingTime;
    Parameters Utilization;
    Parameters Throughput;
    Parameters NumberofPacketsDropped;
    Parameters Fairness;
    Parameters RelinquishingTendencyUrgent;
    Parameters RelinquishingTendencyCamera;
    Parameters RelinquishingTendencyBestEffort;

    StartingPositions=AssignStartingPositions(numberOfBands,1,numberOfBands);
    initializeSystem();
    double availableTimeSlots = 0;
    vector <double> UrgentThroughputsOverTime;

    vector <double> CameraThroughputsOverTime;

    vector <double> BestEffortThroughputsOverTime;
    vector <double> utilizationPerTimeSlot;
    vector <vector<double>> TXPeriodsOverTimeSlots(4, vector<double>(numberOfTimeSlots, 0));

    for(int t=0; t< numberOfTimeSlots; t++)
    { //TIMESLOTS LOOP
        cout<< "time slot: "<< t<< endl;
        availableTimeSlots += PUInitDeterministic(PU,t,DutyCycleDeterministic);
        generatePKTS(SU, t);
        vector <unsigned int> TXFreqArray= allocationFunction(PU, SU,t);
        double number = 0;
        for(int i=0; i< TXFreqArray.size(); i++){
            if(TXFreqArray[i] > 0){
                number++;
            }
        }
        number /= TXFreqArray.size();
        utilizationPerTimeSlot.push_back(number);
        printVector(TXFreqArray, "TXFreqArray: ");
        UpdateParameters(t);
        StayOrRelinquish(t);

        double totalNnumbersOfPktsGeneratedUrgent = 0;
        double totalNnumbersOfPktsGeneratedCamera= 0;
        double totalNnumbersOfPktsGeneratedBestEffort= 0;
        double totalNnumbersOfPktsSentUrgent = 0;
        double totalNnumbersOfPktsSentCamera = 0;
        double totalNnumbersOfPktsSentBestEffort = 0;
        for(int i=2; i< 4; i++){
            if(SU[i].urgency == 1 && SU[i].dataRateClass ==1){
            TXPeriodsOverTimeSlots[i][t] =  SU[i].TxRates[SU[i].currentTxIndex];
            }
        }

        for (int i=0;i<SU.size();i++)
        {
            if(SU[i].dataRateClass ==0){
                totalNnumbersOfPktsGeneratedUrgent += SU[i].NumOfPacketsGenerated;
                totalNnumbersOfPktsSentUrgent += SU[i].NumOfPacketsSent;
            }
            if(SU[i].dataRateClass ==1 && SU[i].urgency ==1){
                totalNnumbersOfPktsGeneratedCamera += SU[i].NumOfPacketsGenerated;
                totalNnumbersOfPktsSentCamera += SU[i].NumOfPacketsSent;
            }
            if(SU[i].dataRateClass ==1 && SU[i].urgency ==2){
                totalNnumbersOfPktsGeneratedBestEffort += SU[i].NumOfPacketsGenerated;
                totalNnumbersOfPktsSentBestEffort += SU[i].NumOfPacketsSent;
            }
        }
        if(totalNnumbersOfPktsGeneratedUrgent !=0){
            UrgentThroughputsOverTime.push_back(totalNnumbersOfPktsSentUrgent / totalNnumbersOfPktsGeneratedUrgent);

        }else{
            UrgentThroughputsOverTime.push_back(0);

        }

        if(totalNnumbersOfPktsGeneratedCamera !=0){
            CameraThroughputsOverTime.push_back(totalNnumbersOfPktsSentCamera / totalNnumbersOfPktsGeneratedCamera);

        }else{
            UrgentThroughputsOverTime.push_back(0);

        }
        if(totalNnumbersOfPktsGeneratedBestEffort !=0){
            BestEffortThroughputsOverTime.push_back(totalNnumbersOfPktsSentBestEffort / totalNnumbersOfPktsGeneratedBestEffort);
        }else{
            UrgentThroughputsOverTime.push_back(0);

        }


    }
    double num = 0;
    for(int i=0; i< utilizationPerTimeSlot.size(); i++){
        num +=utilizationPerTimeSlot[i];
    }
    cout<< "UTIL: "<< num / utilizationPerTimeSlot.size();
    cout<< "***********************************************"<< endl;
    // printVector(TXPeriodsOverTimeSlots[2], "TXPeriodsOverTimeSlots: ");
    string filename="D:\\ElectricalEngineering\\#0 Graduation project\\The Project\\afterUpdate\\Graduation-Project-QtUpdated\\TXrates1.txt";
    ofstream File1 (filename);
    for (int n=0;n<TXPeriodsOverTimeSlots[2].size();n++)
    {
        File1<<TXPeriodsOverTimeSlots[2][n]<< " ";

    }
    File1.close();
    filename="D:\\ElectricalEngineering\\#0 Graduation project\\The Project\\afterUpdate\\Graduation-Project-QtUpdated\\TXrates2.txt";

    ofstream File2 (filename);
    for (int n=0;n<TXPeriodsOverTimeSlots[3].size();n++)
    {
        File2<<TXPeriodsOverTimeSlots[3][n]<< " ";

    }
    File2.close();

    cout<< "***********************************************"<< endl;
    // printVector(UrgentThroughputsOverTime, "UrgentThroughputsOverTime: ");
    // here
    filename="D:\\ElectricalEngineering\\#0 Graduation project\\The Project\\afterUpdate\\Graduation-Project-QtUpdated\\ThroughputURGENT2.txt";
    ofstream File6 (filename);
    for (int n=0;n<UrgentThroughputsOverTime.size();n++)
    {
        File6<<UrgentThroughputsOverTime[n]<< " ";

    }
    File6.close();
    cout<< "***********************************************"<< endl;
    cout<< "***********************************************"<< endl;
    // printVector(CameraThroughputsOverTime, "CameraThroughputsOverTime: ");
    filename="D:\\ElectricalEngineering\\#0 Graduation project\\The Project\\afterUpdate\\Graduation-Project-QtUpdated\\ThroughputCAMERA2.txt";
    ofstream File7 (filename);
    for (int n=0;n<CameraThroughputsOverTime.size();n++)
    {
        File7<<CameraThroughputsOverTime[n]<< " ";

    }
    File7.close();
    cout<< "***********************************************"<< endl;
    cout<< "***********************************************"<< endl;

    // printVector(BestEffortThroughputsOverTime, "BestEffortThroughputsOverTime: ");
    filename="D:\\ElectricalEngineering\\#0 Graduation project\\The Project\\afterUpdate\\Graduation-Project-QtUpdated\\ThroughputBESTEFFORT2.txt";
    ofstream File8 (filename);
    for (int n=0;n<BestEffortThroughputsOverTime.size();n++)
    {
        File8<<BestEffortThroughputsOverTime[n]<< " ";

    }
    File8.close();

    // printVector(utilizationPerTimeSlot, "utilizationPerTimeSlot: ");

    filename="D:\\ElectricalEngineering\\#0 Graduation project\\The Project\\afterUpdate\\Graduation-Project-QtUpdated\\Utilization.txt";
    ofstream File9 (filename);
    for (int n=0;n<utilizationPerTimeSlot.size();n++)
    {
        File9<<utilizationPerTimeSlot[n]<< " ";

    }
    File9.close();
    cout<< "***********************************************"<< endl;



    double totalNumberOfPktsGenerated = 0;
    double totalNumberOfPktsSent= 0;
    double avgThroughputPerSU = 0;

    double totalNnumbersOfPktsGeneratedUrgent = 0;
    double totalNnumbersOfPktsGeneratedCamera= 0;
    double totalNnumbersOfPktsGeneratedBestEffort= 0;
    double totalNnumbersOfPktsSentUrgent = 0;
    double totalNnumbersOfPktsSentCamera= 0;
    double totalNnumbersOfPktsSentBestEffort= 0;

    for (int i=0;i<SU.size();i++)
    {
        cout<<"SU"<<i<<" PacketsGenerated:"<<SU[i].NumOfPacketsGenerated<<" ";
        totalNumberOfPktsGenerated +=SU[i].NumOfPacketsGenerated;

        if(SU[i].dataRateClass ==0){
            totalNnumbersOfPktsGeneratedUrgent += SU[i].NumOfPacketsGenerated;
            totalNnumbersOfPktsSentUrgent += SU[i].NumOfPacketsSent;
        }
        if(SU[i].dataRateClass ==1 && SU[i].urgency ==1){
            totalNnumbersOfPktsGeneratedCamera += SU[i].NumOfPacketsGenerated;
            totalNnumbersOfPktsSentCamera += SU[i].NumOfPacketsSent;
        }
        if(SU[i].dataRateClass ==1 && SU[i].urgency ==2){
            totalNnumbersOfPktsGeneratedBestEffort += SU[i].NumOfPacketsGenerated;
            totalNnumbersOfPktsSentBestEffort += SU[i].NumOfPacketsSent;
        }
        cout<<" PacketsSent:"<<SU[i].NumOfPacketsSent;
        totalNumberOfPktsSent += SU[i].NumOfPacketsSent;
        avgThroughputPerSU += SU[i].NumOfPacketsSent / SU[i].NumOfPacketsGenerated;
        cout<<" PacketsDropped:"<<SU[i].NumOfPacketsDropped;
        cout<<" Que Size: "<<SU[i].pktqueue.size();
        cout<<" RelinquishingTendency: "<<SU[i].RelinquishingTendency;
        cout<<endl;
    }

    cout<< "***********************************************"<< endl;
    cout<< "totalNnumbersOfPktsGeneratedUrgent: "<< totalNnumbersOfPktsGeneratedUrgent<< endl;
    cout<< "totalNnumbersOfPktsSentUrgent: "<<  totalNnumbersOfPktsSentUrgent<< endl;
    cout<< "ThroughputURGENT: "<< totalNnumbersOfPktsSentUrgent / totalNnumbersOfPktsGeneratedUrgent<< endl;
    cout<< "totalNnumbersOfPktsGeneratedCamera: "<< totalNnumbersOfPktsGeneratedCamera<< endl;
    cout<< "totalNnumbersOfPktsSentCamera: "<<  totalNnumbersOfPktsSentCamera<< endl;
    cout<< "ThroughputCAMERA: "<< totalNnumbersOfPktsSentCamera / totalNnumbersOfPktsGeneratedCamera<< endl;
    cout<< "totalNnumbersOfPktsGeneratedBestEffort: "<< totalNnumbersOfPktsGeneratedBestEffort<< endl;
    cout<< "totalNnumbersOfPktsSentBestEffort: "<<  totalNnumbersOfPktsSentBestEffort<< endl;
    cout<< "ThroughputBESTEFFORT: "<< totalNnumbersOfPktsSentBestEffort / totalNnumbersOfPktsGeneratedBestEffort<< endl;
    cout<< "***********************************************"<< endl;
    cout<< endl<< endl;
    cout<< "totalNumberOfPktsGenerated: "<< totalNumberOfPktsGenerated<< endl;
    cout<< "totalNumberOfPktsSent: "<< totalNumberOfPktsSent<< endl;
    cout<< "Load is: "<< totalNumberOfPktsGenerated / availableTimeSlots<< endl;

    cout<< "avgThroughputPerSU: "<< avgThroughputPerSU/ numberOfSU << endl;
    cout<<"Utilization is: "<<utilizedBands/availableTimeSlots<< endl;


}
