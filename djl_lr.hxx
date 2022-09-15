#pragma once

//
// This class exists to isolate all libraw dependencies into a single file
//
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

        static bool ExportAsTiff( WCHAR const * pwcPath, WCHAR * pwcExportPath, bool createXMP = true )
        {
            putenv( (char *) "TZ=UTC" ); // dcraw compatibility, affects TIFF datestamp field
        
            unique_ptr<LibRaw> RawProcessor( new LibRaw );
            RawProcessor->imgdata.params.output_tiff = 1;
            RawProcessor->imgdata.params.output_bps = 16;
          
            size_t len = 1 + wcslen( pwcPath );
            unique_ptr<char> inputFile( new char[ len ] );
            size_t converted = 0;
            wcstombs_s( &converted, inputFile.get(), len, pwcPath, len );
  
            int ret = RawProcessor->open_file( inputFile.get() );
            if ( LIBRAW_SUCCESS != ret )
            {
                tracer.Trace( "error %#x == %d; can't open input file %s: %s\n", ret, ret, inputFile.get(), libraw_strerror( ret ) );
                return false;
            }
          
            ret = RawProcessor->unpack();
            if ( LIBRAW_SUCCESS != ret )
            {
                tracer.Trace( "error %#x == %d; can't unpack %s: %s\n", ret, ret, inputFile.get(), libraw_strerror( ret ) );
                return false;
            }
          
            ret = RawProcessor->dcraw_process();
            if ( LIBRAW_SUCCESS != ret )
            {
                tracer.Trace( "error %#x == %d, can't do pocessing on %s: %s\n", ret, ret, inputFile.get(), libraw_strerror( ret ) );
                return false;
            }

            const char * pcTIFFExt = "-lr.tiff";
            unique_ptr<char> outputFile( new char[ len + strlen( pcTIFFExt ) ] );
            strcpy( outputFile.get(), inputFile.get() );
            char * dot = strrchr( outputFile.get(), '.' );
            if ( 0 == dot )
            {
                tracer.Trace( "input filename doesn't have a file extension, which is required\n" );
                return false;
            }
        
            strcpy( dot, pcTIFFExt );
          
            ret = RawProcessor->dcraw_ppm_tiff_writer( outputFile.get() );
            if ( LIBRAW_SUCCESS != ret )
            {
                tracer.Trace( "error %#x == %d; can't write %s: %s\n", ret, ret, outputFile.get(), libraw_strerror( ret ) );
                return false;
            }
        
            RawProcessor->recycle_datastream();
            RawProcessor->recycle();
            RawProcessor.reset( 0 );

            converted = 0;
            len = 1 + strlen( outputFile.get() );
            mbstowcs_s( &converted, pwcExportPath, len, outputFile.get(), len );

            if ( createXMP )
            {
                // Create a .xmp file with a rating of 1 so it's easier to find the image in Lightroom

                const char * pcXMPExt = ".xmp";
                unique_ptr<char> xmpFile( new char[ 1 + strlen( outputFile.get() ) + strlen( pcXMPExt ) ] );
                strcpy( xmpFile.get(), outputFile.get() );
                dot = strrchr( xmpFile.get(), '.' );
                strcpy( dot, pcXMPExt );
            
                FILE * fp = fopen( xmpFile.get(), "w" );
                if ( fp )
                {
                    const char * pcRating1XMP =
                       R"(<x:xmpmeta xmlns:x="adobe:ns:meta/" x:xmptk="Adobe XMP Core 7.0-c000 1.000000, 0000/00/00-00:00:00        ">)" "\n"
                       R"( <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">)"                                           "\n"
                       R"(  <rdf:Description rdf:about="")"                                                                              "\n"
                       R"(    xmlns:xmp="http://ns.adobe.com/xap/1.0/")"                                                                 "\n"
                       R"(   xmp:Rating="1")"                                                                                            "\n"
                       R"(  </rdf:Description>)"                                                                                         "\n"
                       R"( </rdf:RDF>)"                                                                                                  "\n"
                       R"(</x:xmpmeta>)"                                                                                                 "\n"
                       ;

                    fprintf( fp, "%s", pcRating1XMP );
                    fclose( fp );
                }
                else
                {
                    // don't fail this call just because the .xmp file can't be created

                    tracer.Trace( "unable to create xmp file, error %d\n", errno );
                }
            }

            return true;
        } //ExportAsTiff
};
