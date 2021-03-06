//
//  ofxOceanodeContainer.cpp
//  example-basic
//
//  Created by Eduard Frigola on 19/06/2017.
//
//


#include "ofxOceanodeContainer.h"
#include "ofxOceanodeNodeRegistry.h"
#include "ofxOceanodeTypesRegistry.h"
#include "ofxOceanodeNodeModel.h"

#ifdef OFXOCEANODE_USE_MIDI
#include "ofxOceanodeMidiBinding.h"
#include "ofxMidiIn.h"
#include "ofxMidiOut.h"
#endif


ofxOceanodeContainer::ofxOceanodeContainer(shared_ptr<ofxOceanodeNodeRegistry> _registry, shared_ptr<ofxOceanodeTypesRegistry> _typesRegistry, bool _isHeadless) : registry(_registry), typesRegistry(_typesRegistry), isHeadless(_isHeadless){
    window = ofGetCurrentWindow();
    transformationMatrix = glm::mat4(1);
    temporalConnection = nullptr;
    bpm = 120;
    collapseAll = false;
    
#ifdef OFXOCEANODE_USE_OSC
    updateListener = window->events().update.newListener(this, &ofxOceanodeContainer::update);
#endif
    
#ifdef OFXOCEANODE_USE_MIDI
    ofxMidiIn* midiIn = new ofxMidiIn();
    midiInPortList = midiIn->getInPortList();
    delete midiIn;
    for(auto port : midiInPortList){
        midiIns[port].openPort(port);
    }
    
    
    ofxMidiOut* midiOut = new ofxMidiOut();
    midiOutPortList = midiOut->getOutPortList();
    delete midiOut;
    for(auto port : midiOutPortList){
        midiOuts[port].openPort(port);
    }
    isListeningMidi = false;
#endif
}

ofxOceanodeContainer::~ofxOceanodeContainer(){
    dynamicNodes.clear();
    persistentNodes.clear();
}

ofxOceanodeAbstractConnection* ofxOceanodeContainer::createConnection(ofAbstractParameter& p, ofxOceanodeNode& n){
    if(temporalConnection != nullptr) return nullptr;
    temporalConnectionNode = &n;
    temporalConnection = new ofxOceanodeTemporalConnection(p);
    if(!isHeadless){
        temporalConnection->setSourcePosition(n.getNodeGui().getSourceConnectionPositionFromParameter(p));
        temporalConnection->getGraphics().subscribeToDrawEvent(window);
    }
    destroyConnectionListeners.push(temporalConnection->destroyConnection.newListener(this, &ofxOceanodeContainer::temporalConnectionDestructor));
    return temporalConnection;
}

ofxOceanodeAbstractConnection* ofxOceanodeContainer::disconnectConnection(ofxOceanodeAbstractConnection* connection){
    for(auto c : connections){
        if(c.second.get() == connection){
            if(!ofGetKeyPressed(OF_KEY_ALT)){
                connections.erase(std::remove(connections.begin(), connections.end(), c));
            }
            return createConnection(connection->getSourceParameter(), *c.first);
            break;
        }
    }
    return nullptr;
}

void ofxOceanodeContainer::destroyConnection(ofxOceanodeAbstractConnection* connection){
    for(auto c : connections){
        if(c.second.get() == connection){
            connections.erase(std::remove(connections.begin(), connections.end(), c));
            break;
        }
    }
}

ofxOceanodeNode* ofxOceanodeContainer::createNodeFromName(string name, int identifier, bool isPersistent){
    unique_ptr<ofxOceanodeNodeModel> type = registry->create(name);
    
    if (type)
    {
        auto &node =  createNode(std::move(type), identifier, isPersistent);
        if(!isHeadless){
            node.getNodeGui().setTransformationMatrix(&transformationMatrix);
        }
        return &node;
    }
    return nullptr;
}

