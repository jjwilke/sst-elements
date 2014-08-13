// Copyright 2009-2013 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
// 
// Copyright (c) 2009-2013, Sandia Corporation
// All rights reserved.
// 
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include "sst_config.h"
#include "Factory.h"

#include <fstream>
#include <sstream>
#include <stdio.h>
#include <string>

#include <sst/core/params.h>

#include "Machine.h"
#include "MeshMachine.h"
#include "schedComponent.h"
#include "SimpleMachine.h"
#include "InputParser.h"
 
#include "allocators/NearestAllocator.h"
#include "allocators/OctetMBSAllocator.h"
#include "allocators/BestFitAllocator.h"
#include "allocators/ConstraintAllocator.h"
#include "allocators/EnergyAllocator.h"
#include "allocators/FirstFitAllocator.h"
#include "allocators/GranularMBSAllocator.h"
#include "allocators/LinearAllocator.h"
#include "allocators/MBSAllocator.h"
#include "allocators/RandomAllocator.h"
#include "allocators/RoundUpMBSAllocator.h"
#include "allocators/SimpleAllocator.h"
#include "allocators/SortedFreeListAllocator.h"

#include "schedulers/EASYScheduler.h"
#include "schedulers/PQScheduler.h"
#include "schedulers/StatefulScheduler.h"

#include "taskMappers/RandomTaskMapper.h"
#include "taskMappers/RCBTaskMapper.h"
#include "taskMappers/SimpleTaskMapper.h"
#include "taskMappers/TopoMapper.h"

#include "allocMappers/NearestAllocMapper.h"
#include "allocMappers/SpectralAllocMapper.h"

using namespace SST::Scheduler;
using namespace std;

/* 
 * Factory file helps parse the parameters in the sdl file
 * returns correct type of machine, allocator, and scheduler
 */

const Factory::schedTableEntry Factory::schedTable[] = {
    {PQUEUE, "pqueue"},
    {EASY, "easy"},
    {CONS, "cons"},
    {PRIORITIZE, "prioritize"},
    {DELAYED, "delayed"},
    {ELC, "elc"},
};

const Factory::machTableEntry Factory::machTable[] = {
    {SIMPLEMACH, "simple"},
    {MESH, "mesh"},
};

const Factory::allocTableEntry Factory::allocTable[] = {
    {SIMPLEALLOC, "simple"},
    {RANDOM, "random"},
    {NEAREST, "nearest"},
    {GENALG, "genalg"},
    {MM, "mm"},
    {MC1X1, "mc1x1"},
    {OLDMC1X1, "oldmc1x1"},
    {MBS, "mbs"},
    {GRANULARMBS, "granularmbs"},
    {OCTETMBS, "octetmbs"},
    {FIRSTFIT, "firstfit"},
    {BESTFIT, "bestfit"},
    {SORTEDFREELIST, "sortedfreelist"},
    {CONSTRAINT, "constraint"},
    {ENERGY, "energy"},
    {HYBRID, "hybrid"},
    {NEARESTAMAP, "nearestamap"},
    {SPECTRALAMAP, "spectralamap"},
};

const Factory::taskMapTableEntry Factory::taskMapTable[] = {
    {SIMPLEMAP, "simple"},
    {RCBMAP, "rcb"},
    {RANDOMMAP, "random"},
    {TOPOMAP, "topo"},
    {RCMMAP, "rcm"},
    {NEARESTAMT, "nearestamap"},
    {SPECTRALAMT, "spectralamap"},
};

const Factory::FSTTableEntry Factory::FSTTable[] = {
    {NONE, "none"},
    {RELAXED, "relaxed"},
    {STRICT, "strict"},
};

Factory::Factory() 
{
    schedout.init("", 8, 0, Output::STDOUT);
}

