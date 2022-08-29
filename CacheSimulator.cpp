#include <fstream>
#include <iostream>
#include <random>
#include <vector>
#include <utility>
#include <set>
using namespace std;

unsigned int size,blockSize,associativity,replacementPolicy;

//Struct to represent a block in the cache memory
struct cacheBlock
{
    int tag;
    bool valid;
    bool dirty;
    struct cacheBlock* next;
};

//Definition of class cache
class Cache
{
private :
    struct cacheBlock* cache;
    int numSets;
    int numWays;
    int* num;                                       //Stores the number of valid blocks in each set of the cache
    set<int> processed;                             //To check whether a block previously in cache is referenced
    int numAcc;
    int numRd;
    int numWr;
    int numMisses;
    int numCompulsoryMisses;
    int numConflictMisses;
    int numReadMisses;
    int numWriteMisses;
    int numDirtyEvicted;
    void process(pair<unsigned int,char> x);        //Private method to service each request

    //Functions for Pseudo LRU
    int** PLRU_Tree;                                //Stores the binary tree for each set
    int PLRU_Get_Victim(int Set_Index);             //Gets the victim node in each set, based on PLRU_Tree
    void PLRU_Update(int Set_Index, int Node);      //Updates all parent node values in tree to not point given node


public :
    void readTraces(ifstream& traces);              //Reads memory traces from specified file
    void printDetails();                            //Prints details
    Cache();
};

Cache::Cache()
{
    if(associativity==0)                            //Fully associative
    {
        numSets=1;
        numWays=size/blockSize;
    }
    else
    {
        numSets=size/(blockSize*associativity);
        numWays=associativity;
    }

    cache=new struct cacheBlock[numSets];           //Cache as array of cache blocks
    num=new int[numSets];                           //Stores the number of valid blocks in each set of the cache
    PLRU_Tree = new int*[numSets];                  //Tree for each set

    numAcc=0;
    numRd=0;
    numWr=0;
    numMisses=0;
    numCompulsoryMisses=0;
    numConflictMisses=0;
    numReadMisses=0;
    numWriteMisses=0;
    numDirtyEvicted=0;

    for(int i=0; i<numSets; i++)                    //Initializing data
    {
        num[i]=0;
        cache[i].dirty=false;
        cache[i].valid=false;
        cache[i].tag=0;
        cache[i].next=NULL;
        struct cacheBlock* temp=&cache[i];
        PLRU_Tree[i] = new int[numWays];            //PLRU Tree for  set i, with NumWays node.

        for(int j=0; j<numWays; j++)                //Creating a linked list corresponding to each set in the cache
        {
            temp->next=new struct cacheBlock;       //Initializing each block
            temp=temp->next;
            temp->next=NULL;
            temp->dirty=false;
            temp->valid=false;
            temp->tag=0;
            PLRU_Tree[i][j] = 0;                    //Initially all pointing to left = 0
        }
    }
}

//Gets the victim node in each set, based on PLRU_Tree
int Cache::PLRU_Get_Victim(int Set_Index)
{
    //The tree will have nodes from 1 to NumWays-1,
    // and the leaves after this will be the index to Cache Blocks

    int Index = 1;                                  // Root Node
    int mx = numWays;                               //After this all node will be leaves

    while(Index<mx)
    {
        int cur = Index;                            //Current Node
        int Dir = PLRU_Tree[Set_Index][cur];        //Direction from current node, 0 = Left, 1 = Right.
        Index = 2*cur + Dir;                        //Child Node
        PLRU_Tree[Set_Index][cur] = (Dir+1)%2;      //Changes state of current node
    }

    return (Index - mx);
}

//Updates all parent node values in tree to not point away from recently accessed node
void Cache::PLRU_Update(int setIndex, int node)
{
    int mx = numWays;
    int lst = node + mx;                            //Index of Leaf Node.

    while(lst>1)
    {
        int cur=lst/2;                              //Parent Node
        int dir = lst%2;                            //Direction for last node from parent
        dir = (dir+1)%2;                            //Change the direction
        PLRU_Tree[setIndex][cur]=dir;
        lst=cur;                                    //Go to parent
    }
}