ofxOceanodeNode& ofxOceanodeContainer::createNode(unique_ptr<ofxOceanodeNodeModel> && nodeModel, int identifier, bool isPersistent){
    auto &collection = !isPersistent ? dynamicNodes : persistentNodes;
    int toBeCreatedId = identifier;
    string nodeToBeCreatedName = nodeModel->nodeName();
    if(identifier == -1){
        int lastId = 1;
        while (dynamicNodes[nodeToBeCreatedName].count(lastId) != 0 || persistentNodes[nodeToBeCreatedName].count(lastId) != 0) lastId++;
        toBeCreatedId = lastId;
    }
    nodeModel->setNumIdentifier(toBeCreatedId);
    nodeModel->registerLoop(window);
    auto node = make_unique<ofxOceanodeNode>(move(nodeModel));
    node->setup();
    if(!isHeadless){
        auto nodeGui = make_unique<ofxOceanodeNodeGui>(*this, *node, window);
        if(collapseAll) nodeGui->collapse();
#ifdef OFXOCEANODE_USE_MIDI
        nodeGui->setIsListeningMidi(isListeningMidi);
#endif
        node->setGui(std::move(nodeGui));
    }
    node->setBpm(bpm);
    node->setPhase(phase);
    node->setIsPersistent(isPersistent);
    
    auto nodePtr = node.get();
    collection[nodeToBeCreatedName][toBeCreatedId] = std::move(node);
    
    if(!isPersistent){
        destroyNodeListeners.push(nodePtr->deleteModuleAndConnections.newListener([this, nodeToBeCreatedName, toBeCreatedId](vector<ofxOceanodeAbstractConnection*> connectionsToBeDeleted){
            for(auto containerConnectionIterator = connections.begin(); containerConnectionIterator!=connections.end();){
                bool foundConnection = false;
                for(auto nodeConnection : connectionsToBeDeleted){
                    if(containerConnectionIterator->second.get() == nodeConnection){
                        foundConnection = true;
                        connections.erase(containerConnectionIterator);
                        connectionsToBeDeleted.erase(std::remove(connectionsToBeDeleted.begin(), connectionsToBeDeleted.end(), nodeConnection));
                        break;
                    }
                }
                if(!foundConnection){
                    containerConnectionIterator++;
                }
            }
            
#ifdef OFXOCEANODE_USE_MIDI
            string toBeCreatedEscaped = nodeToBeCreatedName + " " + ofToString(toBeCreatedId);
            ofStringReplace(toBeCreatedEscaped, " ", "_");
            vector<string> midiBindingToBeRemoved;
            for(auto &midiBind : midiBindings){
                if(ofSplitString(midiBind.first, "-|-")[0] == toBeCreatedEscaped){
                    for(auto &midiInPair : midiIns){
                        midiInPair.second.removeListener(midiBind.second.get());
                    }
                    midiBindingDestroyed.notify(this, *midiBind.second.get());
                    midiBindingToBeRemoved.push_back(midiBind.first);
                }
            }
            for(auto &s : midiBindingToBeRemoved){
                midiBindings.erase(s);
            }
#endif
            
            dynamicNodes[nodeToBeCreatedName].erase(toBeCreatedId);
        }));
        
        destroyNodeListeners.push(nodePtr->deleteConnections.newListener([this](vector<ofxOceanodeAbstractConnection*> connectionsToBeDeleted){
            for(auto containerConnectionIterator = connections.begin(); containerConnectionIterator!=connections.end();){
                bool foundConnection = false;
                for(auto nodeConnection : connectionsToBeDeleted){
                    if(containerConnectionIterator->second.get() == nodeConnection){
                        foundConnection = true;
                        connections.erase(containerConnectionIterator);
                        connectionsToBeDeleted.erase(std::remove(connectionsToBeDeleted.begin(), connectionsToBeDeleted.end(), nodeConnection));
                        break;
                    }
                }
                if(!foundConnection){
                    containerConnectionIterator++;
                }
            }
        }));
        
        duplicateNodeListeners.push(nodePtr->duplicateModule.newListener([this, nodeToBeCreatedName, nodePtr](glm::vec2 pos){
            auto newNode = createNodeFromName(nodeToBeCreatedName);
            newNode->getNodeGui().setPosition(pos);
            newNode->loadConfig("tempDuplicateGroup.json");
            ofFile config("tempDuplicateGroup.json");
            config.remove();
        }));
    }
    
    return *nodePtr;
}

void ofxOceanodeContainer::temporalConnectionDestructor(){
    delete temporalConnection;
    temporalConnection = nullptr;
}

