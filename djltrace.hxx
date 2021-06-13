// In one source file, declare the CDJLTrace named tracer like this:
//    CDJLTrace tracer;
// When the app starts, enable tracing like this:
//    tracer.Enable( true );
// By default the tracing file is placed in %temp%\tracer.txt
// Arguments to Trace() are just like printf. e.g.:
//    tracer.Trace( "what to log with an integer argument %d and a wide string %ws\n", 10, pwcHello );
//

#pragma once

#include <stdio.h>
#include <mutex>

using namespace std;

class CDJLTrace
{
    private:
        FILE * fp;
        std::mutex mtx;

    public:
        CDJLTrace()
        {
            fp = NULL;
        }

        bool Enable( bool enable, const WCHAR * pwcLogFile = NULL )
        {
            Shutdown();

            if ( enable )
            {
                if ( NULL == pwcLogFile )
                {
                    const WCHAR * pwcFile = L"tracer.txt";
                    size_t len = wcslen( pwcFile );

                    unique_ptr<WCHAR> tempPath( new WCHAR[ MAX_PATH + 1 ] );
                    size_t available = MAX_PATH - len;

                    DWORD result = GetTempPath( available , tempPath.get() );
                    if ( result > available || 0 == result )
                        return false;

                    wcscat( tempPath.get(), pwcFile );
                    fp = _wfsopen( tempPath.get(), L"a+t", _SH_DENYWR );
                }
                else
                {
                    fp = _wfsopen( pwcLogFile, L"a+t", _SH_DENYWR );
                }
            }

            return ( NULL != fp );
        } //Enable

        void Shutdown()
        {
            if ( NULL != fp )
            {
                fclose( fp );
                fp = NULL;
            }
        } //Shutdown

        ~CDJLTrace()
        {
            Shutdown();
        }

        void Trace( const char * format, ... )
        {
            if ( NULL != fp )
            {
                lock_guard<mutex> lock( mtx );

                va_list args;
                va_start( args, format );
                fprintf( fp, "PID %6d TID %6d -- ", GetCurrentProcessId(), GetCurrentThreadId() );
                vfprintf( fp, format, args );
                va_end( args );
                fflush( fp );
            }
        } //Trace

        // Don't prepend the PID and TID to the trace

        void TraceQuiet( const char * format, ... )
        {
            if ( NULL != fp )
            {
                lock_guard<mutex> lock( mtx );

                va_list args;
                va_start( args, format );
                vfprintf( fp, format, args );
                va_end( args );
                fflush( fp );
            }
        } //TraceQuiet
}; //CDJLTrace

extern CDJLTrace tracer;