//Function to process each request to the cache
//The function checks in cache for the presence of the block, if present, cache hit
//If absent, cache miss occurs and is handled differently based on the replacement policy employed
void Cache::process(pair<unsigned int,char> x)
{
    numAcc++;                                       //Count no. of accesses

    char reqType = x.second;
    unsigned int memAddress = x.first;

    unsigned int tagVal = memAddress/(numSets*blockSize);                       //Tag value is calculated by dividing away the bits corresponding to the set index and block offset
    unsigned int setIndex = (memAddress%(numSets*blockSize))/blockSize;         //Set index is calculated by taking the bits to the right of the tag bits and dividing away the block offset
    unsigned int blockOffset = memAddress%blockSize;                            //Remaining bits are block offset

    if(reqType=='w') numWr++;
    else numRd++;


    //Searching the set for the required tag value
    struct cacheBlock* temp = &cache[setIndex];
    for(int j=0; j<numWays; j++)
    {
        temp=temp->next;
        if(temp->valid && temp->tag==tagVal)                                    //Cache hit
        {
            if(reqType=='w') temp->dirty=true;                                  //Write to block => block is dirty
            if(replacementPolicy==1)
            {
                //Pushing the current accessed cache block to front of list of this set
                struct cacheBlock* temp2 = &cache[setIndex];
                while (temp2->next != temp)
                {
                    temp2 = temp2->next;
                }
                temp2->next = temp->next;
                temp2=&cache[setIndex];
                temp->next = temp2->next;
                temp2->next = temp;
            }
            else if(replacementPolicy==2)
            {
                //Update the tree based on the block accessed
                PLRU_Update(setIndex, j);
            }
            return;         //Return after request processed
        }
    }
    //Search complete, block not found, so cache miss

    numMisses++;
    if(reqType=='w') numWriteMisses++;
    else numReadMisses++;

    //If block is accessed for the first time, compulsory miss
    if(processed.count(memAddress/blockSize)) numConflictMisses++;
    else numCompulsoryMisses++;

    processed.insert(memAddress/blockSize);

    temp=&cache[setIndex];

    if(replacementPolicy!=2 && num[setIndex]<numWays)       //If invalid blocks are present, evict them first
    {
        for(int j=0; j<numWays; j++)
        {
            temp=(temp->next);
            if(!temp->valid)
            {
                temp->valid=true;

                if(reqType=='w') temp->dirty = true;
                else temp->dirty = false;

                temp->tag=tagVal;
                num[setIndex]++;
                if (replacementPolicy==1) {
                    // Pushing the current accessed cache block to front of list of this set
                    struct cacheBlock* temp2=&cache[setIndex];
                    while (temp2->next != temp)
                    {
                        temp2 = temp2->next;
                    }

                    temp2->next = temp->next;
                    temp2=&cache[setIndex];
                    temp->next = temp2->next;
                    temp2->next = temp;
                }
                break;
            }
        }
    }
    else
    {
        if(replacementPolicy==0)                                        //Cache set is full and cache miss
        {
            default_random_engine generator;
            uniform_int_distribution<int> distribution(0,numWays-1);

            int victim=distribution(generator);                         //Choose random victim

            for(int j=0; j<=victim; j++)
            {
                temp=(temp->next);
            }

            if(temp->dirty) numDirtyEvicted++;
            //Victim evicted
            temp->tag=tagVal;
            temp->valid=true;

            if(reqType=='w') temp->dirty = true;
            else temp->dirty = false;
        }
        else if(replacementPolicy==1)
        {

            // Pointing to the last cache block of current set
            for(int j=0; j<numWays; j++)
            {
                temp=(temp->next);
            }

            // If block is dirty write back and evict
            if(temp->dirty) numDirtyEvicted++;

            // New block with tag value is replaced with evicted block
            temp->tag=tagVal;
            temp->valid=true;

            // If this is a write request then dirty = true , else false
            if(reqType=='w') temp->dirty = true;
            else temp->dirty = false;

            // Pushing the current accessed cache block to front of list of this set
            struct cacheBlock* temp2=&cache[setIndex];
            while (temp2->next != temp)
            {
                temp2 = temp2->next;
            }

            temp2->next = temp->next;
            temp2=&cache[setIndex];
            temp->next = temp2->next;
            temp2->next = temp;

        }
        else if(replacementPolicy==2)
        {
            //Pseudo LRU

            //Gets The Victim
            int victim = PLRU_Get_Victim(setIndex);

            //Evict victim
            temp=&cache[setIndex];
            for(int j=0; j<=victim; j++)
            {
                temp=(temp->next);
            }

            if(temp->dirty) numDirtyEvicted++;

            temp->tag=tagVal;
            temp->valid=true;

            if(reqType=='w') temp->dirty = true;
            else temp->dirty = false;
        }
    }
}