bool ofxOceanodeContainer::loadPreset(string presetFolderPath){
    ofStringReplace(presetFolderPath, " ", "_");
    ofLog()<<"Load Preset " << presetFolderPath;
    
    window->makeCurrent();
    ofGetMainLoop()->setCurrentWindow(window);
    
    for(auto &nodeTypeMap : dynamicNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->presetWillBeLoaded();
        }
    }
    
    for(auto &nodeTypeMap : persistentNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->presetWillBeLoaded();
        }
    }
    
    for(int i = 0; i < connections.size();){
        if(!connections[i].second->getIsPersistent()){
            connections.erase(connections.begin()+i);
        }else{
            i++;
        }
    }
    
    //Read new nodes in preset
    //Check if the nodes exists and update them, (or update all at the end)
    //Create new modules and update them (or update at end)
    ofJson json = ofLoadJson(presetFolderPath + "/modules.json");
    if(!json.empty()){;
        for(auto &models : registry->getRegisteredModels()){
            string moduleName = models.first;
            vector<int>  vector_of_dynamic_identifiers;
            vector<int>  vector_of_persistent_identifiers;
            if(dynamicNodes.count(moduleName) != 0){
                for(auto &nodes_of_a_give_type : dynamicNodes[moduleName]){
                    vector_of_dynamic_identifiers.push_back(nodes_of_a_give_type.first);
                }
            }
            if(persistentNodes.count(moduleName) != 0){
                for(auto &nodes_of_a_give_type : persistentNodes[moduleName]){
                    vector_of_persistent_identifiers.push_back(nodes_of_a_give_type.first);
                }
            }
            
            for(auto identifier : vector_of_dynamic_identifiers){
                string stringIdentifier = ofToString(identifier);
                if(json.find(moduleName) != json.end() && json[moduleName].find(stringIdentifier) != json[moduleName].end()){
                    vector<float> readArray = json[moduleName][stringIdentifier];
                    if(!isHeadless){
                        glm::vec2 position(readArray[0], readArray[1]);
                        dynamicNodes[moduleName][identifier]->getNodeGui().setPosition(position);
                    }
                    json[moduleName].erase(stringIdentifier);
                }else{
                    dynamicNodes[moduleName][identifier]->deleteSelf();
                }
            }
            for(auto identifier : vector_of_persistent_identifiers){
                string stringIdentifier = ofToString(identifier);
                if(json.find(moduleName) != json.end() && json[moduleName].find(stringIdentifier) != json[moduleName].end()){
                    vector<float> readArray = json[moduleName][stringIdentifier];
                    if(!isHeadless){
                        glm::vec2 position(readArray[0], readArray[1]);
                        persistentNodes[moduleName][identifier]->getNodeGui().setPosition(position);
                    }
                    json[moduleName].erase(stringIdentifier);
                }
            }
            
            
            for (ofJson::iterator it = json[moduleName].begin(); it != json[moduleName].end(); ++it) {
                int identifier = ofToInt(it.key());
                if(dynamicNodes[moduleName].count(identifier) == 0){
                    auto node = createNodeFromName(moduleName, identifier);
                    if(!isHeadless){
                        node->getNodeGui().setPosition(glm::vec2(it.value()[0], it.value()[1]));
                    }
                }
            }
        }
    }else{
        dynamicNodes.clear();
    }
    
    
#ifdef OFXOCEANODE_USE_MIDI
    json.clear();
    for(auto &binding : midiBindings){
        for(auto &midiInPair : midiIns){
            midiInPair.second.removeListener(binding.second.get());
        }
        midiBindingDestroyed.notify(this, *binding.second.get());
    }
    midiBindings.clear();
    json = ofLoadJson(presetFolderPath + "/midi.json");
    for (ofJson::iterator module = json.begin(); module != json.end(); ++module) {
        for (ofJson::iterator parameter = module.value().begin(); parameter != module.value().end(); ++parameter) {
            auto midiBinding = createMidiBindingFromInfo(module.key(), parameter.key());
            if(midiBinding != nullptr){
                midiBinding->loadPreset(json[module.key()][parameter.key()]);
            }
        }
    }
#endif
    
    for(auto &nodeTypeMap : dynamicNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->loadPreset(presetFolderPath);
        }
    }
    
    for(auto &nodeTypeMap : persistentNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->loadPreset(presetFolderPath);
        }
    }
    
    json.clear();
    json = ofLoadJson(presetFolderPath + "/connections.json");
    for (ofJson::iterator sourceModule = json.begin(); sourceModule != json.end(); ++sourceModule) {
        for (ofJson::iterator sourceParameter = sourceModule.value().begin(); sourceParameter != sourceModule.value().end(); ++sourceParameter) {
            for (ofJson::iterator sinkModule = sourceParameter.value().begin(); sinkModule != sourceParameter.value().end(); ++sinkModule) {
                for (ofJson::iterator sinkParameter = sinkModule.value().begin(); sinkParameter != sinkModule.value().end(); ++sinkParameter) {
                    createConnectionFromInfo(sourceModule.key(), sourceParameter.key(), sinkModule.key(), sinkParameter.key());
                }
            }
        }
    }
    
    for(auto &nodeTypeMap : dynamicNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->presetHasLoaded();
        }
    }
    
    for(auto &nodeTypeMap : persistentNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->presetHasLoaded();
        }
    }
    
    resetPhase();
    
    return true;
}

void ofxOceanodeContainer::savePreset(string presetFolderPath){
    ofStringReplace(presetFolderPath, " ", "_");
    ofLog()<<"Save Preset " << presetFolderPath;
    
    ofJson json;
    for(auto &nodeTypeMap : dynamicNodes){
        for(auto &node : nodeTypeMap.second){
            glm::vec2 pos(0,0);
            if(!isHeadless){
                pos = node.second->getNodeGui().getPosition();
            }
            json[nodeTypeMap.first][ofToString(node.first)] = {pos.x, pos.y};
        }
    }
    ofSavePrettyJson(presetFolderPath + "/modules.json", json);
    
    json.clear();
    for(auto &connection : connections){
        if(!connection.second->getIsPersistent()){
            string sourceName = connection.second->getSourceParameter().getName();
            string sourceParentName = connection.second->getSourceParameter().getGroupHierarchyNames()[0];
            string sinkName = connection.second->getSinkParameter().getName();
            string sinkParentName = connection.second->getSinkParameter().getGroupHierarchyNames()[0];
            json[sourceParentName][sourceName][sinkParentName][sinkName];
        }
    }
    
    ofSavePrettyJson(presetFolderPath + "/connections.json", json);
    
    
    for(auto &nodeTypeMap : dynamicNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->savePreset(presetFolderPath);
        }
    }
    
    for(auto &nodeTypeMap : persistentNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->savePreset(presetFolderPath);
        }
    }
    
