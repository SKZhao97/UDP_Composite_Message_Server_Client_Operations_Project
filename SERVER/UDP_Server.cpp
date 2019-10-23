/*
Author: Sikai Zhao
Class: ECE 6122
Last date modified: 10/22/19
Description: Server for UDP, using two thread to handle command and message. Support three types of server command and four kinds of message type.
*/

#include <iostream>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <thread>
#include <mutex>
#include <map>          //Map structure
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>  /* Needed for getaddrinfo() and freeaddrinfo() */
#include <unistd.h> /* Needed for close() */
#include <errno.h>  //To output the error code of sendto and recvfrom

using namespace std;

typedef int SOCKET;
struct udpMessage       //Define the udpMessage structure
{
	unsigned char nVersion;
    unsigned char nType;
    unsigned short nMsgLen;
    unsigned long lSeqNum;
    char chMsg[1000];
};
//Global variables
int sockfd;                  //Socket descriptor
bool shutDown = false;       //Bool to shutdown the server
int compSeqNum = 0;          //Composite sequence number
int messageMax=1000;         //Maximum of the message length
map<int, udpMessage> compositeMessage;//Map to store the composit from clients
map<int, sockaddr_in> clientsInfo;//Map to store the address of clients
mutex comp_Mutex;             //Define the mutex

//Functions
int sockInit();              //Initiate the socket
int sockQuit();              //Quit the socket
int sockClose(SOCKET sock);  //Close the socket
void error(const char *msg); //Error detection
void startServer(int portno);//Start the server
void messageReceiving();     //Reveive the message from the clients
void messageHandling(udpMessage message);//Handle the message received
void commandInput();         //Input the command and do the correspnding tasks
void compositeAdding(udpMessage message);   //Add the commposite
void compositeSending();     //Send the composite
void compositeClearing();    //Clear the composite
void compositeDisplaying();  //Display the composite

