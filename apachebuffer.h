/**
 * header file for apachebuffer
 *
 * see comments in apachebuffer.cpp
 */

#include <stdio.h>
#include <iostream>
#include <fstream>

extern "C"
{
#include <httpd.h>
#include <http_protocol.h>
#include <apr_strings.h>
#include <apr_pools.h>
#include <jpeglib.h>
}

class apachebuffer : public std::streambuf 
{
public:
   apachebuffer(request_rec *r);
   ~apachebuffer();

protected:
   virtual int overflow(int c = EOF);
   virtual int sync();

private:
   request_rec *r;
   std::string buffer;
   void dump_buffer();
};