#ifdef OFXOCEANODE_USE_MIDI
    json.clear();
    for(auto &bindingPair : midiBindings){
        bindingPair.second->savePreset(json[ofSplitString(bindingPair.first, "-|-")[0]][ofSplitString(bindingPair.first, "-|-")[1]]);
    }
    ofSavePrettyJson(presetFolderPath + "/midi.json", json);
#endif
}

void ofxOceanodeContainer::savePersistent(){
    ofLog()<<"Save Persistent";
    string persistentFolderPath = "Persistent";
    
    ofJson json;
    for(auto &nodeTypeMap : dynamicNodes){
        for(auto &node : nodeTypeMap.second){
            glm::vec2 pos(0,0);
            if(!isHeadless){
                pos = node.second->getNodeGui().getPosition();
            }
            json[nodeTypeMap.first][ofToString(node.first)] = {pos.x, pos.y};
        }
    }
    for(auto &nodeTypeMap : persistentNodes){
        for(auto &node : nodeTypeMap.second){
            glm::vec2 pos(0,0);
            if(!isHeadless){
                pos = node.second->getNodeGui().getPosition();
            }
            json[nodeTypeMap.first][ofToString(node.first)] = {pos.x, pos.y};
        }
    }
    ofSavePrettyJson(persistentFolderPath + "/modules.json", json);
    
    json.clear();
    for(auto &connection : connections){
        string sourceName = connection.second->getSourceParameter().getName();
        string sourceParentName = connection.second->getSourceParameter().getGroupHierarchyNames()[0];
        string sinkName = connection.second->getSinkParameter().getName();
        string sinkParentName = connection.second->getSinkParameter().getGroupHierarchyNames()[0];
        json[sourceParentName][sourceName][sinkParentName][sinkName];
    }
    
    ofSavePrettyJson(persistentFolderPath + "/connections.json", json);
    
    
    for(auto &nodeTypeMap : dynamicNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->savePersistentPreset(persistentFolderPath);
        }
    }
    for(auto &nodeTypeMap : persistentNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->savePersistentPreset(persistentFolderPath);
        }
    }

#ifdef OFXOCEANODE_USE_MIDI
    json.clear();
    for(auto &bindingPair : midiBindings){
        bindingPair.second->savePreset(json[ofSplitString(bindingPair.first, "-|-")[0]][ofSplitString(bindingPair.first, "-|-")[1]]);
    }
    ofSavePrettyJson(persistentFolderPath + "/midi.json", json);
#endif
}

void ofxOceanodeContainer::loadPersistent(){
    ofLog()<<"Load Persistent";
    string persistentFolderPath = "Persistent";
    
    window->makeCurrent();
    ofGetMainLoop()->setCurrentWindow(window);
    
    //Read new nodes in preset
    //Check if the nodes exists and update them, (or update all at the end)
    //Create new modules and update them (or update at end)
    ofJson json = ofLoadJson(persistentFolderPath + "/modules.json");
    if(!json.empty()){;
        for(auto &models : registry->getRegisteredModels()){
            string moduleName = models.first;
            vector<int>  vector_of_identifiers;
            if(persistentNodes.count(moduleName) != 0){
                for(auto &nodes_of_a_give_type : persistentNodes[moduleName]){
                    vector_of_identifiers.push_back(nodes_of_a_give_type.first);
                }
            }
            for(auto identifier : vector_of_identifiers){
                string stringIdentifier = ofToString(identifier);
                if(json.find(moduleName) != json.end() && json[moduleName].find(stringIdentifier) != json[moduleName].end()){
                    vector<float> readArray = json[moduleName][stringIdentifier];
                    if(!isHeadless){
                        glm::vec2 position(readArray[0], readArray[1]);
                        persistentNodes[moduleName][identifier]->getNodeGui().setPosition(position);
                    }
                    json[moduleName].erase(stringIdentifier);
                }else{
                    persistentNodes[moduleName][identifier]->deleteSelf();
                }
            }
            for (ofJson::iterator it = json[moduleName].begin(); it != json[moduleName].end(); ++it) {
                int identifier = ofToInt(it.key());
                if(persistentNodes[moduleName].count(identifier) == 0){
                    auto node = createNodeFromName(moduleName, identifier, true);
                    if(!isHeadless){
                        node->getNodeGui().setPosition(glm::vec2(it.value()[0], it.value()[1]));
                    }
                }
            }
        }
    }else{
        persistentNodes.clear();
    }
    
    //connections.clear();
    for(int i = 0; i < connections.size();){
        if(!connections[i].second->getIsPersistent()){
            connections.erase(connections.begin()+i);
        }else{
            i++;
        }
    }
    
    for(auto &nodeTypeMap : persistentNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->loadPersistentPreset(persistentFolderPath);
        }
    }
    
    json.clear();
    json = ofLoadJson(persistentFolderPath + "/connections.json");
    for (ofJson::iterator sourceModule = json.begin(); sourceModule != json.end(); ++sourceModule) {
        for (ofJson::iterator sourceParameter = sourceModule.value().begin(); sourceParameter != sourceModule.value().end(); ++sourceParameter) {
            for (ofJson::iterator sinkModule = sourceParameter.value().begin(); sinkModule != sourceParameter.value().end(); ++sinkModule) {
                for (ofJson::iterator sinkParameter = sinkModule.value().begin(); sinkParameter != sinkModule.value().end(); ++sinkParameter) {
                    auto connection = createConnectionFromInfo(sourceModule.key(), sourceParameter.key(), sinkModule.key(), sinkParameter.key());
                    connection->setIsPersistent(true);
                }
            }
        }
    }
    
