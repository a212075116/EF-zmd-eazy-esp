#include "../include/utils.hxx"
#include <cstdio>

void Log( const std::string & msg ) { std::printf( "%s\n", msg.c_str( ) ); std::fflush( stdout ); }
