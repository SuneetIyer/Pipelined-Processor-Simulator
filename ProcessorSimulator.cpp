#include <fstream>
#include <iostream>
#include <iomanip>
#include <thread>
#include <unistd.h>
using namespace std;

ifstream ica("ICache.txt");
ifstream dca("DCache.txt");
ifstream rf("RF.txt");
ofstream out("Output.txt");

#define SLEEP_TIME 10000

int Instruction[128][4];
int Data[256];
int Register[16];
bool Invalid[16]= {0};
int Op[5]= {0};
bool Valid_Stage[5]= {0};
bool Stall=0;
bool InvalidFetch=0;
int LMD;
bool ignoreF=0;

int Instruction_Register[4];
int PC;
int R_source1, R_source2, R_destination[5];
int Temp_A;
int Temp_B;
int ALU_Output[5];
int Halt_Program=0;
int Total_Clock_Cycles=0;

int Tot_Ins = 0;
int Ari_Ins = 0;
int Log_Ins = 0;
int Data_Ins = 0;
int Con_Ins = 0;
int Hal_Ins = 0;
double CPI = 0;

int Tot_Stalls = 0;
int Data_Stalls = 0;
int Con_Stalls = 0;

int Condition_BEQZ = 0;

//Function to update the DCache after program is executed
void Update_DCache() {
    ofstream dcaOut("DCache.txt");
    for(int i=0;i<256;i++) {
        if(Data[i]<0)
            Data[i]+=256;
        char ch1=Data[i]/16+'0';
        char ch2=Data[i]%16+'0';
        if(ch1>'9') {
            ch1=ch1-'9'-1+'a';
        }
        if(ch2>'9') {
            ch2=ch2-'9'-1+'a';
        }
        dcaOut<<ch1<<ch2<<endl;
    }
    dcaOut.close();
}

//Convert a hexadecimal digit to decimal equivalent
int Hex_to_Dec(char x) {
    if(x<='9') return x-'0';
    else return x-'a'+10;
}

//Fetch the register file values, data cache values and instruction cache values
void Fetch_Input() {
    for(int i=0; i<128; i++) {
        for(int j=0; j<4; j++) {
            char x;
            ica>>x;
            Instruction[i][j] = Hex_to_Dec(x);
        }
    }

    for(int i=0; i<256; i++) {
        char x, y;
        dca>>x>>y;
        int tmp = Hex_to_Dec(x);
        tmp*=16;
        tmp += Hex_to_Dec(y);

        if(tmp>=128) tmp-=256;
        Data[i]=tmp;
    }

    for(int i=0; i<16; i++) {
        char x, y;
        rf>>x>>y;
        int tmp = Hex_to_Dec(x);
        tmp*=16;
        tmp += Hex_to_Dec(y);
        if(tmp>=128) tmp-=256;
        Register[i]=tmp;
    }
}

//Print the ICache
void Print_ICache() {
    cout<<"Instruction Cache"<<endl;
    for(int i=0; i<128; i++) {
        cout<<i<<" -> ";
        for(int j=0; j<4; j++) cout<<Instruction[i][j]<<" ";
        cout<<endl;
    }
}

//Print the DCache
void Print_DCache() {
    cout<<endl<<"Data Cache"<<endl;
    for(int i=0; i<256; i++) {
        cout<<i<<" -> "<<Data[i]<<endl;
    }
}

//Print RF
void Print_RF() {
    cout<<"Register Values"<<endl;
    for(int i=0; i<16; i++) {
        cout<<i<<" -> "<<Register[i]<<endl;
    }
}

void Print_All_Files() {
    Print_DCache();
    Print_ICache();
    Print_RF();
}

//Print Output file
void Print_Output() {
    Tot_Ins=Ari_Ins+Con_Ins+Log_Ins+Data_Ins+Hal_Ins;
    Tot_Stalls=Data_Stalls+Con_Stalls;
    //out<<endl<<"/******************************************"<<endl;
    out<<"Total number of instructions executed  : "<<Tot_Ins<<endl;

    out<<"Number of instructions in each class"<<endl;
    out<<"Arithmetic instructions\t\t: "<<Ari_Ins<<endl;
    out<<"Logical instructions\t\t\t: "<<Log_Ins<<endl;
    out<<"Data instructions\t\t\t: "<<Data_Ins<<endl;
    out<<"Control instructions\t\t\t: "<<Con_Ins<<endl;
    out<<"Halt instructions\t\t\t: "<<Hal_Ins<<endl;

    if(Tot_Ins!=0) CPI = (double)Total_Clock_Cycles/Tot_Ins;
    else CPI = -1;

    out<<"Cycles Per Instruction\t\t\t: "<<CPI<<endl<<endl;

    out<<"Total number of stalls\t\t\t: "<<Tot_Stalls<<endl;
    out<<"Data stalls (RAW)\t\t\t: "<<Data_Stalls<<endl;
    out<<"Control stalls\t\t\t\t: "<<Con_Stalls<<endl;
    //out<<"******************************************/"<<endl<<endl;

}

