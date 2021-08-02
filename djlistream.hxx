//
// Implement IStream sufficiently for WIC to open images.
//

#pragma once

class CIStream : public IStream
{
    private:
        long refcount;
        const byte * pbytes;
        ULARGE_INTEGER length;
        ULARGE_INTEGER offset;

    public:
        CIStream( const byte * pb, ULARGE_INTEGER len )
        {
            // ownership of pb is transferred.

            refcount = 1;
            pbytes = pb;
            length = len;
            offset.QuadPart = 0;
        }

        CIStream( const WCHAR * pwcFile ) :
            refcount( 1 ),
            pbytes( 0 ),
            length {},
            offset {}
        {
            HANDLE hFile = CreateFile( pwcFile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, 0 );

            if ( INVALID_HANDLE_VALUE != hFile )
            {
                LARGE_INTEGER liSize;
                GetFileSizeEx( hFile, &liSize );

                if ( 0 != liSize.HighPart )
                    return;

                pbytes = new byte[ liSize.QuadPart ];
                DWORD dwRead = 0;
                BOOL ok = ReadFile( hFile, (void *) pbytes, liSize.LowPart, &dwRead, NULL );

                if ( ok )
                {
                    length.QuadPart = liSize.QuadPart;
                }
                else
                {
                    delete pbytes;
                    pbytes = 0;
                }
                
                CloseHandle( hFile );
            }
        }

        CIStream( const WCHAR * pwcFile, long long subsetOffset, long long subsetLength ) :
            refcount( 1 ),
            pbytes( 0 ),
            length {},
            offset {}
        {
            if ( 0xffffffff00000000 & subsetLength )
                return;

            HANDLE hFile = CreateFile( pwcFile, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, 0 );

            if ( INVALID_HANDLE_VALUE != hFile )
            {
                LARGE_INTEGER liMove;
                liMove.QuadPart = subsetOffset;
                BOOL ok = SetFilePointerEx( hFile, liMove, NULL, FILE_BEGIN );

                if ( ok )
                {
                    pbytes = new byte[ subsetLength ];
                    DWORD dwRead = 0;

                    ok = ReadFile( hFile, (void *) pbytes, (ULONG) subsetLength, &dwRead, NULL );

                    if ( ok )
                    {
                        length.QuadPart = subsetLength;
                    }
                    else
                    {
                        delete pbytes;
                        pbytes = 0;
                    }
                }
                
                CloseHandle( hFile );
            }
        }

        ~CIStream()
        {
            delete pbytes;
        }

        bool Ok() { return ( 0 != pbytes ); }

        ULONG AddRef()
        {
            return InterlockedIncrement( &refcount );
        }

        HRESULT QueryInterface( REFIID riid, void **ppvObject )
        {
            HRESULT hr = E_NOINTERFACE;

            //tracer.Trace( "CIStream::QI called\n" );
            // IID_IMILCStreamBase not implemented and it doesn't matter: {C3933843-C24B-45A2-8298-B462F59DAAF2}
            // same for this unknown interface {3A55501A-BDCC-4E63-96BC-4DDB6F44CCDD}

            if ( riid == __uuidof(IUnknown) ||
                 riid == __uuidof(IStream) ||
                 riid == __uuidof(ISequentialStream) )
            {
                hr = S_OK;
                *ppvObject = static_cast<IStream *>(this);
                //tracer.Trace( "CIStream::QI this %p, istream %p, isequentialstream %p\n", this, static_cast<IStream *>(this), static_cast<ISequentialStream *>(this) );
                AddRef();
            }

            //LPOLESTR os;
            //StringFromIID( riid, &os );
            //tracer.Trace( "qi returning %#x for riid %ws\n", hr, os );

            return hr;
        }

        ULONG Release()
        {
            long l = InterlockedDecrement( &refcount );
            if ( 0 == l )
                delete this;

            return l;
        }

        HRESULT Read( void *pv, ULONG cb, ULONG *pcbRead )
        {
            //tracer.Trace( "read called for %d bytes, current offset %lld\n", cb, offset.QuadPart );
            HRESULT hr = S_OK;

            if ( ( offset.QuadPart + cb ) > length.QuadPart )
                cb = (ULONG) ( length.QuadPart - offset.QuadPart );

            memcpy( pv, pbytes + offset.QuadPart, cb );
            offset.QuadPart += cb;

            if ( NULL != pcbRead )
                *pcbRead = cb;

            //tracer.Trace( "returned %d bytes, hr %#x\n", cb, hr );

            return hr;
        }

        HRESULT Write( const void *pv, ULONG cb, ULONG *pcbWritten ) { return S_OK; }
        HRESULT Clone( IStream **ppstm ) { tracer.Trace( "clone called\n" ); return S_OK; }
        HRESULT Commit( DWORD flags ) { return S_OK; }
        HRESULT CopyTo( IStream *pstm, ULARGE_INTEGER cb, ULARGE_INTEGER *pcbRead, ULARGE_INTEGER *pcbWritten ) { tracer.Trace( "copyto called\n" ); return S_OK; }
        HRESULT LockRegion( ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType ) { return S_OK; }
        HRESULT Revert() { return S_OK; }
        HRESULT Seek( LARGE_INTEGER  dlibMove, DWORD dwOrigin, ULARGE_INTEGER *plibNewPosition )
        {
            if ( STREAM_SEEK_SET == dwOrigin )
                offset.QuadPart = dlibMove.QuadPart;
            else if ( STREAM_SEEK_CUR == dwOrigin )
                offset.QuadPart += dlibMove.QuadPart;
            else if ( STREAM_SEEK_END == dwOrigin )
                offset.QuadPart = length.QuadPart + dlibMove.QuadPart;

            //tracer.Trace( "  Seek move %lld, origin %lld, final position %lld\n", dlibMove.QuadPart, dwOrigin, offset.QuadPart );
            //tracer.Trace( "  seek hints set %d, cur %d, end %d\n", STREAM_SEEK_SET, STREAM_SEEK_CUR, STREAM_SEEK_END );

            if ( NULL != plibNewPosition )
                *plibNewPosition = offset;

            return S_OK;
        }

        HRESULT SetSize( ULARGE_INTEGER libNewSize ) { return S_OK; }
        HRESULT Stat( STATSTG *pstatstg, DWORD grfStatFlag )
        {
            //tracer.Trace( "stat called\n" );

            memset( pstatstg, 0, sizeof STATSTG );

            pstatstg->cbSize = length;

            return S_OK;
        }
        HRESULT UnlockRegion( ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType ) { return S_OK; }
};