/*
* Function name: Main
* Summary: Take in commandline arguments and detect error. Define socket related variables such as portno and start the Sever. Then spawn threads to do tasks of receiving message and inputting command. The server and send, display or add the composite according to the command and the nType of the message received.
* Parameter:
* 1. argc: number of input commandline arguments
* 2. *argv[]: array of input commandline arguments
* Return: N/A
*/
int main(int argc, char*argv[])
{
    if (argc < 2)   //Error if arguments less than 2
    {
        fprintf(stderr, "ERROR, no port provided\n");
        exit(1);
    }
    int sockfd, newsockfd, portno;
    portno = atoi(argv[1]);//Convert the port number string to an int
    startServer(portno);//Start the server

    // Spawn two threads to handle message receiving and command input
    std::thread threadReceiving(messageReceiving);
    std::thread threadCommand(commandInput);

    //Wait until threads finish when shutted down
    threadReceiving.join();
    threadCommand.join();

    // Quit and Close the socket
    sockQuit();
    sockClose(sockfd);//
}
/*
* Function name: sockInit
* Summary: Initiate the socket
* Parameter: N/A
* Return: N/A
*/
int sockInit(void)
{
#ifdef _WIN32
    WSADATA wsa_data;
    return WSAStartup(MAKEWORD(1, 1), &wsa_data);
#else
    return 0;
#endif
}
/*
* Function name: sockQuit
* Summary: Cross-platform socket quit
* Parameter: N/A
* Return: N/A
*/
int sockQuit(void)
{
#ifdef _WIN32
    return WSACleanup();
#else
    return 0;
#endif
}
/*
* Function name: sockQuit
* Summary: Cross-platform socket close
* Parameter: N/A
* Return: N/A
*/
int sockClose(SOCKET sock)
{
    int status = 0;
#ifdef _WIN32
    status = shutdown(sock, SD_BOTH);
    if (status == 0) 
    {
        status = closesocket(sock); 
    }
#else
    status = shutdown(sock, SHUT_RDWR);
    if (status == 0) 
    { 
        status = close(sock);
    }
#endif
    return status;
}
/*
* Function name: error
* Summary: Output error message and exit
* Parameter: const char *msg
* Return: N/A
*/
void error(const char *msg)
{
    perror(msg);
    exit(1);
}
/*
* Function name: startServer
* Summary: Start the Server using the commandline input server address and port number;
* Parameter: int portno: port number used
* Return: N/A
*/
void startServer(int portno)
{
    struct sockaddr_in serv_addr;
    sockInit();// Create the socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    // Make sure the socket was created
    if (sockfd < 0)
        error("ERROR opening socket");
    // Zero out the variable serv_addr
    memset((char *)&serv_addr, 0, sizeof(serv_addr));
    
    // Initialize the serv_addr
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    // Convert port number from host to network
    serv_addr.sin_port = htons(portno);
    // Bind the socket to the port number
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
    {
        error("ERROR on binding");
    }
}
/*
* Function name: messageReceiving
* Summary: Receiving the message from the clients
* Parameter: N/A
* Return: N/A
*/
void messageReceiving()
{
	int n;
    int newsockfd;
    struct sockaddr_in from;//Define the sockaddr_in for client
    socklen_t fromlen;
    fromlen = sizeof(struct sockaddr_in);
    udpMessage buffer;//Define udpMessage to store the message
    
    // Loop indefinetly receiving messages from clients
    while (true)
    {
        // Receive a message
        n = recvfrom(sockfd, &buffer, sizeof(buffer), 0, (struct sockaddr *)&from, &fromlen);
        if (n < 0)
        {
            error("recvfrom");
        }
        // Store the clients information in the map
        clientsInfo[from.sin_port] = from;
        //Call function the handle the message
        messageHandling(buffer);
    }
    
    sockQuit();
    sockClose(newsockfd);
    sockClose(sockfd);//Close the socket
}
/*
* Function name: messageHandling
* Summary: Handling the message that received from clients
* Parameter: udpMessage message
* Return: N/A
*/
void messageHandling(udpMessage message)
{
    // Use mutex to lock to protect the conflict between command and message
    lock_guard<mutex> lock(comp_Mutex);
    // Convert the lSeqNum and nMsgLen values from network to host
    message.lSeqNum = ntohl(message.lSeqNum);
    message.nMsgLen = ntohs(message.nMsgLen);

    // Ignore the message without nVersion 1
    if (message.nVersion != 1) {
        cout<<"The nVersion is not 1, message ignored"<<endl;
	cout << "Please enter a command:"<<endl;
        return;
    }

    // Determines function according to the nType of the message
    switch(message.nType)
    {
        case 0://If the nType is 0
        {
            //Clear the composite message
            cout << "Message nType 0 : Clearing the composite message" << endl;
	    cout << "Please enter a command:"<<endl;
            compositeClearing();//Call function compositeClearing()
            break;
        }
        case 1://If the nType is 1
        {
            //Clear the composite message and use the current message as the start of a new composite message
            cout << "Message nType 1: Clearing and starting a new composite message" << endl;
	    cout << "Please enter a command:"<<endl;
            compositeClearing();//Call function compositeClearing()
            message.lSeqNum = 0;//Set the lSeqNum of the current message is 0
            compositeAdding(message);//Call function compositeAdding(message)
            break;
        }
        case 2://If the nType is 2
        {
            //Add the current message to the composite message
            cout << "Message nType 2: Adding to the composite message" << endl;
	    cout << "Please enter a command:"<<endl;
            compositeAdding(message);//Call function compositeAdding(message)
            break;
        }
        case 3://If the nType is 3
        {
            //Send the composite message to all clients and clear the composite
            cout << "Message nType 3: Sending composite message to all clients" << endl;
	    cout << "Please enter a command:"<<endl;
            compositeAdding(message);//Call function compositeAdding(message)
            compositeSending();//Call function compositeAdding(message)
            break;
        }
        default://If the nType is not defined
        {
            cout<<"The nType is not valid, ignored"<<endl;
	    break;
        }
    }
}
/*
* Function name: commandInput
* Summary:
    1. Prompt the user for command
    2. Call functions to do the stuff according to the user's input command
* Parameter: N/A
* Return: N/A
*/
void commandInput()
{
    int command;
    //Prompt fot input command while client not shutted
    while(!shutDown)
    {
        cin.clear();
        cout << "Please enter a command: " << endl;
        cin >> command;
        // Check if the input is valid
        if(cin.good())
        {
			lock_guard<mutex> lock(comp_Mutex);
    		switch(command)
    		{
        		case 0:// If command is 0
                {
                    //Sends the composite message to all clients
            		compositeSending();
            		break;
                }
        		case 1://If command is 1
                {
                    // Clears the current composite message
                    compositeClearing();
                    break;
                }
        		case 2://If command is 2
                {
                    //Display the current composite message
                    compositeDisplaying();
                    break;
                }
                default:
                {
                    cout << "Please input a valid command, choosing from 0, 1, and 2" << endl;
                    break;
                }
    		}
        }
        else
        {
            cout << "Invalid input. Command should be a number" << endl;
            cin.clear();
	    //cin.sync();
            cin.ignore(1000,'\n');
        }
    }
}
/*
* Function name: compositeAdding
* Summary: Add the composite newly received to the composite message
* Parameter: udpMessage message
* Return: N/A
*/
void compositeAdding(udpMessage message)
{
    //Store the message and its lSeqNum to the map of composite Message
	compositeMessage[message.lSeqNum] = message;
	int size=0;//Define a variable to store the size
    
    //Check if the length is larger than 1000 after adding
	for (auto& message_sub : compositeMessage)//Loop the compositeMessage
	{
		size += message_sub.second.nMsgLen;//Obtain total length of composite message
	}
	if (size > 1000)//If the size is larger than 1000, then send the first 1000
	{
		compositeSending();//Call function compositeSending()
	}
}
/*
* Function name: compositeSending
* Summary: Sending the composite message to all clients when:
    1. the command of server is 0;
    2. the nType of message is 3
    3. the total length of the composite message is larger than 1000;
* Parameter: N/A
* Return: N/A
*/
void compositeSending()
{
    char msgSending[messageMax];//Define a char array to store the message to be sent and initiate it to zero
    int msgSendLength=0;//Define an int to store the length of the message to be sent
    char msgRemaining[messageMax];//Define a char array to store the remaining message after sending first 1000 character
    int msgRemLength=0;//Define an int to store the length of the message remaining
    //Traverse the compositeMessage map and combine messages togehter
    for(const auto& message_sub : compositeMessage)
    {
        for(int i=0; i<message_sub.second.nMsgLen; i++)
        {
            if(msgSendLength < messageMax)
            {
                msgSending[msgSendLength] = message_sub.second.chMsg[i];//If current length is less than max, store the char in the combined message
                msgSendLength++;
            }
            else
            {
                msgRemaining[msgRemLength] = message_sub.second.chMsg[i];//If current length is larger than max, store the char in the the remaining;
                msgRemLength++;
            }
        }
    }
    
    compositeClearing();//Clear the composite after store message in chMsg
    
    //Send to all clients
    int n;
    socklen_t fromlen = 0; 
    fromlen = sizeof(struct sockaddr_in);
    
    udpMessage message{0};
    //Copy the message of length of sending to the udpMessage's chMsg
    strncpy(message.chMsg, msgSending, msgSendLength);
    //Convert values from host to network
    message.lSeqNum = htonl(compSeqNum);
    message.nMsgLen = htons(msgSendLength);
    
    // Send out the composite message to all connected clients
    for(const auto &client : clientsInfo)//Traverse the clientsInfo map to get the client addresses
    {
        n = sendto(sockfd, &message, sizeof(message), 0, (struct sockaddr *)&client.second, fromlen);//Send
        if (n < 0)
        {
            error("ERROR writing to socket");
        }
    }
    // Increment sequence number
    compSeqNum++;
    
    //If there is message remaining
    if(msgRemLength > 0)
    {
	//Insert the remaining message to the composite message as the first
        udpMessage remMessage;
        memset(remMessage.chMsg, 0, messageMax);
        strncpy(remMessage.chMsg, msgRemaining, msgRemLength);
        remMessage.lSeqNum = 0;
        remMessage.nMsgLen = msgRemLength;
        compositeMessage.insert({remMessage.lSeqNum, remMessage});
    }
}
/*
* Function name: compositeClearing
* Summary: Clear the composite message
* Parameter: N/A
* Return: N/A
*/
void compositeClearing()
{
    compositeMessage.clear();//Clear the map
}
/*
* Function name: compositeDisplaying
* Summary: Display the composite message
* Parameter: N/A
* Return: N/A
*/
void compositeDisplaying()
{
    //use the method used when sending composite to get the message.chMsg and print it to the console
    char msgDisplaying[messageMax]{0};
    int msgDisLength=0;
    char msgRemaining[messageMax];
    int msgRemLength=0;

    for(const auto& msg : compositeMessage)
    {
        for(int i=0; i<msg.second.nMsgLen; i++)
        {
            if(msgDisLength < messageMax)
            {
                msgDisplaying[msgDisLength] = msg.second.chMsg[i];
                msgDisLength++;
            }
        }
    }
    //Print the composite message to the console
    printf("Composite message: %.*s\n", msgDisLength, msgDisplaying);
}
