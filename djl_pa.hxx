#pragma once

//
// Wrapper for vector that stores paths and file information
//

#include "djltrace.hxx"

#include <random>

class CPathArray
{
    public:
        struct PathItem
        {
            WCHAR * pwcPath;
            FILETIME ftCreation;
            FILETIME ftLastWrite;
        };

    private:
        vector<PathItem> elements;
        std::mutex mtx;

        static int CompareFT( FILETIME & ftA, FILETIME & ftB )
        {
            // note: invert sort so most recent files come first

            ULARGE_INTEGER ulA, ulB;
            ulA.LowPart = ftA.dwLowDateTime;
            ulA.HighPart = ftA.dwHighDateTime;
            ulB.LowPart = ftB.dwLowDateTime;
            ulB.HighPart = ftB.dwHighDateTime;

            return ( ulA.QuadPart > ulB.QuadPart ) ? -1 : ( ulA.QuadPart < ulB.QuadPart ) ? 1 : 0;
        } //CompareFT

        static int PILastWriteCompare( const void * a, const void * b )
        {
            PathItem *pa = (PathItem *) a;
            PathItem *pb = (PathItem *) b;

            return CompareFT( pa->ftLastWrite, pb->ftLastWrite );
        } //PILastWriteCompare
        
        static int PICreationCompare( const void * a, const void * b )
        {
            PathItem *pa = (PathItem *) a;
            PathItem *pb = (PathItem *) b;

            return CompareFT( pa->ftCreation, pb->ftCreation );
        } //PICreationCompare
        
        static int PIPathCompare( const void * a, const void * b )
        {
            PathItem *pa = (PathItem *) a;
            PathItem *pb = (PathItem *) b;

            return ( wcscmp( pa->pwcPath, pb->pwcPath ) );
        } //PIPathCompare

        void PrintList()
        {
            for ( size_t i = 0; i < Count(); i++ )
            {
                PathItem & e = elements[i];
                tracer.Trace( "path %ws\n", e.pwcPath );

                SYSTEMTIME st;
                ULARGE_INTEGER uli;
                uli.LowPart = e.ftCreation.dwLowDateTime;
                uli.HighPart = e.ftCreation.dwHighDateTime;
                FileTimeToSystemTime( &e.ftCreation, &st );
                tracer.Trace( "    Creation:   %2d-%02d-%04d %2d:%02d:%02d == %#llx\n", st.wMonth, st.wDay, st.wYear, st.wHour, st.wMinute, st.wSecond, uli.QuadPart );

                uli.LowPart = e.ftLastWrite.dwLowDateTime;
                uli.HighPart = e.ftLastWrite.dwHighDateTime;
                FileTimeToSystemTime( &e.ftLastWrite, &st );
                tracer.Trace( "    Last Write: %2d-%02d-%04d %2d:%02d:%02d == %#llx\n", st.wMonth, st.wDay, st.wYear, st.wHour, st.wMinute, st.wSecond, uli.QuadPart );
            }
        } //PrintList
        
    public:
        CPathArray()
        {
        }

        ~CPathArray()
        {
            Clear();
        }

        size_t Count() { return elements.size(); }
        WCHAR * Get( size_t i ) { return elements[ i ].pwcPath; }
        PathItem & GetPathItem( size_t i ) { return elements[ i ]; }
        PathItem & operator[] ( size_t i ) { return elements[ i ]; }

        void Clear()
        {
            for ( size_t i = 0; i < elements.size(); i++ )
            {
                delete elements[ i ].pwcPath;
                elements[ i ].pwcPath = NULL;
            }

            elements.resize( 0 );
        } //Clear

        void Randomize()
        {
            if ( elements.size() <= 1 )
                return;

            std::random_device rd;
            std::mt19937 gen( rd() );
            std::uniform_int_distribution<> distrib( 0, (int) elements.size() - 1 );

            for ( size_t i = 0; i < elements.size() * 2; i++ )
            {
                int a = distrib( gen );
                int b = distrib( gen );

                swap( elements[ a ], elements[ b ] );
            }
        } //Randomize

        void SortOnLastWrite()
        {
            qsort( elements.data(), elements.size(), sizeof PathItem, PILastWriteCompare );
        } //SortOnLastWrite

        void SortOnCreation()
        {
            qsort( elements.data(), elements.size(), sizeof PathItem, PICreationCompare );
        } //SortOnCreation

        void SortOnPath()
        {
            qsort( elements.data(), elements.size(), sizeof PathItem, PIPathCompare );
        } //SortOnPath

        void InvertSort()
        {
            size_t t = 0;
            size_t b = elements.size() - 1;

            while ( t < b )
                swap( elements[ t++ ], elements[ b-- ] );
        } //InvertSort

        void Add( WCHAR * pwc, FILETIME & creation, FILETIME & lastWrite )
        {
            PathItem pi;
            pi.ftCreation = creation;
            pi.ftLastWrite = lastWrite;
            size_t len = 1 + wcslen( pwc );
            pi.pwcPath = new WCHAR[ len ];
            wcscpy_s( pi.pwcPath, len, pwc );

            lock_guard<mutex> lock( mtx );

            elements.push_back( pi );
        } //Add

        void Add( WCHAR * pwc )
        {
            PathItem pi = {};
            size_t len = 1 + wcslen( pwc );
            pi.pwcPath = new WCHAR[ len ];
            wcscpy_s( pi.pwcPath, len, pwc );

            lock_guard<mutex> lock( mtx );

            elements.push_back( pi );
        } //Add

        bool Delete( size_t item )
        {
            tracer.Trace( "deleting CPathArray of size %zu item %zu\n", elements.size(), item );

            if ( item >= elements.size() )
                return false;

            delete elements[ item ].pwcPath;
            elements[ item ].pwcPath = NULL;

            elements.erase( elements.begin() + item );

            tracer.Trace( "after deleting CPathArray item, new size %zu\n", elements.size() );
            return true;
        }
}; //CPathArray