Scheduler* Factory::getScheduler(SST::Params& params, int numNodes)
{
    if(params.find("scheduler") == params.end()){
        schedout.verbose(CALL_INFO, 1, 0, "Defaulting to Priority Scheduler with FIFO queue\n");
        return new PQScheduler(PQScheduler::JobComparator::Make("fifo"));
    }
    else
    {
        int filltimes = 0;
        vector<string>* schedparams = parseparams(params["scheduler"]);
        if(schedparams->size() == 0)
            schedout.fatal(CALL_INFO, 1, "Error in parsing scheduler parameter");
        switch(schedulername(schedparams->at(0)))
        {
            //Priority Queue Scheduler
        case PQUEUE:
            schedout.debug(CALL_INFO, 4, 0, "Priority Queue Scheduler\n");
            if(schedparams->size() == 1)
                return new PQScheduler(PQScheduler::JobComparator::Make("fifo"));
            else
                return new PQScheduler(PQScheduler::JobComparator::Make(schedparams->at(1)));
            break;

            //EASY Scheduler
        case EASY:
            schedout.debug(CALL_INFO, 4, 0, "Easy Scheduler\n");
            if (schedparams -> size() == 1) {
                return new EASYScheduler(EASYScheduler::JobComparator::Make("fifo"));
            } else if (schedparams -> size() == 2) {
                EASYScheduler::JobComparator* comp = EASYScheduler::JobComparator::Make(schedparams->at(1));
                if (comp == NULL) {
                    schedout.fatal(CALL_INFO, 1, "Argument to Easy Scheduler parameter not found:%s", schedparams->at(1).c_str());
                }
                return new EASYScheduler(comp);
            } else {
                schedout.fatal(CALL_INFO, 1, "EASY Scheduler requires 1 or 0 parameters (determines type of queue or defaults to FIFO");
            }
            break;

            //Stateful Scheduler with Convervative Manager
        case CONS:
            schedout.debug(CALL_INFO, 4, 0, "Conservative Scheduler\n");
            if (schedparams -> size() == 1) {
                return new StatefulScheduler(numNodes, StatefulScheduler::JobComparator::Make("fifo"), true);
            } else {
                return new StatefulScheduler(numNodes, StatefulScheduler::JobComparator::Make(schedparams->at(1)), true);
            }
            break;

            //Stateful Scheduler with PrioritizeCompression Manager
        case PRIORITIZE:
            schedout.debug(CALL_INFO, 4, 0, "Prioritize Scheduler\n");
            if (schedparams -> size() == 1) {
                schedout.fatal(CALL_INFO, 1, "PrioritizeCompression scheduler requires number of backfill times as an argument");
            }
            filltimes = strtol(schedparams->at(1).c_str(),NULL,0);
            if (2 == schedparams -> size()) {
                return new StatefulScheduler(numNodes, StatefulScheduler::JobComparator::Make("fifo"),filltimes);
            } else {
                return new StatefulScheduler(numNodes, StatefulScheduler::JobComparator::Make(schedparams->at(2)), filltimes);
            }
            break;

            //Stateful Scheduler with Delayed Compression Manager
        case DELAYED:
            schedout.debug(CALL_INFO, 4, 0, "Delayed Compression Scheduler\n");
            if (schedparams -> size() == 1) {
                return new StatefulScheduler(numNodes, StatefulScheduler::JobComparator::Make("fifo"));
            } else {
                return new StatefulScheduler(numNodes, StatefulScheduler::JobComparator::Make(schedparams -> at(1)));
            }
            break;

            //Stateful Scheduler with Even Less Conservative Manager
        case ELC:
            schedout.debug(CALL_INFO, 4, 0, "Even Less Convervative Scheduler\n");
            if (schedparams -> size() == 1) {
                schedout.fatal(CALL_INFO, 1, "Even Less Conservative scheduler requires number of backfill times as an argument");
            }
            filltimes = strtol(schedparams->at(1).c_str(),NULL,0);
            if (schedparams -> size() == 2) {
                return new StatefulScheduler(numNodes, StatefulScheduler::JobComparator::Make("fifo"),filltimes, true);
            } else {
                return new StatefulScheduler(numNodes, StatefulScheduler::JobComparator::Make(schedparams -> at(2)), filltimes, true);
            }
            break;

            //Default: scheduler name not matched
        default:
            schedout.fatal(CALL_INFO, 1, "Could not parse name of scheduler");
        }

    }
    return NULL; //control never reaches here
}

