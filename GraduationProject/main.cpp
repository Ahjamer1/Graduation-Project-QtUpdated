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
const double numberOfTimeSlots = 20;
const double durationOfTimeSlot = 0.01;
const int numberOfBandsPerSU = 1;
vector <double> PuActiveProb={0.2,0.4,0.5,0.6};
double probOffToOn = 0.01;//never change this
unsigned int seed = 123;
std::mt19937_64 randEngine(seed);
vector <unsigned int> StartingPositions;//To choose random starting positions , change later for 100 bands
int collisionCounterHistoryPerSUSize = 50;
int SensedBandsSUPerspectiveHistorySize = 25;
int PuBehaviorHistorySize=10;
const int offtime=5;
double DutyCycleDeterministic=0.5;

class Parameters {
public:
    vector <double> AvgPerTimeSlot;
    vector <double> AvgPacketWaitingTime;
    vector <double> AvgPerBand;
    Parameters() : AvgPerTimeSlot(numberOfTimeSlots,0),AvgPacketWaitingTime(numberOfSU,0),AvgPerBand(numberOfBands,0){}
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


    // This deque shows the values of collision as 1 / no collision as zero experineced by each SU every 10 time slots
    // to calculate collisions according to this vector later
    deque <unsigned int> collisionCounterHistoryPerSU;

    vector<unsigned int> potentialBands;

    //Function(PUState) => fill potentialBands

    int urgency; // one of possible three values: delay-intolerant / delay tolerant uninterruptable (will lower datarate) / delay tolerant interruptable
    // Urgency possible values:
    // 0: Delay intolerant
    // 1: Delay tolerant uninterruptable
    // 2: Delay tolerant interruptable
    int dataRateClass; // one of possible two values: realtime traffic (high data rate) / low dataRate
    // Urgency possible values:
    // 0: low data rate
    // 1: High data rate


    vector <unsigned int> periodsForBulky = {1,4,9};
    // periodsForBulky generates a packet every 2, 5, 10 (1,4,9 are set for counting purposes)
    vector <unsigned int> TxRates = {0,1,4,9,19}; // one: means every time slot(fastest) / 20: means every 20 time slot (SLowest)
    // TxRates transmits a packet every 1, 2, 5, 10, 20 (0,1,4,9,19 are set for counting purposes)



    int selectedBand=-1;
    vector <Packet> sentPackets;
    // Possible combinations of urgency and dataRateClass:
    // 1. 00 => summation = 0
    // 2. 11 => summation = 2
    // 3. 21 => summation  = 3
    queue <Packet> pktqueue;  // instantiate an empty queue of integers

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
        this->pktqueue.push(pkt);
    }

    //**********************************************************
    //************* Handling GenerationRate Change ***********
    //**********************************************************
    int counterPeriod=1; // is the value chosenPeriod from periodsForBulky;
    int chosenPeriod =0; // index of the counter (which is the value chosenPeriod from the periodsForBulky)
    int choosePktPeriod(vector<unsigned int> periods, string actionToTake){    //actionToTake : "increaseQuality" || "decreaseQulaity"
        if(actionToTake == "increaseQuality"){
            if(this->chosenPeriod >0){
                this->chosenPeriod--;
            }
        }else if(actionToTake == "decreaseQuality"){
            if(this->chosenPeriod < this->periodsForBulky.size()-1){
                this->chosenPeriod++;
            }
        }
        return this-> periodsForBulky[chosenPeriod]; // to be changed later to choose dynamically according to feedback
    }
    //**********************************************************
    //**********************************************************


    //**********************************************************
    //***************** Handling TXRate Change ***************
    //**********************************************************
    int counterTxRate= 0; // is the value chosenPeriod from periodsForBulky;
    int chosenTxRate = 0; // index of the counter (which is the value chosenPeriod from the periodsForBulky)


    int chooseTxRate(vector<unsigned int> TxRates, string actionToTake){    //actionToTake : "increaseTxRate" || "decreaseTxRate"
        if(actionToTake == "increaseTxRate"){
            if(this->chosenTxRate >0){
                this->chosenTxRate--;
            }
        }else if(actionToTake == "decreaseTxRate"){
            if(this->chosenTxRate < this->TxRates.size()-1){
                this->chosenTxRate++;
            }
        }
        return this-> TxRates[chosenTxRate]; // to be changed later to choose dynamically according to feedback
    }
    //**********************************************************
    //**********************************************************


    int collisionCounterEvery5TimeSlots =0;

    //**********************************************************
    //******************* SUSensingBands *********************
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
        this->SensedBandsSUPerspectiveHistory.push_back(bandsAsSeenBySU);
        this->SensedBandsSUPerspectiveHistory.pop_front();
        // for(int i=0; i< this->SensedBandsSUPerspectiveHistory.size(); i++){
        //     string s =  "SU[" + to_string(i) + "] " + "SensedBandsSUPerspectiveHistory: ";
        //     printVector(SensedBandsSUPerspectiveHistory[i],s);
        // }
    }
    //**********************************************************
    //**********************************************************

    vector <double> weights;

    SecondaryUser():
        collisionCounterHistoryPerSU(collisionCounterHistoryPerSUSize, 0),
        bandsAsSeenBySU(numberOfBands, 0),
        SensedBandsSUPerspectiveHistory(SensedBandsSUPerspectiveHistorySize, std::vector<unsigned int>(numberOfBands, 0)),
        weights(numberOfBands, 0)
    {
        // constructor body (optional)
    }
};

