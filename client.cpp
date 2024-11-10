#include <iostream>
#include <fstream>
#include <string>
#include "Socket.h"
#include "Address.h"

using namespace std;

//parameters to determine what operations to execute
#define SERVER_PROVIDED    0
#define FILE_PROVIDED      1
#define ONLY_CHECK_FILE    2
#define LOSS_PROVIDED      3
#define HELP               4

//error responses
#define TIMEOUT             -1
#define PACKET_ERROR        -2
#define FILE_UNAVAILABLE    -3

//other labels
#define DATA_MTU 1024
#define INFO_MTU 1024

Address server_address = Address(127,0,0,1,30001);
int server_port = 30001;
int own_port = 30000;
string filename = "";
int filesize = 0;
int chunk_start = 0;
int chunk_end = 0;
int loss_start = 0;
int loss_end = 0;
bool parameters[5] = {false, false, false, false, false};
bool not_timeout = true;
char* file_data;


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
    if ( !sock.Open( own_port ) )
    {
        printf( "failed to open socket!\n" );
    }
}

int receiveFileQueryResponse()
{
    while(not_timeout)
    {
        Address sender;
        char buffer[256];
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
        if(packet_data_string.find("error file unavailable") != string::npos)
        {
            cout << "file " << filename << " unavailable on server " << server_address.GetAddressString() << "\n";
            return FILE_UNAVAILABLE;
        }
        cout << filename << " exists in server " << server_address.GetAddressString() << " and has " << packet_data_string << " bytes\n";
        return atoi(packet_data_string.c_str());
    }
    return TIMEOUT;
}

boolean receiveFileChunkResponse()
{
    while(not_timeout)
    {
        Address sender;
        char buffer[INFO_MTU+DATA_MTU];
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
        //extract file data from packet
        int data_start_on_packet = packet_data_string.find("data:")+5;
        // check for errors with checksum
        boolean no_error = true;
        if (no_error)
        {
            for (int i=0;i<chunk_end-chunk_start;i++)
            {
                file_data[i]=buffer[data_start_on_packet+i];
            }
            return bytes_read;
        }
        else
            return PACKET_ERROR;
    }
    return TIMEOUT;
}

// verify if file is available in server and returns its size. If not available, returns negative number (error)
int checkFile()
{
    string check_file_request = "query file:" + filename;
    sock.Send(server_address, check_file_request.c_str(), check_file_request.length());
    int response = receiveFileQueryResponse();
    if (response == TIMEOUT)
        cout << "timed out!\n";
    else if (response == PACKET_ERROR)
        cout << "Packet received with error!\n";
    return response;
}

int requestFileChunk()
{
    string file_chunk_request = "request file chunk:" + filename +
                                "start:" + to_string(chunk_start) +
                                "end:" + to_string(chunk_end);
    //cout << "(" << (int)(chunk_end*100./filesize) << "%) requesting for bytes [" << chunk_start << ", " << chunk_end << "[ from file " << filename << "\n";
    sock.Send(server_address, file_chunk_request.c_str(), file_chunk_request.length());
    int response = receiveFileChunkResponse();
    if (response == TIMEOUT)
        cout << "timed out!\n";
    else if (response == PACKET_ERROR)
        cout << "Packet received with error!\n";
    return response;
}

void requestFile()
{
    filesize = checkFile();
    if (filesize <= 0)
    {
        cout << "aborting request\n";
        return;
    }
    file_data = new char[DATA_MTU];
    ofstream output(filename, ios::binary);
    while (chunk_start < filesize)
    {
        int retries_left = 10;
        chunk_end = chunk_start + DATA_MTU;
        if (chunk_end > filesize)
            chunk_end = filesize;

        while (requestFileChunk() <= 0 && retries_left >= 0)
        {
            retries_left--;
        };
        output.write(file_data, chunk_end-chunk_start);
        chunk_start = chunk_end;
    }
    // TODO: check error
    output.close();
    cout << "transfer completed successfully\n";
}

bool ParseCmdLine(int argc, char** argv)
{
    for (int i = 0; i < argc; i++)
    {
        string argStr = string(argv[i]);
        if (argStr == "--serveraddr" || argStr == "-sa")
        {
            parameters[SERVER_PROVIDED] = true;
            char a, b, c, d;
            unsigned short p;
            a = atoi(argv[i + 1]);
            b = atoi(argv[i + 2]);
            c = atoi(argv[i + 3]);
            d = atoi(argv[i + 4]);
            p = atoi(argv[i + 5]);
            server_address = Address(a, b, c, d, p);
        }
        else if (argStr == "--clientport" || argStr == "-cp")
        {
            own_port = atoi(argv[i + 1]);
        }
        else if (argStr == "--file" || argStr == "-f")
        {
            parameters[FILE_PROVIDED] = true;
            filename = string(argv[i + 1]);
        }
        else if (argStr == "--checkfile" || argStr == "-cf")
        {
            parameters[ONLY_CHECK_FILE] = true;
        }
        else if (argStr == "--generateloss" || argStr == "-gl")
        {
            parameters[LOSS_PROVIDED] = true;
            loss_start = atoi(argv[i + 1]);
            loss_end = atoi(argv[i + 2]);
        }
        else if (argStr == "--help" || argStr == "-h")
        {
            parameters[HELP] = true;
            cout << "\nClient side of simple file transfer using UDP\n\n"
            << "options:\n"
            << "--serveraddr | -sa <a b c d p>  configure server address and port a.b.c.d:p (default is 127.0.0.1:30001)\n"
            << "--file | -f <filename>          filename to be requested (with extension)\n"
            << "--checkfile | -cf               if present, will only request file availability and size (will not transfer it)\n"
            << "--generateloss | -gl <a b>      will act as if errors ocurred when transmitting bytes s to e from file\n"
            << "\nexamples:\nto ask for file a.txt in server 10.10.0.10:20000 use \".\\client.exe -f a.txt -sa 10 10 0 10 20000\"\n\n";
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

    if (!parameters[HELP])
    {
        cout << "\n";
        InitializeSockets();
        CreateSocket();
        if (parameters[ONLY_CHECK_FILE])
        {
            if (parameters[FILE_PROVIDED])
                checkFile();
            else
                cout << "please provide a file name with -f option\n";
        }
        else
        {
            if (parameters[FILE_PROVIDED])
                requestFile();
            else
                cout << "please provide a file name with -f option\n";
        }
    }

    ShutdownSockets();
    cout << "\n";
    return 0;
}