//returns the correct machine based on the parameters
Machine* Factory::getMachine(SST::Params& params, int numNodes)
{
    Machine* retMachine = NULL;
    double** D_matrix = NULL;
    
    //get the heat recirculation matrix if available
    string dMatrixFile = "none";
    if( params.find("dMatrixFile") != params.end() ){
        dMatrixFile = params["dMatrixFile"];
    }
    if (dMatrixFile.compare("none") == 0 ) { 
        //default: no recuirculation
        schedout.verbose(CALL_INFO, 4, 0, "Defaulting to no heat recirculation\n");
    } else {
        DParser dParser = DParser(numNodes, params);
        D_matrix = dParser.readDMatrix();
    }
    
    int coresPerNode = 1;
    if( params.find("coresPerNode") != params.end() ){
        coresPerNode = strtol(params["coresPerNode"].c_str(), NULL, 0);
    }
    
    //get machine
    if (params.find("machine") == params.end()) {
        //default: FIFO queue priority scheduler
        schedout.verbose(CALL_INFO, 4, 0, "Defaulting to Simple Machine\n");
        retMachine = new SimpleMachine(numNodes, false, coresPerNode, D_matrix);
    }
    else
    {
        vector<string>* schedparams = parseparams(params["machine"]);
        switch(machinename(schedparams -> at(0)))
        {
            //simple machine
        case SIMPLEMACH:
            schedout.debug(CALL_INFO, 4, 0, "Simple Machine\n");
            retMachine = new SimpleMachine(numNodes, false, coresPerNode, D_matrix);
            break;

            //Mesh Machine
        case MESH:
            {
                schedout.debug(CALL_INFO, 4, 0, "Mesh Machine\n");

                if (schedparams -> size() != 3 && schedparams -> size() != 4) {
                    schedout.fatal(CALL_INFO, 1, "Wrong number of arguments for Mesh Machine:\nNeed 3 (x, y, and z dimensions) or 2 (z defaults to 1)");
                }
                int x = strtol(schedparams -> at(1).c_str(), NULL, 0); 
                int y = strtol(schedparams -> at(2).c_str(), NULL, 0); 
                int z;
                if (schedparams -> size() == 4) {
                    z = strtol(schedparams -> at(3).c_str(), NULL, 0); 
                } else {
                    z = 1;
                }
                if (x * y * z != numNodes) {
                    schedout.fatal(CALL_INFO, 1, "The dimensions of the mesh do not correspond to the number of nodes");
                }
                retMachine = new MeshMachine(x, y, z, coresPerNode, D_matrix);
                break;

            }
        default:
            schedout.fatal(CALL_INFO, 1, "Cannot parse name of machine");
        }
    }
        
    return retMachine;
}


