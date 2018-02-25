//
//  ofxOceanodeNodeModel.hpp
//  example-basic
//
//  Created by Eduard Frigola on 19/06/2017.
//
//

#ifndef ofxOceanodeNodeModel_h
#define ofxOceanodeNodeModel_h

#include "ofMain.h"

class ofxOceanodeContainer;
class ofxOceanodeAbstractConnection;

struct parameterInfo{
    bool isSavePreset;
    bool isSaveProject;
    bool acceptInConnection;
    bool acceptOutConnection;
};

class ofxOceanodeNodeModel {
public:
    ofxOceanodeNodeModel(string _name);
    ~ofxOceanodeNodeModel(){};
    
    //get parameterGroup
    ofParameterGroup* getParameterGroup(){return parameters;};
    
    //getters
    bool getIsDynamic(){return isDynamic;};
    string nodeName(){return nameIdentifier;};
    
    /// Function creates instances of a model stored in ofxOceanodeNodeRegistry
    virtual std::unique_ptr<ofxOceanodeNodeModel> clone() const = 0;
    
    virtual ofxOceanodeAbstractConnection* createConnectionFromCustomType(ofxOceanodeContainer& c, ofAbstractParameter& source, ofAbstractParameter& sink, glm::vec2 pos){return nullptr;};
    
    ofEvent<string> parameterChangedMinMax;
    
protected:
    ofParameterGroup* parameters;
//    std::map<ofAbstractParameter&, parameterInfo> parametersInfo; //information about interaction of parameter
    
private:
    bool isDynamic;
    string nameIdentifier;
};

#endif /* ofxOceanodeNodeModel_h */