//ALU
class Arithmetic_Logic_Unit {
public:
    void Add() {
        ALU_Output [2]= (Temp_A + Temp_B)%256;
        if(ALU_Output[2]>=128)  ALU_Output[2]-=256;
        else if(ALU_Output[2]<-128) ALU_Output[2]+=256;
    }

    void Sub() {
        ALU_Output [2]= (Temp_A - Temp_B)%256;
        if(ALU_Output[2]>=128)  ALU_Output[2]-=256;
        else if(ALU_Output[2]<-128) ALU_Output[2]+=256;
    }

    void Mul() {
        ALU_Output [2]= (Temp_A * Temp_B)%256;
        if(ALU_Output[2]>=128)  ALU_Output[2]-=256;
        else if(ALU_Output[2]<-128) ALU_Output[2]+=256;
    }

    void Inc() {
        Add();
    }

    void And() {
        ALU_Output [2]= Temp_A & Temp_B;
    }

    void Or() {
        ALU_Output[2] = Temp_A | Temp_B;
    }

    void Not() {
        ALU_Output[2] = ~Temp_A;
    }

    void Xor() {
        ALU_Output[2] = Temp_A ^ Temp_B;
    }
};

Arithmetic_Logic_Unit ALU;

//IF stage
void IF(int time) {
    if(!Valid_Stage[0]||ignoreF)
        return;
    usleep(2*SLEEP_TIME);
    //Reading instruction into IR
    for(int i=0; i<4; i++) {
        Instruction_Register[i]=Instruction[PC][i];
    }
    PC++;   //Updating PC
    Op[0] = Instruction_Register[0];
    /*
    switch (Op[0]) {
        case 0: { cout<<"Add "<<"R"<<Instruction_Register[1]<<", "<<"R"<<Instruction_Register[2]<<", "<<"R"<<Instruction_Register[3]<<" "<<endl; break; }
        case 1: { cout<<"Sub "<<"R"<<Instruction_Register[1]<<", "<<"R"<<Instruction_Register[2]<<", "<<"R"<<Instruction_Register[3]<<" "<<endl; break; }
        case 2: { cout<<"Mul "<<"R"<<Instruction_Register[1]<<", "<<"R"<<Instruction_Register[2]<<", "<<"R"<<Instruction_Register[3]<<" "<<endl; break; }
        case 3: { cout<<"Inc "<<"R"<<Instruction_Register[1]<<", "<<"Ignore"<<Instruction_Register[2]<<", "<<"Ignore"<<Instruction_Register[3]<<" "<<endl; break; }
        case 4: { cout<<"And "<<"R"<<Instruction_Register[1]<<", "<<"R"<<Instruction_Register[2]<<", "<<"R"<<Instruction_Register[3]<<" "<<endl; break; }
        case 5: { cout<<"Or "<<"R"<<Instruction_Register[1]<<", "<<"R"<<Instruction_Register[2]<<", "<<"R"<<Instruction_Register[3]<<" "<<endl; break; }
        case 6: { cout<<"Not "<<"R"<<Instruction_Register[1]<<", "<<"R"<<Instruction_Register[2]<<", "<<"Ignore"<<Instruction_Register[3]<<" "<<endl; break; }
        case 7: { cout<<"Xor "<<"R"<<Instruction_Register[1]<<", "<<"R"<<Instruction_Register[2]<<", "<<"R"<<Instruction_Register[3]<<" "<<endl; break; }
        case 8: { cout<<"Load "<<"R"<<Instruction_Register[1]<<", "<<"R"<<Instruction_Register[2]<<", "<<"X = "<<Instruction_Register[3]<<" "<<endl; break; }
        case 9: { cout<<"Store "<<"R"<<Instruction_Register[1]<<", "<<"R"<<Instruction_Register[2]<<", "<<"X = "<<Instruction_Register[3]<<" "<<endl; break; }
        case 10: { cout<<"Jmp "<<endl; break; }
        case 11: { cout<<"Beqz "<<"R"<<Instruction_Register[1]<<", "<<endl; break; }
        case 15: { cout<<"Hlt"<<endl; break; }
    }
    */
    usleep(SLEEP_TIME);
}