//returns the correct allocator based on the parameters
Allocator* Factory::getAllocator(SST::Params& params, Machine* m, schedComponent* sc)
{
    MeshMachine *mMachine = dynamic_cast<MeshMachine*>(m);
    if (params.find("allocator") == params.end()) {
        //default: FIFO queue priority scheduler
        schedout.verbose(CALL_INFO, 4, 0, "Defaulting to Simple Allocator\n");
        SimpleMachine* mach = dynamic_cast<SimpleMachine*>(m);
        if (mach == NULL) {
            schedout.fatal(CALL_INFO, 1, "Simple Allocator requires SimpleMachine");
        }
        return new SimpleAllocator(mach);
    } else {
        vector<string>* schedparams = parseparams(params["allocator"]);
        vector<string>* nearestparams = NULL;
        switch (allocatorname(schedparams -> at(0)))
        {
            //Simple Allocator for use with simple machine
        case SIMPLEALLOC:
            {
                schedout.debug(CALL_INFO, 4, 0, "Simple Allocator\n");

                SimpleMachine* mach = dynamic_cast<SimpleMachine*>(m);
                if (mach == NULL) {
                    schedout.fatal(CALL_INFO, 1, "SimpleAllocator requires SimpleMachine");
                }
                return new SimpleAllocator(mach);
                break;
            }

            //Random Allocator, allocates nodes randomly from a mesh
        case RANDOM:
            schedout.debug(CALL_INFO, 4, 0, "Random Allocator\n");
            return new RandomAllocator(m);
            break;

            //Nearest Allocators try to minimize distance between nodes
            //according to various metrics
        case NEAREST:
            schedout.debug(CALL_INFO, 4, 0, "Nearest Allocator\n");
            nearestparams = new vector<string>;
            for (int x = 0; x < (int)schedparams -> size(); x++) {
                nearestparams -> push_back(schedparams -> at(x));
            }
            return new NearestAllocator(nearestparams, m);
            break;
        case GENALG:
            schedout.debug(CALL_INFO, 4, 0, "General Algorithm Nearest Allocator\n");
            nearestparams = new vector<string>;
            nearestparams -> push_back("genAlg");
            return new NearestAllocator(nearestparams, m);
            break;
        case MM:
            schedout.debug(CALL_INFO, 4, 0, "MM Allocator\n");
            nearestparams = new vector<string>;
            nearestparams -> push_back("MM");
            return new NearestAllocator(nearestparams, m);
            break;
        case ENERGY:
            schedout.debug(CALL_INFO, 4, 0, "Energy-Aware Allocator\n");
            nearestparams = new vector<string>;
            nearestparams -> push_back("Energy");
            return new EnergyAllocator(nearestparams, m);
            break;
        case HYBRID:
            schedout.debug(CALL_INFO, 4, 0, "Hybrid Allocator\n");
            nearestparams = new vector<string>;
            nearestparams -> push_back("Hybrid");
            return new NearestAllocator(nearestparams, m);
            break;
        case MC1X1:
            schedout.debug(CALL_INFO, 4, 0, "MC1x1 Allocator\n");
            nearestparams = new vector<string>;
            nearestparams -> push_back("MC1x1");
            return new NearestAllocator(nearestparams, m);
            break;
        case OLDMC1X1:
            schedout.debug(CALL_INFO, 4, 0, "Old MC1x1 Allocator\n");
            nearestparams = new vector<string>;
            nearestparams -> push_back("OldMC1x1");
            return new NearestAllocator(nearestparams, m);
            break;

            //MBS Allocators use a block-based approach
        case MBS:
            schedout.debug(CALL_INFO, 4, 0, "MBS Allocator\n");
            return new MBSAllocator(nearestparams, m);
        case GRANULARMBS:
            schedout.debug(CALL_INFO, 4, 0, "Granular MBS Allocator\n");
            return new GranularMBSAllocator(nearestparams, m);
        case OCTETMBS: 
            schedout.debug(CALL_INFO, 4, 0, "Octet MBS Allocator\n");
            return new OctetMBSAllocator(nearestparams, m);

            //Linear Allocators allocate in a linear fashion
            //along a curve
        case FIRSTFIT:
            schedout.debug(CALL_INFO, 4, 0, "First Fit Allocator\n");
            nearestparams = new vector<string>;
            for (int x = 1; x < (int)schedparams -> size(); x++) {
                nearestparams -> push_back(schedparams -> at(x));
            }
            return new FirstFitAllocator(nearestparams, m);
        case BESTFIT:
            schedout.debug(CALL_INFO, 4, 0, "Best Fit Allocator\n");
            nearestparams = new vector<string>;
            for (int x = 1; x < (int)schedparams -> size(); x++) {
                nearestparams -> push_back(schedparams -> at(x));
            }
            return new BestFitAllocator(nearestparams, m);
        case SORTEDFREELIST:
            schedout.debug(CALL_INFO, 4, 0, "Sorted Free List Allocator\n");
            nearestparams = new vector<string>;
            for (int x = 1; x < (int)schedparams -> size(); x++)
                nearestparams -> push_back(schedparams -> at(x));
            return new SortedFreeListAllocator(nearestparams, m);

            //Constraint Allocator tries to separate nodes whose estimated failure rates are close
        case CONSTRAINT:
            {
                if (params.find("ConstraintAllocatorDependencies") == params.end()) {
                    schedout.fatal(CALL_INFO, 1, "Constraint Allocator requires ConstraintAllocatorDependencies scheduler parameter");
                }
                if (params.find("ConstraintAllocatorConstraints") == params.end()) {
                    schedout.fatal(CALL_INFO, 1, "Constraint Allocator requires ConstraintAllocatorConstraints scheduler parameter");
                }
                SimpleMachine* mach = dynamic_cast<SimpleMachine*>(m);
                if (NULL == mach) {
                    schedout.fatal(CALL_INFO, 1, "ConstraintAllocator requires SimpleMachine");
                }
                // will get these file names from schedparams eventually
                return new ConstraintAllocator(mach, params.find("ConstraintAllocatorDependencies") -> second, params.find("ConstraintAllocatorConstraints") -> second, sc);
                break;
            }
        case NEARESTAMAP:
            if(params.find("taskMapper") != params.end()){
                vector<string>* taskmapparams = parseparams(params["taskMapper"]);
                if(taskmappername( taskmapparams->at(0) ) != NEARESTAMT){
                    schedout.fatal(CALL_INFO, 1, "Allocator and Task Mapper should be the same when using AllocMappers\n");
                }
            }
            if(mMachine == NULL){
                schedout.fatal(CALL_INFO, 1, "NearestAllocMapper requires MeshMachine\n");
            } else {
                return new NearestAllocMapper(*mMachine);
            }
            break;
        case SPECTRALAMAP:
            if(params.find("taskMapper") != params.end()){
                vector<string>* taskmapparams = parseparams(params["taskMapper"]);
                if(taskmappername( taskmapparams->at(0) ) != SPECTRALAMT){
                    schedout.fatal(CALL_INFO, 1, "Allocator and Task Mapper should be the same when using AllocMappers\n");
                }
            }
            return new SpectralAllocMapper(*m);
            break;
        default:
            schedout.fatal(CALL_INFO, 1, "Could not parse name of allocator\n");
        }
    }
    return NULL; //control never reaches here
}