#ifdef OFXOCEANODE_USE_MIDI
    json.clear();
    for(auto &binding : persistentMidiBindings){
        for(auto &midiInPair : midiIns){
            midiInPair.second.removeListener(binding.second.get());
        }
        midiBindingDestroyed.notify(this, *binding.second.get());
    }
    persistentMidiBindings.clear();
    json = ofLoadJson(persistentFolderPath + "/midi.json");
    for (ofJson::iterator module = json.begin(); module != json.end(); ++module) {
        for (ofJson::iterator parameter = module.value().begin(); parameter != module.value().end(); ++parameter) {
            auto midiBinding = createMidiBindingFromInfo(module.key(), parameter.key(), true);
            if(midiBinding != nullptr){
                midiBinding->loadPreset(json[module.key()][parameter.key()]);
            }
        }
    }
#endif
    
    for(auto &nodeTypeMap : persistentNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->presetHasLoaded();
        }
    }
}

void ofxOceanodeContainer::updatePersistent(){
    string persistentFolderPath = "Persistent";
    for(auto &nodeTypeMap : persistentNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->savePersistentPreset(persistentFolderPath);
        }
    }
}

void ofxOceanodeContainer::setBpm(float _bpm){
    bpm = _bpm;
    for(auto &nodeTypeMap : dynamicNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->setBpm(bpm);
        }
    }
    for(auto &nodeTypeMap : persistentNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->setBpm(bpm);
        }
    }
}

void ofxOceanodeContainer::setPhase(float _phase){
    phase = _phase;
    for(auto &nodeTypeMap : dynamicNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->setPhase(phase);
        }
    }
    for(auto &nodeTypeMap : persistentNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->setPhase(phase);
        }
    }
}

void ofxOceanodeContainer::resetPhase(){
    for(auto &nodeTypeMap : dynamicNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->resetPhase();
        }
    }
    for(auto &nodeTypeMap : persistentNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->resetPhase();
        }
    }
}

void ofxOceanodeContainer::collapseGuis(){
    collapseAll = true;
    for(auto &nodeTypeMap : dynamicNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->getNodeGui().collapse();
        }
    }
    for(auto &nodeTypeMap : persistentNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->getNodeGui().collapse();
        }
    }
}
void ofxOceanodeContainer::expandGuis(){
    collapseAll = false;
    for(auto &nodeTypeMap : dynamicNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->getNodeGui().expand();
        }
    }
    for(auto &nodeTypeMap : persistentNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->getNodeGui().expand();
        }
    }
}

#ifdef OFXOCEANODE_USE_OSC

void ofxOceanodeContainer::setupOscSender(string host, int port){
    oscSender.setup(host, port);
}

void ofxOceanodeContainer::setupOscReceiver(int port){
    oscReceiver.setup(port);
}

