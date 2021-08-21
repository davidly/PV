//
// Photo Viewer
// David Lee
//
// Usage:   pv [path of photos] (default is current directory)
//          pv [path of a photo]
//

#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <combaseapi.h>

#include <stdio.h>
#include <math.h>
#include <ppl.h>
#include <mutex>
#include <assert.h>

#include <chrono>

using namespace std;
using namespace std::chrono;

#include <djltrace.hxx>
#include <djlres.hxx>
#include <djl_pa.hxx>
#include <djlenum.hxx>
#include <djlistream.hxx>
#include <djl_strm.hxx>
#include <djlimagedata.hxx>
#include <djl_rotate.hxx>

#ifdef PV_USE_LIBRAW
#include <djl_lr.hxx>
#endif // PV_USE_LIBRAW

#include <wrl.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <d3d11_1.h>
#include <dwrite.h>

#include "pv.hxx"

using namespace Microsoft::WRL;
using namespace D2D1;

#pragma comment( lib, "advapi32.lib" )
#pragma comment( lib, "ole32.lib" )
#pragma comment( lib, "comctl32.lib" )
#pragma comment( lib, "user32.lib" )
#pragma comment( lib, "gdi32.lib" )
#pragma comment( lib, "shell32.lib" )
#pragma comment( lib, "shlwapi.lib" )
#pragma comment( lib, "windowscodecs.lib" )
#pragma comment( lib, "d2d1.lib" )
#pragma comment( lib, "d3d11.lib" )
#pragma comment( lib, "dwrite.lib" )
#pragma comment( lib, "ntdll.lib" )

CDJLTrace tracer;

#define REGISTRY_APP_NAME L"SOFTWARE\\davidlypv"
#define REGISTRY_WINDOW_POSITION  L"WindowPosition"
#define REGISTRY_PHOTO_PATH L"PhotoPath"
#define REGISTRY_PHOTO_DELAY L"PhotoDelay"
#define REGISTRY_PROCESS_RAW L"ProcessRAW"
#define REGISTRY_SORT_IMAGES_BY L"SortImagesBy"
#define REGISTRY_SHOW_METADATA L"ShowMetadata"
#define REGISTRY_IN_F11_FULLSCREEN L"InF11FullScreen"

typedef enum PVZoomLevel { zl_ZoomFullImage, zl_Zoom1, zl_Zoom2 } PVZoomLevel;
typedef enum PVProcessRAW { pr_Always, pr_Sometimes, pr_Never } PVProcessRAW;
typedef enum PVSortImagesBy { si_LastWrite, si_Creation, si_Path } PVSortImagesBy;
typedef enum PVMoveDirection { md_Previous, md_Stay, md_Next } PVMoveDirection;

ComPtr<ID2D1DeviceContext> g_target;
ComPtr<IDXGISwapChain1> g_swapChain;
ComPtr<ID2D1Factory1> g_ID2DFactory;
ComPtr<IWICImagingFactory> g_IWICFactory;
ComPtr<ID2D1Bitmap> g_D2DBitmap;
ComPtr<IWICBitmapSource> g_BitmapSource;
ComPtr<IDWriteFactory> g_dwriteFactory;
ComPtr<IDWriteTextFormat> g_dwriteTextFormat;

CPathArray * g_pImageArray = NULL;
CImageData * g_pImageData = 0;

size_t g_currentBitmapIndex = 0;
const int g_validDelays[] = { 0, 1, 5, 15, 30, 60, 600 };
int g_photoDelay = g_validDelays[ 2 ];
char g_acImageMetadata[ 1024 ];
WCHAR g_awcImageMetadata[ 1024 ];
bool g_showMetadata = true;
bool g_inF11FullScreen = false;
PVProcessRAW g_ProcessRAW = pr_Sometimes;
PVSortImagesBy g_SortImagesBy = si_LastWrite;

long long timeMetadata = 0;
long long timePaint = 0;
long long timeRotate = 0;
long long timeLoad = 0;
long long timeSwapChain = 0;
long long timeD2DB = 0;
long long timeLibRaw = 0;

class CCursor
{
    private:
        HCURSOR previousCursor;

    public:
        CCursor( HCURSOR c )
        {
            previousCursor = SetCursor( c );
        }

        ~CCursor()
        {
            SetCursor( previousCursor );
        }
};

// Keep this sorted and lowercase

const WCHAR * imageExtensions[] =
{
    L"3fr",
    L"arw",
    L"bmp",
    L"cr2",
    L"cr3",
    L"dng",
    L"flac",
    L"gif",
    L"heic",   
    L"hif",
    L"ico",
    L"jfif",
    L"jpeg",
    L"jpg",
    L"nef",
    L"orf",
    L"png",
    L"raf",
    L"rw2",
    L"tif",
    L"tiff",
};

// images that don't likely have an embedded JPG/PNG/etc. useful for display

const WCHAR * nonEmbedExtensions[] =
{
    L"bmp",
    L"gif",
    L"heic",   
    L"hif",
    L"ico",
    L"jfif",
    L"jpeg",
    L"jpg",
    L"png",
    L"tif",
    L"tiff",
};

// Images that quite likely (though not for sure) have good quality embedded JPGs
// Images from older cameras (e.g. Nikon D100) and more recent cameras (Leica M240) have very small embedded JPGs.
// Flac files aren't RAW at all, but they can have embedded images.

const WCHAR * RawFileExtensions[] =
{
    L"3fr",
    L"arw",
    L"cr2",
    L"cr3",
    L"dng",
    L"flac",
    L"nef",
    L"orf",
    L"raf",
    L"rw2",
};

const WCHAR * FindExtension( const WCHAR * pwcPath, size_t len )
{
    const WCHAR * pwcEnd = pwcPath + len - 1;

    while ( pwcEnd >= pwcPath )
    {
        if ( L'.' == *pwcEnd )
            return pwcEnd;

        pwcEnd--;
    }

    return pwcPath + len;
} //FindExtension

bool IsInExtensionList( const WCHAR * pwcPath, WCHAR ** pExtensions, int cExtensions )
{
    size_t len = wcslen( pwcPath );
    const WCHAR * pext = FindExtension( pwcPath, len );
    if ( L'.' == *pext )
        pext++;

    size_t extlen = wcslen( pext );

    if ( extlen < 3 || extlen > 4 )
        return false;

    for ( int i = 0; i < cExtensions; i++ )
    {
        int c = _wcsicmp( pext, pExtensions[ i ] );

        if ( 0 == c )
            return true;

        if ( c < 0 )
            break;
    }

    return false;
} //IsInExtensionList

bool IsFLAC( const WCHAR * pwcPath )
{
    size_t len = wcslen( pwcPath );

    return ( len >= 6 && !_wcsicmp( pwcPath + len - 5, L".flac" ) );
} //IsFLAC

void LoadRegistryParams()
{
    WCHAR awcBuffer[ 10 ] = { 0 };
    BOOL ok = CDJLRegistry::readStringFromRegistry( HKEY_CURRENT_USER, REGISTRY_APP_NAME, REGISTRY_PHOTO_DELAY, awcBuffer, sizeof( awcBuffer ) );

    //tracer.Trace( "read delay %ws from registry, ok %d\n", awcBuffer, ok );

    if ( ok )
    {
        swscanf_s( awcBuffer, L"%d", & g_photoDelay );

        bool found = false;

        for ( int d = 0; d < _countof( g_validDelays ); d++ )
        {
            if ( g_validDelays[ d ] == g_photoDelay )
            {
                found = true;
                break;
            }
        }

        if ( !found )
            g_photoDelay = g_validDelays[ 2 ];

        //tracer.Trace( "g_photoDelay found: %d, final value %d\n", found, g_photoDelay );
    }

    awcBuffer[ 0 ] = 0;
    ok = CDJLRegistry::readStringFromRegistry( HKEY_CURRENT_USER, REGISTRY_APP_NAME, REGISTRY_PROCESS_RAW, awcBuffer, sizeof( awcBuffer ) );
    if ( ok )
    {
        int val = 1;
        swscanf_s( awcBuffer, L"%d", & val );

        if ( val < 0 || val > 2 )
            val = 1;

        g_ProcessRAW = (PVProcessRAW) val;
    }

    awcBuffer[ 0 ] = 0;
    ok = CDJLRegistry::readStringFromRegistry( HKEY_CURRENT_USER, REGISTRY_APP_NAME, REGISTRY_SORT_IMAGES_BY, awcBuffer, sizeof( awcBuffer ) );
    if ( ok )
    {
        int val = 0;
        swscanf_s( awcBuffer, L"%d", & val );

        if ( val < 0 || val > 2 )
            val = 1;

        g_SortImagesBy = (PVSortImagesBy) val;
    }

    awcBuffer[ 0 ] = 0;
    ok = CDJLRegistry::readStringFromRegistry( HKEY_CURRENT_USER, REGISTRY_APP_NAME, REGISTRY_SHOW_METADATA, awcBuffer, sizeof( awcBuffer ) );
    if ( ok )
        g_showMetadata = ( !_wcsicmp( awcBuffer, L"Yes" ) );

    awcBuffer[ 0 ] = 0;
    ok = CDJLRegistry::readStringFromRegistry( HKEY_CURRENT_USER, REGISTRY_APP_NAME, REGISTRY_IN_F11_FULLSCREEN, awcBuffer, sizeof( awcBuffer ) );
    if ( ok )
        g_inF11FullScreen = ( !_wcsicmp( awcBuffer, L"Yes" ) );
} //LoadRegistryParams

