#pragma once

//
// Stream over a file or subset of a file
//

class CStream
{
    private:
        __int64 length;
        __int64 offset;
        __int64 embedOffset;
        HANDLE hFile;
        bool handleOwned;
        bool seekCalled;
        bool forWrite;
        // Opt in only for metadata readers. This buffer is local to this stream.
        bool bufferReads = false;
        BYTE readBuffer[4096];
        __int64 readBufferOffset = -1;
        DWORD readBufferLength = 0;

    public:
        CStream()
        {
            length = 0;
            offset = 0;
            embedOffset = 0;
            hFile = INVALID_HANDLE_VALUE;
            handleOwned = false;
            seekCalled = false;
            forWrite = false;
        } //CStream

        CStream( WCHAR const * pwcFile, bool write = false )
        {
            embedOffset = 0;
            length = 0;
            offset = 0;
            seekCalled = false;
            handleOwned = true;
            forWrite = write;

            if ( forWrite )
                hFile = CreateFile( pwcFile, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS, 0, 0 );
            else
            {
                hFile = CreateFile( pwcFile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, 0 );

                if ( INVALID_HANDLE_VALUE != hFile )
                {
                    LARGE_INTEGER liSize;
                    GetFileSizeEx( hFile, &liSize );
                    length = liSize.QuadPart;
                }
            }
        } //CStream

        CStream( HANDLE h, bool bufferedReads = false )
        {
            embedOffset = 0;
            length = 0;
            offset = 0;
            seekCalled = true; // don't trust where this handle has been
            handleOwned = false;
            hFile = h;
            bufferReads = bufferedReads;
            forWrite = false;

            LARGE_INTEGER liSize;
            BOOL ok = GetFileSizeEx( hFile, &liSize );
            if ( ok )
                length = liSize.QuadPart;
        } //CStream

        CStream( WCHAR const * pwcFile, __int64 embeddedOffset, __int64 embeddedLength, bool bufferedReads = false )
        {
            if ( embeddedOffset < 0 || embeddedLength < 0 )
            {
                embeddedOffset = 0;
                embeddedLength = 0;
            }

            embedOffset = embeddedOffset;
            bufferReads = bufferedReads;
            length = embeddedLength;
            offset = 0;
            seekCalled = true; // need to get to virtual 0 on first read
            handleOwned = true;
            forWrite = false;
            hFile = CreateFile( pwcFile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, 0 );

            if ( INVALID_HANDLE_VALUE == hFile )
                length = 0;
            else
            {
                LARGE_INTEGER liSize;
                BOOL ok = GetFileSizeEx( hFile, &liSize );
                if ( ok )
                {
                    if ( embedOffset > liSize.QuadPart )
                    {
                        embedOffset = 0;
                        length = 0;
                    }
                    else
                    {
                        length = __min( liSize.QuadPart - embeddedOffset, length );
                    }
                }
                else
                {
                    length = 0;
                    embedOffset = 0;
                }
             }
        } //CStream

        void CloseFile()
        {
            if ( handleOwned && INVALID_HANDLE_VALUE != hFile )
            {
                CloseHandle( hFile );
                hFile = INVALID_HANDLE_VALUE;
            }
        } //CloseFile

        ~CStream()
        {
            CloseFile();
        }

        ULONG Read( void *pv, ULONG cb )
        {
            if ( offset >= length || 0 == cb )
                return 0;

            cb = (ULONG) __min( (__int64) cb, length - offset );

            if ( bufferReads && cb <= sizeof readBuffer )
            {
                if ( readBufferOffset < 0 || offset < readBufferOffset ||
                     offset + cb > readBufferOffset + readBufferLength )
                {
                    // Read-ahead moves the handle past the logical stream position.
                    // Keep it dirty even on failure so direct reads/writes always seek.
                    seekCalled = true;
                    readBufferOffset = -1;
                    readBufferLength = 0;
                    LARGE_INTEGER li;
                    li.QuadPart = offset + embedOffset;
                    if ( !SetFilePointerEx( hFile, li, NULL, FILE_BEGIN ) )
                        return 0;

                    DWORD wanted = (DWORD) __min( (__int64) sizeof readBuffer, length - offset );
                    if ( !ReadFile( hFile, readBuffer, wanted, &readBufferLength, NULL ) )
                        return 0;

                    readBufferOffset = offset;
                }

                ULONG copied = (ULONG) __min( (__int64) cb, readBufferOffset + readBufferLength - offset );
                memcpy( pv, readBuffer + (size_t) ( offset - readBufferOffset ), copied );
                offset += copied;
                seekCalled = true;
                return copied;
            }

            if ( seekCalled )
            {
                LARGE_INTEGER li;
                li.QuadPart = offset + embedOffset;
                if ( !SetFilePointerEx( hFile, li, NULL, FILE_BEGIN ) )
                    return 0;
                seekCalled = false;
            }

            DWORD dwRead = 0;
            if ( !ReadFile( hFile, pv, cb, &dwRead, NULL ) )
                return 0;

            offset += dwRead;
            return dwRead;
        } //Read

        bool Seek( __int64 location )
        {
            if ( location < 0 || location > length )
                return false;

            if ( offset != location )
            {
                offset = location;
                seekCalled = true;
            }

            return true;
        } //Seek

        bool Ok() { return ( INVALID_HANDLE_VALUE != hFile ); }
        __int64 Tell() { return offset; }
        __int64 Length() { return length; }
        bool AtEOF() { return ( offset >= length ); }

        void GetBytes( __int64 seek_offset, void * pData, int byteCount )
        {
            memset( pData, 0, byteCount );

            if ( Seek( seek_offset ) )
                Read( pData, byteCount );
        } //GetBytes

        ULONG Write( void *pv, ULONG cb )
        {
            // A write must never reuse bytes cached before the modification.
            readBufferOffset = -1;
            readBufferLength = 0;
            if ( seekCalled )
            {
                LARGE_INTEGER li;
                li.QuadPart = offset + embedOffset;
                if ( !SetFilePointerEx( hFile, li, NULL, FILE_BEGIN ) )
                    return 0;
                seekCalled = false;
            }

            DWORD dwWritten = 0;
            BOOL ok = WriteFile( hFile, pv, cb, &dwWritten, NULL );

            if ( ok )
            {
                offset += dwWritten;

                if ( offset > length )
                    length = offset;
            }
            else
                cb = 0;

            return ok ? dwWritten : 0;
        } //Write
};

