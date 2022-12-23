//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
// 

#include "Node.h"
using namespace std;
Define_Module(Node);

void Node::initialize()
{
    imSender = 0;
    isInit = 1;
    frameExpected = 0;
    senderWindow = NULL;
    timers = NULL;
    WS = getParentModule()->par("WS").intValue();
    TO = getParentModule()->par("TO").doubleValue();
    PT = getParentModule()->par("PT").doubleValue();
    TD = getParentModule()->par("TD").doubleValue();
    ED = getParentModule()->par("ED").doubleValue();
    DD = getParentModule()->par("DD").doubleValue();
    LP = getParentModule()->par("LP").doubleValue();
}

void Node::initializeRoutine(cMessage *msg)
{
    isInit = 0;
    if(string(msg->getName())!="-1") //imSender
    {
        imSender = 1;

        string filepath = "../texts/inputx.txt";
        char index = par("Index").stringValue()[0];
        filepath[14] = index;

        senderWindow = new SenderWindow(WS,filepath);
        timers = new cMessage*[WS + 1];
        for (int i = 0; i <= WS; ++i)
        {

            timers[i] = new cMessage(to_string(i).c_str(),TimerType::timeout);
        }

        int startTime = atoi(msg->getName());
        scheduleAt(simTime()+startTime, new cMessage("",0));
    }
    cancelAndDelete(msg);
}

string Node::constructLog1Message(int error_code)
{
    string message;
    message += "At time " + simTime().str();
    message += ", Node " + par("Index").stdstringValue();
    message += ", Introducing channel error with code=" + bitset<4>(error_code).to_string();
    message += "\n";
    return message;
}

string Node::constructLog2Message(bool sent, int seq_num, string payload, char parity, int isValid, int error_code, bool isSecondDuplicate)
{
    string message;
    message += "At time " + simTime().str();
    message += ", Node " + par("Index").stdstringValue();
    message += sent ? " sent" : " received";
    message += " frame with seq_num=" + to_string(seq_num);
    message += " and payload=\"" + payload;
    message += "\" and trailer=" + bitset<8>(parity).to_string();
    message += ", Modified " + to_string(isValid);
    message += ", Lost ";
    message += (error_code & 0b0100) ? "Yes": "No";
    message += ", Duplicate ";
    message += (!(error_code & 0b0010)) ? "0": ((!isSecondDuplicate) ? "1" : "2");
    message += ", Delay ";
    ostringstream strs;
    strs << ((error_code & 0b0001) ? ED : 0);
    message += strs.str(); //TODO: Check which delay to print
    message += "\n";
    return message;

}

string Node::constructLog3Message(int seq_num)
{
    string message;
    message += "Timeout event at time " + simTime().str();
    message += ", Node " + par("Index").stdstringValue();
    message += " for frame with seq_num=" + to_string(seq_num);
    message += "\n";
    return message;
}

string Node::constructLog4Message(FrameType type, int seq_num, bool isLost)
{
    string message;
    message += "At time " + simTime().str();
    message += ", Node " + par("Index").stdstringValue();
    message += " Sending ";
    message += (type == FrameType::Ack) ? "ACK" : "NACK";
    message += " with number " + to_string(seq_num);
    message += ", loss ";
    message += (isLost) ? "Yes" : "No";
    message += "\n";
    return message;
}

// Processes send request according to the four control bits
// Returns whether the message processed was a second duplicate
bool Node::senderProcessMessage(cMessage *msg)
{
    int seq = atoi(msg->getName());
    DataMessage* message = senderWindow->getMsg(seq);
    bool isSecondDuplicate = false;
    if(msg->getKind() == TimerType::duplicateProcessTime) //if the message is not a second duplicate
    {
        isSecondDuplicate = true;
        cancelAndDelete(msg);
    }
    int prefix = message->getKind();
    bool delay = prefix & 0b0001;
    bool loss = prefix & 0b0100;
    bool mod = prefix & 0b1000;
    double delayValue = TD + (delay ? ED : 0);
    int bitToModify = 2; //TODO: OPTIONAL: Add randomness
    // Log 2
    string logMessage = constructLog2Message(true , message->getSeqNum(), message->getPayloadWithFraming(), message->getParity(), mod ? bitToModify : -1, prefix, isSecondDuplicate);
    cout << logMessage;
    if (!loss)
    {
        message = new DataMessage(*message);
        if(mod)
        {
            string temp = message->getPayloadWithFraming();
            temp[1] = char(temp[1]^(1 << bitToModify));
            message->setPayload(temp);
        }
        sendDelayed(message,delayValue,"outNode");
    }
    return isSecondDuplicate;
}