void ofxOceanodeContainer::update(ofEventArgs &args){
    
    auto setParameterFromMidiMessage = [this](ofAbstractParameter& absParam, ofxOscMessage& m){
        if(absParam.type() == typeid(ofParameter<float>).name()){
            ofParameter<float> castedParam = absParam.cast<float>();
            castedParam = ofMap(m.getArgAsFloat(0), 0, 1, castedParam.getMin(), castedParam.getMax(), true);
        }else if(absParam.type() == typeid(ofParameter<int>).name()){
            ofParameter<int> castedParam = absParam.cast<int>();
            castedParam = ofMap(m.getArgAsFloat(0), 0, 1, castedParam.getMin(), castedParam.getMax(), true);
        }else if(absParam.type() == typeid(ofParameter<bool>).name()){
            absParam.cast<bool>() = m.getArgAsBool(0);
        }else if(absParam.type() == typeid(ofParameter<void>).name()){
            absParam.cast<void>().trigger();
        }else if(absParam.type() == typeid(ofParameter<string>).name()){
            absParam.cast<string>() = m.getArgAsString(0);
        }else if(absParam.type() == typeid(ofParameterGroup).name()){
            absParam.castGroup().getInt(1) = m.getArgAsInt(0);
        }else if(absParam.type() == typeid(ofParameter<vector<float>>).name()){
            ofParameter<vector<float>> castedParam = absParam.cast<vector<float>>();
            vector<float> tempVec;
            tempVec.resize(m.getNumArgs(), 0);
            for(int i = 0; i < tempVec.size(); i++){
                tempVec[i] = ofMap(m.getArgAsFloat(i), 0, 1, castedParam.getMin()[0], castedParam.getMax()[0], true);
            }
            castedParam = tempVec;
        }
        else if(absParam.type() == typeid(ofParameter<vector<int>>).name()){
            ofParameter<vector<int>> castedParam = absParam.cast<vector<int>>();
            vector<int> tempVec;
            tempVec.resize(m.getNumArgs(), 0);
            if(m.getArgType(0) == ofxOscArgType::OFXOSC_TYPE_FLOAT){
                for(int i = 0; i < tempVec.size(); i++){
                    tempVec[i] = ofMap(m.getArgAsFloat(i), 0, 1, castedParam.getMin()[0], castedParam.getMax()[0], true);
                }
            }
            else if(m.getArgType(0) == ofxOscArgType::OFXOSC_TYPE_INT32 || m.getArgType(0) == ofxOscArgType::OFXOSC_TYPE_INT64){
                for(int i = 0; i < tempVec.size(); i++){
                    tempVec[i] = ofClamp(m.getArgAsInt(i), castedParam.getMin()[0], castedParam.getMax()[0]);
                }
            }
            castedParam = tempVec;
        }
    };
    
    while(oscReceiver.hasWaitingMessages()){
        ofxOscMessage m;
        oscReceiver.getNextMessage(m);
        
        vector<string> splitAddress = ofSplitString(m.getAddress(), "/");
        if(splitAddress[0].size() == 0) splitAddress.erase(splitAddress.begin());
        if(splitAddress.size() == 1){
            if(splitAddress[0] == "phaseReset"){
                resetPhase();
            }else if(splitAddress[0] == "bpm"){
                setBpm(m.getArgAsFloat(0));
            }
        }else if(splitAddress.size() == 2){
            if(splitAddress[0] == "presetLoad"){
                string bankName = splitAddress[1];
                
                ofDirectory dir;
                map<int, string> presets;
                dir.open("Presets/" + bankName);
                if(!dir.exists())
                    return;
                dir.sort();
                int numPresets = dir.listDir();
                for ( int i = 0 ; i < numPresets; i++){
                    if(ofToInt(ofSplitString(dir.getName(i), "--")[0]) == m.getArgAsInt(1)){
                        string bankAndPreset = bankName + "/" + ofSplitString(dir.getName(i), ".")[0];
                        ofNotifyEvent(loadPresetEvent, bankAndPreset);
                        break;
                    }
                }
            }else if(splitAddress[0] == "presetSave"){
                savePreset("Presets/" + splitAddress[1] + "/" + m.getArgAsString(0));
            }else if(splitAddress[0] == "Global"){
                for(auto &nodeType  : dynamicNodes){
                    for(auto &node : nodeType.second){
                        ofParameterGroup* groupParam = node.second->getParameters();
                        if(groupParam->contains(splitAddress[1])){
                            ofAbstractParameter &absParam = groupParam->get(splitAddress[1]);
                            setParameterFromMidiMessage(absParam, m);
                        }
                    }
                }
                for(auto &nodeType  : persistentNodes){
                    for(auto &node : nodeType.second){
                        ofParameterGroup* groupParam = node.second->getParameters();
                        if(groupParam->contains(splitAddress[1])){
                            ofAbstractParameter &absParam = groupParam->get(splitAddress[1]);
                            setParameterFromMidiMessage(absParam, m);
                        }
                    }
                }
            }else{
                string moduleName = splitAddress[0];
                string moduleId = ofSplitString(moduleName, "_").back();
                moduleName.erase(moduleName.find(moduleId)-1);
                ofStringReplace(moduleName, "_", " ");
                if(dynamicNodes.count(moduleName) == 1){
                    if(dynamicNodes[moduleName].count(ofToInt(moduleId))){
                        ofParameterGroup* groupParam = dynamicNodes[moduleName][ofToInt(moduleId)]->getParameters();
                        if(groupParam->contains(splitAddress[1])){
                            ofAbstractParameter &absParam = groupParam->get(splitAddress[1]);
                            setParameterFromMidiMessage(absParam, m);
                        }
                    }
                }
                if(persistentNodes.count(moduleName) == 1){
                    if(persistentNodes[moduleName].count(ofToInt(moduleId))){
                        ofParameterGroup* groupParam = persistentNodes[moduleName][ofToInt(moduleId)]->getParameters();
                        if(groupParam->contains(splitAddress[1])){
                            ofAbstractParameter &absParam = groupParam->get(splitAddress[1]);
                            setParameterFromMidiMessage(absParam, m);
                        }
                    }
                }
            }
        }
        else if(splitAddress.size() == 3){
            if(splitAddress[0] == "presetLoad"){
                string bankAndPreset = splitAddress[1] + "/" + splitAddress[2];
                ofNotifyEvent(loadPresetEvent, bankAndPreset);
            }
        }
    }
}

