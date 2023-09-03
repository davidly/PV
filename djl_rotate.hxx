#pragma once

#include <windows.h>
#include <windowsx.h>
#include <wincodec.h>
#include <wincodecsdk.h>
#include <combaseapi.h>

#include <stdio.h>
#include <math.h>
#include <assert.h>

using namespace std;

#include <wrl.h>

#include "djltrace.hxx"
#include "djlimagedata.hxx"

using namespace Microsoft::WRL;

// Typically:
//   -- JPG, TIFF, Heif, and RAW file formats work by updating an existing Exif Orientation field using CImageData::RotateImage
//      If there isn't space allocated for Exif Orientation, WIC is used to set the value then copy all other data to a new file.
//      But for RAW files with no space allocated for Exif Orientation, there is no WIC encoder, so the rotate call will fail.
//   -- BMP files fail to get a BlockReader object, which that codec doesn't support, and fall back to flip-rotate of pixels.
//   -- GIF and PNG files fail in SetMetadataByName with WINCODEC_ERR_UNEXPECTEDMETADATATYPE, and fall back to flip-rotate of pixels.
//   -- ICO files fail because WIC has no ICO encoder. There is no code here to rotate ICO files.

class CImageRotation
{
private:

    static WCHAR const * ExpectedOrientationName( GUID & containerFormat )
    {
        // JPG and JFIF store Orientation under /app1/
    
        if ( ! memcmp( & containerFormat, & GUID_ContainerFormatJpeg, sizeof GUID ) )
            return L"/app1/ifd/{ushort=274}";
    
        // TIFF and many raw formats store Orientation here.
    
        return L"/ifd/{ushort=274}";
    } //ExpectedOrientationName
    
    static bool CreateOutputPath( WCHAR const * pwcIn, WCHAR * pwcOut )
    {
        WCHAR const * dot = wcsrchr( pwcIn, L'.' );
    
        if ( !dot )
        {
            tracer.Trace( "can't create a temporary filename for path %ws\n", pwcIn );
            return false;
        }
    
        wcscpy( pwcOut, pwcIn );
        wcscpy( pwcOut + ( dot - pwcIn ), L"-rotated" );
        wcscat( pwcOut, dot );
    
        return true;
    } //CreateOutputPath
    
    static bool CreateSafetyPath( WCHAR const * pwcIn, WCHAR * pwcOut )
    {
        WCHAR const * dot = wcsrchr( pwcIn, L'.' );
    
        if ( !dot )
        {
            tracer.Trace( "can't create a safety filename for path %ws\n", pwcIn );
            return false;
        }
    
        wcscpy( pwcOut, pwcIn );
        wcscpy( pwcOut + ( dot - pwcIn ), L"-safety" );
        wcscat( pwcOut, dot );
    
        return true;
    } //CreateSafetyPath

    static void TraceContainerFormat( GUID & cf )
    {
        tracer.Trace( "  container format of source: " );

        if ( GUID_ContainerFormatBmp == cf )
            tracer.TraceQuiet( "Bmp: " );
        else if ( GUID_ContainerFormatPng == cf )
            tracer.TraceQuiet( "Png: " );
        else if ( GUID_ContainerFormatIco == cf )
            tracer.TraceQuiet( "Ico: " );
        else if ( GUID_ContainerFormatJpeg == cf )
            tracer.TraceQuiet( "Jpeg: " );
        else if ( GUID_ContainerFormatTiff == cf )
            tracer.TraceQuiet( "Tiff: " );
        else if ( GUID_ContainerFormatGif == cf )
            tracer.TraceQuiet( "Gif: " );
        else if ( GUID_ContainerFormatWmp == cf )
            tracer.TraceQuiet( "Wmp: " );
        else if ( GUID_ContainerFormatDds == cf )
            tracer.TraceQuiet( "Dds: " );
        else if ( GUID_ContainerFormatAdng == cf )
            tracer.TraceQuiet( "Adng: " );
        else if ( GUID_ContainerFormatHeif == cf )
            tracer.TraceQuiet( "Heif: " );
        else if ( GUID_ContainerFormatWebp == cf )
            tracer.TraceQuiet( "Webp: " );
        else if ( GUID_ContainerFormatRaw == cf )
            tracer.TraceQuiet( "Raw: " );
        else
            tracer.TraceQuiet( "(unknown): " );

        OLECHAR * olestr = NULL;
        HRESULT hr = StringFromCLSID( cf, & olestr );
        if ( SUCCEEDED( hr ) )
        {
            tracer.TraceQuiet( "%ws\n", olestr );
            CoTaskMemFree( olestr );
        }
    } //TraceContainerFormat
    
