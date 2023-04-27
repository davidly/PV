#pragma once

// sets the compression state of a TIFF file

#include <windows.h>
#include <wincodecsdk.h>
#include <stdio.h>
#include <wrl.h>

#include <djltrace.hxx>

using namespace Microsoft::WRL;

#pragma comment( lib, "ole32.lib" )
#pragma comment( lib, "oleaut32.lib" )
#pragma comment( lib, "windowscodecs.lib" )
#pragma comment( lib, "ntdll.lib" )

class CTiffCompression
{
private:
    BOOL CreateOutputPath( WCHAR const * pwcIn, WCHAR * pwcOut )
    {
        WCHAR const * dot = wcsrchr( pwcIn, L'.' );
    
        if ( !dot )
            return false;
    
        wcscpy( pwcOut, pwcIn );
        wcscpy( pwcOut + ( dot - pwcIn ), L"-temp" );
        wcscat( pwcOut, dot );
        return true;
    } //CreateOutputPath

    // OneDrive, Defender, the Indexer, etc. all hold files and prevent renames.

    BOOL MoveFileWithRetries( WCHAR const * oldname, WCHAR const * newname )
    {
        //printf( "renaming %ws to %ws\n", oldname, newname );
        BOOL ok;
    
        for ( int attempt = 0; attempt < 20; attempt++ )
        {
            ok = MoveFile( oldname, newname );
            if ( ok )
                break;
    
            Sleep( 500 );
            //printf( "r" );
        }
    
        return ok;
    } //MoveFileWithRetries

    BOOL DeleteFileWithRetries( WCHAR const * path )
    {
        //printf( "deleting %ws\n", path );
        BOOL ok;

        for ( int attempt = 0; attempt < 20; attempt++ )
        {
            ok = DeleteFile( path );
            if ( ok )
                break;

            DWORD err = GetLastError();

            if ( ERROR_FILE_NOT_FOUND )
            {
                ok = true;
                break;
            }

            if ( ERROR_SHARING_VIOLATION != err )
                break;
    
            Sleep( 500 );
            //printf( "d" );
        }
    
        return ok;
    } //DeleteFileWithRetries

