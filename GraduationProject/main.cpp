#include <iostream>
#include <vector>
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
const double numberOfBands = 10;
// vector <double> numofBands ={5,10,25};
const int numberOfPU = numberOfBands;
const double numberOfTimeSlots = 500;
const double durationOfTimeSlot = 0.01;
const int numberOfBandsPerSU = 1;
vector <double> PuActiveProb={0.2,0.4,0.5,0.6};
double probOffToOn = 0.01;//never change this
unsigned int seed = 123;
std::mt19937_64 randEngine(seed);

vector <unsigned int> StartingPositions;        //To choose random starting positions , change later for 100 bands
int collisionCounterHistoryPerSUSize = 10;
int SensedBandsSUPerspectiveHistorySize = 25;
int PuBehaviorHistorySize=10;
const int offtime=5;
double DutyCycleDeterministic=0.5;
double MaxQueueSize = 50;
double PossiblePacketDroppedSize=20;
double DecayingTime=100;
double BandTrustFactor=10;

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

    vector <unsigned int> periodsForBulky = {0,1,4,9,14};
    // periodsForBulky generates a packet every 2, 5, 10 (1,4,9 are set for counting purposes)
    vector <unsigned int> TxRates = {0,2,5,10,15}; // the value "0" means "1" which means: every time slot(fastest) / the value "0" means "2" which means: ON OFF ON OFF
    // TxRates transmits a packet every 1, 2, 5, 10, 20 (0,1,4,9,19 are set for counting purposes)



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
            pktGenerationRate = chooseProbabilityToGeneratePKT().first;                          //geometric with low probability
        }else if(this->urgency ==1 && this->dataRateClass == 1){ // camera
            pktGenerationRate = -1;                                                   // periodic (to be chosenPeriod in each timeslot according to feedback)
        } else if(this->urgency ==2 && this->dataRateClass == 1){                     // best effort (internet traffic)
            pktGenerationRate = chooseProbabilityToGeneratePKT().second;             //geometric with higher probability
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
        }else{
            this->pktqueue.push_back(pkt);
        }
    }

    //**********************************************************
    //************* Handling GenerationRate Change *********
    //**********************************************************
    int counterPeriod=1; // is the value chosenPeriod from periodsForBulky;
    int chosenPeriod =0; // index of the counter (which is the value chosenPeriod from the periodsForBulky)
    int choosePktPeriod(vector<unsigned int> periods, string actionToTake){    //actionToTake : "increaseQuality" || "decreaseQulaity"
        vector <unsigned int> PeriodsToChooseFrom;
        if(actionToTake == "increaseQuality"){
            if(this->chosenPeriod >0){
                for(int i=0; i< this->chosenPeriod; i++){
                    PeriodsToChooseFrom.push_back(i);
                }
                if(PeriodsToChooseFrom.size() >0){
                    this ->chosenPeriod = selectRandomValues(PeriodsToChooseFrom,1)[0];
                }
                // this->chosenPeriod--;
            }
        }else if(actionToTake == "decreaseQuality"){
            if(this->chosenPeriod < this->periodsForBulky.size()-1){
                for(int i=this->chosenPeriod + 1; i< this->periodsForBulky.size()-1; i++){
                    PeriodsToChooseFrom.push_back(i);
                }
                if(PeriodsToChooseFrom.size()>0){
                    this ->chosenPeriod = selectRandomValues(PeriodsToChooseFrom,1)[0];
                }
                // this->chosenPeriod++;
            }
        }
        return this-> periodsForBulky[chosenPeriod]; // to be changed later to choose dynamically according to feedback
    }
    //**********************************************************
    //**********************************************************


    //**********************************************************
    //***************** Handling TXRate Change *************
    //**********************************************************
    int counterTxRate= 0; // is the value chosenPeriod from periodsForBulky;
    int chosenTxRate = 0; // index of the counter (which is the value chosenPeriod from the periodsForBulky)


    int chooseTxRate(vector<unsigned int> TxRates, string actionToTake){    //actionToTake : "increaseTxRate" || "decreaseTxRate"
        vector <unsigned int> RatesToChooseFrom;
        if(actionToTake == "increaseTxRate"){
            if(this->chosenTxRate >0){
                for(int i=0; i< this->chosenTxRate; i++){
                    RatesToChooseFrom.push_back(i);
                }
                if(RatesToChooseFrom.size() >0){
                    this ->chosenTxRate = selectRandomValues(RatesToChooseFrom,1)[0];
                }
                // this->chosenTxRate--;
            }
        }else if(actionToTake == "decreaseTxRate"){
            if(this->chosenTxRate < this->TxRates.size()-1){
                for(int i=this->chosenTxRate + 1; i< this->TxRates.size()-1; i++){
                    RatesToChooseFrom.push_back(i);
                }
                if(RatesToChooseFrom.size()>0){
                    this ->chosenTxRate = selectRandomValues(RatesToChooseFrom,1)[0];
                }
                // this->chosenTxRate++;
            }
        }
        return this-> TxRates[chosenTxRate]; // to be changed later to choose dynamically according to feedback
    }
    //**********************************************************
    //**********************************************************

    vector <double> BandsRankingSeenByEachSu;

    int collisionCounterEvery5TimeSlots =0;

    //**********************************************************
    //******************* SUSensingBands *******************
    //**********************************************************
    vector <unsigned int> bandsAsSeenBySU;
    void fillbandsAsSeenBySU(vector<unsigned int> TXFREQARRAY, int t){
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
        //     string s =  "SU[" + to_string(i) + "] " + "SensedBandsSUPerspectiveHistory: ";
        //     printVector(SensedBandsSUPerspectiveHistory[i],s);
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
        SensedBandsSUPerspectiveHistory(SensedBandsSUPerspectiveHistorySize, std::vector<unsigned int>(numberOfBands, 0)),
        weights(numberOfBands, 0),BandsRankingSeenByEachSu(numberOfBands,0),PossiblePacketsDropped(PossiblePacketDroppedSize,0),
        BandsExperienceHistory(numberOfBands,0)

    {
        // constructor body (optional)
    }
};