    static HRESULT RotateImage90Degrees( IWICImagingFactory * pIWICFactory, WCHAR const * pwcPath, WCHAR const * pwcOutputPath, bool right )
    {
        ComPtr<IWICBitmapDecoder> decoder;
        HRESULT hr = pIWICFactory->CreateDecoderFromFilename( pwcPath, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf() );
        if ( FAILED( hr ) ) tracer.Trace( "hr from create decoder: %#x\n", hr );
    
        ComPtr<IWICStream> fileStream = NULL;
        if ( SUCCEEDED( hr ) )
        {
            hr = pIWICFactory->CreateStream( fileStream.GetAddressOf() );
            if ( FAILED( hr ) ) tracer.Trace( "create stream hr: %#x\n", hr );
        }
    
        if ( SUCCEEDED( hr ) )
        {
            hr = fileStream->InitializeFromFilename( pwcOutputPath, GENERIC_WRITE );
            if ( FAILED( hr ) ) tracer.Trace( "hr from InitializeFromFilename: %#x\n", hr );
        }
    
        GUID containerFormat = {0};
    
        if ( SUCCEEDED( hr ) )
        {
            hr = decoder->GetContainerFormat( &containerFormat );
            if ( FAILED( hr ) ) tracer.Trace( "hr from GetContainerFormat %#x\n", hr );
        }
    
        WCHAR const * orientationName = ExpectedOrientationName( containerFormat );
    
        ComPtr<IWICBitmapEncoder> encoder;
        if ( SUCCEEDED( hr ) )
        {
            hr = pIWICFactory->CreateEncoder( containerFormat, NULL, encoder.GetAddressOf() );
            if ( FAILED( hr ) )
            {
                tracer.Trace( "hr from create encoder: %#x\n", hr );

                TraceContainerFormat( containerFormat );
            }
        }
    
        if ( SUCCEEDED( hr ) )
        {
            hr = encoder->Initialize( fileStream.Get(), WICBitmapEncoderNoCache );
            if ( FAILED( hr ) ) tracer.Trace( "initialize encoder: %#x\n", hr );
        }
    
        UINT count = 0;
        if ( SUCCEEDED( hr ) )
        {
            hr = decoder->GetFrameCount( &count );
            if ( FAILED( hr ) ) tracer.Trace( "hr from GetFrameCount: %#x\n", hr );
        }
    
        if ( SUCCEEDED( hr ) )
        {
            for ( UINT i = 0; i < count && SUCCEEDED( hr ); i++ )
            {
                bool useFlipRotator = false;
                ComPtr<IWICBitmapFrameDecode> frameDecode;
       
                if ( SUCCEEDED( hr ) )
                {
                    hr = decoder->GetFrame( i, frameDecode.GetAddressOf() );
                    if ( FAILED( hr ) ) tracer.Trace( "hr from GetFrame %d: %#x\n", i, hr );
                }
    
                ComPtr<IWICBitmapFrameEncode> frameEncode;
                if ( SUCCEEDED( hr ) )
                {
                    hr = encoder->CreateNewFrame( frameEncode.GetAddressOf(), NULL );
                    if ( FAILED( hr ) ) tracer.Trace( "hr from CreateNewFrame: %#x\n", hr );
                }
       
                if ( SUCCEEDED( hr ) )
                {
                    hr = frameEncode->Initialize( NULL );
                    if ( FAILED( hr ) ) tracer.Trace( "hr from frameEncode->Initialize: %#x\n", hr );
                }
    
                ComPtr<IWICMetadataBlockWriter> blockWriter;
                ComPtr<IWICMetadataBlockReader> blockReader;
    
                if ( SUCCEEDED( hr ) )
                {
                    hr = frameDecode->QueryInterface( IID_IWICMetadataBlockReader, (void **) blockReader.GetAddressOf() );
                    if ( FAILED( hr ) ) tracer.Trace( "qi of blockReader: %#x\n", hr );

                    if ( SUCCEEDED( hr ) )
                    {
                        hr = frameEncode->QueryInterface( IID_IWICMetadataBlockWriter, (void **) blockWriter.GetAddressOf() );
                        if ( FAILED( hr ) ) tracer.Trace( "qi of blockWriter: %#x\n", hr );
                    }
    
                    if ( SUCCEEDED( hr ) )
                    {
                        hr = blockWriter->InitializeFromBlockReader( blockReader.Get() );
                        if ( FAILED( hr ) ) tracer.Trace( "hr from InitializeFromBlockReader: %#x\n", hr );
                    }
                    else if ( E_NOINTERFACE == hr )
                    {
                        // bmp files do this -- they have no metadata block and so don't support the interface

                        useFlipRotator = true;
                        hr = S_OK;
                    }
                }

                if ( !useFlipRotator )
                {
                    ComPtr<IWICMetadataQueryWriter> frameQWriter;
        
                    if ( SUCCEEDED( hr ) )
                    {
                        hr = frameEncode->GetMetadataQueryWriter( frameQWriter.GetAddressOf() );
                        if ( FAILED( hr ) ) tracer.Trace( "hr from GetMetadataQueryWriter: %#x\n", hr );
                    }
        
                    // Attempt to set the Exif Orientation flag. Some file formats don't support this.
           
                    if ( SUCCEEDED( hr ) )
                    {
                        ComPtr<IWICMetadataQueryReader> queryReader;
                        hr = frameDecode->GetMetadataQueryReader( queryReader.GetAddressOf() );
                        if ( FAILED( hr ) ) tracer.Trace( "hr from get metadata query reader: %#x\n", hr );
        
                        PROPVARIANT value;
                        PropVariantInit( &value );
                        WORD o = 1;
        
                        if ( SUCCEEDED( hr ) )
                        {
                            hr = queryReader->GetMetadataByName( orientationName, &value );
        
                            if ( SUCCEEDED( hr ) )
                                o = (WORD) value.uiVal;
                            else if ( WINCODEC_ERR_PROPERTYNOTFOUND == hr ) // go with the default
                                hr = S_OK;
                            else
                                tracer.Trace( "getmetadatabyname %ws hr %#x, vt %d, value %d\n", orientationName, hr, value.vt, value.uiVal );
                        }
        
                        if ( SUCCEEDED( hr ) )
                        {
                            if ( right )
                            {
                                if ( 1 == o )
                                    o = 6;
                                else if ( 8 == o )
                                    o = 1;
                                else if ( 3 == o )
                                    o = 8;
                                else
                                    o = 3;
                            }
                            else
                            {
                                if ( 1 == o )
                                    o = 8;
                                else if ( 8 == o )
                                    o = 3;
                                else if ( 3 == o )
                                    o = 6;
                                else
                                    o = 1;
                            }
            
                            value.vt = VT_UI2;
                            value.uiVal = o;
            
                            hr = frameQWriter->SetMetadataByName( orientationName, &value );
            
                            if ( WINCODEC_ERR_UNEXPECTEDMETADATATYPE == hr )
                            {
                                // This file type doesn't support an orientation flag (PNG, GIF, ICO, etc.), so need to rotate pixels
        
                                useFlipRotator = true;
                                hr = S_OK;

                                tracer.Trace( "reverting to flipRotator for file %ws\n", pwcPath );
                                TraceContainerFormat( containerFormat );
                            }
                            else if ( FAILED( hr ) )
                                tracer.Trace( "hr from SetMetadataByName: %#x\n", hr );
                        }
                    }
                }
    
                ComPtr<IWICBitmapSource> bitmapSource;
                bitmapSource.Attach( frameDecode.Detach() );
    
                // Transform the bits if the file format doesn't support Exif Orientation.
             
                if ( SUCCEEDED( hr ) && useFlipRotator )
                {
                    ComPtr<IWICBitmapFlipRotator> rotator;
                    hr = pIWICFactory->CreateBitmapFlipRotator( rotator.GetAddressOf() );
                    if ( FAILED( hr ) ) tracer.Trace( "createbitmapfliprotator: %#x\n", hr );
            
                    if ( SUCCEEDED( hr ) )
                    {
                        WICBitmapTransformOptions wbto = right ? WICBitmapTransformRotate90 : WICBitmapTransformRotate270;
    
                        hr = rotator->Initialize( bitmapSource.Get(), wbto );
                        if ( FAILED( hr ) ) tracer.Trace( "rotated wic bitmap wic transform %d, hr %#x\n", wbto, hr );
                
                        if ( SUCCEEDED( hr ) )
                        {
                            bitmapSource.Reset();
                            bitmapSource.Attach( rotator.Detach() );
                        }
                    }
                }
       
                if ( SUCCEEDED( hr ) )
                {
                    hr = frameEncode->WriteSource( bitmapSource.Get(), NULL );
                    if ( FAILED( hr ) ) tracer.Trace( "frameencode->writesource: %#x\n", hr );
                }
       
                if ( SUCCEEDED( hr ) )
                {
                    hr = frameEncode->Commit();
                    if ( FAILED( hr ) ) tracer.Trace( "hr from frameencode->commit: %#x\n", hr );
                }
            }
        }
       
        if ( SUCCEEDED( hr ) )
        {
            hr = encoder->Commit();
            if ( FAILED( hr ) ) tracer.Trace( "hr from encoder->commit() %#x\n", hr );
        }
       
        if ( SUCCEEDED( hr ) )
        {
            hr = fileStream->Commit( STGC_DEFAULT );
            if ( FAILED( hr ) ) tracer.Trace( "hr from filestream->commit() %#x\n", hr );
        }
    
        return hr;
    } //RotateImage90Degrees

public:
    
