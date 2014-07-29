// Copyright 2009-2014 Sandia Corporation. Under the terms // of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
// 
// Copyright (c) 2009-2014, Sandia Corporation
// All rights reserved.
// 
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include "sst_config.h"
#include "Statistics.h"

#include <fstream>
#include <stdio.h>
#include <string>
#include <time.h>
#include <string.h>

#include "AllocInfo.h"
#include "Allocator.h"
#include "FST.h"
#include "Job.h"
#include "Machine.h"
#include "MeshMachine.h"
#include "MeshAllocInfo.h"
#include "misc.h"
#include "output.h"
#include "Scheduler.h"
#include "TaskMapInfo.h"
#include "TaskMapper.h"

using namespace std;
using namespace SST::Scheduler;


struct logInfo {  //information about one type of log that can be created
    string logName;  //name and extension of log
    string header;   //information at top of log
};

const logInfo supportedLogs[] = {
    {"time", "\n# Job \tArrival\tStart\tEnd\tRun\tWait\tResp.\tProcs\n"},
    {"alloc", "\n# Job\tProcs\tActual Time\t Avg Pairwise L1 Distance\n"},
    {"visual", ""},   //requires special header
    {"util", "\n# Time\tUtilization\n"},
    {"wait", "\n# Time\tWaiting Jobs\n"}
};

const logInfo supportedLogsFST[] = {
    {"time", "\n# Job \tArrival\tStart\tEnd\tRun\tWait\tResp.\tProcs\tFST\n"},
    {"alloc", "\n# Procs Needed\tActual Time\t Avg Pairwise L1 Distance\n"},
    {"visual", ""},   //requires special header
    {"util", "\n# Time\tUtilization\n"},
    {"wait", "\n# Time\tWaiting Jobs\n"}
};

const int numSupportedLogs = 5;

enum LOGNAME {  //to use symbolic names on logs; must be updated with supportedLogs
    TIME = 0,
    ALLOC = 1,
    VISUAL = 2,
    UTIL = 3,
    WAIT = 4
};
/*
   UTIL = 1,
   WAIT = 2};
   */

void Statistics::printLogList(ostream& out) 
{  //print list of possible logs
    for (int i = 0; i < numSupportedLogs; i++) {
        out << "  " << supportedLogs[i].logName << endl;
    }
}

