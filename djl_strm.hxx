//
// Stream over a file or subset of a file
//

#pragma once

class CStream
{
    private:
        bool handleOwned;
        HANDLE hFile;
        __int64 length;
        __int64 offset;
        __int64 embedOffset;
        bool seekCalled;

    public:
        CStream( WCHAR const * pwcFile )
        {
            embedOffset = 0;
            length = 0;
            offset = 0;
            seekCalled = false;
            handleOwned = true;
            hFile = CreateFile( pwcFile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, 0 );

            if ( INVALID_HANDLE_VALUE != hFile )
            {
                LARGE_INTEGER liSize;
                GetFileSizeEx( hFile, &liSize );
                length = liSize.QuadPart;
            }
        } //CStream

        CStream( HANDLE h )
        {
            embedOffset = 0;
            length = 0;
            offset = 0;
            seekCalled = true; // don't trust where this handle has been
            handleOwned = false;
            hFile = h;

            LARGE_INTEGER liSize;
            BOOL ok = GetFileSizeEx( hFile, &liSize );
            if ( ok )
                length = liSize.QuadPart;
        } //CStream

        CStream( WCHAR const * pwcFile, __int64 embeddedOffset, __int64 embeddedLength )
        {
            embedOffset = embeddedOffset;
            length = embeddedLength;
            offset = 0;
            seekCalled = true; // need to get to virtual 0 on first read
            handleOwned = true;
            hFile = CreateFile( pwcFile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, 0 );

            if ( INVALID_HANDLE_VALUE == hFile )
                length = 0;
        } //CStream

        ~CStream()
        {
            if ( handleOwned && INVALID_HANDLE_VALUE != hFile )
                CloseHandle( hFile );
        }

        ULONG Read( void *pv, ULONG cb )
        {
            if ( seekCalled )
            {
                LARGE_INTEGER li;
                li.QuadPart = offset + embedOffset;
                SetFilePointerEx( hFile, li, NULL, FILE_BEGIN );
                seekCalled = false;
            }

            if ( ( offset + cb ) > length )
                cb = (ULONG) ( length - offset );

            DWORD dwRead = 0;
            BOOL ok = ReadFile( hFile, pv, cb, &dwRead, NULL );

            if ( ok )
                offset += cb;
            else
                cb = 0;

            return cb;
        } //Read

        void Seek( __int64 location )
        {
            if ( offset != location )
            {
                offset = location;
                seekCalled = true;
            }
        }

        bool Ok() { return ( INVALID_HANDLE_VALUE != hFile ); }
        __int64 Tell() { return offset; }
        __int64 Length() { return length; }
        bool AtEOF() { return ( offset >= length ); }
};