    static bool Rotate90ViaExifOrBits( IWICImagingFactory * pIWICFactory, WCHAR const * photoPath, bool infoOnly, bool rotateRight, bool updateFile )
    {
        bool ok = false;
    
        // first try to update the exif tag in place
    
        {
            CImageData imageData;
    
            int orientation = 1;
            ok = imageData.GetOrientation( photoPath, &orientation );
    
            if ( ok )
                tracer.Trace( "original orientation in the file: %d\n", orientation );
            else
                tracer.Trace( "can't find an orientation value in the file's metadata\n" );
    
            if ( infoOnly )
                return ok;
    
            if ( ok )
            {
                ok = imageData.RotateImage( photoPath, rotateRight );
    
                if ( ok )
                    tracer.Trace( "image rotated successfully via updating metadata\n" );
            }
        }
    
        // if updating the exif tag didn't work, rotate into a new file
    
        if ( !ok )
        {
            WCHAR outputPath[ MAX_PATH ];
            ok = CreateOutputPath( photoPath, outputPath );
    
            if ( ok )
            {
                HRESULT hr = RotateImage90Degrees( pIWICFactory, photoPath, outputPath, rotateRight );

                if ( SUCCEEDED( hr ) )
                {
                    ok = true;
        
                    if ( updateFile )
                    {
                        WCHAR awcPhotoPathSafety[ MAX_PATH ];
                        ok = CreateSafetyPath( photoPath, awcPhotoPathSafety );
    
                        if ( ok )
                        {
                            // rename original to a safety name in case of later errors
            
                            ok = MoveFile( photoPath, awcPhotoPathSafety );
                            if ( !ok )
                                tracer.Trace( "can't create safety file for original, error %d\n", GetLastError() );
            
                            // rename new file to original name
            
                            if ( ok )
                            {
                                ok = MoveFile( outputPath, photoPath );
                                if ( !ok )
                                    tracer.Trace( "can't rename temporary file to original filename %d\n", GetLastError() );
                            }
            
                            // delete original, which had been renamed
            
                            if ( ok )
                            {
                                DeleteFile( awcPhotoPathSafety );
                                tracer.Trace( "successfully rotated the file by creating an updated file and overwriting the original\n" );
                            }
                        }
                    }
                    else
                    {
                        tracer.Trace( "success with rotation by creating a new file %ws\n", outputPath );
                    }
                }
                else
                {
                    tracer.Trace( "rotate failed with error %#x file %ws, temporary file %ws\n", hr, photoPath, outputPath );

                    ok = false;
        
                    // clean up temporary file if it got created.
        
                    DeleteFile( outputPath );
                }
            }
        }
    
        return ok;
    } //Rotate90ViaExifOrBits
};                                                    