void NavigateToStartingPhoto( WCHAR * pwcStartingPhoto )
{
    if ( 0 == g_pImageArray->Count() )
        return;

    for ( size_t i = 0; i < g_pImageArray->Count(); i++ )
    {
        if ( !_wcsicmp( pwcStartingPhoto, g_pImageArray->Get( i ) ) )
        {
            g_currentBitmapIndex = i;
            break;
        }
    }
} //NavigateToStartingPhoto

HRESULT CreateDevice( D3D_DRIVER_TYPE const type, ComPtr<ID3D11Device> & device )
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    return D3D11CreateDevice( NULL, type, NULL, flags, NULL, 0, D3D11_SDK_VERSION, device.GetAddressOf(), NULL, NULL );
} //CreateDevice

HRESULT CreateJustSwapChainBitmap()
{
    ComPtr<IDXGISurface> surface;
    HRESULT hr = g_swapChain->GetBuffer( 0, __uuidof(surface), reinterpret_cast<void **> ( surface.GetAddressOf() ) );

    if ( FAILED( hr ) )
    {
        tracer.Trace( "can't create IDXGISurface %#x\n", hr );
        return hr;
    }

    auto bprops = BitmapProperties1( D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, PixelFormat( DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE ) );

    ComPtr<ID2D1Bitmap1> bitmap;
    hr = g_target->CreateBitmapFromDxgiSurface( surface.Get(), bprops, bitmap.GetAddressOf() );

    if ( FAILED( hr ) )
    {
        tracer.Trace( "can't create ID2D1Bitmap1 %#x\n", hr );
        return hr;
    }

    g_target->SetTarget( bitmap.Get() );

    float dpiX, dpiY;
    g_ID2DFactory->GetDesktopDpi( &dpiX, &dpiY );
    g_target->SetDpi( dpiX, dpiY );
    //tracer.Trace( "desktop dpi %f %f\n", dpiX, dpiY );

    // default is D2D1_UNIT_MODE_DIPS, so on high-pixel-density displays without this line coordinates are broken

    g_target->SetUnitMode( D2D1_UNIT_MODE_PIXELS );

    return hr;
} //CreateJustSwapChainBitmap

HRESULT CreateDeviceSwapChainBitmap( HWND hwnd )
{
    ComPtr<ID3D11Device> d3device;
    HRESULT hr = CreateDevice( D3D_DRIVER_TYPE_HARDWARE, d3device );
    if ( DXGI_ERROR_UNSUPPORTED == hr )
    {
        tracer.Trace( "GPU isn't available, falling back to WARP\n" );
        hr = CreateDevice( D3D_DRIVER_TYPE_WARP, d3device ); // fall back to CPU if GPU isn't available
    }

    if ( FAILED( hr ) )
    {
        tracer.Trace( "can't create d3d device %#x\n", hr );
        return hr;
    }

    ComPtr<IDXGIDevice> dxdevice;
    hr = d3device.As( &dxdevice );

    if ( FAILED( hr ) )
    {
        tracer.Trace( "can't get idxgiDevice %#x\n", hr );
        return hr;
    }

    ComPtr<IDXGIAdapter> adapter;
    hr = dxdevice->GetAdapter( adapter.GetAddressOf() );

    if ( FAILED( hr ) )
    {
        tracer.Trace( "can't get idxgiAdapter %#x\n", hr );
        return hr;
    }

    ComPtr<IDXGIFactory2> factory;
    hr = adapter->GetParent( __uuidof( factory ), reinterpret_cast<void **> ( factory.GetAddressOf() ) );

    if ( FAILED( hr ) )
    {
        tracer.Trace( "can't get idxgiFactory2 %#x\n", hr );
        return hr;
    }

    DXGI_SWAP_CHAIN_DESC1 props = { 0 };
    props.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.SampleDesc.Count = 1;
    props.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    props.BufferCount = 2;
    hr = factory->CreateSwapChainForHwnd( d3device.Get(), hwnd, &props, NULL, NULL, g_swapChain.GetAddressOf() );

    if ( FAILED( hr ) )
    {
        tracer.Trace( "can't create swapchain %#x\n", hr );
        return hr;
    }

    ComPtr<ID2D1Device> device;
    hr = g_ID2DFactory->CreateDevice( dxdevice.Get(), device.GetAddressOf() );

    if ( FAILED( hr ) )
    {
        tracer.Trace( "can't create ID2D1Device %#x\n", hr );
        return hr;
    }

    hr = device->CreateDeviceContext( D2D1_DEVICE_CONTEXT_OPTIONS_ENABLE_MULTITHREADED_OPTIMIZATIONS, g_target.GetAddressOf() );

    if ( FAILED( hr ) )
    {
        tracer.Trace( "can't create device context %#x\n", hr );
        return hr;
    }

    hr = CreateJustSwapChainBitmap();

    return hr;
} //CreateDeviceSwapChainBitmap

HRESULT CreateTargetAndD2DBitmap( HWND hwnd )
{
    high_resolution_clock::time_point tA = high_resolution_clock::now();
    HRESULT hr = S_OK;

    if ( ! ( g_target && g_swapChain ) )
    {
        tracer.Trace( "no target or swapchain in CreateTargetAndD2DBitmap, so creating them\n" );
        g_target.Reset();
        g_swapChain.Reset();
        hr = CreateDeviceSwapChainBitmap( hwnd );

        if ( FAILED( hr ) )
            tracer.Trace( "CreateDeviceSwapChainBitmap failed with %#x\n", hr );
    }

    high_resolution_clock::time_point tB = high_resolution_clock::now();
    timeSwapChain += duration_cast<std::chrono::nanoseconds>( tB - tA ).count();

    tA = high_resolution_clock::now();

    g_D2DBitmap.Reset();

    // >97% of the time spent getting a JPG from disk to glass is in this one call -- CreateBitmapFromWicBitmap.
    // About 80% of that time is decompressing the (JPG) image and 20% is spent sending it to the display hardware.

    if ( SUCCEEDED( hr ) )
        hr = g_target->CreateBitmapFromWicBitmap( g_BitmapSource.Get(), NULL, g_D2DBitmap.GetAddressOf() );

    if ( FAILED( hr ) )
        tracer.Trace( "CreateBitmapFromWicBitmap failed with %#x\n", hr );

    tB = high_resolution_clock::now();
    timeD2DB += duration_cast<std::chrono::nanoseconds>( tB - tA ).count();

    return hr;
} //CreateTargetAndD2DBitmap