TaskMapper* Factory::getTaskMapper(SST::Params& params, Machine* mach)
{
    TaskMapper* taskMapper;
    if(params.find("taskMapper") == params.end()){
        taskMapper = new SimpleTaskMapper(*mach);
        schedout.verbose(CALL_INFO, 4, 0, "Defaulting to Simple Task Mapper\n");
    } else {
        MeshMachine *mMachine = dynamic_cast<MeshMachine*>(mach);
        vector<string>* taskmapparams = parseparams(params["taskMapper"]);
        switch (taskmappername( taskmapparams->at(0) )){
        case SIMPLEMAP:
            taskMapper = new SimpleTaskMapper(*mach);
            break;
        case RCBMAP:
            if(mMachine == NULL){
                schedout.fatal(CALL_INFO, 1, "RCB Mapper requires mesh machine\n");
            }
            taskMapper = new RCBTaskMapper(*mMachine);
            break;
        case RANDOMMAP:
            taskMapper = new RandomTaskMapper(*mach);
            break;
        case TOPOMAP:
            taskMapper = new TopoMapper(*mach, TopoMapper::RECURSIVE);
            break;
        case RCMMAP:
            taskMapper = new TopoMapper(*mach, TopoMapper::RCM);
            break;  
        case NEARESTAMT:
            if(mMachine == NULL){
                schedout.fatal(CALL_INFO, 1, "NearestAllocMapper requires MeshMachine");
            }
            taskMapper = new NearestAllocMapper(*mMachine);
            break;  
        case SPECTRALAMT:
            taskMapper = new SpectralAllocMapper(*mach);
            break;    
        default: 
            taskMapper = NULL;
            schedout.fatal(CALL_INFO, 1, "Could not parse name of task mapper");
        }
    }
    return taskMapper;
}