//Reads input memory traces from specified file
//Converts the 8-digit hexadecimal address to 32-bit unsigned integer
void Cache::readTraces(ifstream& traces)
{
    string in;
    vector <pair<unsigned int,char>> memTraces;
    while(!traces.eof())
    {
        if(!(traces>>in))
        {
            break;
        }

        unsigned int eqInt=0;                   //Stores equivalent integer representation
        for(int i=2; i<10; i++)
        {
            char ch=in[i];
            eqInt*=16;                          //Shift left by 4 bits

            if(ch>='0' && ch<='9')              //Add 0 to 9 depending on character
            {
                eqInt+=(ch-'0');
            }
            else
            {
                eqInt+=(ch-'a'+10);             //Add 10 to 15 depending on character
            }
        }

        char reqType;
        traces>>reqType;                        //Read-write request

        memTraces.push_back({eqInt,reqType});
    }

    for(auto x:memTraces)                       //Processing each request
    {
        process(x);
    }
}


//Function to print the required details
void Cache::printDetails()
{
    cout<<numAcc<<endl;
    cout<<numRd<<endl;
    cout<<numWr<<endl;
    cout<<numMisses<<endl;
    cout<<numCompulsoryMisses<<endl;

    if(numSets==1) {
        cout<<numConflictMisses<<endl;
        cout<<0<<endl;
    }
    else {
        cout<<0<<endl;
        cout<<numConflictMisses<<endl;
    }

    cout<<numReadMisses<<endl;
    cout<<numWriteMisses<<endl;
    cout<<numDirtyEvicted<<endl;
}


/***********
Main method
***********/
int main()
{
    //Reading inputs from file input.txt
    ifstream inputs("input.txt");
    string traceFile;

    inputs>>size;
    inputs>>blockSize;
    inputs>>associativity;
    inputs>>replacementPolicy;
    inputs>>traceFile;
    //File from which memory traces are read
    ifstream traces(traceFile);

    //Printing cache specifications
    cout<<size<<endl;
    cout<<blockSize<<endl;
    if(associativity==0)
    {
        cout<<"Fully associative cache"<<endl;
    }
    else if(associativity==1)
    {
        cout<<"Direct-mapped cache"<<endl;
    }
    else
    {
        cout<<associativity<<"-way set-associative"<<endl;
    }

    if(replacementPolicy==0)
    {
        cout<<"Random Replacement"<<endl;
    }
    else if(replacementPolicy==1)
    {
        cout<<"LRU Replacement"<<endl;
    }
    else
    {
        cout<<"Pseudo-LRU Replacement"<<endl;
    }

    
    Cache C;
    C.readTraces(traces);
    C.printDetails();

    inputs.close();
    traces.close();
    return 0;
}
