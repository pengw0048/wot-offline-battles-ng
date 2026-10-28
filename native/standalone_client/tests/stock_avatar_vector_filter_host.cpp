// Runs the production AvatarFilter Vector3 channels over scripted connection
// times. Floats and doubles travel as IEEE bit patterns in hexadecimal.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
namespace BW { template <class T> using vector = std::vector<T>; }

namespace oracle
{
inline float sinf_( float v ) { return float( std::sin( double( v ) ) ); }
inline float cosf_( float v ) { return float( std::cos( double( v ) ) ); }
}
#define sinf oracle::sinf_
#define cosf oracle::cosf_

struct Vector3
{
	float x, y, z;
	Vector3() : x( 0.f ), y( 0.f ), z( 0.f ) {}
	Vector3( float a, float b, float c ) : x( a ), y( b ), z( c ) {}
};

namespace host
{
bool connected = false;
double lastMessage = 0.0, server = 0.0;
}

// @PRODUCTION@

bool stockAvatarVectorLastMessageTime( double & time )
{
	if (!host::connected) return false;
	time = host::lastMessage;
	return true;
}

bool stockAvatarVectorServerTime( double & time )
{
	if (!host::connected) return false;
	time = host::server;
	return true;
}

namespace
{
std::string token() { std::string t; std::cin >> t; return t; }
float readFloat() { uint32_t b = uint32_t( std::stoul( token(), nullptr, 16 ) ); float v; std::memcpy( &v, &b, 4 ); return v; }
double readDouble() { uint64_t b = std::stoull( token(), nullptr, 16 ); double v; std::memcpy( &v, &b, 8 ); return v; }
int readInt() { return std::stoi( token() ); }
void put( float v ) { uint32_t b; std::memcpy( &b, &v, 4 ); std::printf( " %08x", b ); }
void put( double v ) { uint64_t b; std::memcpy( &b, &v, 8 ); std::printf( " %016llx", (unsigned long long)b ); }
}

int main()
{
	StockAvatarVectorChannels channels;
	std::string command;
	while (std::cin >> command)
	{
		std::printf( "%s", command.c_str() );
		if (command == "connect")
		{
			host::connected = readInt() != 0;
			host::lastMessage = readDouble();
			host::server = readDouble();
		}
		else if (command == "sync")
		{
			const int id = readInt();
			const float x = readFloat(), y = readFloat(), z = readFloat();
			channels.sync( id, Vector3( x, y, z ), readDouble() );
		}
		else if (command == "get")
		{
			const int id = readInt();
			const Vector3 v = channels.get( id, readDouble() );
			put( v.x ); put( v.y ); put( v.z );
		}
		else if (command == "reset") channels.reset( readInt() );
		else if (command == "interpolation")
		{
			const int id = readInt();
			channels.setInterpolationType( id, readInt() );
		}
		const StockAvatarVectorLatency & l = stockAvatarVectorLatency();
		put( l.latency ); put( l.maximum ); put( l.lastUpdate );
		for (int i = 0; i < 10; ++i)
		{
			const StockAvatarVectorChannel * c = channels.channel( i );
			std::printf( " %d", c ? int( c->samples().size() ) : -1 );
		}
		std::printf( "\n" );
	}
	return 0;
}