//ID stage
void ID(int time) {
    if(!Valid_Stage[1])
        return;
    usleep(SLEEP_TIME);

    //ALU operations
    if(0<=Op[1] && Op[1]<=7) {
        if(ignoreF && (Invalid[R_source1] || Invalid[R_source2])) { //Checking if required operands are available
            Stall=1;
            Data_Stalls++;
            return;
        }
        else if(ignoreF) {
            Temp_A=Register[R_source1];
            Temp_B=Register[R_source2];
            Invalid[R_destination[1]]=1;
            if(Op[1]==3) {
                Temp_B=1;
            }
            return;
        }
        //Decoding instruction
        R_destination[1] = Instruction_Register[1];
        R_source1 = Instruction_Register[2];
        R_source2 = Instruction_Register[3];
        if(Op[1]==3) {
            R_source1=R_destination[1];
            R_source2=R_source1;
        }
        else if(Op[1]==6) {
            R_source2=Register[R_source1];
        }
        if(Invalid[R_source1] || Invalid[R_source2]) {  //Checking if operands are available
            Stall=1;
            Data_Stalls++;
            return;
        }
        Temp_A=Register[R_source1]; //If available write to ALU input
        Temp_B=Register[R_source2];
        Invalid[R_destination[1]]=1;
        if(Op[1]==3) {
            Temp_B=1;
        }
    }
    //Load-stores
    else if(Op[1]>=8 && Op[1]<=9) {
        if(Op[1]==8) {  //Load instruction
            if(ignoreF && (Invalid[R_source1])) {   //Checking availability of operands
                Stall=1;
                Data_Stalls++;
                return;
            }
            else if(ignoreF) {
                Temp_A=Register[R_source1];
                Temp_B=R_source2;
                Invalid[R_destination[1]]=1;
                if(Temp_B>=8)   Temp_B-=16;
                return;
            }
        }
        else {  //Store instruction
            if(ignoreF && (Invalid[R_source1] || Invalid[R_destination[1]])) {
                Stall=1;
                Data_Stalls++;
                return;
            }
            else if(ignoreF) {
                Temp_A=Register[R_source1];
                Temp_B=R_source2;
                if(Temp_B>=8)   Temp_B-=16;
                return;
            }
        }
        R_destination[1] = Instruction_Register[1];
        R_source1 = Instruction_Register[2];
        R_source2 = Instruction_Register[3];
        if(Op[1]==8) {  //Load instruction
            if(Invalid[R_source1]) {
                Stall=1;
                Data_Stalls++;
                return;
            }
            Temp_A=Register[R_source1];
            Temp_B=R_source2;
            Invalid[R_destination[1]]=1;
            if(Temp_B>=8)   Temp_B-=16;
        }
        else {  //Store instruction
            if(Invalid[R_source1] || Invalid[R_destination[1]]) {
                Stall=1;
                Data_Stalls++;
                return;
            }
            Temp_A=Register[R_source1];
            Temp_B=R_source2;
            if(Temp_B>=8)   Temp_B-=16;
        }
    }
    //Branch instructions
    else if(Op[1]>=10 && Op[1]<=11) {
        if(Op[1]==11) {
            if(ignoreF && Invalid[R_destination[1]]) {
                Data_Stalls++;
                Stall=1;
                Valid_Stage[0]=0;
                return;
            }
            else if(ignoreF) {
                Valid_Stage[0]=0;
                return;
            }
        }
        Valid_Stage[0]=0;                           //Current fetch invalidated
        Con_Stalls++;                               //Control stall incurred
        R_destination[1] = Instruction_Register[1];
        R_source1 = Instruction_Register[2];
        R_source2 = Instruction_Register[3];
        Temp_A = PC;
        if(Op[1]==11) {
            Temp_B = R_source1*16 + R_source2;
            if(Invalid[R_destination[1]]) {         //Checking operand availability for BEQZ instruction
                Data_Stalls++;
                Stall=1;
                return;
            }
        }
        else
            Temp_B = R_destination[1]*16 + R_source1;
        if(Temp_B>=128)
            Temp_B-=256;
    }
    //Halt
    else if(Op[1]==15) {
        InvalidFetch=1;
    }
    usleep(SLEEP_TIME);
}