//**********************************************************
//***************** Helper Functions *********************
//**********************************************************
void printQueue(vector<Packet> q) {
    for(int i=0; i< q.size(); i++){
        Packet p = q[i];
        cout<< "GenTime: " <<p.pktGenerationTime << endl;
        cout<< "WaitingTime: "<< p.pktWaitingTimeInQueue<< endl;
        cout<< "Arrival: "<< p.pktArrivalTime << endl;
    }
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
        cout<<PU[i].PUState<<" ";
        PU[i].PuBehaviorHistory.pop_front();
        PU[i].PuBehaviorHistory.push_back(PU[i].PUState);
        double c=0;
        for (int j=0;j<PuBehaviorHistorySize;j++)
        {
            if (PU[i].PuBehaviorHistory[j]==1)
                c++;


        }
        PU[i].Weight=c/PuBehaviorHistorySize;
    }




    return counter;
}





//**********************************************************
//***************** Performance Parameters ***************
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




//**********************************************************
//*************** Allocation Function ********************
//**********************************************************

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
                    if(i==1){
                        cout<< "TIME SLOTTTTTT: "<< t<< " "<< endl;
                    }
                    //INTELLIGENCE
                    SU[i].selectedBand =selectRandomValues(possibleBands,1)[0]; // to be changed with history
                    // cout<< "SU["<< i<< "]: selectedBand:"<< SU[i].selectedBand<< endl;

                    occupiedBands[SU[i].selectedBand]+=1;
                }
                // else{
                //     continue;
                //     //choose to stay or relinquish
                //     // if(SU[i].lastTXState ==1){
                //     //     continue;
                //     // }else{
                //     //     SU[i].selectedBand = -1;
                //     // }

                // }

            }
            if(SU[i].counterTxRate > 0){
                SU[i].counterTxRate--;
            }else if(SU[i].counterTxRate== 0){
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
                SU[i].collisionCounterHistoryPerSU.push_back(1);
                SU[i].collisionCounterHistoryPerSU.pop_front();
                // SU[i].lastTXState = 0;

            }else{
                if(SU[i].pktqueue.size()> 0){


                    Packet poppedPacket = SU[i].pktqueue.front();
                    SU[i].pktqueue.pop();
                    poppedPacket.pktArrivalTime = t;
                    poppedPacket.pktWaitingTimeInQueue = poppedPacket.pktArrivalTime - poppedPacket.pktGenerationTime;
                    SU[i].sentPackets.push_back(poppedPacket);
                    // cout<< "true"<< endl;
                    SU[i].collisionCounterHistoryPerSU.push_back(0); //No collision
                    SU[i].collisionCounterHistoryPerSU.pop_front();
                    // SU[i].lastTXState = 1;
                }
            }
            SU[i].counterTxRate= SU[i].chooseTxRate(SU[i].TxRates, "");
            // SU[i].selectedBand =-1;
        }else{
            SU[i].collisionCounterHistoryPerSU.push_back(0);
            SU[i].collisionCounterHistoryPerSU.pop_front();

        }
        // if(i ==3){
        //     cout<< "SU[3].queueSIZE: "<< SU[i].pktqueue.size()<< endl;
        // }
    }


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
                SU[i].counterPeriod= SU[i].choosePktPeriod(SU[i].periodsForBulky, "");
            }
        }else{ // Urgent and Best Effort
            if(randomCoinFlipper(SU[i].pktGenerationRate)){
                SU[i].generatePkt(t);
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


//INTELLIGENCE
void TakeDecisionStayOrRelinquish(vector <SecondaryUser> &SU, int t){
    for(int i=0; i< SU.size(); i++){
        if(t %10 ==0 && t !=0){
            if(SU[i].collisionCounterEvery5TimeSlots >=7){
                SU[i].selectedBand = -1;
                SU[i].collisionCounterEvery5TimeSlots = 0; // WRONG TO BE FIXED lATER
            }else{
                continue;
            }
        }
    }
}


//INTELLIGENCE
void TakeDecisionDataRate(vector <SecondaryUser> &SU, int t){
    for(int i=0; i< SU.size(); i++){
        if(t%10==0 && t!=0){
            // cout<< "SU["<< i<< "]: collisionCounterEvery5TimeSlots: "<< SU[i].collisionCounterEvery5TimeSlots<< endl;
        }
        if(SU[i].pktGenerationRate ==-1){ //TYPE BULKY uninterruptible

            if(t%10 == 0 && t!=0){
                // cout<< "SU["<< i<< "]: collisionCounterEvery5TimeSlots: "<< SU[i].collisionCounterEvery5TimeSlots<< endl;
                if(SU[i].collisionCounterEvery5TimeSlots >= 2){
                    // choose slower TX number
                    SU[i].chooseTxRate(SU[i].TxRates, "decreaseTxRate");
                    SU[i].choosePktPeriod(SU[i].periodsForBulky, "decreaseQuality");

                }else{
                    // choose higher TX number
                    SU[i].chooseTxRate(SU[i].TxRates, "increaseTxRate");
                    SU[i].choosePktPeriod(SU[i].periodsForBulky, "increaseQuality");

                }
            }
        }else{ // URGENT and BULKY interruptible SU's


            if(SU[i].urgency == 2 && SU[i].dataRateClass == 1){ //Best effort
                if(t%10 == 0 && t!=0){
                    // cout<< "SU["<< i<< "]: collisionCounterEvery5TimeSlots: "<< SU[i].collisionCounterEvery5TimeSlots<< endl;
                    if(SU[i].collisionCounterEvery5TimeSlots >= 2){
                        // choose slower TX number
                        SU[i].chooseTxRate(SU[i].TxRates, "decreaseTxRate");
                    }else{
                        // choose higher TX number
                        SU[i].chooseTxRate(SU[i].TxRates, "increaseTxRate");
                    }
                }
            }
        }

    }
}

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

int main(){
    Parameters Collisions;
    Parameters TotalPackets;
    Parameters WaitingTime;
    Parameters Utilization;
    Parameters Throughput;
    StartingPositions=AssignStartingPositions(numberOfBands,1,numberOfBands);
    //initialize system
    initializeSystem();
    // vector<vector<unsigned int>> pktgenerationrate(numberOfTimeSlots, vector<unsigned int>(SU.size(), 0));
    for(int t=0; t< numberOfTimeSlots; t++){ //TIMESLOTS LOOP
        cout<< "time slot: "<< t<< endl;
        // cout<< "TxRate for SU 2 is:"<<SU[2].counterTxRate<<endl;
        // PUInitMarkov(PU);
        PUInitDeterministic(PU,t,DutyCycleDeterministic);
        cout<<endl;
        // for (int i=0;i<PU.size();i++){
        //     PU[i].PUState=0;
        // }

        //**********************************************************
        //******************** Generate Packets ******************
        //**********************************************************
        generatePKTS(SU, t);


        //**********************************************************
        //***************** Allocation Function ******************
        //**********************************************************
        vector <unsigned int> TXFreqArray= allocationFunction(PU, SU,t);


        //**********************************************************
        //*************** Calculate CollisionCounter *************
        //**********************************************************
        collisionCounter(SU, t);

        //**********************************************************
        //************ Take Decision Stay or Relinquish **********
        //**********************************************************
        TakeDecisionStayOrRelinquish(SU,t);

        //**********************************************************
        //****** Take Decision to increase/Decrease DataRate *****
        //**********************************************************
        TakeDecisionDataRate(SU,t);


        candidateBandsWeights(SU);
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




    // printVector(Collisions.AvgPerTimeSlot,"Collision Count per timeslot Averaged Per Band");
    // printVector(TotalPackets.AvgPerTimeSlot,"Total Packets in whole queue");
    // printVector(WaitingTime.AvgPacketWaitingTime,"Average Packet Waiting time for each SU ");
    // printVector(Utilization.AvgPerTimeSlot,"TimeSlot Average");
    // printVector(Utilization.AvgPerBand,"Band Average");
    // printVector(Throughput.AvgPerTimeSlot,"TimeSlot Average");
    // printVector(Throughput.AvgPerBand,"Band Average");
    printDeQueue(PU[0].PuBehaviorHistory);



    for (int i=0;i<PU.size();i++)
    {
        cout<<PU[i].Weight<<" ";
    }

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
