#include "netClient.h"

using std::endl;

//
//  netClient function implementations
//

netClient::netClient() : netbase( 1), sdServer((int)INVALID_SOCKET), serverPort(-1)
{
    openLog();
    debugLog << "===Starting client===" << endl;
    
    //Set the timeout for connecting to the server...
    connTimeout.tv_sec = 3;
    connTimeout.tv_usec = 0;
}

netClient::~netClient()
{
    openLog();
    debugLog << "===Ending client===" << endl << endl;
    if (sdServer != (int)INVALID_SOCKET)
        closeSocket(sdServer);
    closeLog();

    //Now the base class destructor is invoked by C++
}

bool netClient::setConnTimeout( int seconds, int microsec)
{
    //TODO: make sure the input is reasonable

    connTimeout.tv_sec = seconds;
    connTimeout.tv_usec = microsec;
    
    return true;
}


//connect to server "address:port"
int netClient::doConnect(const string& serverAddress, int port, int lport)
{
	struct	sockaddr_in sad;   //Server address struct
	int rv;                    //Return value
	int namelen = sizeof( struct sockaddr_in);
    struct hostent *host;       //Host name entry

    openLog();

    //Make sure we aren't already connected !!!  Client only allows one connection
    if (ready) {
        debugLog << "Already connected on socket " << sdServer << endl;
        lastError = "Already connected";
        return -1;
    }

    //Create a socket
    sdServer = socket( AF_INET, SOCK_STREAM, 0);
    if ( sdServer == (int)INVALID_SOCKET ) {
        lastError = "Could not create socket";
        debugLog << lastError << endl;
        return -1;
    }

    //Set socket to be non-blocking
    if (unblockSocket( sdServer) < 0)
        return -1;

    //Load connection information
	memset((char *)&sad,0,sizeof(sad));    // clear sockaddr structure
	sad.sin_family = AF_INET;	          //set family to Internet
	sad.sin_port = htons(port);            //Set port (network short format)
	sad.sin_addr.s_addr = inet_addr(serverAddress.c_str()); //Convert the server address


    //If not a numeric aaddress, resolve it
	if (sad.sin_addr.s_addr == INADDR_NONE)
	{
        host = NULL;    //...
        host = gethostbyname(serverAddress.c_str());
        if (host == NULL) {
            lastError = string("Unknown host ") + serverAddress.c_str();
            debugLog << lastError << endl;
            closeSocket(sdServer);                     //Cleanup the socket
            return -1;
        }
        memcpy( &sad.sin_addr, host->h_addr_list[0], host->h_length);
	}


    //Connect to the server
	rv = connect(sdServer, (const struct sockaddr*)&sad, sizeof( struct sockaddr_in));

    //Special case for blocking sockets
	if (rv == SOCKET_ERROR) {
        #ifdef WIN32
        fd_set sdConnSet;
        FD_ZERO(&sdConnSet);
        FD_SET( (unsigned int)sdServer, &sdConnSet);
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
             rv = select(sdMax+1, (fd_set *) 0, &sdConnSet, (fd_set *) 0, &connTimeout);
             if (rv == 0) {
                debugLog << "Timeout waiting to connect to server" << endl;
                lastError = "Timed out";
                return SOCKET_ERROR;      //Try again later
            }
        }
        #endif
    }
    
    if (rv == SOCKET_ERROR) {
        debugLog << "   " << getSocketError() << endl;
        lastError = string("Could not connect to ") + serverAddress;
        debugLog << lastError << endl;
        closeSocket(sdServer);                     //Cleanup the socket
        return SOCKET_ERROR;    //Some problem connecting to the server
    }
    else if (rv > 0) {
        getsockname( sdServer, (struct sockaddr*)&sad, &namelen);
        debugLog << "Connected to socket " << sdServer
        << " from " << inet_ntoa(sad.sin_addr) << ":" << ntohs(sad.sin_port) << endl;

        //Add to sdSet
        conSet.insert(sdServer);
        buildSocketSet();

        //This is not needed since there is only one connection!!
        if (sdMax < sdServer)
            sdMax = sdServer;


        //Connected, set the member variables
        serverPort = port;
        ready = true;
        return sdServer;
    }

    return 0;       //Still connecting!!  Try back later...
}

//Inherited function for closing a network socket
int netClient::closeSocket( int sd)
{
    int rv;

    //Special case if server port was shut down
    if (sd == sdServer)
        ready = false;

    //Do normal base class closing
    rv = netbase::closeSocket( sd);

    if (sd == sdServer)
        sdServer = INVALID_SOCKET;

    return rv;
}

//Disconnect from the server... May or may not actually be connected
int netClient::doDisconnect()
{
    int result = 0;

    openLog();
    if (sdServer != (int)INVALID_SOCKET) {
        
        //Debug information when closing a connected socket
        if (ready) {
        
            //Send explicit disconnect message to server
            sendMessage( NET_DISCONNECT_ID, 0, NULL, NET_CB_VERSION);
        
            debugLog << "Closing socket " << sdServer << endl;
            lastError = lastError + string("; Closing socket");
        }
        closeSocket(sdServer);      //Base class socket close, handles conSet
    }
    else {
        lastError = "Already disconnected";
        debugLog << "Client already disconnected" << endl;
        result = -1;
    }
    ready = false;
    closeLog();
    return result;
}


//Send message to the server (use socket sdServer)
int netClient::sendMessage( short i, short len, unsigned char* d, short ver) {
    netpacket nmsg;
    nmsg.create(sdServer, i, ++lastMessage, len, d, ver);

    return netbase::sendMessage( nmsg);
}


//Read the network, handle any incoming data
int netClient::run()
{
    int rv;

    //debugLog << "Checking network connection...";

    try {
        //Rebuild the client set, check for incoming data
        buildSocketSet();
        rv = select(sdMax+1, &sdSet, (fd_set *) 0, (fd_set *) 0, &timeout);

        if (rv == SOCKET_ERROR) {   //Socket select failed
            debugLog << "Socket select error:"  << getSocketError() << endl;
        }
        else if (rv == 0) {         //No new messages
            //debugLog << "No new server data" << endl;
        }
        else {                      //Something pending on a socket
            debugLog << "Incoming server message" << endl;
            readSockets();
        }
    }
    catch(...) {
        debugLog << "Unhandled exception!!" << endl;
        rv = -1;
    };

    return rv;
}