HRESULT LoadCurrentFileD2D( HWND hwnd, const WCHAR * pwcPath, IStream * pStream, int * pwidth, int * pheight, int orientation, bool useLibRaw )
{
    g_BitmapSource.Reset();
    g_D2DBitmap.Reset();
    HRESULT hr = S_OK;

    ComPtr<IWICBitmapSource> bitmapSource;

#ifdef PV_USE_LIBRAW

    // use libraw when WIC won't do -- for Canon CR3 files without an embedded JPG for example, or when the embedded JPG is insufficient

    if ( useLibRaw )
    {
        high_resolution_clock::time_point tA = high_resolution_clock::now();
        CLibRaw libraw;
        int colors = 0;
        int bpc = 8; // use 16 when hdr display support is added
        unique_ptr<byte> pb( libraw.ProcessRaw( pwcPath, bpc, *pwidth, *pheight, colors ) );

        if ( 0 == pb.get() )
        {
            tracer.Trace( "libraw failed to open the file %ws\n", pwcPath );
            return E_FAIL;
        }

        UINT stride = ( *pwidth ) * colors * ( bpc / 8 );
        UINT bytes = stride * ( *pheight );
        ComPtr<IWICBitmap> bitmap;
        //tracer.Trace( "creating WIC bitmap from memory, width %d, height %d, stride %d, bytes %d colors %d\n", *pwidth, *pheight, stride, bytes, colors );

        if ( 16 == bpc )
        {
            if ( 1 == colors )
                hr = g_IWICFactory->CreateBitmapFromMemory( *pwidth, *pheight, GUID_WICPixelFormat16bppGray, stride, bytes, pb.get(), bitmap.GetAddressOf() );
            else if ( 3 == colors )
                hr = g_IWICFactory->CreateBitmapFromMemory( *pwidth, *pheight, GUID_WICPixelFormat48bppBGR, stride, bytes, pb.get(), bitmap.GetAddressOf() );
            else
                hr = E_FAIL;
        }
        else if ( 8 == bpc )
        {
            if ( 1 == colors )
                hr = g_IWICFactory->CreateBitmapFromMemory( *pwidth, *pheight, GUID_WICPixelFormat8bppGray, stride, bytes, pb.get(), bitmap.GetAddressOf() );
            else if ( 3 == colors )
                hr = g_IWICFactory->CreateBitmapFromMemory( *pwidth, *pheight, GUID_WICPixelFormat24bppBGR, stride, bytes, pb.get(), bitmap.GetAddressOf() );
            else
                hr = E_FAIL;
        }
        else
            hr = E_FAIL;
     
        high_resolution_clock::time_point tB = high_resolution_clock::now();
        timeLibRaw += duration_cast<std::chrono::nanoseconds>( tB - tA ).count();

        if ( FAILED( hr ) )
        {
            tracer.Trace( "unable to create WIC bitmap from libraw memory: %#x for file %ws\n", hr, pwcPath );
            return hr;
        }

        g_BitmapSource.Reset();
        bitmapSource.Attach( bitmap.Detach() );
    }
    else
#endif // PV_USE_LIBRAW
    {
        ComPtr<IWICBitmapDecoder> decoder;
    
        if ( pwcPath )
            hr = g_IWICFactory->CreateDecoderFromFilename( pwcPath, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf() );
        else
            hr = g_IWICFactory->CreateDecoderFromStream( pStream, NULL, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf() );

        if ( FAILED( hr ) )
        {
            tracer.Trace( "IWICFactory->CreateDecoderxxx failed with %#x on file %ws\n", hr, pwcPath ? pwcPath : L"stream" );
            return hr;
        }

        ComPtr<IWICBitmapFrameDecode> frame;
    
        if ( SUCCEEDED( hr ) )
            hr = decoder->GetFrame( 0, frame.GetAddressOf() );

        if ( SUCCEEDED( hr ) )
        {
            g_BitmapSource.Reset();
            bitmapSource.Attach( frame.Detach() );
        }
    }

    ComPtr<IWICFormatConverter> formatConverter;
    if ( SUCCEEDED( hr ) )
        hr = g_IWICFactory->CreateFormatConverter( formatConverter.GetAddressOf() );

    if ( SUCCEEDED( hr ) )
    {
        hr = formatConverter->Initialize( bitmapSource.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeCustom );

        if ( SUCCEEDED( hr ) )
        {
            g_BitmapSource.Reset();
            g_BitmapSource.Attach( formatConverter.Detach() );
        }
    }

    // LibRaw rotates the image appropriately when creating a buffer, so there is no need to do it again.
    // But images loaded via WIC don't automatically rotate.

    if ( SUCCEEDED( hr ) && ( orientation >= 3 ) && ( orientation <= 8 ) && !useLibRaw )
    {
        ComPtr<IWICBitmapFlipRotator> rotator;
        HRESULT hr = g_IWICFactory->CreateBitmapFlipRotator( rotator.GetAddressOf() );

        if ( SUCCEEDED( hr ) )
        {
            WICBitmapTransformOptions wbto = WICBitmapTransformRotate0;
    
            if ( orientation == 3 || orientation == 4 )
                wbto = WICBitmapTransformRotate180;
            else if ( orientation == 5 || orientation == 6 )
                wbto = WICBitmapTransformRotate90;
            else if ( orientation == 7 || orientation == 8 )
                wbto = WICBitmapTransformRotate270;
        
            //if ( val == 2 || val == 4 || val == 5 || val == 7 )
            //    rot = (RotateFlipType) ( (int) rot | (int) RotateNoneFlipX );

            if ( WICBitmapTransformRotate0 != wbto )
            {
                hr = rotator->Initialize( g_BitmapSource.Get(), wbto );
                tracer.Trace( "rotated wic bitmap exif orientation %d, wic transform %d, hr %#x\n", orientation, wbto, hr );
    
                if ( SUCCEEDED( hr ) )
                {
                    g_BitmapSource.Reset();
                    g_BitmapSource.Attach( rotator.Detach() );
                }
            }
        }
    }

    high_resolution_clock::time_point tA = high_resolution_clock::now();

    if ( SUCCEEDED( hr ) )
        hr = g_BitmapSource->GetSize( (UINT *) pwidth, (UINT *) pheight );

    if ( SUCCEEDED( hr ) )
        hr = CreateTargetAndD2DBitmap( hwnd );

    if ( FAILED( hr ) )
    {
        g_BitmapSource.Reset();
        g_D2DBitmap.Reset();
    }

    //tracer.Trace( "hr from LoadCurrentFileD2D: %#x\n", hr );

    return hr;
} //LoadCurrentFileD2D

bool LoadCurrentFileUsingD2D( HWND hwnd )
{
    unique_ptr<WCHAR> titleResource( new WCHAR[ 100 ] );
    int ret = LoadStringW( NULL, ID_PV_STRING_TITLE, titleResource.get(), 100 );
    if ( 0 == ret )
        titleResource.get()[0] = 0;

    const int maxTitleLen = MAX_PATH + 100;
    unique_ptr<WCHAR> winTitle( new WCHAR[ maxTitleLen ] );

    if ( 0 == g_pImageArray->Count() )
    {
        int len = swprintf_s( winTitle.get(), maxTitleLen, titleResource.get(), 0, 0, L"" );
        if ( -1 != len )
            SetWindowText( hwnd, winTitle.get() );

        return true;
    }

    CCursor hourglass( LoadCursor( NULL, IDC_WAIT ) );
    int availableWidth = 0;
    int availableHeight = 0;
    const WCHAR * pwcFile = g_pImageArray->Get( g_currentBitmapIndex );

    high_resolution_clock::time_point tBMA = high_resolution_clock::now();

    g_pImageData->PurgeCache();

    // First try to find an embedded JPG/PNG rather than having WIC do so. This is because WIC's codecs are buggy and
    // resource leaking. This won't work for iPhone .heic files and primitives like .jpg, .png, etc. That's OK.
    // Always call this even for files like JPG in order to get the orientation and cache other metadata

    long long llEmbeddedOffset = 0;
    long long llEmbeddedLength = 0;
    int orientationValue = -1;
    int embeddedWidth = 0;
    int embeddedHeight = 0;
    int fullWidth = 0;
    int fullHeight = 0;

    high_resolution_clock::time_point tEmbeddedA = high_resolution_clock::now();
    bool foundEmbedding = g_pImageData->FindEmbeddedImage( pwcFile, & llEmbeddedOffset, & llEmbeddedLength, & orientationValue, & embeddedWidth, & embeddedHeight, & fullWidth, & fullHeight );
    high_resolution_clock::time_point tEmbeddedB = high_resolution_clock::now();
    timeMetadata += duration_cast<std::chrono::nanoseconds>( tEmbeddedB - tEmbeddedA ).count();

    // If the embedded JPG is large enough, use it. For some cameras, it's not. For those use LibRaw to process the RAW image.

    bool isRaw = IsInExtensionList( pwcFile, (WCHAR **) RawFileExtensions, _countof( RawFileExtensions ) );
    bool useLibRaw = false;

    if ( ( pr_Always == g_ProcessRAW ) && isRaw )
        useLibRaw = true;
    else if ( pr_Never == g_ProcessRAW )
        useLibRaw = false;
    else if ( foundEmbedding )
        useLibRaw = ( fullWidth > ( 3 * embeddedWidth ) ) && isRaw;

    if ( isRaw && !foundEmbedding )
        useLibRaw = true;

    if ( IsFLAC( pwcFile ) )
        useLibRaw = false;

#ifndef PV_USE_LIBRAW
    useLibRaw = false;
#endif // PV_USE_LIBRAW

    tracer.Trace( "  find embedded image result %d, %lld, %lld, orientation %d useLibRaw %d for %ws\n", foundEmbedding, llEmbeddedOffset, llEmbeddedLength, orientationValue, useLibRaw, pwcFile );

    high_resolution_clock::time_point tLoadA = high_resolution_clock::now();

    if ( foundEmbedding && !useLibRaw && isRaw )
    {
        CIStream * pStream = new CIStream( pwcFile, llEmbeddedOffset, llEmbeddedLength );
        if ( !pStream->Ok() )
        {
            tracer.Trace( "can't open IStream for embedded image\n" );
            return false;
        }

        // The ComPtr constructor that takes an object does an AddRef(), and this leads to leaks. Avoid that.

        ComPtr<IStream> stream;
        stream.Attach( pStream );

        HRESULT hr = LoadCurrentFileD2D( hwnd, NULL, stream.Get(), &availableWidth, &availableHeight, orientationValue, false );

        if ( FAILED( hr ) )
        {
            tracer.Trace( "  failed error %#x to load embedded image for %ws\n", hr, pwcFile );
            return false;
        }

        stream.Reset();
        //tracer.Trace( "  loaded embedded image for %ws\n", pwcFile );
    }
    else
    {
        HRESULT hr = LoadCurrentFileD2D( hwnd, pwcFile, NULL, &availableWidth, &availableHeight, orientationValue, useLibRaw );

        if ( FAILED( hr ) )
        {
            tracer.Trace( "  LoadCurrentFileD2D failed with error %#x, can't load file %ws\n", hr, pwcFile );
            return false;
        }

        //tracer.Trace( "  no embedded image, so loaded via WIC: %ws\n", pwcFile );
    }

    high_resolution_clock::time_point tLoadB = high_resolution_clock::now();
    timeLoad += duration_cast<std::chrono::nanoseconds>( tLoadB - tLoadA ).count();

    high_resolution_clock::time_point tMetadataA = high_resolution_clock::now();
    g_acImageMetadata[ 0 ] = 0;
    g_awcImageMetadata[ 0 ] = 0;
    bool ok = g_pImageData->GetInterestingMetadata( pwcFile, g_acImageMetadata, _countof( g_acImageMetadata ), availableWidth, availableHeight );

    if ( ok )
    {
        size_t cConverted = 0;
        mbstowcs_s( &cConverted, g_awcImageMetadata, _countof( g_awcImageMetadata ), g_acImageMetadata, 1 + strlen( g_acImageMetadata ) );
    }

    high_resolution_clock::time_point tMetadataB = high_resolution_clock::now();
    timeMetadata += duration_cast<std::chrono::nanoseconds>( tMetadataB - tMetadataA ).count();

    int len = swprintf_s( winTitle.get(), maxTitleLen, titleResource.get(), g_currentBitmapIndex + 1, g_pImageArray->Count(), pwcFile );
    if ( -1 != len )
        SetWindowText( hwnd, winTitle.get() );

    return true;
} //LoadCurrentFileUsingD2D