Statistics::Statistics(Machine* machine, Scheduler* sched, Allocator* alloc, TaskMapper* taskMap,
                       string baseName, char* logList, bool simulation, FST* incalcFST) 
{
    this -> simulation = simulation;
    this -> calcFST = incalcFST;
    schedout.init("", 8, ~0, Output::STDOUT);
    size_t pos = baseName.rfind("/");
    if (pos == string::npos) {
        this -> baseName = baseName;  //didn't find it so entire given string is base
    } else {
        this -> baseName = baseName.substr(pos+1);
    }

    this -> machine = machine;
    currentTime = 0;
    procsUsed = 0;

    //initialize outputDirectory
    char* dir = getenv("SIMOUTPUT");
    if (NULL == dir) {
        outputDirectory = "./";
    } else {
        outputDirectory = dir;
    }

    //initialize fileHeader
    time_t raw;
    time(&raw);
    struct tm* structured = localtime(&raw);
    fileHeader= "# Simulation for trace " + baseName +
        " started " + asctime(structured) + "# [Machine] \n" +
        machine -> getSetupInfo(true) + "\n# [Scheduler] \n" +
        sched -> getSetupInfo(true) + "\n# [Allocator] \n" +
        alloc -> getSetupInfo(true) + "\n# [TaskMapper] \n" +
        taskMap -> getSetupInfo(true) + "\n";

    record = new bool[numSupportedLogs];
    for (int i = 0; i < numSupportedLogs; i++) {
        record[i] = false;
    }
    char* logName = strtok(logList, ",");
    while(NULL != logName) {
        bool found = false;
        for (int i = 0; !found && i < numSupportedLogs; i++) {
            if (NULL == calcFST) {
                if (logName == supportedLogs[i].logName) {
                    found = true;

                    if ((NULL == (MeshMachine*)machine) && ((ALLOC == i) || (VISUAL == i))) {
                        //error(string(logName) + " log only implemented for meshes");
                        schedout.fatal(CALL_INFO, 1, "%s log only implemented for meshes", string(logName).c_str());
                    }

                    initializeLog(logName);
                    if (supportedLogs[i].header.length() > 0) {
                        appendToLog(supportedLogs[i].header, supportedLogs[i].logName);
                    }
                    /*
                       if(i == VISUAL) {
                       char mesg[100];
                       sprintf(mesg, "MESH %d %d %d\n\n", mesh -> getXDim(),
                       mesh -> getYDim(), mesh -> getZDim());
                       appendToLog(mesg, supportedLogs[VISUAL].logName);
                       }
                       */
                    record[i] = true;
                } 
            } else {
                if (logName == supportedLogsFST[i].logName) {
                    found = true;

                    if ((NULL == (MeshMachine*)machine) && ((ALLOC == i) || (VISUAL == i))) {
                        //error(string(logName) + " log only implemented for meshes");
                        schedout.fatal(CALL_INFO, 1, "%s log only implemented for meshes", string(logName).c_str());
                    }

                    initializeLog(logName);
                    if (supportedLogsFST[i].header.length() > 0) {
                        appendToLog(supportedLogsFST[i].header, supportedLogsFST[i].logName);
                    }
                    /*
                       if(i == VISUAL) {
                       char mesg[100];
                       sprintf(mesg, "MESH %d %d %d\n\n", mesh -> getXDim(),
                       mesh -> getYDim(), mesh -> getZDim());
                       appendToLog(mesg, supportedLogs[VISUAL].logName);
                       }
                       */
                    record[i] = true;
                }
            }
        }
        if (!found) {
            //error(string("invalid log name: ") + logName);
            schedout.fatal(CALL_INFO, 1, "%s%s", string("invalid log name: ").c_str(), logName);
        }

        logName = strtok(NULL, ",");
    }

    lastUtil = 0;
    lastUtilTime = -1;

    lastWaitTime = -1;
    lastWaitJobs = -1;
    waitingJobs = 0;
    tempWaiting = 0;
}

Statistics::~Statistics() 
{
    delete[] record;
}

//called when a job has arrived; update our statistics accordingly.
void Statistics::jobArrives(unsigned long time) 
{   
    tempWaiting++;
    if(record[WAIT])
        writeWaiting(time);
}

//called every time a job starts
void Statistics::jobStarts(TaskMapInfo* tmi, unsigned long time) 
{
    if (record[ALLOC]) {
        writeAlloc(tmi);
    }
    /*
       if(record[VISUAL]) {
       char mesg[100];
       sprintf(mesg, "BEGIN %ld ", allocInfo -> job -> getJobNum());
       writeVisual(mesg + allocInfo -> getProcList());
       }
       */

    procsUsed += tmi -> job -> getProcsNeeded();
    if (record[UTIL]) {
        writeUtil(time);
    }

    tempWaiting--;
    if (record[WAIT]) {
        writeWaiting(time);
    }

    currentTime = time;
}

//called every time a job completes
void Statistics::jobFinishes(TaskMapInfo* tmi, unsigned long time) 
{ 
    /*
       if(record[VISUAL]) {
       char mesg[100];
       sprintf(mesg, "END %ld", allocInfo -> job -> getJobNum());
       writeVisual(mesg);
       }
       */

    if (record[TIME]) {
        writeTime(tmi->allocInfo, time);
    }

    procsUsed -= tmi -> job -> getProcsNeeded();

    if (record[UTIL]) {
        writeUtil(time);
    }

    currentTime = time;
}