int Factory::getFST(SST::Params& params)
{
    if(params.find("FST") == params.end()){
        //default: FIFO queue priority scheduler
        //schedout.verbose(CALL_INFO, 4, 0, "Defaulting to no FST");
        return 0;
    } else {
        vector<string>* FSTparams = parseparams(params["FST"]);
        switch (FSTname(FSTparams -> at(0)))
        {
        case NONE:
            return 0;
        case STRICT:
            return 1;
        case RELAXED:
            return 2;
        default:
            schedout.fatal(CALL_INFO, 1, "Could not parse FST type; should be none, strict, or relaxed");
        }
    }
    schedout.fatal(CALL_INFO, 1, "Could not parse FST type; should be none, strict, or relaxed");
    return 0; 
}

vector<double>* Factory::getTimePerDistance(SST::Params& params)
{
    vector<double>* ret = new vector<double>;
    for (int x = 0; x < 5; x++) {
        ret -> push_back(0);
    }
    if(params.find("timeperdistance") == params.end()){
        //default: FIFO queue priority scheduler
        //schedout.verbose(CALL_INFO, 4, 0, "Defaulting to no FST");
        return ret;
    } else {
        vector<string>* tpdparams = parseparams(params["timeperdistance"]);
        for (unsigned int x = 0; x < tpdparams -> size(); x++) {
            ret -> at(x) = atof(tpdparams -> at(x).c_str());
            //printf("%s %f %f\n", tpdparams -> at(x).c_str(), atof(tpdparams->at(x).c_str()), ret->at(x));
        }
        return ret;
        //return atof(tpdparams -> at(0).c_str());
    }
    //schedout.fatal(CALL_INFO, 1, "Could not parse timeperdistance; should be a floating point integer");
    return ret; 
}


//takes in a parameter and breaks it down from class[arg,arg,...]
//into {class, arg, arg}
vector<string>* Factory::parseparams(string inparam)
{
    vector<string>* ret = new vector<string>;
    stringstream ss;
    ss.str(inparam);
    string str;
    getline(ss,str,'[');
    transform(str.begin(), str.end(), str.begin(), ::tolower);
    ret -> push_back(str);
    while(getline(ss, str, ',') && str != "]") {
        transform(str.begin(), str.end(), str.begin(), ::tolower);
        if(*(str.rbegin()) == ']') {
            str = str.substr(0,str.size()-1);
        }
        ret -> push_back(str);
    }
    return ret;
}

Factory::SchedulerType Factory::schedulername(string inparam)
{
    for (int i = 0; i < numSchedTableEntries; i++) {
        if (inparam == schedTable[i].name) return schedTable[i].val;
    }
    schedout.fatal(CALL_INFO, 1, "Scheduler name not found:%s", inparam.c_str());
    exit(0); // control never reaches here
}

Factory::MachineType Factory::machinename(string inparam)
{
    for(int i = 0; i < numMachTableEntries; i++) {
        if (inparam == machTable[i].name) return machTable[i].val;
    }
    schedout.fatal(CALL_INFO, 1, "Machine name not found:%s", inparam.c_str());
    exit(0);
}

Factory::AllocatorType Factory::allocatorname(string inparam)
{
    for(int i = 0; i < numAllocTableEntries; i++) {
        if (inparam == allocTable[i].name) return allocTable[i].val;
    }
    schedout.fatal(CALL_INFO, 1, "Allocator name not found:%s", inparam.c_str());
    exit(0);
}

Factory::TaskMapperType Factory::taskmappername(string inparam)
{
    for(int i = 0; i < numTaskMapTableEntries; i++) {
        if (inparam == taskMapTable[i].name) return taskMapTable[i].val;
    }
    schedout.fatal(CALL_INFO, 1, "Task Mapper name not found:%s", inparam.c_str());
    exit(0);
}

Factory::FSTType Factory::FSTname(string inparam)
{
    for(int i = 0; i < numFSTTableEntries; i++) {
        if (inparam == FSTTable[i].name) return FSTTable[i].val;
    } 
    schedout.fatal(CALL_INFO, 1, "FST name not found:%s", inparam.c_str());
    exit(0);
}