bool LoadNextImageInternal( HWND hwnd, PVMoveDirection md )
{
    //tracer.Trace( "LoadNextImage, count of images %d, current %d, forward %d\n", g_pImageArray->Count(), g_currentBitmapIndex, forward );

    if ( 0 == g_pImageArray->Count() )
        return true;

    if ( md_Next == md )
    {
        g_currentBitmapIndex++;

        if ( g_currentBitmapIndex >= g_pImageArray->Count() )
            g_currentBitmapIndex = 0;
    }
    else if ( md_Previous == md )
    {
        if ( 0 == g_currentBitmapIndex )
            g_currentBitmapIndex = g_pImageArray->Count() - 1;
        else
            g_currentBitmapIndex--;
    }

    tracer.Trace( "loading image index %d, %ws\n", g_currentBitmapIndex, g_pImageArray->Get( g_currentBitmapIndex ) );

#if false // Used for performance testing using Slideshow. Show all images 10 times then exit the app
    static int timesaround = 0;

    if ( 0 == g_currentBitmapIndex )
    {
        tracer.Trace( "made it to image 0\n" );

        timesaround++;

        if ( 10 == timesaround )
            exit( 1 );
    }
#endif

    return LoadCurrentFileUsingD2D( hwnd );
} //LoadNextImageInternal

void LoadNextImage( HWND hwnd, PVMoveDirection md )
{
    // Skip over files that can't be loaded

    size_t start = g_currentBitmapIndex;

    do
    {
        bool worked = LoadNextImageInternal( hwnd, md );

        if ( worked )
            return;

        // avoid infinite loop if none of the imags can be loaded (e.g. all are .cr3 with no embedded JPGs and LibRaw isn't used)

        if ( md_Stay != md && g_currentBitmapIndex == start )
            return;

        // The current image didn't load, so find one that will

        if ( md_Stay == md )
            md = md_Next;

    } while ( true );
} //LoadNextImage

HBITMAP CreateHBITMAP( IWICBitmapSource & bitmap )
{
    UINT width, height;
    bitmap.GetSize( &width, &height );

    BITMAPINFO bminfo;
    ZeroMemory( &bminfo, sizeof bminfo );
    bminfo.bmiHeader.biSize = sizeof BITMAPINFOHEADER;
    bminfo.bmiHeader.biWidth = width;
    bminfo.bmiHeader.biHeight = (LONG) height;
    bminfo.bmiHeader.biPlanes = 1;
    bminfo.bmiHeader.biBitCount = 32;
    bminfo.bmiHeader.biCompression = BI_RGB;

    void * pvImageBits = NULL;
    HDC hdcScreen = GetDC( NULL );
    HBITMAP hbmp = CreateDIBSection( hdcScreen, &bminfo, DIB_RGB_COLORS, &pvImageBits, NULL, 0 );
    ReleaseDC( NULL, hdcScreen );

    if ( NULL == hbmp )
        return hbmp;

    const UINT cbStride = width * 4;
    const UINT cbImage = cbStride * height;
    bitmap.CopyPixels( NULL, cbStride, cbImage, static_cast<BYTE *>( pvImageBits ) );

    // It's upside down, because of course HBITMAPs are upside down.

    if ( height > 1 )
    {
        unique_ptr<::byte> buffer( new ::byte[ cbStride ] );

        for ( size_t y = 0; y < height / 2; y++ )
        {
            BYTE * pA = ( (BYTE *) pvImageBits ) + ( y * cbStride );
            BYTE * pB = ( (BYTE *) pvImageBits ) + ( ( height - 1 - y ) * cbStride );
            memcpy( buffer.get(), pA, cbStride );
            memcpy( pA, pB, cbStride );
            memcpy( pB, buffer.get(), cbStride );
        }
    }

    return hbmp;
} //CreateHBITMAP

void PutPathTextInClipboard( const WCHAR * pwcPath )
{
    size_t len = wcslen( pwcPath );
    size_t bytes = ( len + 1 ) * sizeof WCHAR;

    LPTSTR pstr = (LPTSTR) GlobalAlloc( GMEM_FIXED, bytes );
    if ( 0 != pstr )
    {
        // Windows takes ownership of pstr once it's in the clipboard

        wcscpy_s( pstr, 1 + len, pwcPath );
        SetClipboardData( CF_UNICODETEXT, pstr );
    }
} //PutPathTextInClipboard

void PutPathInClipboard( const WCHAR * pwcPath )
{
    size_t len = wcslen( pwcPath );

    // save room for two trailing null characters

    size_t bytes = sizeof( DROPFILES ) + ( ( len + 2 ) * sizeof( WCHAR ) );

    DROPFILES * df = (DROPFILES *) GlobalAlloc( GMEM_FIXED, bytes );

    if ( 0 != df )
    {
        ZeroMemory( df, bytes );
        df->pFiles = sizeof( DROPFILES );
        df->fWide = TRUE;
        WCHAR * ptr = (LPWSTR) ( df + 1 );
        wcscpy_s( ptr, len + 1, pwcPath );
    
        // Windows takes ownership of the buffer
    
        SetClipboardData( CF_HDROP, df );
    }
} //PutPathInClipboard

void PutBitmapInClipboard()
{
    HBITMAP hbm = CreateHBITMAP( * g_BitmapSource.Get() );
    
    if ( 0 != hbm )
    {
        // The HBITMAP in its current form can't go in the clipboard.
        // Copy it into a Device Independent Bitmap for the clipboard.
    
        DIBSECTION ds;
        if ( sizeof ds == GetObject( hbm, sizeof ds, &ds ) )
        {
            HDC hdc = GetDC( HWND_DESKTOP );
            HBITMAP hdib = CreateDIBitmap( hdc, &ds.dsBmih, CBM_INIT, ds.dsBm.bmBits, (BITMAPINFO *) &ds.dsBmih, DIB_RGB_COLORS );
            ReleaseDC( HWND_DESKTOP, hdc );
    
            if ( NULL != hdib )
            {
                HANDLE h = SetClipboardData( CF_BITMAP, hdib );
    
                if ( NULL == h )
                    DeleteObject( hdib ); // failure case
            }
        }
    
        // Windows now owns hdib but not hbm
    
        DeleteObject( hbm );
    }
    else
        tracer.Trace( "can't create hbitmap in copy command\n" );
} //PutBitmapInClipboard

void CopyCommand( HWND hwnd )
{
    tracer.Trace( "copying path and image into clipboard\n" );

    if ( ( 0 != g_pImageArray ) && ( 0 != g_pImageArray->Count() ) && ( 0 != g_BitmapSource ) )
    {
        if ( OpenClipboard( hwnd ) )
        {
            EmptyClipboard();

            const WCHAR * pwcPath = g_pImageArray->Get( g_currentBitmapIndex );
            PutPathTextInClipboard( pwcPath );
            PutPathInClipboard( pwcPath );
            PutBitmapInClipboard();

            CloseClipboard();
        }
    }
} //CopyCommand