void EX(int time) {
    if(!Valid_Stage[2])
        return;
    //ALU operations
    if(Op[2]<=7) {
        switch (Op[2]) {
            case 0: {
                ALU.Add();
                break;
            }
            case 1: {
                ALU.Sub();
                break;
            }
            case 2: {
                ALU.Mul();
                break;
            }
            case 3: {
                ALU.Inc();
                break;
            }
            case 4: {
                ALU.And();
                break;
            }
            case 5: {
                ALU.Or();
                break;
            }
            case 6: {
                ALU.Not();
                break;
            }
            case 7: {
                ALU.Xor();
                break;
            }
        }
    }
    if(Op[2]>=8 && Op[2]<=9) {  //Effective address calculation
        ALU.Add();
    }
    if(Op[2]>=10 && Op[2]<=11) {    //Target address calculation
        ALU.Add();
        //Valid_Stage[0]=0;
        Con_Stalls++;
        if(Op[2]==10) {
            PC=ALU_Output[2];
        }
        else {
            if(Register[R_destination[2]]==0)
                PC=ALU_Output[2];
            else
                PC-=1;
        }
    }
    if(Op[2]==15) {
        InvalidFetch=1;
    }
    usleep(SLEEP_TIME);
}

//Memory access stage
void MEM(int time) {
    if(!Valid_Stage[3])
        return;
    usleep(SLEEP_TIME);
    if(Op[3]==8)    LMD=Data[ALU_Output[3]];
    else if(Op[3]==9)   Data[ALU_Output[3]]=Register[R_destination[3]];
    if(Op[3]==15) {
        InvalidFetch=1;
    }
    usleep(SLEEP_TIME);
}

//Register write-back stage
void WB(int time) {
    if(!Valid_Stage[4])
        return;
    if(Op[4]>=0 && Op[4]<=3) Ari_Ins++;
    if(Op[4]>=4 && Op[4]<=7) Log_Ins++;
    if(Op[4]>=8 && Op[4]<=9) Data_Ins++;
    if(Op[4]>=10 && Op[4]<=11) Con_Ins++;
    if(Op[4]<=7) {
        Register[R_destination[4]] = ALU_Output[4];
        Invalid[R_destination[4]]=0;
    }
    else if(Op[4]==8) {
        Register[R_destination[4]] = LMD;
        Invalid[R_destination[4]]=0;
    }
    if(Op[4]==15) {
        Valid_Stage[0]=0;
        Halt_Program=1;
        Hal_Ins++;
    }
    usleep(SLEEP_TIME);
}

//Main method
int main() {
    Fetch_Input();
    //Print_All_Files();
    //Print_ICache();
    //Print_Output();

    PC=0;
    Valid_Stage[0]=1;

    for(int clock=1; true; clock++) {
        Stall=0;
        cout<<clock<<" -> "<<PC<<endl;
        //Print_RF();
        //cout<<endl;


        //Executing each pipeline stage in parallel
        thread th1(IF, clock);
        thread th2(ID, clock);
        thread th3(EX, clock);
        thread th4(MEM, clock);
        thread th5(WB, clock);

        th1.join();
        th2.join();
        th3.join();
        th4.join();
        th5.join();
        ignoreF=0;
        /*
        for(int i=0;i<5;i++) {
            cout<<Op[i]<<" "<<ALU_Output[i]<<" "<<R_destination[i]<<" "<<Valid_Stage[i]<<endl;
        }
        */
        //Forwarding the instructions in the pipeline
        if(InvalidFetch) {
            Valid_Stage[0]=0;
        }
        if(Stall) { //Stall in ID stage
            ignoreF=1;
            for(int i=4;i>2;i--) {
                Op[i]=Op[i-1];
                ALU_Output[i]=ALU_Output[i-1];
                R_destination[i]=R_destination[i-1];
                Valid_Stage[i]=Valid_Stage[i-1];
            }
            Valid_Stage[2]=0;
        }
        else {  //No stall
            for(int i=4;i>0;i--) {
                Op[i]=Op[i-1];
                ALU_Output[i]=ALU_Output[i-1];
                R_destination[i]=R_destination[i-1];
                Valid_Stage[i]=Valid_Stage[i-1];
            }
        }
        Valid_Stage[0]=1;
        if(Halt_Program==1) {
            Total_Clock_Cycles=clock;
            break;
        }
    }
    Print_Output();
    //Print_RF();
    //Print_DCache();
    ica.close();
    dca.close();
    rf.close();
    out.close();
    Update_DCache();
    return 0;
}