    HRESULT SetTiffCompression( ComPtr<IWICImagingFactory> & wicFactory, WCHAR const * pwcPath, WCHAR const * pwcOutputPath, DWORD compressionMethod )
    {
        ComPtr<IWICBitmapDecoder> decoder;
        HRESULT hr = wicFactory->CreateDecoderFromFilename( pwcPath, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf() );
        if ( FAILED( hr ) )
        {
            tracer.Trace( "hr from create decoder: %#x\n", hr );
            return hr;
        }
    
        GUID containerFormat;
        hr = decoder->GetContainerFormat( &containerFormat );
        if ( FAILED( hr ) )
        {
            tracer.Trace( "hr from GetContainerFormat: %#x\n", hr );
            return hr;
        }
    
        if ( GUID_ContainerFormatTiff != containerFormat )
        {
            tracer.Trace( "container format of the input file isn't TIFF, so not compressing\n" );
            return E_FAIL;
        }
    
        ComPtr<IWICStream> fileStream = NULL;
        hr = wicFactory->CreateStream( fileStream.GetAddressOf() );
        if ( FAILED( hr ) )
        {
            tracer.Trace( "create stream hr: %#x\n", hr );
            return hr;
        }
    
        hr = fileStream->InitializeFromFilename( pwcOutputPath, GENERIC_WRITE );
        if ( FAILED( hr ) )
        {
            tracer.Trace( "hr from InitializeFromFilename: %#x\n", hr );
            return hr;
        }
    
        ComPtr<IWICBitmapEncoder> encoder;
        hr = wicFactory->CreateEncoder( containerFormat, NULL, encoder.GetAddressOf() );
        if ( FAILED( hr ) )
        {
            tracer.Trace( "hr from create encoder: %#x\n", hr );
            return hr;
        }
        
        hr = encoder->Initialize( fileStream.Get(), WICBitmapEncoderNoCache );
        if ( FAILED( hr ) )
        {
            tracer.Trace( "initialize encoder: %#x\n", hr );
            return hr;
        }
    
        UINT count = 0;
        hr = decoder->GetFrameCount( &count );
        if ( FAILED( hr ) )
        {
            tracer.Trace( "hr from GetFrameCount: %#x\n", hr );
            return hr;
        }
    
        for ( UINT i = 0; i < count; i++ )
        {
            ComPtr<IWICBitmapFrameDecode> frameDecode;
            hr = decoder->GetFrame( i, frameDecode.GetAddressOf() );
            if ( FAILED( hr ) )
            {
                tracer.Trace( "hr from GetFrame %d: %#x\n", i, hr );
                return hr;
            }
    
            ComPtr<IWICBitmapFrameEncode> frameEncode;
            ComPtr<IPropertyBag2> propertyBag;
            hr = encoder->CreateNewFrame( frameEncode.GetAddressOf(), propertyBag.GetAddressOf() );
            if ( FAILED( hr ) )
            {
                tracer.Trace( "hr from CreateNewFrame: %#x\n", hr );
                return hr;
            }
    
            PROPBAG2 option = { 0 };
            option.pstrName = L"TiffCompressionMethod";
            VARIANT varValue;
            VariantInit( &varValue );
            varValue.vt = VT_UI1;
    
            if ( 8 == compressionMethod )
                varValue.bVal = WICTiffCompressionZIP;
            else if ( 5 == compressionMethod )
                varValue.bVal = WICTiffCompressionLZW;
            else if ( 1 == compressionMethod )
                varValue.bVal = WICTiffCompressionNone;
            else
            {
                tracer.Trace( "invalid compression method %d\n", compressionMethod );
                return E_FAIL;
            }
    
            hr = propertyBag->Write( 1, &option, &varValue );
            if ( FAILED( hr ) )
            {
                tracer.Trace( "propertybag->write failed with error %#x\n", hr );
                return hr;
            }
    
            hr = frameEncode->Initialize( propertyBag.Get() );
            if ( FAILED( hr ) )
            {
                tracer.Trace( "hr from frameEncode->Initialize: %#x\n", hr );
                return hr;
            }
    
            ComPtr<IWICMetadataBlockReader> blockReader;
            hr = frameDecode->QueryInterface( IID_IWICMetadataBlockReader, (void **) blockReader.GetAddressOf() );
            if ( FAILED( hr ) )
            {
                tracer.Trace( "qi of blockReader: %#x\n", hr );
                return hr;
            }
    
            ComPtr<IWICMetadataBlockWriter> blockWriter;
            hr = frameEncode->QueryInterface( IID_IWICMetadataBlockWriter, (void **) blockWriter.GetAddressOf() );
            if ( FAILED( hr ) )
            {
                tracer.Trace( "qi of blockWriter: %#x\n", hr );
                return hr;
            }
    
            hr = blockWriter->InitializeFromBlockReader( blockReader.Get() );
            if ( FAILED( hr ) )
            {
                tracer.Trace( "hr from InitializeFromBlockReader: %#x\n", hr );
                return hr;
            }
    
            hr = frameEncode->WriteSource( static_cast<IWICBitmapSource*> ( frameDecode.Get() ), NULL );
            if ( FAILED( hr ) )
            {
                tracer.Trace( "frameencode->writesource: %#x\n", hr );
                return hr;
            }
       
            hr = frameEncode->Commit();
            if ( FAILED( hr ) )
            {
                tracer.Trace( "hr from frameencode->commit: %#x\n", hr );
                return hr;
            }
        }
        
        hr = encoder->Commit();
        if ( FAILED( hr ) )
        {
            tracer.Trace( "hr from encoder->commit() %#x\n", hr );
            return hr;
        }
       
        hr = fileStream->Commit( STGC_DEFAULT );
        if ( FAILED( hr ) )
        {
            tracer.Trace( "hr from filestream->commit() %#x\n", hr );
            return hr;
        }
    
        return hr;
    } //SetTiffCompression
    
public:

    HRESULT GetTiffCompression( ComPtr<IWICImagingFactory> & wicFactory, WCHAR const * pwcPath, DWORD * pCompression )
    {
        *pCompression = 0;
        ComPtr<IWICBitmapDecoder> decoder;
        HRESULT hr = wicFactory->CreateDecoderFromFilename( pwcPath, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf() );
        if ( FAILED( hr ) )
        {
            tracer.Trace( "hr from create decoder: %#x\n", hr );
            return hr;
        }
    
        GUID containerFormat;
        hr = decoder->GetContainerFormat( &containerFormat );
        if ( FAILED( hr ) )
        {
            tracer.Trace( "hr from GetContainerFormat: %#x\n", hr );
            return hr;
        }
    
        if ( GUID_ContainerFormatTiff != containerFormat )
        {
            tracer.Trace( "container format of the input file isn't TIFF, so exiting early\n" );
            return E_FAIL;
        }
    
        UINT count = 0;
        hr = decoder->GetFrameCount( &count );
        if ( FAILED( hr ) )
        {
            tracer.Trace( "hr from GetFrameCount: %#x\n", hr );
            return hr;
        }
    
        for ( UINT i = 0; i < count; i++ )
        {
            ComPtr<IWICBitmapFrameDecode> frameDecode;
            hr = decoder->GetFrame( i, frameDecode.GetAddressOf() );
            if ( FAILED( hr ) )
            {
                tracer.Trace( "hr from GetFrame %d: %#x\n", i, hr );
                return hr;
            }
    
            ComPtr<IWICMetadataQueryReader> queryReader;
            hr = frameDecode->GetMetadataQueryReader( queryReader.GetAddressOf() );
            if ( FAILED( hr ) )
            {
                tracer.Trace( "hr from GetMetadataQueryReader: %#x\n", hr );
                return hr;
            }
    
            WCHAR const * compressionName = L"/ifd/{ushort=259}";
            PROPVARIANT value;
            PropVariantInit( &value );
            hr = queryReader->GetMetadataByName( compressionName, &value );
            if ( FAILED( hr ) )
            {
                tracer.Trace( "hr from getmetadatabyname for compression: %#x\n", hr );
                return hr;
            }
    
            // just use the compression value from the first frame
    
            *pCompression = value.uiVal;
            return S_OK;
        }
        
        return E_FAIL; // none of the frames had compression set
    } //GetTiffCompression