extern "C" INT_PTR WINAPI HelpDialogProc( HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam )
{
    static const WCHAR * helpText = L"usage:\n"
                                     "\tpv photo [-s] [-t]\n"
                                     "\tpv [folder] [-s] [-t]\n"
                                     "\n"
                                     "arguments:\n"
                                     "\tphoto\t\tpath of image to display\n"
                                     "\tfolder\t\tpath of folder with images (default is current path)\n"
                                     "\t-s\t\tstart slideshow\n"
                                     "\t-t\t\tenable debug tracing to %temp%\\tracer.txt\n"
                                     "\n"
                                     "mouse:\n"
                                     "\tleft-click\tdisplay 1:1 pixel for pixel\n"
                                     "\tctrl+left-click\tdisplay 4:1 pixel for pixel\n"
                                     "\tright-click\tcontext menu\n"
                                     "\n"
                                     "keyboard:\n"
                                     "\tctrl+c\t\tcopy image path and bitmap to the clipboard\n"
                                     "\tctrl+d\t\tdelete the current file\n"
                                     "\ti\t\tshow or hide image EXIF information\n"
                                     "\tl\t\trotate image left\n"
                                     "\tm\t\tshow GPS coordinates (if any) in Google Maps\n"
                                     "\tn\t\tnext image (also right arrow)\n"
                                     "\tp\t\tprevious image (also left arrow)\n"
                                     "\tq or esc\tquit the app\n"
                                     "\tr\t\trotate image right\n"
                                     "\ts\t\tstart or stop slideshow\n"
                                     "\tF11\t\tenter or exit full-screen mode\n"
                                     "\n"
                                     "notes:\n"
                                     "\t- All image files below the given folder are enumerated.\n"
                                     "\t- Entering slideshow for 1st time randomizes image order.\n"
                                     "\t- iPhone .heic photos require a free Microsoft Store codec.\n"
                                     "\t- WIC leaks handles for .heic photos.\n"
                                     "\t- WIC crashes for DNGs created by Lightroom's \"enhance.\"\n"
                                     "\t- Tested with 3fr arw bmp cr2 cr3 dng flac gif heic hif ico\n"
                                     "\t      jfif jpeg jpg nef orf png raf rw2 tif tiff.\n"
                                     "\t- Tested with RAW from Apple Canon Fujifilm Hasselblad Leica\n"
                                     "\t      Nikon Olympus Panasonic Pentax Ricoh Sigma Sony.\n"
                                     "\t- Rotate tries to update Exif Orientation, but may re-encode the file\n";


    switch( message )
    {
        case WM_INITDIALOG:
        {
            SetDlgItemText( hdlg, ID_PV_HELP_DIALOG_TEXT, helpText );
            return true;
        }

        case WM_GETDLGCODE:
        {
            return DLGC_WANTALLKEYS;
        }

        case WM_COMMAND:
        {
            switch( LOWORD( wParam ) )
            {
                case IDOK:
                    EndDialog( hdlg, IDOK );
                    break;
                case IDCANCEL:
                    EndDialog( hdlg, IDCANCEL );
                    break;
            }
            break;
        }

        default:
            return 0;
    }

    return 0;
} //HelpDialogProc

void ReleaseDevice()
{
    g_target.Reset();
    g_swapChain.Reset();
} //ReleaseDevice

void ResizeSwapChainBitmap( HWND hwnd )
{
    if ( g_target )
    {
        g_target->SetTarget( NULL );

        if ( S_OK == g_swapChain->ResizeBuffers( 0, 0, 0, DXGI_FORMAT_UNKNOWN, 0 ) )
        {
            tracer.Trace( "creating deviceswapchainbitmap from ResizeSwapChainBitmap\n" );
            CreateJustSwapChainBitmap();
        }
        else
        {
            tracer.Trace( "releasing device from ResizeSwapChainBitmap\n" );
            ReleaseDevice();
        }
    }
} //ResizeSwapChainBitmap

void OnPaint( HWND hwnd, int mouseX, int mouseY, PVZoomLevel zoomLevel )
{
    HRESULT hr = S_OK;
    PAINTSTRUCT ps;

    if ( !g_D2DBitmap )
        return;

    if ( !g_target )
    {
        tracer.Trace( "in OnPaint and recreating swapchainbitmap\n" );

        hr = CreateTargetAndD2DBitmap( hwnd );
        if ( FAILED( hr ) )
        {
            tracer.Trace( "failed to in CreateTargetAndD2DBitmap %#x\n", hr );
            return;
        }
    }

    if ( BeginPaint( hwnd, &ps ) )
    {
        g_target->BeginDraw();

        g_target->SetTransform( D2D1::Matrix3x2F::Identity() );
        g_target->Clear( D2D1::ColorF( D2D1::ColorF::Black ) );

        D2D1_SIZE_F rtSize = g_target->GetSize();
        D2D1_RECT_F rectangle = D2D1::RectF( 0.0f, 0.0f, rtSize.width, rtSize.height );
        double winAR = rtSize.width / rtSize.height;

        RECT rc;
        GetClientRect( hwnd, &rc );
        //tracer.Trace( "client rect %d %d %d %d\n", rc.left, rc.top, rc.right, rc.bottom );

        UINT wImg = 0, hImg = 0;
        g_BitmapSource->GetSize( &wImg, &hImg );
        //tracer.Trace( "image size %d %d\n", wImg, hImg );

        double imgAR = (double) wImg / (double) hImg;
        UINT hOut, wOut;
        float xOffset = 0.0, yOffset = 0.0;

        if ( imgAR > winAR )
        {
            wOut = rc.right;
            hOut = (UINT) round( (double) rc.right / (double) wImg * (double) hImg );
            yOffset = (float) ( rc.bottom - hOut ) / 2.0f;
        }
        else
        {
            hOut = rc.bottom;
            wOut = (UINT) round( (double) rc.bottom / (double) hImg * (double) wImg );
            xOffset = (float) ( rc.right - wOut ) / 2.0f;
        }

        if ( zl_Zoom1 == zoomLevel || zl_Zoom2 == zoomLevel )
        {
            // show the bitmap 1:1 pixel to display pixel, locked on the current mouse location
            // Map the mouse location to the bitmap location

            float mX = (float) mouseX - xOffset;
            float mY = (float) mouseY - yOffset;
            mX = mX / (float) wOut * (float) wImg;
            mY = mY / (float) hOut * (float) hImg;

            float factor = ( zl_Zoom1 == zoomLevel ) ? 1.0f : 2.0f;

            rectangle.left = (float) mouseX - factor * mX;
            rectangle.top = (float) mouseY - factor * mY;
            rectangle.right = rectangle.left + factor * (float) wImg;
            rectangle.bottom = rectangle.top + factor * (float) hImg;
        }
        else
        {
            rectangle.left = xOffset;
            rectangle.top = yOffset;
            rectangle.right = (float) wOut + xOffset;
            rectangle.bottom = (float) hOut + yOffset;
        }

        D2D1_RECT_F rectSource = D2D1::RectF( 0.0f, 0.0f, (float) wImg, (float) hImg );
        D2D1_INTERPOLATION_MODE mode = D2D1_INTERPOLATION_MODE::D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC;
        g_target->DrawBitmap( g_D2DBitmap.Get(), &rectangle, 1.0, mode, &rectSource );

        //tracer.Trace( "drew bitmap from %f %f %f %f to %f %f %f %f\n", rectSource.left, rectSource.top, rectSource.right, rectSource.bottom, rectangle.left, rectangle.top, rectangle.right, rectangle.bottom );

        size_t len = wcslen( g_awcImageMetadata );
        //tracer.Trace( "length of image metadata %d\n", len );

        if ( g_showMetadata && ( 0 != len ) )
        {
            ComPtr<ID2D1SolidColorBrush> brushText;
            hr = g_target->CreateSolidColorBrush( D2D1::ColorF( 0.94f, 0.94f, 0.78f, 1.0f ), brushText.GetAddressOf() );
            D2D1_RECT_F rectText = { 0.0f, 0.0f, (float) rc.right - 4.0f, (float) rc.bottom - 2.0f };
            g_target->DrawText( g_awcImageMetadata, (UINT32) len, g_dwriteTextFormat.Get(), &rectText, brushText.Get() );
        }

        g_target->EndDraw();

        hr = g_swapChain->Present( 1, 0 );

        if ( S_OK != hr && DXGI_STATUS_OCCLUDED != hr )
            ReleaseDevice();

        EndPaint( hwnd, &ps );
    }
} //OnPaint

void SortImages()
{
    if ( si_LastWrite == g_SortImagesBy )
        g_pImageArray->SortOnLastWrite();
    else if ( si_Creation == g_SortImagesBy )
        g_pImageArray->SortOnCreation();
    else if ( si_Path == g_SortImagesBy )
        g_pImageArray->SortOnPath();
} //SortImages

// Each monitor on which the window resides results in a call (not all monitors).
// Use the last one called (which is fine).

BOOL CALLBACK EnumerateMonitors( HMONITOR hMon, HDC hdc, LPRECT rMon, LPARAM lParam )
{
    tracer.Trace( "  enumerate, monitor %p, %d, %d, %d, %d\n", hMon, rMon->left, rMon->top, rMon->right, rMon->bottom );

    RECT * pRect = (RECT *) lParam;
    MONITORINFO mi;
    mi.cbSize = sizeof mi;
    if ( GetMonitorInfo( hMon, &mi ) )
        *pRect = mi.rcMonitor;

    tracer.Trace( "  updated rc %d %d %d %d\n", pRect->left, pRect->top, pRect->right, pRect->bottom );
    return TRUE;
} //EnumerateMonitors

void GetCurrentDesktopRect( HWND hwnd, RECT * pRectDesktop )
{
    RECT rcWin;
    GetClientRect( hwnd, &rcWin );
    ClientToScreen( hwnd, (POINT *) ( &rcWin.left ) );
    ClientToScreen( hwnd, (POINT *) ( &rcWin.right ) );
    tracer.Trace( "rectangle of the window passed to EnumDisplayMonitors %d %d %d %d\n", rcWin.left, rcWin.top, rcWin.right, rcWin.bottom );

    // Safe initialization in case enumeration fails
    GetWindowRect( GetDesktopWindow(), pRectDesktop );

    EnumDisplayMonitors( NULL, &rcWin, EnumerateMonitors, (LPARAM) pRectDesktop );
} //GetCurrentDesktopRect