//Write time statistics to the log.
void Statistics::writeTime(AllocInfo* allocInfo, unsigned long time) 
{

    unsigned long arrival = allocInfo -> job -> getArrivalTime();
    //unsigned long runtime = allocInfo -> job -> getActualTime();
    unsigned long startTime = allocInfo -> job -> getStartTime();
    unsigned long runtime = time - startTime;
    int procsneeded = allocInfo -> job -> getProcsNeeded();
    long jobNum = allocInfo -> job -> getJobNum();

    char mesg[100];
    if (NULL == calcFST) {
        sprintf(mesg, "%ld\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu\t%d\n",
                jobNum,                           //Job Num
                arrival,			      //Arrival time
                startTime,                        //Start time(currentTime)
                time,                             //End time
                runtime,                          //Run time
                (startTime - arrival),            //Wait time
                (time - arrival),                 //Response time
                procsneeded                      //Processors needed
               );    
    } else {
        sprintf(mesg, "%ld\t%lu\t%lu\t%lu\t%lu\t%lu\t%lu\t%d\t%lu\n",
                jobNum,                           //Job Num
                arrival,			      //Arrival time
                startTime,                        //Start time(currentTime)
                time,                             //End time
                runtime,                          //Run time
                (startTime - arrival),            //Wait time
                (time - arrival),                 //Response time
                procsneeded,                      //Processors needed
                calcFST -> getFST(jobNum));    //FST	                    
    }
    appendToLog(mesg, supportedLogs[TIME].logName);
}


//Write allocation information to the log.
void Statistics::writeAlloc(TaskMapInfo* tmi) 
{
    MeshMachine* mMachine = dynamic_cast<MeshMachine*>(machine);
    char mesg[100];
    int num = tmi -> job -> getProcsNeeded();
    sprintf(mesg, "%ld\t%d\t%lu\t%f\n",
            tmi -> job -> getJobNum(),
            num,
            tmi -> job -> getActualTime(),
            tmi -> getAvgHopDist(*mMachine) );
    appendToLog(mesg, supportedLogs[ALLOC].logName);
}

//Write to log for visualization.
void Statistics::writeVisual(string mesg) 
{
    appendToLog(mesg + "\n", supportedLogs[VISUAL].logName);
}


//Method to write utilization statistics to file;
//force it to write last entry by setting time = -1.
void Statistics::writeUtil(unsigned long time) 
{
    if ((unsigned long)-1 == lastUtilTime) {  //if first observation, just remember it
        lastUtil = procsUsed;
        lastUtilTime = time;
        return;
    }

    if ((procsUsed == lastUtil) && ((unsigned long)-1 != time))   {
        return;  //don't record if utilization unchanged unless forced
    }
    if (lastUtilTime == time) {  //update record of utilization for this time
        lastUtil = procsUsed;
    } else {  //actually record the previous utilization
        char mesg[100];
        sprintf(mesg, "%lu\t%d\n", lastUtilTime, lastUtil);
        appendToLog(mesg, supportedLogs[UTIL].logName);
        lastUtil = procsUsed;
        lastUtilTime = time;
    }
}

//possibly add line to log recording number of waiting jobs
//  (only prints 1 line per time: #waiting jobs after all events at that time)
//argument is current time or -1 at end of trace
void Statistics::writeWaiting(unsigned long time) 
{
    if ((unsigned long)-1 == lastWaitTime) {  //if first observation, just remember it
        lastWaitTime = time;
        return;
    }

    if (lastWaitTime == time) {  //update record of waiting jobs for this time
        waitingJobs = tempWaiting;
        return;
    } else {  //actually record the previous # waiting jobs
        if (lastWaitJobs != waitingJobs) {
            char mesg[100];
            sprintf(mesg, "%lu\t%d\n", lastWaitTime, waitingJobs);
            appendToLog(mesg, supportedLogs[WAIT].logName);
        }

        lastWaitJobs = waitingJobs;
        lastWaitTime = time;
        waitingJobs = tempWaiting;
    }
}

//called after all events have occurred
void Statistics::done() {  
    if (record[UTIL]) {
        writeUtil(-1);
    }

    if (record[WAIT]) {
        writeWaiting(-1);
    }
}

void Statistics::initializeLog(string extension) 
{
    string name = outputDirectory + baseName + "." + extension;
    ofstream file(name.c_str(), ios::out | ios::trunc);
    if (file.is_open()) {
        file << fileHeader;
    } else {
        //error("Unable to open file " + name);
        schedout.fatal(CALL_INFO, 1, "Unable to open file %s", name.c_str());
    }
    file.close();
}

void Statistics::appendToLog(string mesg, string extension) 
{
    string name = outputDirectory + baseName+ "." + extension;
    ofstream file(name.c_str(), ios::out | ios::app);
    if (file.is_open()) {
        file << mesg;
    } else {
        //error("Unable to open file " + name);
        schedout.fatal(CALL_INFO, 1, "Unable to open file %s", name.c_str());
    }
    file.close();
}