    HRESULT CompressTiff( ComPtr<IWICImagingFactory> & wicFactory, WCHAR const * pwcPath, DWORD compressionMethod )
    {
        // these constants are part of the TIFF specification -- 8 is zip, 5 is lzw, and 1 is no compression

        if ( 1 != compressionMethod && 5 != compressionMethod && 8 != compressionMethod )
        {
            tracer.Trace( "compression method is invalid -- it must be 1, 5, or 8. value is %d\n", compressionMethod );
            return E_INVALIDARG;
        }

        WCHAR awcInputPath[ MAX_PATH ];
        WCHAR * pwcResult = _wfullpath( awcInputPath, pwcPath, _countof( awcInputPath ) );
        if ( NULL == pwcResult )
        {
            tracer.Trace( "can't call _wfullpath on %ws\n", pwcPath );
            return E_INVALIDARG;
        }
    
        WCHAR awcOutputPath[ MAX_PATH ];
        BOOL ok = CreateOutputPath( awcInputPath, awcOutputPath );
        if ( !ok )
        {
            tracer.Trace( "can't create output tiff path\n" );
            return HRESULT_FROM_WIN32( GetLastError() );
        }
    
        ok = DeleteFileWithRetries( awcOutputPath );
        if ( !ok )
        {
            tracer.Trace( "can't delete existing temporary file\n" );
            return HRESULT_FROM_WIN32( GetLastError() );
        }

        DWORD currentCompression = 0;
        HRESULT hr = GetTiffCompression( wicFactory, awcInputPath, &currentCompression );
        if ( FAILED( hr ) )
        {
            tracer.Trace( "failed to read current compression value: %#x\n", hr );
            return hr;
        }
    
        tracer.Trace( "the current compression is %d for file %ws\n", currentCompression, awcInputPath );
    
        if ( compressionMethod == currentCompression )
            return S_OK;
    
        hr = SetTiffCompression( wicFactory, awcInputPath, awcOutputPath, compressionMethod );
        if ( SUCCEEDED( hr ) )
        {
            // 1) rename the original file to a safety name
            // Loop because sometimes Onedrive opens the file for a long time and doesn't let go.
    
            WCHAR awcSafety[ MAX_PATH ];
            wcscpy( awcSafety, awcInputPath );
            wcscat( awcSafety, L"-saved" );

            // if it exists from a prior failed run, delete the safety file

            ok = DeleteFileWithRetries( awcSafety );
            if ( !ok )
            {
                tracer.Trace( "can't delete the residual safety file, error %d\n", GetLastError() );
                return HRESULT_FROM_WIN32( GetLastError() );
            }

            ok = MoveFileWithRetries( awcInputPath, awcSafety );
            if ( !ok )
            {
                tracer.Trace( "can't rename the original file to safety name, error %d\n", GetLastError() );
                return HRESULT_FROM_WIN32( GetLastError() );
            }
    
            // rename the compressed file as the origianal file
    
            ok = MoveFileWithRetries( awcOutputPath, awcInputPath );
            if ( !ok )
            {
                tracer.Trace( "can't rename new file to the original file name, error %d\n", GetLastError() );
                return HRESULT_FROM_WIN32( GetLastError() );
            }
    
            // delete the (renamed) original file
    
            ok = DeleteFileWithRetries( awcSafety );
            if ( !ok )
            {
                tracer.Trace( "can't delete the saved original file, error %d\n", GetLastError() );
                return HRESULT_FROM_WIN32( GetLastError() );
            }
        }

        return hr;
    } //CompressTiff
}; //CTiffCompression