void DeleteCommand( HWND hwnd )
{
    if ( 0 != g_pImageArray->Count() )
    {
        // the file is being held open and not shared for delete by WIC -- close it.

        g_BitmapSource.Reset();
        g_D2DBitmap.Reset();

        BOOL deleteWorked = DeleteFile( g_pImageArray->Get( g_currentBitmapIndex ) );

        if ( deleteWorked )
        {
            g_pImageArray->Delete( g_currentBitmapIndex );

            if ( g_currentBitmapIndex >= g_pImageArray->Count() )
                g_currentBitmapIndex = 0;
        }
        else
        {
            DWORD err = GetLastError();
            unique_ptr<WCHAR> deleteFailed( new WCHAR[ 100 ] );
            int ret = LoadStringW( NULL, ID_PV_STRING_DELETE_FAILED, deleteFailed.get(), 100 );

            if ( 0 != ret )
            {
                static WCHAR awcBuffer[ 100 ];
                int len = swprintf_s( awcBuffer, _countof( awcBuffer ), deleteFailed.get(), err );

                if ( -1 != len )
                    MessageBoxEx( hwnd, awcBuffer, NULL, MB_OK, 0 );
            }
        }

        // load the current file

        LoadCurrentFileUsingD2D( hwnd );
        InvalidateRect( hwnd, NULL, TRUE );
    }
} //DeleteCommand

