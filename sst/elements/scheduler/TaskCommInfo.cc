// Copyright 2009-2014 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
// 
// Copyright (c) 2009-2014, Sandia Corporation
// All rights reserved.
// 
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#include "TaskCommInfo.h"

#include "Job.h"
#include "output.h"

using namespace SST::Scheduler;

TaskCommInfo::TaskCommInfo(Job* job)
{
    init(job);
    taskCommType = TaskCommInfo::ALLTOALL;
    commInfo = NULL;
    coordMatrix = NULL;
    xdim = 0;
    ydim = 0;
    zdim = 0;
}

TaskCommInfo::TaskCommInfo(Job* job, std::vector<int*>* inCommInfo)
{
    init(job);
    taskCommType = TaskCommInfo::CUSTOM;
    commInfo = inCommInfo;
    coordMatrix = NULL;
    xdim = 0;
    ydim = 0;
    zdim = 0;
}

TaskCommInfo::TaskCommInfo(Job* job, int xdim, int ydim, int zdim)
{
    init(job);
    taskCommType = TaskCommInfo::MESH;
    commInfo = NULL;
    coordMatrix = NULL;
    this->xdim = xdim;
    this->ydim = ydim;
    this->zdim = zdim;
}

TaskCommInfo::TaskCommInfo(Job* job, std::vector<int*>* inCommInfo, double** inCoords)
{
    init(job);
    taskCommType = TaskCommInfo::COORDINATE;
    commInfo = inCommInfo;
    coordMatrix = inCoords;
    this->xdim = 0;
    this->ydim = 0;
    this->zdim = 0;
}

TaskCommInfo::TaskCommInfo(const TaskCommInfo& tci)
{
    size = tci.size;
    taskCommType = tci.taskCommType;
    xdim = tci.xdim;
    ydim = tci.ydim;
    zdim = tci.zdim;
    
    if(taskCommType == CUSTOM || taskCommType == COORDINATE){
        commInfo = new std::vector<int*>(*(tci.commInfo));
    } else {
        commInfo = NULL;
    }
    
    if(taskCommType == COORDINATE){
        double ** coordMatrix = new double*[size];
        for(int i = 0; i < size; i++){
            coordMatrix[i] = new double[3];
            for(int j = 0; j < 3; j++){
                coordMatrix[i][j] = tci.coordMatrix[i][j];
            }
        }
    } else {
        coordMatrix = NULL;
    }
}

void TaskCommInfo::init(Job* job)
{
    job->taskCommInfo = this;
    size = job -> getProcsNeeded();
}

TaskCommInfo::~TaskCommInfo()
{
    if(commInfo != NULL){
        for(unsigned int i = 0; i < commInfo->size(); ++i) {
            delete [] commInfo->at(i);
        }
    }
    delete commInfo;
    if(coordMatrix != NULL){
        for(int i = 0; i < size; ++i) {
            delete [] coordMatrix[i];
        }
        delete [] coordMatrix;
    }
}

int** TaskCommInfo::getCommMatrix() const
{
    int **outMatrix;
    switch(taskCommType){
    case ALLTOALL:
        outMatrix = buildAllToAllComm(size);
        break;
    case MESH:
        outMatrix = buildMeshComm(xdim, ydim, zdim);
        break;
    case CUSTOM:
    case COORDINATE:
        outMatrix = commInfoToMatrix();
        break;
    default:
        schedout.fatal(CALL_INFO, 1, "Unknown Communication type");
        outMatrix = NULL;
    }
    return outMatrix;
}

int** TaskCommInfo::buildAllToAllComm(int size) const
{
    int** outMatrix = new int*[size];
    for(int i = 0; i < size; i++){
        outMatrix[i] = new int[size];
        for(int j = 0; j < size; j++){
            outMatrix[i][j] = 1;
        }
    }
    return outMatrix;
}

int** TaskCommInfo::buildMeshComm(int xdim, int ydim, int zdim) const
{
    int ** outMatrix;
    //initialize matrix
    int size = xdim * ydim * zdim;
    outMatrix = new int*[size];
    for(int i = 0; i < size; i++){
        outMatrix[i] = new int[size];
        for(int j = 0; j < size; j++){
            outMatrix[i][j] = 0;
        }
    }
    //index dimensions of matrix elements
    int x_ind[size];
    int y_ind[size];
    int z_ind[size];
    for(int i = 0; i < size; i++){
        x_ind[i] = i % xdim;
        y_ind[i] = (i / xdim) % ydim;
        z_ind[i] = i / (xdim*ydim);
    }
    //set neighbor communication
    for(int i = 0; i < size; i++){
        //x_dim
        if(x_ind[i] != 0){
            outMatrix[i][i-1] = 1;
            outMatrix[i-1][i] = 1;
        }
        if((x_ind[i] + 1) != xdim){
            outMatrix[i+1][i] = 1;
            outMatrix[i][i+1] = 1;
        }
        //y_dim
        if(y_ind[i] != 0){
            outMatrix[i][i-xdim] = 1;
            outMatrix[i-xdim][i] = 1;
        }
        if((y_ind[i] + 1) != ydim){
            outMatrix[i+xdim][i] = 1;
            outMatrix[i][i+xdim] = 1;
        }
        //z_dim
        if(z_ind[i] != 0){
            outMatrix[i][i-(xdim*ydim)] = 1;
            outMatrix[i-(xdim*ydim)][i] = 1;
        }
        if((z_ind[i] + 1) != zdim){
            outMatrix[i+(xdim*ydim)][i] = 1;
            outMatrix[i][i+(xdim*ydim)] = 1;
        }
    }
    return outMatrix;
}

int** TaskCommInfo::commInfoToMatrix() const
{
    //init matrix
    int** outMatrix = new int*[size];
    for(int i = 0; i < size; i++){
        outMatrix[i] = new int[size];
        for(int j = 0; j < size; j++){
            outMatrix[i][j] = 0;
        }
    }
    
    //write data
    int* data;
    for(unsigned int i = 0; i < commInfo->size(); ++i) {
        data = commInfo->at(i);
        outMatrix[data[0]][data[1]] = data[2];
    }
    return outMatrix;
}