#endif


#ifdef OFXOCEANODE_USE_MIDI

void ofxOceanodeContainer::setIsListeningMidi(bool b){
    isListeningMidi = b;
    for(auto &nodeTypeMap : dynamicNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->getNodeGui().setIsListeningMidi(b);
        }
    }
    for(auto &nodeTypeMap : persistentNodes){
        for(auto &node : nodeTypeMap.second){
            node.second->getNodeGui().setIsListeningMidi(b);
        }
    }
}

ofxOceanodeAbstractMidiBinding* ofxOceanodeContainer::createMidiBinding(ofAbstractParameter &p, bool isPersistent){
    if(midiBindings.count(p.getGroupHierarchyNames()[0] + "-|-" + p.getEscapedName()) == 0 && persistentMidiBindings.count(p.getGroupHierarchyNames()[0] + "-|-" + p.getEscapedName()) == 0){
        unique_ptr<ofxOceanodeAbstractMidiBinding> midiBinding = nullptr;
        if(p.type() == typeid(ofParameter<float>).name()){
            midiBinding = make_unique<ofxOceanodeMidiBinding<float>>(p.cast<float>());
        }
        else if(p.type() == typeid(ofParameter<int>).name()){
            midiBinding = make_unique<ofxOceanodeMidiBinding<int>>(p.cast<int>());
        }
        else if(p.type() == typeid(ofParameter<bool>).name()){
            midiBinding = make_unique<ofxOceanodeMidiBinding<bool>>(p.cast<bool>());
        }
        else if(p.type() == typeid(ofParameter<void>).name()){
            midiBinding = make_unique<ofxOceanodeMidiBinding<void>>(p.cast<void>());
        }
        else if(p.type() == typeid(ofParameter<vector<float>>).name()){
            midiBinding = make_unique<ofxOceanodeMidiBinding<vector<float>>>(p.cast<vector<float>>());
        }
        else if(p.type() == typeid(ofParameter<vector<int>>).name()){
            midiBinding = make_unique<ofxOceanodeMidiBinding<vector<int>>>(p.cast<vector<int>>());
        }
        else if(p.type() == typeid(ofParameterGroup).name()){
            midiBinding = make_unique<ofxOceanodeMidiBinding<int>>(p.castGroup().getInt(1));
        }
        else if(p.type() == typeid(ofParameter<pair<int, bool>>).name()){
            midiBinding = make_unique<ofxOceanodeMidiBinding<pair<int, bool>>>(p.cast<pair<int, bool>>());
        }
        if(midiBinding != nullptr){
            for(auto &midiInPair : midiIns){
                midiInPair.second.addListener(midiBinding.get());
            }
            midiBindingCreated.notify(this, *midiBinding.get());
            midiUnregisterlisteners.push(midiBinding->unregisterUnusedMidiIns.newListener(this, &ofxOceanodeContainer::midiBindingBound));
            auto midiBindingPointer = midiBinding.get();
            if(!isPersistent){
                midiBindings[p.getGroupHierarchyNames()[0] + "-|-" + p.getEscapedName()] = move(midiBinding);
            }else{
                persistentMidiBindings[p.getGroupHierarchyNames()[0] + "-|-" + p.getEscapedName()] = move(midiBinding);
            }
            return midiBindingPointer;
        }
    }else{
        ofLog() << "Parameter \"" << p.getEscapedName() << "\" already binded";
    }
    return nullptr;
}

bool ofxOceanodeContainer::removeMidiBinding(ofAbstractParameter &p){
    string midiBindingName = p.getGroupHierarchyNames()[0] + "-|-" + p.getEscapedName();
    if(midiBindings.count(midiBindingName) != 0){
        for(auto &midiInPair : midiIns){
            midiInPair.second.removeListener(midiBindings[midiBindingName].get());
        }
        midiBindingDestroyed.notify(this, *midiBindings[midiBindingName].get());
        midiBindings.erase(midiBindingName);
        return true;
    }
    return false;
}