LRESULT CALLBACK WindowProc( HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
    const int TIMER_SLIDESHOW_ID = 1;

    static bool firstEraseBackground = true;
    static PVZoomLevel zoomLevel = zl_ZoomFullImage;
    static int mouseX, mouseY;
    static bool slideShowActive = false;
    static WINDOWPLACEMENT savedF11WinPlacement {};
    static bool randomizedYet = false;
    static HMENU hMenu = 0;
    static WCHAR awcBuffer[ 100 ];

    switch ( uMsg )
    {
        case WM_ENTERSIZEMOVE:
        {
            //tracer.Trace( "wm_entersizemove\n" );
            return 0;
        }

        case WM_EXITSIZEMOVE:
        {
            //tracer.Trace( "wm_exitsizemove\n" );
            ResizeSwapChainBitmap( hwnd );
            InvalidateRect( hwnd, NULL, TRUE );
            return 0;
        }

        case WM_COMMAND:
        {
            //tracer.Trace( "wm_command, wParam %d, lParam %d\n", wParam, lParam );

            if ( ID_PV_COPY == wParam )
                CopyCommand( hwnd );
            else if ( ID_PV_DELETE == wParam )
                DeleteCommand( hwnd );
            else if ( ID_PV_SLIDESHOW == wParam )
                SendMessage( hwnd, WM_CHAR, 's', 0 );
            else if ( ID_PV_INFORMATION == wParam )
                SendMessage( hwnd, WM_CHAR, 'i', 0 );
            else if ( ID_PV_MAP == wParam )
                SendMessage( hwnd, WM_CHAR, 'm', 0 );
            else if ( ID_PV_NEXT == wParam )
                SendMessage( hwnd, WM_KEYDOWN, VK_RIGHT, 0 );
            else if ( ID_PV_PREVIOUS == wParam )
                SendMessage( hwnd, WM_KEYDOWN, VK_LEFT, 0 );
            else if ( ID_PV_FULLSCREEN == wParam )
                SendMessage( hwnd, WM_KEYDOWN, VK_F11, 0 );
            else if ( wParam >= ID_PV_SLIDESHOW_ASAP && wParam <= ID_PV_SLIDESHOW_600 )
            {
                int delayIndex = (int) wParam - ID_PV_SLIDESHOW_ASAP;

                g_photoDelay = g_validDelays[ delayIndex ];

                if ( slideShowActive )
                {
                    SendMessage( hwnd, WM_CHAR, 's', 0 );
                    SendMessage( hwnd, WM_CHAR, 's', 0 );
                }
            }
            else if ( wParam >= ID_PV_RAW_ALWAYS && wParam <= ID_PV_RAW_NEVER )
            {
                int processingIndex = (int) wParam - ID_PV_RAW_ALWAYS;
                //tracer.Trace( "changing processraw from %d to %d\n", g_ProcessRAW, processingIndex );
                g_ProcessRAW = (PVProcessRAW) processingIndex;
                LoadCurrentFileUsingD2D( hwnd );
                InvalidateRect( hwnd, NULL, TRUE );
            }
            else if ( wParam >= ID_PV_SORT_LASTWRITE && wParam <= ID_PV_SORT_PATH )
            {
                int sortIndex = (int) wParam - ID_PV_SORT_LASTWRITE;
                //tracer.Trace( "changing SortImagesBy from %d to %d\n", g_SortImagesBy, sortIndex );
                g_SortImagesBy = (PVSortImagesBy) sortIndex;

                if ( g_pImageArray )
                {
                    SortImages();

                    g_currentBitmapIndex = 0;
                    LoadCurrentFileUsingD2D( hwnd );
                    InvalidateRect( hwnd, NULL, TRUE );
                }
            }
            else if ( ID_PV_ROTATE_LEFT == wParam )
                SendMessage( hwnd, WM_CHAR, 'l', 0 );
            else if ( ID_PV_ROTATE_RIGHT == wParam )
                SendMessage( hwnd, WM_CHAR, 'r', 0 );

            break;
        }

        case WM_CREATE:
        {
            hMenu = LoadMenu( NULL, MAKEINTRESOURCE( ID_PV_POPUPMENU ) );
            break;
        }

        case WM_DESTROY:
        {
            if ( 0 != hMenu )
                DestroyMenu( hMenu );

            if ( slideShowActive )
            {
                SetThreadExecutionState( ES_CONTINUOUS );
                slideShowActive = false;
                KillTimer( hwnd, TIMER_SLIDESHOW_ID );
            }

            WINDOWPLACEMENT wp {};

            if ( g_inF11FullScreen )
                memcpy( &wp, &savedF11WinPlacement, sizeof wp );
            else
            {
                wp.length = sizeof wp;
                GetWindowPlacement( hwnd, &wp );
            }
    
            int len = swprintf_s( awcBuffer, _countof( awcBuffer ), L"%d %d %d %d %d %d %d %d %d %d",
                                  wp.flags, wp.showCmd, wp.ptMinPosition.x, wp.ptMinPosition.y, wp.ptMaxPosition.x, wp.ptMaxPosition.y,
                                  wp.rcNormalPosition.left, wp.rcNormalPosition.top, wp.rcNormalPosition.right, wp.rcNormalPosition.bottom );
            if ( -1 != len )
            {
                CDJLRegistry::writeStringToRegistry( HKEY_CURRENT_USER, REGISTRY_APP_NAME, REGISTRY_WINDOW_POSITION, awcBuffer );
                CDJLRegistry::writeStringToRegistry( HKEY_CURRENT_USER, REGISTRY_APP_NAME, REGISTRY_SHOW_METADATA, g_showMetadata ? L"Yes" : L"No" );
                CDJLRegistry::writeStringToRegistry( HKEY_CURRENT_USER, REGISTRY_APP_NAME, REGISTRY_IN_F11_FULLSCREEN, g_inF11FullScreen ? L"Yes" : L"No" );
    
                len = swprintf_s( awcBuffer, _countof( awcBuffer ), L"%d", g_photoDelay );
                if ( -1 != len )
                    CDJLRegistry::writeStringToRegistry( HKEY_CURRENT_USER, REGISTRY_APP_NAME, REGISTRY_PHOTO_DELAY, awcBuffer );
    
                len = swprintf_s( awcBuffer, _countof( awcBuffer ), L"%d", g_ProcessRAW );
                if ( -1 != len )
                    CDJLRegistry::writeStringToRegistry( HKEY_CURRENT_USER, REGISTRY_APP_NAME, REGISTRY_PROCESS_RAW, awcBuffer );
    
                len = swprintf_s( awcBuffer, _countof( awcBuffer ), L"%d", g_SortImagesBy );
                if ( -1 != len )
                    CDJLRegistry::writeStringToRegistry( HKEY_CURRENT_USER, REGISTRY_APP_NAME, REGISTRY_SORT_IMAGES_BY, awcBuffer );
            }

            PostQuitMessage( 0 );
            return 0;
        }

        case WM_TIMER:
        {
            if ( TIMER_SLIDESHOW_ID == wParam )
            {
                LoadNextImage( hwnd, md_Next );
                InvalidateRect( hwnd, NULL, TRUE );
            }

            return 0;
        }

        case WM_DISPLAYCHANGE:
        {
            InvalidateRect( hwnd, NULL, TRUE );
            break;
        }

        case WM_SIZE:
        {
            ResizeSwapChainBitmap( hwnd );
            InvalidateRect( hwnd, NULL, TRUE );
            break;
        }

        case WM_SIZING:
        {
            //tracer.Trace( "wm_sizing message\n" );
            break;
        }

        case WM_CHAR:
        {
            if ( 'q' == wParam || 0x1b == wParam ) // q or ESC
                DestroyWindow( hwnd );
            else if ( 'i' == wParam || ' ' == wParam )
            {
                g_showMetadata = !g_showMetadata;
                InvalidateRect( hwnd, NULL, TRUE );
            }
            else if ( 'l' == wParam || 'r' == wParam )
            {
                if ( 0 != g_pImageArray->Count() )
                {
                    // the file is being held open and not shared for write by WIC -- close it.
    
                    g_BitmapSource.Reset();
                    g_D2DBitmap.Reset();

                    bool ok = CImageRotation::Rotate90ViaExifOrBits( g_IWICFactory.Get(), g_pImageArray->Get( g_currentBitmapIndex ), false, 'r' == wParam, true );

                    if ( !ok )
                    {
                        int ret = LoadStringW( NULL, ID_PV_STRING_ROTATE_FAILED, awcBuffer, _countof( awcBuffer ) );

                        if ( 0 != ret )
                            MessageBoxEx( hwnd, awcBuffer, NULL, MB_OK, 0 );
                    }

                    // regardless of whether the rotate succeeded, reload the file since it was closed above
    
                    LoadCurrentFileUsingD2D( hwnd );
                    InvalidateRect( hwnd, NULL, TRUE );
                }
            }
            else if ( 's' == wParam )
            {
                if ( slideShowActive )
                    KillTimer( hwnd, TIMER_SLIDESHOW_ID );
                else
                {
                    if ( !randomizedYet )
                    {
                        g_pImageArray->Randomize();
                        randomizedYet = true;
                    }

                    SetTimer( hwnd, TIMER_SLIDESHOW_ID, ( 0 == g_photoDelay ) ? 10 : 1000 * g_photoDelay, NULL );
                }

                slideShowActive = !slideShowActive;

                if ( slideShowActive )
                    SetThreadExecutionState( ES_CONTINUOUS | ES_DISPLAY_REQUIRED );
                else
                    SetThreadExecutionState( ES_CONTINUOUS );

            }
            else if ( 'm' == wParam )
            {
                if ( 0 != g_pImageArray->Count() )
                {
                    double lat, lon;

                    if ( g_pImageData->GetGPSLocation( g_pImageArray->Get( g_currentBitmapIndex ), &lat, &lon ) )
                    {
                        //tracer.Trace( "found lat %.7lf and lon %.7lf\n", lat, lon );

                        unique_ptr<WCHAR> mapUrl( new WCHAR[ 100 ] );
                        int ret = LoadStringW( NULL, ID_PV_STRING_MAP_URL, mapUrl.get(), 100 );

                        if ( 0 != ret )
                        {
                            int len = swprintf_s( awcBuffer, _countof( awcBuffer ), mapUrl.get(), lat, lon );

                            if ( -1 != len )
                                ShellExecute( 0, 0, awcBuffer, 0, 0, SW_SHOW );
                        }
                    }
                }
            }
            else if ( 'n' == wParam )
                SendMessage( hwnd, WM_KEYDOWN, VK_RIGHT, 0 );
            else if ( 'p' == wParam )
                SendMessage( hwnd, WM_KEYDOWN, VK_LEFT, 0 );

            //tracer.Trace( "wm_char %#x\n", wParam );
            break;
        }

        case WM_CONTEXTMENU:
        {
            // the values can be negative for multi-mon, so cast the WORDs apprriately

            POINT pt;
            pt.x = (LONG) (short) LOWORD (lParam);
            pt.y = (LONG) (short) HIWORD (lParam);

            if ( -1 == pt.x && -1 == pt.y )
                GetCursorPos( &pt );

            //tracer.Trace( "context menu pt.x: %d, pt.y: %d\n", pt.x, pt.y );

            int delayIndex = 0;
            for ( int i = 0; i < _countof( g_validDelays ); i++ )
            {
                if ( g_validDelays[ i ] == g_photoDelay )
                {
                    delayIndex = i;
                    break;
                }
            }

            CheckMenuRadioItem( GetSubMenu( hMenu, 0 ), ID_PV_SLIDESHOW_ASAP, ID_PV_SLIDESHOW_600, ID_PV_SLIDESHOW_ASAP + delayIndex, MF_BYCOMMAND );
            CheckMenuRadioItem( GetSubMenu( hMenu, 0 ), ID_PV_RAW_ALWAYS, ID_PV_RAW_NEVER, ID_PV_RAW_ALWAYS + (int) g_ProcessRAW, MF_BYCOMMAND );
            CheckMenuRadioItem( GetSubMenu( hMenu, 0 ), ID_PV_SORT_LASTWRITE, ID_PV_SORT_PATH, ID_PV_SORT_LASTWRITE + (int) g_SortImagesBy, MF_BYCOMMAND );

            TrackPopupMenu( GetSubMenu( hMenu, 0 ), TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL );
            break;
        }

        case WM_LBUTTONDOWN:
        {
            if ( GetAsyncKeyState( VK_CONTROL ) & 0x8000 )
                zoomLevel = zl_Zoom2;
            else
                zoomLevel = zl_Zoom1;

            mouseX = GET_X_LPARAM( lParam );
            mouseY = GET_Y_LPARAM( lParam );

            SetCapture( hwnd );
            SetCursor( LoadCursor( NULL, IDC_HAND ) );

            RECT rectCapture;
            GetClientRect( hwnd, &rectCapture );

            POINT ptUL;
            ptUL.x = rectCapture.left;
            ptUL.y = rectCapture.top;

            POINT ptLR;
            ptLR.x = rectCapture.right + 1;
            ptLR.y = rectCapture.bottom + 1;

            ClientToScreen( hwnd, &ptUL );
            ClientToScreen( hwnd, &ptLR );

            SetRect( &rectCapture, ptUL.x, ptUL.y, ptLR.x, ptLR.y );
            ClipCursor( &rectCapture );

            InvalidateRect( hwnd, NULL, TRUE );
            return 0;
        }

        case WM_MOUSEMOVE:
        {
            if ( wParam & MK_LBUTTON )
            {
                mouseX = GET_X_LPARAM( lParam );
                mouseY = GET_Y_LPARAM( lParam );
                InvalidateRect( hwnd, NULL, TRUE );
            }

            break;
        }

        case WM_LBUTTONUP:
        {
            zoomLevel = zl_ZoomFullImage;

            SetCursor( LoadCursor( NULL, IDC_ARROW ) );
            ClipCursor( NULL );
            ReleaseCapture();

            InvalidateRect( hwnd, NULL, TRUE );
            return 0;
        }

        case WM_KEYDOWN:
        {
            if ( ( 0x43 == wParam ) && ( GetKeyState( VK_CONTROL ) & 0x8000 ) ) // ^c for copy
            {
                CopyCommand( hwnd );
            }
            else if ( ( 0x44 == wParam ) && ( GetKeyState( VK_CONTROL ) & 0x8000 ) ) // ^d for delete
            {
                DeleteCommand( hwnd );
            }
            else if ( VK_F1 == wParam )
            {
                HWND helpDialog = CreateDialog( NULL, MAKEINTRESOURCE( ID_PV_HELP_DIALOG ), hwnd, HelpDialogProc );

                ShowWindow( helpDialog, SW_SHOW );
            }
            else if ( VK_F11 == wParam )
            {
                //tracer.Trace( "f11 full screen flip\n" );

                if ( g_inF11FullScreen )
                {
                    DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
                    if ( SW_MAXIMIZE == savedF11WinPlacement.showCmd )
                        style |= WS_MAXIMIZE;
                    SetWindowLongPtr( hwnd, GWL_STYLE, style );

                    SetWindowPlacement( hwnd, &savedF11WinPlacement );
                }
                else
                {
                    savedF11WinPlacement.length = sizeof WINDOWPLACEMENT;
                    GetWindowPlacement( hwnd, &savedF11WinPlacement );

                    RECT rectDesk;
                    GetCurrentDesktopRect( hwnd, &rectDesk );
                    tracer.Trace( "rect desktop: %d %d %d %d\n", rectDesk.left, rectDesk.top, rectDesk.right, rectDesk.bottom );

                    int left = rectDesk.left;
                    int top = rectDesk.top;
                    int dx = rectDesk.right - left;
                    int dy = rectDesk.bottom - top;

                    firstEraseBackground = true;

                    SetWindowLongPtr( hwnd, GWL_STYLE, WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE );
                    SetWindowPos( hwnd, 0, left, top, dx, dy, SWP_FRAMECHANGED );
                }

                g_inF11FullScreen = !g_inF11FullScreen;
            }
            else if ( VK_LEFT == wParam || VK_RIGHT == wParam )
            {
                if ( 0 == g_pImageArray->Count() )
                    break;

                if ( slideShowActive )
                {
                    SetThreadExecutionState( ES_CONTINUOUS );
                    KillTimer( hwnd, TIMER_SLIDESHOW_ID );
                    slideShowActive = false;
                }

                LoadNextImage( hwnd, ( VK_RIGHT == wParam ) ? md_Next : md_Previous );
                InvalidateRect( hwnd, NULL, TRUE );
                return 0;
            }

            break;
        }
        case WM_ERASEBKGND:
        {
            if ( firstEraseBackground || 0 == g_pImageArray->Count() )
            {
                firstEraseBackground = false;
                break;
            }

            return 1;
        }

        case WM_PAINT:
        {
            high_resolution_clock::time_point tA = high_resolution_clock::now();
            OnPaint( hwnd, mouseX, mouseY, zoomLevel );
            high_resolution_clock::time_point tB = high_resolution_clock::now();
            timePaint += duration_cast<std::chrono::nanoseconds>( tB - tA ).count();

            tracer.Trace( "metadata %lld, load %lld (%lld in libraw, %lld in swap, %lld in D2DB), paint %lld\n", timeMetadata, timeLoad, timeLibRaw, timeSwapChain, timeD2DB, timePaint );

            break;
        }
    }

    return DefWindowProc( hwnd, uMsg, wParam, lParam );
} //WindowProc

