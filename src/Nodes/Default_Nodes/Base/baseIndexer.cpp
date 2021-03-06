//
//  baseIndexer.cpp
//  MIRABCN_Generator
//
//  Created by Eduard Frigola on 09/01/2017.
//
//

#include "baseIndexer.h"

baseIndexer::baseIndexer(int numIndexs, string name) : ofxOceanodeNodeModel(name){
    indexCount.set("Size", numIndexs, 1, 100000);
    previousIndexCount = indexCount;
    indexs.resize(indexCount, 0);
    indexRand.resize(indexCount , 0);
    for(int i = 0; i < indexRand.size(); i++)
        indexRand[i] = i-((float)indexRand.size()/2.f);
    indexRand_Param_previous = 0;
    
    numWaves_Param.set("Num Waves", 1, 0, indexCount);
    indexInvert_Param.set("Index Invert", 0, 0, 1);
    symmetry_Param.set("Symmetry", 0, 0, indexCount/2);
    indexRand_Param.set("Index Random", 0, 0, 1);
    indexOffset_Param.set("Index Offset", 0, -indexCount/2, indexCount/2);
    indexQuant_Param.set("Index Quantization", indexCount, 1, indexCount);
    combination_Param.set("Index Combination", 0, 0, 1);
    modulo_Param.set("Index Modulo", indexCount, 1, indexCount);
    
    recomputeIndexs();

    listeners.push(indexCount.newListener(this, &baseIndexer::indexCountChanged));
    listeners.push(numWaves_Param.newListener(this, &baseIndexer::parameterFloatListener));
    listeners.push(indexInvert_Param.newListener(this, &baseIndexer::parameterFloatListener));
    listeners.push(symmetry_Param.newListener(this, &baseIndexer::parameterIntListener));
    listeners.push(indexRand_Param.newListener(this, &baseIndexer::parameterFloatListener));
    listeners.push(indexOffset_Param.newListener(this, &baseIndexer::parameterFloatListener));
    listeners.push(indexQuant_Param.newListener(this, &baseIndexer::parameterIntListener));
    listeners.push(combination_Param.newListener(this, &baseIndexer::parameterFloatListener));
    listeners.push(modulo_Param.newListener(this, &baseIndexer::parameterIntListener));
    
    listeners.push(indexRand_Param.newListener(this, &baseIndexer::indexRandChanged));
}

void baseIndexer::indexCountChanged(int &indexCount){
    if(indexCount != previousIndexCount){
        indexs.resize(indexCount, 0);
        indexRand.resize(indexCount , 0);
        for(int i = 0; i < indexRand.size(); i++)
            indexRand[i] = i-((float)indexRand.size()/2.f);
        random_shuffle(indexRand.begin(), indexRand.end());
        
        numWaves_Param.setMax(indexCount);
        numWaves_Param = ofClamp(numWaves_Param, numWaves_Param.getMin(), numWaves_Param.getMax());
        string name1 = numWaves_Param.getName();
        ofNotifyEvent(parameterChangedMinMax, name1);
        
        symmetry_Param.setMax(indexCount/2);
        symmetry_Param = ofClamp(symmetry_Param, symmetry_Param.getMin(), symmetry_Param.getMax());
        string name11 = symmetry_Param.getName();
        ofNotifyEvent(parameterChangedMinMax, name11);

        indexOffset_Param.setMin(-indexCount/2);
        indexOffset_Param.setMax(indexCount/2);
        indexOffset_Param = ofClamp(indexOffset_Param, indexOffset_Param.getMin(), indexOffset_Param.getMax());
        string name2 = indexOffset_Param.getName();
        ofNotifyEvent(parameterChangedMinMax, name2);
        
        float indexQuantNormalized = (float)indexQuant_Param / (float)indexQuant_Param.getMax();
        indexQuant_Param.setMax(indexCount);
        string name3 = indexQuant_Param.getName();
        ofNotifyEvent(parameterChangedMinMax, name3);
        indexQuant_Param = ofClamp(indexQuantNormalized * indexCount, indexQuant_Param.getMin(), indexQuant_Param.getMax());
        
        
        float indexModuloNormalized = (float)modulo_Param / (float)modulo_Param.getMax();
        modulo_Param.setMax(indexCount);
        string name4 = modulo_Param.getName();
        ofNotifyEvent(parameterChangedMinMax, name4);
        modulo_Param = ofClamp(indexModuloNormalized * indexCount, modulo_Param.getMin(), modulo_Param.getMax());
        
        recomputeIndexs();
    }
    previousIndexCount = indexCount;
}

void baseIndexer::putParametersInParametersGroup(ofParameterGroup* parameters){
    parameters->add(indexCount);
    parameters->add(numWaves_Param);
    parameters->add(indexInvert_Param);
    parameters->add(symmetry_Param);
    parameters->add(indexRand_Param);
    parameters->add(indexOffset_Param);
    parameters->add(indexQuant_Param);
    parameters->add(combination_Param);
    parameters->add(modulo_Param);
}

void baseIndexer::recomputeIndexs(){
    for (int i = 0; i < indexCount ; i++){
        int index = i;
        
        //QUANTIZE
        int newNumOfPixels = indexQuant_Param;
        
        index = floor(index/((float)indexCount/(float)newNumOfPixels));
        
        
        while(symmetry_Param > newNumOfPixels-1)
            symmetry_Param--;
        
        bool odd = false;
        if((abs(indexOffset_Param) - (int)abs(indexOffset_Param)) > 0.5) odd = !odd;
        
        if((int)((index)/(newNumOfPixels/(symmetry_Param+1)))%2 == 1) odd = true;
        
        
        //SYMMETRY santi
        int veusSym = newNumOfPixels/(symmetry_Param+1);
        index = veusSym-abs((((int)(index/veusSym)%2) * veusSym)-(index%veusSym));
        
        
        if(newNumOfPixels % 2 == 0){
            index += odd ? 1 : 0;
        }
        else if(symmetry_Param > 0){
            index += 1;
            index %= newNumOfPixels;;
        }
        
        
        //INVERSE
        //Fisrt we invert the index to simulate the wave goes from left to right, inverting indexes, if we want to invertit we don't do this calc
        int nonInvertIndex = index-1;
        int invertedIndex = ((float)indexCount-(float)index);
        index = indexInvert_Param*invertedIndex + (1-indexInvert_Param)*nonInvertIndex;
        
        //random
        index += indexRand[index]*indexRand_Param;
        index %= indexCount;
        if(index < 0)
            index += indexCount;
        
        
        //COMB
        index = abs(((index%2)*indexCount*combination_Param)-index);
        
        //Modulo
        if(modulo_Param != modulo_Param.getMax())
            index %= modulo_Param;
        
        
        int shifted_i = i + round(indexOffset_Param);
        if(shifted_i < 0) shifted_i += indexCount;
        shifted_i %= indexCount;
        indexs[shifted_i] = (((float)index/(float)indexCount))*(numWaves_Param*((float)indexCount/(float)newNumOfPixels))*(symmetry_Param+1);
    }
    newIndexs();
}

void baseIndexer::indexRandChanged(float &val){
    if(indexRand_Param_previous == 0)
        random_shuffle(indexRand.begin(), indexRand.end());
    indexRand_Param_previous = val;
}