//**********************************************************
//***************** Helper Functions *******************
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
//**********************************************************
//**********************************************************
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
    for (int i=0;i<PU.size();i++)
    { if  ((((time-StartingPositions[i])%(ActiveTime+offtime)<ActiveTime)&&time>=StartingPositions[i]))//variable for 10
        {
            PU[i].PUState=true;
            counter++;
        }
        else
            PU[i].PUState=false;
        // cout<<PU[i].PUState<<" ";
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

vector <Band> PU(numberOfBands);
vector <SecondaryUser> SU(numberOfSU);

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

vector <unsigned int> test (numberOfTimeSlots,0);
vector <unsigned int> test2 (numberOfTimeSlots,0);
vector <unsigned int> test3 (numberOfTimeSlots,0);
vector <unsigned int> test4 (numberOfTimeSlots,0);
int counter = 0;
int counter2 = 0;
int counter3 = 0;
int counter4 = 0;
//**********************************************************
//*************** Allocation Function ******************
//**********************************************************

// INTELLIGENCE (IT SHOULD BE DOING ACQUIRING A NEW BAND)
vector <unsigned int> allocationFunction(vector <Band> &PU, vector<SecondaryUser>&SU, int t){
    vector <unsigned int> possibleBands;
    vector <unsigned int> occupiedBands(PU.size(), 0);

    vector <unsigned int> TXFreqArray(PU.size(), 0);

    for(int i=0; i< PU.size(); i++){
        if(PU[i].PUState ==0){
            possibleBands.push_back(i);
        }
    }

    for(int i=0; i<SU.size(); i++){
        if(SU[i].pktqueue.size() ==0){
            continue;
        }else{
            if(possibleBands.size() >0){
                // SU[i].selectedBand will not be random, it must be based upon the history parameters
                // SU[i] will not get assigned a band every time slot, however, it will choose to stay or relinquish
                if(SU[i].selectedBand==-1){ // this if statement is satisfied, when SU wasn't assigned a band yet, or when SU relinquished a band it chose before
                    //INTELLIGENCE
                    SU[i].selectedBand =selectRandomValues(possibleBands,1)[0]; // to be changed with history
                    // cout<< "SU["<< i<< "]: selectedBand:"<< SU[i].selectedBand<< endl;
                    // cout<< "SU["<< i<< "]: selectedBand:"<< SU[i].selectedBand<< endl;
                    occupiedBands[SU[i].selectedBand]+=1;
                }
            }
            if(SU[i].counterTxRate > 0){
                // cout<<"SU["<<to_string(i)<< "] has: "<< to_string(SU[i].counterTxRate)<< " time slots left"<< endl;
                SU[i].counterTxRate--;
                if(SU[i].counterTxRate== 0){
                    TXFreqArray[SU[i].selectedBand]+=1;
                }
            }else if(SU[i].counterTxRate== 0){
                // cout<<"SU["<<to_string(i)<< "] Reached 0 counter: "<< SU[i].selectedBand<< endl;
                //SEND PACKET
                TXFreqArray[SU[i].selectedBand]+=1;
            }
        }

        // if(SU[i].counterTxRate > 0){
        //     SU[i].counterTxRate--;
        // }else if(SU[i].counterTxRate== 0){
        //     //choose band only if txRateCounter reached 0;
        //     if(SU[i].pktqueue.size() ==0){
        //         continue;
        //     }else{
        //         if(possibleBands.size() >0){
        //             SU[i].selectedBand =selectRandomValues(possibleBands,1)[0];
        //             TXFreqArray[SU[i].selectedBand]+=1;

        //         }

        //     }
        // }
    }

    for(int i=0; i< SU.size(); i++){
        SU[i].fillbandsAsSeenBySU(TXFreqArray, t);
        // string s = "BandsAsSeenBySU[" + to_string(i) + "]: ";
        // printVector(SU[i].bandsAsSeenBySU,s);
        if(SU[i].selectedBand !=-1){ //was given band
            if(TXFreqArray[SU[i].selectedBand] > 1){ // >1, and NOT (TXFreqArray[SU[i].selectedBand] !=1) because when SU acquired a band (alone) and doesn't have packets to transmit, TXFreqArray is equal to zero, and it counts a collision
                // mark collision of SU on that band
                // SU[i].history[TXFreqArray[SU[i].selectedBand]][]++;
                SU[i].collisionCounterHistoryPerSU.pop_front();
                SU[i].collisionCounterHistoryPerSU.push_back(1);
                SU[i].counterTxRate= SU[i].chooseTxRate(SU[i].TxRates, "");
                if (SU[i].BandsExperienceHistory[SU[i].selectedBand]>=BandTrustFactor) // bigger than 10 to ensure it does not become negative, 0 means the band is really bad
                {
                    SU[i].BandsExperienceHistory[SU[i].selectedBand]=SU[i].BandsExperienceHistory[SU[i].selectedBand]-BandTrustFactor;
                }

            }else{
                if(SU[i].pktqueue.size()> 0 && SU[i].counterTxRate== 0){
                    if(i==3){
                        test[counter] = 1;
                    }
                    if(i==4){
                        test2[counter2] = 1;
                    }
                    if(i==14){
                        test3[counter3] = 1;
                    }
                    if(i==7){
                        test4[counter4] =1;
                    }

                    if (SU[i].BandsExperienceHistory[SU[i].selectedBand]<=100-BandTrustFactor) // less than 90 to ensure it does not overshoot to higher than 1, 1 means the band is very good
                    {
                        SU[i].BandsExperienceHistory[SU[i].selectedBand]=SU[i].BandsExperienceHistory[SU[i].selectedBand]+BandTrustFactor;
                    }

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
                    // PROBLEM LINE
                    SU[i].counterTxRate= SU[i].chooseTxRate(SU[i].TxRates, "");
                }else{
                    // Case: selected band, low TXFreqArray count (not a collision), but no packet to send (or counter > 0)
                    SU[i].collisionCounterHistoryPerSU.pop_front();
                    SU[i].collisionCounterHistoryPerSU.push_back(0);
                }
            }
        }else{
            SU[i].collisionCounterHistoryPerSU.pop_front();
            SU[i].collisionCounterHistoryPerSU.push_back(0);

        }

        // if(i ==3){
        //     cout<< "SU[3].queueSIZE: "<< SU[i].pktqueue.size()<< endl;
        // }
    }

    counter++;
    counter2++;
    counter3++;
    counter4++;
    // cout<< "SU3 PKT QUEUE SIZE: "<< SU[3].pktqueue.size()<< endl;
    // printVector(test, "THIS IS HOW SU 3 SENDS PACKETS");

    // cout<< "SU4 PKT QUEUE SIZE: "<< SU[4].pktqueue.size()<< endl;
    // printVector(test2, "THIS IS HOW SU 4 SENDS PACKETS");
    // cout<< "SU14 PKT QUEUE SIZE: "<< SU[14].pktqueue.size()<< endl;
    // printVector(test3, "THIS IS HOW SU 14 SENDS PACKETS");

    // cout<< "SU7 PKT QUEUE SIZE: "<< SU[7].pktqueue.size()<< endl;
    // printVector(test4, "THIS IS HOW SU 7 SENDS PACKETS");
    // printVector(TXFreqArray,"TX frequency array");

    return TXFreqArray;
}

//**********************************************************
//**********************************************************




void generatePKTS(vector <SecondaryUser> &SU, int t){
    for(int i=0; i< SU.size(); i++){
        if(SU[i].pktGenerationRate ==-1){ //TYPE BULKY uninterruptible CAMERA
            // if(good quality){
            // if(t % SU[i].counter == 0){
            //     SU[i].generatePkt(t);
            // }

            // if(quality changed){
            // SU[i].counter= SU[i].choosePktPeriod(SU[i].periodsForBulky);
            // }

            if(SU[i].counterPeriod > 0){
                SU[i].counterPeriod--;
            }else if(SU[i].counterPeriod== 0){
                SU[i].generatePkt(t);
                SU[i].NumOfPacketsGenerated++;
                SU[i].counterPeriod= SU[i].choosePktPeriod(SU[i].periodsForBulky, "");
            }
        }else{ // Urgent and Best Effort
            if(randomCoinFlipper(SU[i].pktGenerationRate)){
                SU[i].generatePkt(t);
                SU[i].NumOfPacketsGenerated++;
            }
        }
    }
}

void collisionCounter(vector <SecondaryUser> &SU, int t){
    for(int i=0; i< SU.size(); i++){
        int collisionCounter = 0;
        if(t%10 == 0){
            for(int k=0; k< collisionCounterHistoryPerSUSize; k++){
                if(SU[i].collisionCounterHistoryPerSU[k] == 1){
                    collisionCounter++;
                }
            }
        }
        SU[i].collisionCounterEvery5TimeSlots = collisionCounter;
    }
}


bool checkBandsWithNoPU(vector <unsigned int> PUStates){ // RETURNS TRUE IF THEre IS AT LEAST ONE EMPTY BAND NOT OCCUPIED BY PU
    for (int i=0; i< PUStates.size(); i++){
        if(PUStates[i] == 0){
            return true;
        }
    }
    return false;
}

vector<unsigned int> getHigherValues(vector<double> nums, int target_index) {
    vector<unsigned int> result;
    int target_value = nums[target_index];

    for (int i = 0; i < nums.size(); i++) {
        if (nums[i] > target_value) {
            // Store the value and the index as a pair
            result.push_back(i);
        }
    }
    return result;
}


//INTELLIGENCE
void CalculateSuSpecificParameters (vector <SecondaryUser> &SU, int t)
{
    for (int i=0;i<SU.size();i++)
    {
        int counter=0;
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
        SU[i].RelinquishingTendency=(SU[i].CollisionsWeight+SU[i].QueueSizeWeight+SU[i].NumOfPacketsDroppedWeight)/3;
        SU[i].AvgPacketWaitingTimeWeight=counter3/(MaxQueueSize*SU[i].pktqueue.size());
    }
}
void TakeDecisionStayOrRelinquish(vector <SecondaryUser> &SU, int t){
    // FOR EACH SU => Decide to stay or relinquish
    // if: PU is ON on selectedBand => relinquish and acquire another band immediately ✅
    // if urgent:
    // if performance parameters not good over 3 time slots => relinquish => call ACQUIREBAND✅
    // else if camera or best effort:
    // if: performance is good
    // STAY✅
    // Increase DataRate✅


    // else if: performance is bad
    // when measuring the performance of a band: this performance is stored within a vector for each SU
    // each SU: [band 0, band 1, band 2, ... , band 9,] with scores.
    // When Acquiring => you try to acquire a band, that was good for you before (high score) or a new band you haven't tried before
    // these scores show the following:
    // 1. The number of time slots the band was acquired for
    // 2. The total score
    // 3. the score has a TTL that decreases with time => so that we give bands another opportunity after adequate time


    //if: otherBands are not better than current or no other possible bands (occupied by PU)
    // STAY
    // decrease data rate and choose a random shift between 0 and TXperiod time, to transmit at.
    //else (other are bands are better && there are possible bands available to move to):
    // if we decreased TXrate multiple times and current one is low:
    //relinquish => and acquire one of the top candidateBands randomly
    // when calling acquire band =>
    // else: stay for other 10 time slots


    vector <unsigned int> PUActiveRightNow(PU.size(),0);
    for (int i=0; i< PU.size(); i++){
        if(PU[i].PUState == true){
            PUActiveRightNow[i] = 1;
        }else{
            PUActiveRightNow[i] = 0;
        }
    }
    bool bandWithNoPUExists = checkBandsWithNoPU(PUActiveRightNow);

    for(int i=0; i< SU.size(); i++){
        vector<unsigned int> BandsWithHigherScore = getHigherValues(SU[i].BandsRankingSeenByEachSu,SU[i].selectedBand);


        if(PUActiveRightNow[SU[i].selectedBand] == 1){
            SU[i].selectedBand = -1;
            SU[i].numberOfTimesDecreasedTXRATE = 0;
            // CALL ACQUIRE BAND FUNCTION FIXXXXXX!!!!
            // if(BandsWithHigherScore.size() > 0) {
            //     SU[i].selectedBand = selectRandomValues(BandsWithHigherScore, 1)[0];
            // }
            break;

        }else{
            if(t %3 ==0){
                if(SU[i].urgency == 0){ //URGENT SU
                    // CHECK PERFORMANCE PARAMETERS FOR URGENT SU
                    // DECIDE TO STAY OR RELINQUISH
                    if(SU[i].RelinquishingTendency > 0.6){
                        // RELINQUISH
                        SU[i].selectedBand = -1;
                        // CALL ACQUIRE BAND FUNCTION
                        // if(BandsWithHigherScore.size() > 0) {
                        //     SU[i].selectedBand = selectRandomValues(BandsWithHigherScore, 1)[0];
                        // }
                    }else {
                        continue;
                    }
                }

            }
            if(t%20 == 0){
                if(SU[i].dataRateClass == 1){ // CAMERA OR BEST EFFORT
                    // CHECK PERFORMANCE
                    if(SU[i].RelinquishingTendency < 0.6){
                    // ****************************************************
                    // SU IS DOING WELL IN BAND, SO INCREASE TXRATE
                    // ****************************************************
                        if(SU[i].urgency == 1){ // CAMERA
                            SU[i].chooseTxRate(SU[i].TxRates, "increaseTxRate");
                            SU[i].choosePktPeriod(SU[i].periodsForBulky, "increaseQuality");
                        }else if(SU[i].urgency == 2){ // BEST EFFORT
                             SU[i].chooseTxRate(SU[i].TxRates, "increaseTxRate");
                        }
                    // ****************************************************
                    // ****************************************************
                    }else if(SU[i].RelinquishingTendency >= 0.6){

                        if(!bandWithNoPUExists || BandsWithHigherScore.size() ==0){ // NO bands not occupied by a PU (all others have PU in them) OR NO bands with higher score than current band
                            // STAY
                            // DECREASE DATA RATE
                            if(SU[i].urgency == 1){ // CAMERA
                                SU[i].chooseTxRate(SU[i].TxRates, "decreaseTxRate");
                                SU[i].choosePktPeriod(SU[i].periodsForBulky, "decreaseQuality");
                            }else if(SU[i].urgency == 2){ // BEST EFFORT
                                SU[i].chooseTxRate(SU[i].TxRates, "decreaseTxRate");
                            }

                            // (COUNTER OF TIMES DECREASED TXRATE)++
                            SU[i].numberOfTimesDecreasedTXRATE++;
                            // ASSIGN A RANDOM SHIFT NUMBER BETWEEN 0 AND TXPERIOD CHOSEN
                            SU[i].shift = selectRandomValue(SU[i].chosenTxRate);
                        }else if(bandWithNoPUExists && BandsWithHigherScore.size() >=1){
                            // ****************************************************
                            // LOOP OVER EMPTY BANDS
                            // EXTRACT THE BANDS THAT HAS CURRENTLY HIGHER SCORES THAN OURS,
                            // BOTH STEPS ARE DONE IN BandsWithHigherScore
                            // ****************************************************
                            // AND NO PREVIOUS EXPERIENCE SCORE (hasn't been acquired by this SU before) OR HIGH PREVIOUS EXPERIENCE SCORE (higher history)
                            if(SU[i].numberOfTimesDecreasedTXRATE < 4){
                                if(SU[i].urgency == 1){ // CAMERA
                                    SU[i].chooseTxRate(SU[i].TxRates, "decreaseTxRate");
                                    SU[i].choosePktPeriod(SU[i].periodsForBulky, "decreaseQuality");
                                }else if(SU[i].urgency == 2){ // BEST EFFORT
                                    SU[i].chooseTxRate(SU[i].TxRates, "decreaseTxRate");
                                }

                                // (COUNTER OF TIMES DECREASED TXRATE)++
                                SU[i].numberOfTimesDecreasedTXRATE++;
                                // ASSIGN A RANDOM SHIFT NUMBER BETWEEN 0 AND TXPERIOD CHOSEN
                                SU[i].shift = selectRandomValue(SU[i].chosenTxRate);

                            }else{
                                // RELINQUISH
                                SU[i].selectedBand = -1;
                                SU[i].numberOfTimesDecreasedTXRATE = 0;
                                // CALL ACQUIRE BAND FUNCTION
                                if(BandsWithHigherScore.size() > 0) {
                                    SU[i].selectedBand = selectRandomValues(BandsWithHigherScore, 1)[0];
                                }
                            }
                        }
                    }
                }else{
                    continue;
                }
            }
        }
    }
}

void SuBandExperienceUpdate (vector <SecondaryUser> &SU)
{
    for (int i=0;i<SU.size();i++)
    {
        for (int k=0;k<numberOfBands;k++)
        {
            if (k==SU[i].selectedBand)
                continue;
            else
                if (SU[i].BandsExperienceHistory[k]>0)
                SU[i].BandsExperienceHistory[k]=SU[i].BandsExperienceHistory[k]-1;

        }


    }

}
//INTELLIGENCE
// void TakeDecisionDataRate(vector <SecondaryUser> &SU, int t){
//     for(int i=0; i< SU.size(); i++){
//         if(t%10==0 && t!=0){
//             // cout<< "SU["<< i<< "]: collisionCounterEvery5TimeSlots: "<< SU[i].collisionCounterEvery5TimeSlots<< endl;
//         }
//         if(t % 10 ==0 && t!=0){
//             if(SU[i].pktGenerationRate ==-1){
//                 // cout<< "SU["<< i<< "]: collisionCounterEvery5TimeSlots: "<< SU[i].collisionCounterEvery5TimeSlots<< endl;
//                 if(SU[i].collisionCounterEvery5TimeSlots >= 2){
//                     // choose slower TX number
//                     SU[i].chooseTxRate(SU[i].TxRates, "decreaseTxRate");
//                     SU[i].choosePktPeriod(SU[i].periodsForBulky, "decreaseQuality");
//                     // cout<< "CAMERA SU["<< to_string(i)<< "]: on Band: "<<SU[i].selectedBand<< ": Decreased Quality"<< endl;
//                     // cout<< endl;
//                     // cout<<"NEW CHOSEN TX RATEEEEEE index:::::" <<SU[i].chosenTxRate<< endl;
//                     // cout<<"NEW CHOSEN TX RATEEEEEE:::::" <<SU[i].TxRates[SU[i].chosenTxRate]<< endl;

//                 }else{
//                     // choose higher TX number
//                     SU[i].chooseTxRate(SU[i].TxRates, "increaseTxRate");
//                     SU[i].choosePktPeriod(SU[i].periodsForBulky, "increaseQuality");
//                     // cout<< "CAMERA SU["<< to_string(i)<< "]: on Band: "<<SU[i].selectedBand<< ": Increased Quality"<< endl;
//                     // cout<<"NEW CHOSEN TX RATEEEEEE:::::" <<SU[i].chosenTxRate<< endl;
//                     // cout<<"NEW CHOSEN TX RATEEEEEE:::::" <<SU[i].TxRates[SU[i].chosenTxRate]<< endl;

//                 }
//             }else if(SU[i].urgency == 2 && SU[i].dataRateClass == 1){
//                 // cout<< "SU["<< i<< "]: collisionCounterEvery5TimeSlots: "<< SU[i].collisionCounterEvery5TimeSlots<< endl;
//                 if(SU[i].collisionCounterEvery5TimeSlots >= 2){
//                     // choose slower TX number
//                     SU[i].chooseTxRate(SU[i].TxRates, "decreaseTxRate");
//                     // cout<< "BEST EFFORT SU["<< to_string(i)<< "]: on Band: "<<SU[i].selectedBand<< ": Decreased Quality"<< endl;
//                     // cout<<"NEW CHOSEN TX RATEEEEEE:::::" <<SU[i].chosenTxRate<< endl;
//                     // cout<<"NEW CHOSEN TX RATEEEEEE:::::" <<SU[i].TxRates[SU[i].chosenTxRate]<< endl;

//                 }else{
//                     // choose higher TX number
//                     SU[i].chooseTxRate(SU[i].TxRates, "increaseTxRate");
//                     // cout<< "BEST EFFORT SU["<< to_string(i)<< "]: on Band: "<<SU[i].selectedBand<< ": Increased Quality"<< endl;
//                     // cout<<"NEW CHOSEN TX RATEEEEEE:::::" <<SU[i].chosenTxRate<< endl;
//                     // cout<<"NEW CHOSEN TX RATEEEEEE:::::" <<SU[i].TxRates[SU[i].chosenTxRate]<< endl;

//                 }

//             }
//         }
//         // if(SU[i].pktGenerationRate ==-1){ //TYPE BULKY uninterruptible

//         //     if(t%10 == 0 && t!=0){
//         //         // cout<< "SU["<< i<< "]: collisionCounterEvery5TimeSlots: "<< SU[i].collisionCounterEvery5TimeSlots<< endl;
//         //         if(SU[i].collisionCounterEvery5TimeSlots >= 2){
//         //             // choose slower TX number
//         //             SU[i].chooseTxRate(SU[i].TxRates, "decreaseTxRate");
//         //             SU[i].choosePktPeriod(SU[i].periodsForBulky, "decreaseQuality");

//         //         }else{
//         //             // choose higher TX number
//         //             SU[i].chooseTxRate(SU[i].TxRates, "increaseTxRate");
//         //             SU[i].choosePktPeriod(SU[i].periodsForBulky, "increaseQuality");

//         //         }
//         //     }
//         // }else{ // URGENT and BULKY interruptible SU's


//         //     if(SU[i].urgency == 2 && SU[i].dataRateClass == 1){ //Best effort
//         //         if(t%10 == 0 && t!=0){
//         //             // cout<< "SU["<< i<< "]: collisionCounterEvery5TimeSlots: "<< SU[i].collisionCounterEvery5TimeSlots<< endl;
//         //             if(SU[i].collisionCounterEvery5TimeSlots >= 2){
//         //                 // choose slower TX number
//         //                 SU[i].chooseTxRate(SU[i].TxRates, "decreaseTxRate");
//         //             }else{
//         //                 // choose higher TX number
//         //                 SU[i].chooseTxRate(SU[i].TxRates, "increaseTxRate");
//         //             }
//         //         }
//         //     }
//         // }

//     }
// }
void candidateBandsWeights(vector <SecondaryUser> &SU){
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
void DecisionMaker (vector <SecondaryUser> &SU,vector <Band> &PU)
{
    for (int i=0;i<SU.size();i++)
    {
        for (int k=0;k<numberOfBands;k++)
        {
            //perhaps to differeniate between band rankings for  SU's we can increase the weight
            //differently for each su, for example a weight of 1 will be considered for an urgent SU if he sees
            // the band empty for 3 consecutive timeslots while a weight of 1 will be considered for a non urgent
            // SU if he sees the band empty for 10 consecutive time slots
            // make linear combination from all parameters that are calculated
            //
            SU[i].BandsRankingSeenByEachSu[k]=SU[i].weights[k]+PU[k].Weight;
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
    StartingPositions=AssignStartingPositions(numberOfBands,1,numberOfBands);
    //initialize system
    initializeSystem();
    // vector<vector<unsigned int>> pktgenerationrate(numberOfTimeSlots, vector<unsigned int>(SU.size(), 0));
    for(int t=0; t< numberOfTimeSlots; t++){ //TIMESLOTS LOOP
        cout<< "time slot: "<< t<< endl;
        // cout<< "TxRate for SU 2 is:"<<SU[2].counterTxRate<<endl;
        // PUInitMarkov(PU);
        PUInitDeterministic(PU,t,DutyCycleDeterministic);
        // cout<<endl;
        // for (int i=0;i<PU.size();i++){
        //     PU[i].PUState=0;
        // }

        //**********************************************************
        //******************** Generate Packets ****************
        //**********************************************************
        generatePKTS(SU, t);
        CalculateSuSpecificParameters (SU,t);
        cout<<"SU[3].AvgPacketWaitingTimeWeight :";
        cout<<SU[3].AvgPacketWaitingTimeWeight;
        cout<<endl;


        //**********************************************************
        //***************** Allocation Function ****************
        //**********************************************************
        vector <unsigned int> TXFreqArray= allocationFunction(PU, SU,t);
        SuBandExperienceUpdate(SU);
        cout<<"Bands Experience: ";
        for (int i=0;i<numberOfBands;i++)
        {
            cout<<SU[3].BandsExperienceHistory[i]<<" ";
        }
        cout<<endl;



        //**********************************************************
        //*************** Calculate CollisionCounter ***********
        //**********************************************************
        collisionCounter(SU, t);

        //**********************************************************
        //****** Take Decision to increase/Decrease DataRate *****
        //**********************************************************
        // TakeDecisionDataRate(SU,t);

        //**********************************************************
        //************ Take Decision Stay or Relinquish **********
        //**********************************************************
        TakeDecisionStayOrRelinquish(SU,t);


        if (t%10 ==0){
            for(int i=0; i< SU.size(); i++){
                SU[i].collisionCounterEvery5TimeSlots=0;
                // cout<<SU[i].pktqueue.size()<<" ";
            }
        }

        candidateBandsWeights(SU);
        DecisionMaker(SU,PU);
        // for (int i=0;i<SU.size();i++)
        // {
        //     printVector(SU[i].BandsRankingSeenByEachSu,"Bands Rank seen by SU number "+to_string(i));
        //     printVector(SU[i].weights,"weights of bands seen by SU "+to_string(i));

        // }
        // cout<<"selected band for su2: "<<SU[2].selectedBand<<endl;
        // printVector(SU[2].collisionCounterHistoryPerSU, "SU2 ControlHistory: ");
        // printDeQueue(SU[2].collisionCounterHistoryPerSU);

        // SU[i].counter= SU[i].choosePktPeriod(SU[i].periodsForBulky, "decreaseQuality");
        // cout<< "decreaseQualitier"<< endl;
        // cout<< "SIZE: "<< SU[2].pktqueue.size()<< endl;
        // if (t%10==0)
        // {
        //     for (int i=0;i<SU.size();i++)
        //     {
        //         cout<<"SU"<<i<<"TxRate: "<<SU[i].counterTxRate<<" ";
        //     }
        // }


        TotalPacketsCounter(t,SU,TotalPackets.AvgPerTimeSlot);
        CollisionCounter(t,Collisions.AvgPerTimeSlot,TXFreqArray);
        ThroughPutCalculator(t,TXFreqArray,Throughput.AvgPerTimeSlot,Throughput.AvgPerBand);
        UtilizationCalculator(t,TXFreqArray,Utilization.AvgPerTimeSlot,Utilization.AvgPerBand);



        // for(int i=0; i< SU[0].SensedBandsSUPerspectiveHistory.size(); i++){
        //     printVector(SU[0].SensedBandsSUPerspectiveHistory[i], "history: ");
        // }
        // printVector(SU[0].weights,"weightsVector: ");
    }
    // cout<<"All packets: "<< endl;
    // for(int i=0; i< SU.size(); i++){
    //     // cout<< "this is the length of queue for SU[" << i << "]: " << SU[i].pktqueue.size()<< endl;
    //     cout<< SU[i].sentPackets.size() + SU[i].pktqueue.size()<< ", ";
    // }
    // cout<< endl;
    // cout<<"All Time sent packets: "<< endl;
    // for(int i=0; i< SU.size(); i++){
    //     // cout<< "this is the length of queue for SU[" << i << "]: " << SU[i].pktqueue.size()<< endl;
    //    cout<< SU[i].sentPackets.size()<< ", ";
    // }
    // cout<< endl;
    // cout<<"Packets still waiting in the queue to be sent: "<< endl;
    // for(int i=0; i< SU.size(); i++){
    //     // cout<< "this is the length of queue for SU[" << i << "]: " << SU[i].pktqueue.size()<< endl;
    //     cout<<SU[i].pktqueue.size()<<", ";
    // }
    // cout<< endl;
    // printQueue(SU[2].sentPackets);

    WaitingTimeCalculator(SU,WaitingTime.AvgPacketWaitingTime);
    NumberOfPacketsDroppedCalculator(SU,NumberofPacketsDropped.AvgPerSU);
    FairnessCalculator(SU,Fairness.AvgPerSU);




    // printVector(Collisions.AvgPerTimeSlot,"Collision Count per timeslot Averaged Per Band");
    // printVector(TotalPackets.AvgPerTimeSlot,"Total Packets in whole queue");
    // printVector(WaitingTime.AvgPacketWaitingTime,"Average Packet Waiting time for each SU ");
    // printVector(Utilization.AvgPerTimeSlot,"TimeSlot Average");
    // printVector(Utilization.AvgPerBand,"Band Average");
    // printVector(Throughput.AvgPerTimeSlot,"TimeSlot Average");
    // printVector(Throughput.AvgPerBand,"Band Average");
    // printDeQueue(PU[0].PuBehaviorHistory);

    for (int i=0;i<SU.size();i++)
    {
        cout<<"SU"<<i<<" PacketsGenerated:"<<SU[i].NumOfPacketsGenerated<<" ";
        cout<<" PacketsSent:"<<SU[i].NumOfPacketsSent;
        cout<<" PacketsDropped:"<<SU[i].NumOfPacketsDropped;
        cout<<" Que Size: "<<SU[i].pktqueue.size();
        cout<<" RelinquishingTendency: "<<SU[i].RelinquishingTendency;
        cout<<endl;
    }
    // printVector(Fairness.AvgPerSU,"Packets sent over generated for each su");
    // printVector(NumberofPacketsDropped.AvgPerSU,"Packets Dropped for each su");




    // *********************************Writing Into Files*******************************************//



}


// According to number of collisions and throughput calculation:
// 1. for an urgent SU: it is a mean to choose a favorable band over another unfavorable one
// 2. for a bulky uninterruptible SU, it is an indicator to lower its datarate or not
// 3. for the best effort traffic, it means backoff exponentially


// As we change the pkt Generation rate according to (collisions and throughput (that is: lowering the quality))
// we also have to control the pkt transmission rate according to collisions and throughput
// that is: send a pkt every 2 time slots or every 6 time slots?
// this way => it allows for extra time slots to be vacant, and thus urgent SU's can try to transmit on these vacant time slots and might be able to transmit their traffic
// or: another bulky SU, might be able to interleave its pkts with the ongoing transmission of another bulky device => maximum throughput with minimum collision