void Node::scheduleNextMessage(cMessage *msg)
{
    int seq = senderWindow->nextSeqNumToSend();
    if (msg->getKind() == TimerType::timeout)
    {
        senderWindow->resetSendingPointer();
        for (int seqNum = 0; seqNum <= WS; ++seqNum)
        {
            if(timers[seqNum] == msg)
            {
                // Log 3
                string logMessage = constructLog3Message((seqNum + WS) % (WS + 1)); //Circular decrement
                cout << logMessage;
            }
            if(timers[seqNum]->isScheduled())
            {
                cancelEvent(timers[seqNum]);
            }
        }
        seq = senderWindow->nextSeqNumToSend();
        senderWindow->getMsg(seq)->setKind(0);
    }
    if(seq!=-1)
    {
        DataMessage* message = senderWindow->getMsg(seq);
        int prefix = message->getKind();
        bool dup = prefix & 0b0010;
        if(dup)
        {
            scheduleAt(simTime() + PT + DD, new cMessage(to_string(seq).c_str(), TimerType::duplicateProcessTime));
        }
        scheduleAt(simTime() + PT, new cMessage(to_string(seq).c_str(),TimerType::processTime));
        if(timers[(seq + 1) % (WS + 1)]->isScheduled())
        {
            cancelEvent(timers[(seq + 1) % (WS + 1)]);
        }
        scheduleAt(simTime() + TO, timers[(seq + 1) % (WS + 1)]);
        senderWindow->advanceSendingPointer();
        // Log 1
        string logMessage = constructLog1Message(prefix);
        cout << logMessage;
    }

    if (msg->getKind() != TimerType::timeout)
    {
        cancelAndDelete(msg);
    }
}

void Node::senderLogic(cMessage *msg)
{
    if (!msg->isSelfMessage()) //If the message is from the receiver (Ack or Nack)
    {
        DataMessage* message = check_and_cast<DataMessage*>(msg);
        // TODO: Log 2 Stupid, Declaring reception of ACK/NACK can be done is so many better ways
        string logMessage = constructLog2Message(false , message->getSeqNum(), message->getPayloadWithFraming(), message->getParity(), -1, 0, false);
        cout << logMessage;
        senderWindow->moveLowerEdge(message);
        int seq = message->getSeqNum();
        if(timers[seq]->isScheduled() && message->getFrameType() == FrameType::Ack)
        {
            cancelEvent(timers[seq]);
            message->setKind(0);
        }
        else
        {
            cancelAndDelete(msg);
            return;
        }
    }
    //The message is a timing control message
    if(msg->isSelfMessage() && msg->getKind()==TimerType::processTime && senderProcessMessage(msg))
    {
        return;
    }
    scheduleNextMessage(msg);
}

void Node::receiverLogic(cMessage *msg)
{
    if (!msg->isSelfMessage()) //If message is from sender
    {
        //Log 2
        DataMessage* message = check_and_cast<DataMessage*>(msg);
        int prefix = message->getKind();
        //TODO: Second Duplicate???
        string logMessage = constructLog2Message(false , message->getSeqNum(), message->getPayloadWithFraming(), message->getParity(), message->isValid(), prefix, (prefix & 0b0010));
        cout << logMessage;
        scheduleAt(simTime() + PT, msg); //reschedule
        return;
    }
    //Send control frame
    DataMessage * message = check_and_cast<DataMessage*>(msg);
    bool isLost = false; //TODO: Add randomness (random <= Probabilty)
    if(message->isValid()==-1 && message->getSeqNum() == frameExpected)
    {
        // Log 4
        frameExpected = (frameExpected + 1) % (WS + 1);
        message = new DataMessage(frameExpected,FrameType::Ack);
        string logMessage = constructLog4Message(FrameType::Ack, message->getSeqNum(), isLost);
        cout << logMessage;
    }
    else
    {
        // Log 4
        message = new DataMessage(frameExpected,FrameType::Nack);
        string logMessage = constructLog4Message(FrameType::Nack, message->getSeqNum(), isLost);
        cout << logMessage;
    }

    if (isLost)
    {
        cancelAndDelete(message);
    }
    else
    {
        sendDelayed(message, TD, "outNode");
    }

    cancelAndDelete(msg);
}

void Node::handleMessage(cMessage *msg)
{
    if(isInit)
    {
        initializeRoutine(msg);
        return;
    }
    //Sending and Receiving Logic
    if(imSender)
    {
        senderLogic(msg);
    }
    else
    {
        receiverLogic(msg);
    }
}



Node::~Node() {
    if(senderWindow)
    {
        for (int i = 0; i <= WS; ++i)
        {
            cancelAndDelete(timers[i]);
        }
        delete[] timers;
        delete senderWindow;
    }
}
