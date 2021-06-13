//
// This class exists to isolate all libraw dependencies into a single file
//

#pragma once

// Undefine these two to link to libraw.dll.
// Define these two to statically link to libraw_static.lib.

#define LIBRAW_WIN32_DLLDEFS
#define LIBRAW_NODLL

// Need this so it builds

#define LIBRAW_NO_WINSOCK2

// Put libraw.h in your INCLUDE path and libraw_static.lib in your LIB path

#include <libraw.h>
#pragma comment( lib, "libraw_static.lib" )

#include "djltrace.hxx"

using namespace std;

class CLibRaw
{
    public:
        CLibRaw() {}

        static byte * ProcessRaw( const WCHAR * pwcPath, int bpc, int & width, int & height, int & colors )
        {
            if ( 16 != bpc && 8 != bpc )
                return 0;

            unique_ptr<LibRaw> rawProcessor( new LibRaw() );
  
            rawProcessor->imgdata.params.output_tiff = 1;
            rawProcessor->imgdata.params.output_bps = bpc;

            size_t len = 1 + wcslen( pwcPath );
            unique_ptr<char> inputFile( new char[ len ] );
            size_t converted = 0;
            wcstombs_s( &converted, inputFile.get(), len, pwcPath, len );
  
            int ret = rawProcessor->open_file( inputFile.get() );

            if ( LIBRAW_SUCCESS != ret )
            {
                tracer.Trace( "libraw can't (error %d) open input file %s\n", ret, inputFile.get() );
                return 0;
            }
  
            ret = rawProcessor->unpack();

            if ( LIBRAW_SUCCESS != ret )
            {
                tracer.Trace( "libraw can't (error %d) unpack input file %s\n", ret, inputFile.get() );
                return 0;
            }

            ret = rawProcessor->dcraw_process();
  
            if ( LIBRAW_SUCCESS != ret )
            {
                tracer.Trace( "libraw can't (error %d) process input file %s\n", ret, inputFile.get() );
                return 0;
            }

            int bps;
            rawProcessor->get_mem_image_format( &width, &height, &colors, &bps );

            if ( bpc != bps )
            {
                tracer.Trace("libraw in-memory format not as expected: width %d, height %d, colors %d, bps %d\n", width, height, colors, bps );
                return 0;
            }

            int stride = width * colors * bps / 8;
            unique_ptr<byte> data( new byte[ height * stride ] );
            ret = rawProcessor->copy_mem_image( data.get(), stride, true );

            if ( LIBRAW_SUCCESS != ret )
            {
                tracer.Trace( "libraw can't (error %d) copy memory image for file file %s\n", ret, inputFile.get() );
                return 0;
            }

            return data.release();
        } //ProcessRaw
};
