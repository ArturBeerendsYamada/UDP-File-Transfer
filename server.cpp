#include <iostream>
#include <fstream>
#include <string>
#include <openssl/md5.h>
#include "Socket.h"
#include "Address.h"
#include "SHA256.h" //from https://github.com/System-Glitch/SHA256

using namespace std;

#define MTU 2048

int port = 30001;

Socket sock;


bool InitializeSockets()
{
    #if PLATFORM == PLATFORM_WINDOWS
    WSADATA WsaData;
    return WSAStartup( MAKEWORD(2,2),
                       &WsaData )
        == NO_ERROR;
    #else
    return true;
    #endif
}

void ShutdownSockets()
{
    #if PLATFORM == PLATFORM_WINDOWS
    WSACleanup();
    #endif
}


void CreateSocket()
{
    if ( !sock.Open( port ) )
    {
        printf( "failed to open socket!\n" );
    }
}

// returns size of file in bytes, if doesnt exist, returns 0
unsigned int fileExists(string filename)
{
    ifstream in(filename, ifstream::ate | ifstream::binary);
    unsigned int size;
    if (in.good())
        size = in.tellg();
    else
        size = 0;

    return size;
}

// sends back to the sender the size in bytes of the file found
void sendExistsResponse(unsigned int file_bytes, Address sender)
{
    sock.Send(sender, to_string(file_bytes).c_str(), to_string(file_bytes).length());
    cout << "size sent: " << file_bytes << '\n';
}

// sends back to the sender that the thing requested is unavailable
void sendNotFoundResponse(Address sender, string thing)
{
    string message = "error " + thing + " unavailable";
    sock.Send(sender, message.c_str(), message.length());
    cout << "error sent: " << message << '\n';
}

// sends back to the sender the data and information about the chunk of file requested
// information includes filename requested, bytes interval requested, and checksum
void sendDataResponse(Address sender, string filename, int chunk_start, int chunk_end)
{
    string message =    "file:" + filename + 
                        "start:" + to_string(chunk_start) +
                        "end:" + to_string(chunk_end) +
                        "sha256:";
    ifstream input(filename, ios::binary);
    char* file_data = new char[chunk_end-chunk_start];
    input.seekg(chunk_start);
    input.read(file_data, chunk_end-chunk_start);
    SHA256 sha256;
    sha256.update(reinterpret_cast<unsigned char*>(file_data), (chunk_end-chunk_start));
    string error_detection = sha256.toString(sha256.digest());
    string str(file_data, chunk_end-chunk_start);
    message += (error_detection + "data:" + str);
    sock.Send(sender, message.c_str(), message.length());
    //cout << "file data sent\n";
}

// server listening loop
void serverListening()
{
    while ( true )
    {
        char buffer[MTU];
        Address sender;
        int bytes_read =
            sock.Receive( sender,
                            buffer,
                            sizeof( buffer ) );
        if ( bytes_read <= 0)
            continue;

        // process packet
        string packet_data_string = buffer;
        // need to limit the string to the characters got in the current message, or else trash from previous (longer) messages left over on the buffer can get on it
        packet_data_string = packet_data_string.substr(0, bytes_read);
        //cout << "\nreceived: " << packet_data_string << '\n';

        if (packet_data_string.find("query file") != string::npos)
        {
            // extract filename from packet
            string filename = packet_data_string.substr(packet_data_string.find(':')+1);
            cout << sender.GetAddressString() << ':' << sender.GetPort() << " is querying for file " << filename << "\n";

            unsigned int file_bytes = fileExists(filename);
            if (file_bytes)
                sendExistsResponse(file_bytes, sender);
            else
                sendNotFoundResponse(sender, "file");

        }
        else if (packet_data_string.find("request file chunk") != string::npos)
        {
            int chunk_start = 0, chunk_end = 0;

            // extract chunk_end from the end of the string
            chunk_end = atoi(packet_data_string.substr(packet_data_string.rfind(':')+1).c_str());
            // update string to not have chunk_end
            packet_data_string = packet_data_string.substr(0, packet_data_string.rfind("end:"));

            // extract chunk_start from the end of the "NEW" string
            chunk_start = atoi(packet_data_string.substr(packet_data_string.rfind(':')+1).c_str());
            // update string to not have chunk_start
            packet_data_string = packet_data_string.substr(0, packet_data_string.rfind("start:"));

            // extract filename from string 
            string filename = packet_data_string.substr(packet_data_string.find(':')+1);
            //cout << sender.GetAddressString() << ':' << sender.GetPort() << " is requesting for bytes [" << chunk_start << ", " << chunk_end << "[ from file " << filename << "\n";

            unsigned int file_bytes = fileExists(filename);
            if (file_bytes >= chunk_end)
                sendDataResponse(sender, filename, chunk_start, chunk_end);
            else if (file_bytes != 0)
                sendNotFoundResponse(sender, "chunk");
            else
                sendNotFoundResponse(sender, "file");
        }
        else;
    }
}

bool ParseCmdLine(int argc, char** argv)
{
    for (int i = 0; i < argc; i++)
    {
        string argStr = string(argv[i]);
        if (argStr == "--port" || argStr == "-p")
        {
            string portStr = string(argv[i + 1]);
            port = stoi(portStr);
        }
    }
    return true;
}

int main(int argc, char** argv)
{
    if (!ParseCmdLine(argc, argv))
    {
        return 1;
    }

    InitializeSockets();
    CreateSocket();

    serverListening();

    ShutdownSockets();
    return 0;
}