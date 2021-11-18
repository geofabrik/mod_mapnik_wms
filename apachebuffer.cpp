/**
 * apachebuffer
 *
 * subclass of stream buffer class that
 * sends mapnik's output to Apache.
 *
 * part of the Mapnik WMS server module for Apache
 */

#include <sys/time.h>
#include <time.h>

#include "apachebuffer.h"

apachebuffer::apachebuffer(request_rec* rr)
   : std::streambuf(), r(rr)
{
}

void apachebuffer::dump_buffer()
{
    if (buffer.empty()) return;
    int written = ap_rwrite(buffer.data(), buffer.length(), r);
    buffer.erase(0, written);
}

apachebuffer::~apachebuffer()
{
   dump_buffer();
}

int apachebuffer::overflow(int c) 
{
   switch (c) {
      case EOF: return EOF;
      default: buffer.append(1, c); return c;
   }
}

int apachebuffer::sync() 
{
   dump_buffer();
   return 0;
}