int WINAPI wWinMain( HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow )
{
    static WCHAR awcPhotoPath[ MAX_PATH + 2 ] = { 0 };
    static WCHAR awcInput[ MAX_PATH ] = { 0 };
    static WCHAR awcStartingPhoto[ MAX_PATH + 2 ] = { 0 };
    static WCHAR awcPos[ 100 ];

    SetProcessDpiAwarenessContext( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );

    bool enableTracer = false;
    bool startSlideshow = false;
    awcPhotoPath[0] = 0;

    {
        int argc = 0;
        LPWSTR * argv = CommandLineToArgvW( GetCommandLineW(), &argc );

        for ( int i = 1; i < argc; i++ )
        {
            tracer.Trace( "argument %d is '%ws'\n", i, argv[ i ] );

            const WCHAR * pwcArg = argv[ i ];
            WCHAR a0 = pwcArg[ 0 ];

            if ( ( L'-' == a0 ) || ( L'/' == a0 ) )
            {
               WCHAR a1 = towlower( pwcArg[1] );

               if ( 't' == a1 )
                   enableTracer = true;
               else if ( 's' == a1 )
                   startSlideshow = true;
            }
            else
            {
                if ( wcslen( pwcArg ) < _countof( awcInput ) )
                    wcscpy_s( awcInput, _countof( awcInput ), pwcArg );
            }
        }

        LocalFree( argv );
    }

    tracer.Enable( enableTracer );

    if ( 0 == awcInput[ 0 ] )
        wcscpy_s( awcInput, _countof( awcInput ), L".\\" );

    WCHAR * pwcResult = _wfullpath( awcPhotoPath, awcInput, _countof( awcPhotoPath ) );
    if ( NULL == pwcResult )
    {
        tracer.Trace( "can't call _wfullpath\n" );
        return 0;
    }

    DWORD attr = GetFileAttributesW( awcPhotoPath );

    if ( INVALID_FILE_ATTRIBUTES != attr )
    {
        if ( 0 == ( attr & FILE_ATTRIBUTE_DIRECTORY ) )
        {
            WCHAR *pslash = wcsrchr( awcPhotoPath, L'\\' );
            if ( NULL != pslash )
            {
                wcscpy_s( awcStartingPhoto, _countof( awcStartingPhoto ), awcPhotoPath );
                *pslash = 0;
            }
        }
    }

    tracer.Trace( "winmain, awcPhotoPath %ws, starting photo %ws\n", awcPhotoPath, awcStartingPhoto );

    LoadRegistryParams();

    g_pImageData = new CImageData();
    g_pImageArray = new CPathArray();

    RECT rectDesk;
    GetWindowRect( GetDesktopWindow(), &rectDesk );
    int fontHeight = rectDesk.bottom / 80;
 
    WINDOWPLACEMENT wp;
    wp.length = sizeof wp;

    BOOL placementFound = CDJLRegistry::readStringFromRegistry( HKEY_CURRENT_USER, REGISTRY_APP_NAME, REGISTRY_WINDOW_POSITION, awcPos, sizeof( awcPos ) );
    if ( placementFound )
    {
        int x = swscanf_s( awcPos, L"%d %d %d %d %d %d %d %d %d %d",
                           &wp.flags, &wp.showCmd, &wp.ptMinPosition.x, &wp.ptMinPosition.y, &wp.ptMaxPosition.x, &wp.ptMaxPosition.y,
                           &wp.rcNormalPosition.left, &wp.rcNormalPosition.top, &wp.rcNormalPosition.right, &wp.rcNormalPosition.bottom );
        if ( 10 != x )
            placementFound = false;
    }

    HBRUSH brushBlack = CreateSolidBrush( RGB( 0, 0, 0 ) );

    const WCHAR CLASS_NAME[] = L"pv-davidly-Class";
    WNDCLASSEX wc = { };
    wc.cbSize        = sizeof wc;
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor( NULL, IDC_ARROW );
    wc.hIcon         = LoadIcon( hInstance, MAKEINTRESOURCE( ID_PV_ICON ) );
    wc.hbrBackground = brushBlack;
    wc.lpszClassName = CLASS_NAME;
    ATOM classAtom = RegisterClassEx( &wc );

    if ( 0 == classAtom )
    {
        tracer.Trace( "can't register windows class, error %d\n", GetLastError() );
        return 0;
    }

    HWND hwnd = CreateWindowEx( 0, CLASS_NAME, L"photo viewer", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInstance, NULL );
    if ( NULL == hwnd )
    {
        tracer.Trace( "can't create window error %d\n", GetLastError() );
        return 0;
    }

    if ( placementFound )
        SetWindowPlacement( hwnd, &wp );

    if ( g_inF11FullScreen )
    {
        g_inF11FullScreen = false;
        SendMessage( hwnd, WM_KEYDOWN, VK_F11, 0 );
    }

    HRESULT hr = CoInitializeEx( NULL, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE );
    if ( FAILED( hr ) )
        return 0;

    hr = CoCreateInstance( CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, __uuidof( IWICImagingFactory ), reinterpret_cast<void **>  ( g_IWICFactory.GetAddressOf() ) );

    if ( FAILED( hr ) )
    {
        tracer.Trace( "can't initialize wic: %#x\n", hr );
        return 0;
    }

    D2D1_FACTORY_OPTIONS options = {};
    hr = D2D1CreateFactory( D2D1_FACTORY_TYPE_SINGLE_THREADED, options, g_ID2DFactory.GetAddressOf() );

    if ( FAILED( hr ) )
    {
        tracer.Trace( "can't initialize d2d: %#x\n", hr );
        return 0;
    }

    hr = DWriteCreateFactory( DWRITE_FACTORY_TYPE_SHARED, __uuidof( g_dwriteFactory ), reinterpret_cast<IUnknown **> ( g_dwriteFactory.GetAddressOf() ) );

    if ( FAILED( hr ) )
    {
        tracer.Trace( "can't create DWrite factory %#x\n", hr );
        return 0;
    }

    hr = g_dwriteFactory->CreateTextFormat( L"Tahoma", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, (float) fontHeight, L"", g_dwriteTextFormat.GetAddressOf() );

    if ( FAILED( hr ) )
    {
        tracer.Trace( "can't create dwrite text format %#x\n", hr );
        return 0;
    }

    g_dwriteTextFormat->SetTextAlignment( DWRITE_TEXT_ALIGNMENT::DWRITE_TEXT_ALIGNMENT_TRAILING );
    g_dwriteTextFormat->SetParagraphAlignment( DWRITE_PARAGRAPH_ALIGNMENT::DWRITE_PARAGRAPH_ALIGNMENT_FAR );

    // Searching for all photos might take a long time. It's not a design pattern for the app to handle this case well.
    // Loading the 114,559 image files on my C:\ takes 2.4 seconds. The drive has 944,094 files total.
    // Loading the 389,076 image files on my D:\ takes 0.4 seconds. The drive has 693,053 files total.
    // Y:\ is the same as D:\ but it's a spinning drive. It took 0.7 seconds.
    // Loading the 59,465 files over a 100Mbps network takes 3.2 seconds. The share is all photos.
    // These times are acceptable given that most use cases will have far fewer files.
    // This won't work well for network and spinning drives with lots of content. Make the enumeration async if needed.

    high_resolution_clock::time_point tA = high_resolution_clock::now();

    CEnumFolder enumFolder( true, g_pImageArray, (WCHAR **) imageExtensions, _countof( imageExtensions ) );
    enumFolder.Enumerate( awcPhotoPath, L"*" );
    SortImages();

    high_resolution_clock::time_point tB = high_resolution_clock::now();
    long long timeFinding = duration_cast<std::chrono::nanoseconds>( tB - tA ).count();
    tracer.Trace( "time finding %d files: %lld\n", g_pImageArray->Count(), timeFinding );

    if ( 0 != awcStartingPhoto[ 0 ] )
        NavigateToStartingPhoto( awcStartingPhoto );

    LoadNextImage( hwnd, md_Stay );

    ShowWindow( hwnd, placementFound ? wp.showCmd : nCmdShow );

    if ( startSlideshow)
        SendMessage( hwnd, WM_CHAR, 's', 0 );

    SetProcessWorkingSetSize( GetCurrentProcess(), ~0, ~0 );

    MSG msg = { };
    while ( GetMessage( &msg, NULL, 0, 0 ) )
    {
        TranslateMessage( &msg );
        DispatchMessage( &msg );
    }

    delete g_pImageArray;
    g_pImageArray = NULL;
    delete g_pImageData;
    g_pImageData = NULL;

    g_dwriteTextFormat.Reset();
    g_dwriteFactory.Reset();
    g_target.Reset();
    g_swapChain.Reset();

    g_D2DBitmap.Reset();
    g_ID2DFactory.Reset();

    g_BitmapSource.Reset();
    g_IWICFactory.Reset();

    CoUninitialize();

    DeleteObject( brushBlack );

    tracer.Trace( "everything is shut down\n" );

    return 0;
} //wWinMain