void ofxOceanodeContainer::midiBindingBound(const void * sender, string &portName){
    ofxOceanodeAbstractMidiBinding * midiBinding = static_cast <ofxOceanodeAbstractMidiBinding *> (const_cast <void *> (sender));
    for(auto &midiInPair : midiIns){
        if(midiInPair.first != portName){
            midiInPair.second.removeListener(midiBinding);
        }
    }
    if(midiOuts.count(portName) != 0){
        midiBinding->bindParameter();
        midiSenderListeners.push(midiBinding->midiMessageSender.newListener([this, portName](ofxMidiMessage& message){
            switch(message.status){
                case MIDI_CONTROL_CHANGE:{
                    midiOuts[portName].sendControlChange(message.channel, message.control, message.value);
                    break;
                }
                case MIDI_NOTE_ON:{
                    midiOuts[portName].sendNoteOn(message.channel, message.pitch, message.velocity);
                }
                default:{
                    
                }
            }
        }));
    }
}

ofxOceanodeAbstractMidiBinding* ofxOceanodeContainer::createMidiBindingFromInfo(string module, string parameter, bool isPersistent){
    auto &collection = !isPersistent ? dynamicNodes : persistentNodes;
    string moduleId = ofSplitString(module, "_").back();
    module.erase(module.find(moduleId)-1);
    ofStringReplace(module, "_", " ");
    if(collection.count(module) != 0){
        if(collection[module].count(ofToInt(moduleId))){
            ofAbstractParameter* p = nullptr;
            if(collection[module][ofToInt(moduleId)]->getParameters()->contains(parameter)){
                p = &collection[module][ofToInt(moduleId)]->getParameters()->get(parameter);
            }
            else{
                p = &collection[module][ofToInt(moduleId)]->getParameters()->getGroup(parameter + " Selector").getInt(1);
            }
            return createMidiBinding(*p, isPersistent);
        }
    }
}

void ofxOceanodeContainer::addNewMidiMessageListener(ofxMidiListener* listener){
    for(auto &midiInPair : midiIns){
        midiInPair.second.addListener(listener);
    }
}

#endif

ofxOceanodeAbstractConnection* ofxOceanodeContainer::createConnectionFromInfo(string sourceModule, string sourceParameter, string sinkModule, string sinkParameter){
    string sourceModuleId = ofSplitString(sourceModule, "_").back();
    sourceModule.erase(sourceModule.find(sourceModuleId)-1);
    ofStringReplace(sourceModule, "_", " ");
    
    string sinkModuleId = ofSplitString(sinkModule, "_").back();
    sinkModule.erase(sinkModule.find(sinkModuleId)-1);
    ofStringReplace(sinkModule, "_", " ");
    
    bool sourceIsDynamic = false;
    if(dynamicNodes.count(sourceModule) == 1){
        if(dynamicNodes[sourceModule].count(ofToInt(sourceModuleId)) == 1){
            sourceIsDynamic = true;
        }
    }
    
    bool sinkIsDynamic = false;
    if(dynamicNodes.count(sinkModule) == 1){
        if(dynamicNodes[sinkModule].count(ofToInt(sinkModuleId)) == 1){
            sinkIsDynamic = true;
        }
    }
    
    
    auto &sourceModuleRef = sourceIsDynamic ? dynamicNodes[sourceModule][ofToInt(sourceModuleId)] : persistentNodes[sourceModule][ofToInt(sourceModuleId)];
    auto &sinkModuleRef = sinkIsDynamic ? dynamicNodes[sinkModule][ofToInt(sinkModuleId)] : persistentNodes[sinkModule][ofToInt(sinkModuleId)];
    
    if(sourceModuleRef == nullptr || sinkModuleRef == nullptr) return nullptr;
    
    if(sourceModuleRef->getParameters()->contains(sourceParameter) && sinkModuleRef->getParameters()->contains(sinkParameter)){
        ofAbstractParameter &source = sourceModuleRef->getParameters()->get(sourceParameter);
        ofAbstractParameter &sink = sinkModuleRef->getParameters()->get(sinkParameter);
        
        temporalConnectionNode = sourceModuleRef.get();
        auto connection = sinkModuleRef->createConnection(*this, source, sink);
        if(!isHeadless){
            connection->setSinkPosition(sinkModuleRef->getNodeGui().getSinkConnectionPositionFromParameter(sink));
            connection->setTransformationMatrix(&transformationMatrix);
        }
        temporalConnection = nullptr;
        return connection;
    }
    
    return nullptr;
}

ofxOceanodeAbstractConnection* ofxOceanodeContainer::createConnectionFromCustomType(ofAbstractParameter &source, ofAbstractParameter &sink){
    return typesRegistry->createCustomTypeConnection(*this, source, sink);
}

