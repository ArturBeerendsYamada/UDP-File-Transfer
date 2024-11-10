#include "Address.h"

Address::Address()
{
    init(0,0);
}

Address::Address( unsigned char a,
         unsigned char b,
         unsigned char c,
         unsigned char d,
         unsigned short port )
{
    unsigned int address = ( a << 24 ) |
                           ( b << 16 ) |
                           ( c << 8  ) |
                             d;
    init(address, port);
}

Address::Address( unsigned int address,
                unsigned short port )
{
    init(address, port);
}

void Address::init(unsigned int address, unsigned short port)
{
    this->address = address;
    this->port = port;
}

unsigned int Address::GetAddress() const
{
    return this->address;
}

unsigned short Address::GetPort() const
{
    return this->port;
}

std::string Address::GetAddressString() const
{
    std::string address_string, a, b, c, d;
    a = std::to_string((this->address>>24) & 0xFF);
    b = std::to_string((this->address>>16) & 0xFF);
    c = std::to_string((this->address>>8) & 0xFF);
    d = std::to_string((this->address>>0) & 0xFF);
    address_string = a + '.' + b + '.' + c + '.' + d;
    return address_string;
}


// unsigned char GetA() const;
// unsigned char GetB() const;
// unsigned char GetC() const;
// unsigned char GetD() const;