
//netServer: listens for incoming connections, handles incoming data.
//  Derived your class from this, and override handleIncoming( netpacket *pkt)

#include "netserver.h"
#include <iostream>
#include <iomanip>

using std::cerr;
using std::endl;
using std::hex;
using std::dec;
using std::ios;
using std::setfill;
using std::setw;

//
//  Function implementations
//


//Constructor, specify the maximum client connections
netserver::netserver(unsigned int max): netbase( max), serverPort(-1), sdListen(INVALID_SOCKET)
{
    //Everything else should have been taken care of by the netbase constructor
    openLog();
    debugLog << "===Starting server===" << endl;
    FD_ZERO( &listenSet);
}

//Destructor... was virtual
netserver::~netserver()
{
    if ( (sdListen != (int)INVALID_SOCKET) || ready)
        closePort();
    openLog();
    debugLog << "===Ending server===" << endl << endl;
    closeLog();
    //Now the base class destructor is invoked by C++
}


//Open a socket, bind, and listen on specified port
int netserver::openPort(short port) {
	int	sd;	 // socket descriptors
	struct	sockaddr_in sad; // structure to hold server's address
	int rv;    //Return value

    if (ready)
        debugLog << "Warning: Opening port while server ready" << endl;
    ready = false;  //Assume not ready until port is opened

    //Restart the log if it was closed
    openLog();

    //Get a socket
    sd = socket( AF_INET, SOCK_STREAM, 0);
    if ( sd == (int)INVALID_SOCKET )
    {
        debugLog << "Socket error: " << getSocketError() << endl;
        return -1;
    }

    if (unblockSocket( sd) < 0)
        return -1;

	memset((char *)&sad,0,sizeof(sad)); // clear sockaddr structure
	sad.sin_family = AF_INET;	  // set family to Internet
	sad.sin_addr.s_addr = INADDR_ANY; // set the local IP address
	sad.sin_port = htons(port);

	rv = bind(sd, (sockaddr*)&sad, sizeof( struct sockaddr_in));
	if (rv == -1) {
        debugLog << "Cannot bind, localhost:" << port << endl;
        closeSocket(sd);
        return -1;    //Cannot bind socket
    }

    rv  = listen(sd, conMax);    //List on port... allow MAX_CON clients
    if (rv == -1) {
        debugLog << "Cannot listen on port " << port << endl;
        closeSocket(sd);
        return -1;    //Cannot listen on port
    }

    //Add to sdSet
    FD_SET( (unsigned int)sd, &sdSet);
    if (sdMax < sd)
        sdMax = sd;

    //Set the class members
    sdListen = sd;
    serverPort = port;
    ready = true;

    debugLog << "** Listening on port " << port
                << ", socket " << sd
                << ", limit " << conMax << " clients **" << endl;

    return sd;
}

//Stop listening on the port
void netserver::closePort()
{
    openLog();
    if (ready)
        debugLog << "Server closing port " << serverPort << endl << endl;
    else
        debugLog << "Warning: Closing port, may already be closed!" << endl;

    ready = false;
    
    //Close the socket.  ready MUST be set to FALSE or there will be a loop
    if (sdListen != (int)INVALID_SOCKET)
        closeSocket( sdListen);
    sdListen = (int)INVALID_SOCKET;
    
    closeLog();
    
}

//Inherited function for closing a network socket
int netserver::closeSocket( int sd)
{
    int rv;

    //Do normal base class closing
    rv = netbase::closeSocket( sd);

    //Special test case
    if (sd == sdListen)
        sdListen = (int)INVALID_SOCKET;

    return rv;
}

//Read the network, handle any incoming data
int netserver::run()
{
    int rv=0;

    //debugLog << "Checking network connections...";

    try {
    
        //First, look for incoming connections
        checkPort();
    
        if (conSet.size() > 0) {
            //Rebuild the client set
            buildSocketSet();
            
            //Check for incoming client data
            rv = select(sdMax+1, &sdSet, (fd_set *) 0, (fd_set *) 0, &timeout);
            if (rv == SOCKET_ERROR) {   //Socket select failed
                debugLog << "Socket select error:"  << getSocketError() << endl;
            }
            else if (rv == 0) {         //No new messages
                //debugLog << "No new client data" << endl;
            }
            else {                      //Something pending on a socket
                debugLog << "Incoming client data" << endl;
                readSockets();
            }
        }
        //else debugLog << "Waiting for clients" << endl;   //This would print way too much
            
    }
    catch(...) {
        debugLog << "Unhandled exception!!" << endl;
        rv = -1;
    };

    return rv;
}

//This creates an FD_SET from the server port listening sockets only
int netserver::buildListenSet()
{
    FD_ZERO( &listenSet );
    FD_SET( (unsigned int)sdListen, &listenSet );

    return 0;
}

//Check sdListen for incoming connections
int netserver::checkPort()
{
    int connection=0, rv;

    //Rebuild the server socket set
    buildListenSet();
    
    //Check each listen socket for incoming data
    rv = select(sdListen+1, &listenSet, (fd_set *) 0, (fd_set *) 0, &timeout);
    
    if (rv == SOCKET_ERROR) {
        //Socket select failed with error
        debugLog << "Listen select error:"  << getSocketError() << endl;
    }
    else if (rv > 0)
    {
        //Something pending on a socket
        debugLog << "Incoming connection" << endl;

        //Check the incoming server socket (dedicated to listening for new connections)
        if (FD_ISSET(sdListen, &listenSet)) {
            connection = acceptConnection();
            
            if (connection < 0) {
                //Connection refused or failed
            }
            else {
                //Add callback for all incoming packets on connection
                addCB( &cb_incoming, (void*)this);
            }
        }
    }
    
    return rv;  //Return value of select statement...
}

//Handle a new client connection
int netserver::acceptConnection()
{
    int connection;
    struct sockaddr_in addr;
    int addr_len = sizeof(struct sockaddr_in);

    openLog();

    connection = accept(sdListen, (sockaddr*)&addr, &addr_len);        //Non blocking accept call
    if (connection == (int)INVALID_SOCKET) {
        debugLog << "Client connection failed:" << getSocketError() << endl;

        if (sdListen != (int)INVALID_SOCKET)
            closeSocket(sdListen);  //Cleanup the listen port
        
        //Don't give up, try to restart it
        if (openPort(serverPort) == (int)INVALID_SOCKET)
            debugLog << "Cannot restart socket!" << endl;
        else
            debugLog << "Listening socket restarted" << endl;
        return -1;
    }

    if (conSet.size() >= conMax) {
        debugLog << "Connection refused, maximum " << conMax << " connections" << endl;
        return -1;
    }
    debugLog << "Client connected: Socket=" << connection
            << "  Address=" << inet_ntoa( addr.sin_addr )
            << "  Port=" << ntohs(addr.sin_port) << endl;

    conSet.insert( connection );    //Add to the set of connection descriptors
    //unblockSocket( connection );

    //Do something with FD_SET ???

    return connection;
}

//Default function to handle incoming packets
size_t netserver::handleIncoming( netpacket *pkt) {

    debugLog << "PKT len=" << pkt->get_length() << " ID=" << pkt->ID << endl;
    return 0;
}

//Callback function, calls class function using cb_data
size_t netserver::cb_incoming( netpacket* pkt, void *cb_data)
{
    if (cb_data == NULL) {
        return ~0;
    }
    
    return ((netserver*)cb_data)->handleIncoming( pkt );
    
}
