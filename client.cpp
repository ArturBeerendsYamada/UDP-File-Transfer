#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <chrono>
#include "Socket.h"
#include "Address.h"
#include "SHA256.h"

using namespace std;

//parameters to determine what operations to execute
#define SERVER_PROVIDED    0
#define FILE_PROVIDED      1
#define ONLY_CHECK_FILE    2
#define LOSS_PROVIDED      3
#define CORR_PROVIDED      4
#define HELP               5
#define PARAM_COUNT        6

//error responses
#define TIMEOUT             -1
#define PACKET_ERROR        -2
#define FILE_UNAVAILABLE    -3

//other labels
#define DATA_MTU    1024
#define INFO_MTU    1024
#define RETRIES     10
#define TIMEOUT_US  1000000

Address server_address = Address(127,0,0,1,30001);
int server_port = 30001;
int own_port = 30000;
string filename = "";
int filesize = 0;
int chunk_start = 0;
int chunk_end = 0;
int loss_start = 0;
int loss_end = 0;
int corr_start = 0;
int corr_end = 0;
int file_percentage = 0;
bool parameters[6] = {false, false, false, false, false, false};
bool not_timeout = true;
char* file_data;
chrono::steady_clock::time_point packet_sent_clock;


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

bool checkNotTimeout()
{
    chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    int time_elapsed_us = chrono::duration_cast<std::chrono::microseconds>(now - packet_sent_clock).count();
    if (time_elapsed_us > TIMEOUT_US)
        return false;
    return true;
}

int receiveFileQueryResponse()
{
    while(not_timeout)
    {
        not_timeout = checkNotTimeout();
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

int receiveFileChunkResponse()
{
    while(not_timeout)
    {
        not_timeout = checkNotTimeout();
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

        //check that packet is from requested bytes
        int packet_chunk_start = atoi(packet_data_string.substr(
                                packet_data_string.find("start:")+6, 
                                packet_data_string.find("end:")-1).c_str());
        int packet_chunk_end = atoi(packet_data_string.substr(
                                packet_data_string.find("end:")+4, 
                                packet_data_string.find("sha256:")-1).c_str());
        if( packet_chunk_start != chunk_start || //chunck start mismatch
            packet_chunk_end != chunk_end) //chunck end mismatch 
        {
            return PACKET_ERROR; //something went wrong with the packet
        }

        //applies errors if requested by user
        if(parameters[LOSS_PROVIDED] && packet_chunk_start <= loss_end && loss_start < packet_chunk_end) //chunk contains bytes to lose
        {
            loss_start = packet_chunk_end; //loss range no longer includes bytes "just lost"
            return TIMEOUT; //lose packet
        }

        // extract file data from packet (position for byte to byte loading)
        int data_start_on_packet = packet_data_string.find("data:")+5;
        for (int i=0;i<chunk_end-chunk_start;i++)
        {
            file_data[i]=buffer[data_start_on_packet+i];
        }

        // applies errors if requested by user
        if(parameters[CORR_PROVIDED] && packet_chunk_start <= corr_end && corr_start < packet_chunk_end) //chunk contains bytes to corrupt
        {
            for (int i=chunk_start;i<chunk_end;i++)
            {
                if (i >= corr_start && i < corr_end)
                {
                    file_data[i-chunk_start] = rand()%255;
                }
            }
            corr_start = packet_chunk_end; //corruption range no longer includes bytes "just corrupted"
        }

        // check for errors with checksum
        SHA256 sha256;
        sha256.update(reinterpret_cast<unsigned char*>(file_data), chunk_end-chunk_start);
        string sha_string = sha256.toString(sha256.digest());
        // if the packet contains the hash, no error in the data
        if (packet_data_string.find(sha_string) != string::npos)
            return bytes_read;
        else
            return PACKET_ERROR;
    }
    return TIMEOUT;
}

// verify if file is available in server and returns its size. If not available, returns negative number (error)
int checkFile()
{
    string check_file_request = "query file:" + filename;
    int retries_left = RETRIES, response = 0;
    while (response <= 0 && retries_left > 0)
    {
        sock.Send(server_address, check_file_request.c_str(), check_file_request.length());
        packet_sent_clock = chrono::steady_clock::now();
        not_timeout = true;
        response = receiveFileQueryResponse();
        retries_left--;
        if (response == TIMEOUT)
            cout << "timed out!\n";
        else if (response == PACKET_ERROR)
            cout << "Packet received with error!\n";
    }
    return response;
}

int requestFileChunk()
{
    string file_chunk_request = "request file chunk:" + filename +
                                "start:" + to_string(chunk_start) +
                                "end:" + to_string(chunk_end);
    
    sock.Send(server_address, file_chunk_request.c_str(), file_chunk_request.length());
    packet_sent_clock = chrono::steady_clock::now();
    not_timeout = true;
    int response = receiveFileChunkResponse();
    if (response == TIMEOUT)
        cout << "Packet requesting for bytes [" << chunk_start << ", " << chunk_end << "[ timed out!\n";
    else if (response == PACKET_ERROR)
        cout << "Packet requesting for bytes [" << chunk_start << ", " << chunk_end << "[ received with error!\n";
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
        int retries_left = 10, response = 0;
        chunk_end = chunk_start + DATA_MTU;
        if (chunk_end > filesize)
            chunk_end = filesize;

        while (response <= 0 && retries_left > 0)
        {
            response = requestFileChunk();
            retries_left--;
        };
        if (response <= 0)
        {
            cout << "too many failures, aborting transfer\n";
            output.close();
            remove(filename.c_str());
            return; 
        }
        output.write(file_data, chunk_end-chunk_start);
        chunk_start = chunk_end;
        if (file_percentage != (int)(chunk_end*100./filesize))
        {
            file_percentage = (int)(chunk_end*100./filesize);
            cout << filename << " - " << file_percentage << "% complete\n";
        }
    }
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
        else if (argStr == "--generatecorr" || argStr == "-gc")
        {
            parameters[CORR_PROVIDED] = true;
            corr_start = atoi(argv[i + 1]);
            corr_end = atoi(argv[i + 2]);
        }
        else if (argStr == "--help" || argStr == "-h")
        {
            parameters[HELP] = true;
            cout << "\nClient side of simple file transfer using UDP\n\n"
            << "options:\n"
            << "--serveraddr | -sa <a b c d p>  configure server address and port a.b.c.d:p (default is 127.0.0.1:30001)\n"
            << "--clientport | -cp <a>          configure client (self) port to p (default is 30000)\n"
            << "--file | -f <filename>          filename to be requested (with extension)\n"
            << "--checkfile | -cf               if present, will only request file availability and size (will not transfer it)\n"
            << "--generateloss | -gl <s e>      will act as if losses ocurred when transmitting bytes (s, e( from file\n"
            << "--generatecorr | -gc <s e>      will act as if data corruption ocurred when transmitting bytes (s, e( from file\n"
            << "\nexamples:\nto ask for file a.txt in server 10.10.0.10:20000 simulating loss of packets containing bytes 10 through 15 use \".\\client.exe -f a.txt -sa 10 10 0 10 20000 -gl 10 16\"\n\n";
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
    cout << "\n";

    bool flag = false;
    for (int i = 0; i < PARAM_COUNT; i++)
        if (parameters[i])
            flag = true;
    if(!flag)
        cout << "use -h for help\n";
    else if (!parameters[HELP])
    {
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