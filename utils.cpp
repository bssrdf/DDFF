#include <windows.h>

#include <stdio.h>
#include <stdint.h>
#include <wchar.h>
#include <assert.h>

#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <set>
#include <list>

#include "utils.hpp"
#include "sha512.h"

using namespace std;

wstring GetLastError_to_message(DWORD dw) 
{
    wstring rt;

    // Retrieve the system error message for the last-error code

    LPVOID lpMsgBuf;

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR) &lpMsgBuf,
        0, NULL );

    rt=(wchar_t*)lpMsgBuf;
    LocalFree(lpMsgBuf);
    return rt;
};

wstring wstrfmt (const wchar_t * szFormat, ...)
{
    va_list va;
    wstring rt;
    wchar_t * buf;
    int sz, bufsize;

    va_start (va, szFormat);

    sz=_vscwprintf (szFormat, va);
    bufsize=sz+1;

    buf=(wchar_t*)malloc (bufsize*sizeof(wchar_t));

    assert (buf);

    if (_vsnwprintf_s (buf, bufsize, sz, szFormat, va)==-1)
    {
        wprintf (L"%s (%s...)\n", WFUNCTION, szFormat);
        wprintf (L"_vsnwprintf returned -1\n");
        assert (0);
    }
    else
        rt=buf;

    free (buf);

    return rt;
};

bool get_file_size (wstring name, FileSize & out)
{
    HANDLE h=CreateFile(name.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (h==INVALID_HANDLE_VALUE)
    {
        DWORD err=GetLastError();
        //wprintf (L"%s(%s): CreateFile() failed: %s", WFUNCTION, name.c_str(), GetLastError_to_message (err).c_str());
        //wprintf (L"(current dir is %s)\n", get_current_dir ().c_str());
        return false;
    };

    DWORD hi;
    DWORD lo=GetFileSize (h, &hi);
    DWORD last_err=GetLastError();
    bool rt;

    if (lo==INVALID_FILE_SIZE && last_err!=NO_ERROR)
    {
        wcerr << WFUNCTION L"(): lo=INVALID_FILE_SIZE, last_err=0x" << hex << last_err << " " << GetLastError_to_message (last_err) << endl;
        rt=false;
    }
    else
    {
        rt=true;
        out=((DWORD64)hi << 32) | lo;
    };

    CloseHandle (h);

    return rt;
};

wstring get_current_dir ()
{
    wchar_t *cur_dir;
    wstring rt;

    int l=GetCurrentDirectory (0, NULL); // results is in BYTES
    cur_dir=(wchar_t*)malloc (l*sizeof (wchar_t));
    GetCurrentDirectory (l, cur_dir);

    rt=cur_dir;

    free (cur_dir);
    return rt;
};

string SHA512_finish_and_get_result (struct sha512_ctx *ctx)
{
    uint8_t res[64];
    sha512_finish_ctx (ctx, res);

    ostringstream s;

    s << hex; // these are 'sticky' flags
    s.fill('0');

    for (int i=0; i<64; i++)
    {
        s.width (2); // this flag is not 'sticky'
        s << (int) res[i];
   };

    string rt=s.str();
    assert (rt.size()==128);
    return rt;
};

bool NTFS_stream_get_info_if_exist (wstring fname, FILETIME & ft_out, string & hash_out)
{
    HANDLE h=CreateFile(fname.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (h==INVALID_HANDLE_VALUE)
    {
        //wprintf (L"NTFS_streams_get_info_if_exist() can't open file %s\n", fname.c_str());
        return false; // throw exception?
    };

    char buf[1024];
    memset (buf, 0, 1024);
    DWORD actually_read;
    if (ReadFile (h, buf, 1024, &actually_read, NULL)==FALSE)
    {
        //wprintf (L"NTFS_streams_get_info_if_exist() can't read file %s\n", fname.c_str());
        return false; // throw exception?
    };

    istringstream s;
    s.exceptions (ifstream::failbit | ifstream::badbit);
    s.str (buf);

    try
    {
        s >> ft_out.dwLowDateTime >> ft_out.dwHighDateTime >> hash_out;
    }
    catch (ifstream::failure & e)
    {
        wcerr << WFUNCTION << " sname=" << fname << " buf=[" << buf << "]" << endl;
        wcerr << "Exception while reading: " << e.what() << endl;
        CloseHandle (h);
        return false;
    };

    //wcout << "ft_out.dwLowDateTime=" << ft_out.dwLowDateTime << endl;
    //wcout << "ft_out.dwHighDateTime=" << ft_out.dwHighDateTime << endl;
    //wcout << "hash_out=" << hash_out.c_str() << endl;
    //exit(0);

    CloseHandle (h);
    return true;
};

void NTFS_stream_save_info (wstring sname, FILETIME ft, string hash) // or, overwrite
{
    HANDLE h=CreateFile(sname.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (h==INVALID_HANDLE_VALUE)
    {
        //wcout << WFUNCTION << L"() can't open file/NTFS stream " << sname << endl;
        // throw exception?
        return; // do nothing - yet!
    };

    FILETIME LastWriteTime;
    if (GetFileTime (h, NULL, NULL, &LastWriteTime)==FALSE)
    {
        wcerr << WFUNCTION << L"(): GetFileTime failed for stream " << sname << endl;
        return; // do nothing
    };

    ostringstream s;
    s << ft.dwLowDateTime << " " << ft.dwHighDateTime << " " << hash;
    DWORD actually_written;
    string str_to_be_written=s.str();
    if (WriteFile (h, str_to_be_written.c_str(), str_to_be_written.size(), &actually_written, NULL)==FALSE)
    {
        wcerr << WFUNCTION L"() can't write to file/NTFS stream " << sname << endl;
        return; // throw exception?
    };

    CloseHandle (h);

    h=CreateFile(sname.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (h==INVALID_HANDLE_VALUE)
    {
        wcerr << WFUNCTION << L"() can't open file/NTFS stream " << sname << endl;
        //assert (0); // throw exception?
        return; // do nothing - yet!
    };

    if (SetFileTime (h, NULL, NULL, &LastWriteTime)==FALSE)
    {
        wcerr << WFUNCTION << L"(): SetFileTime failed";
        return; // do nothing
    };

    //wprintf (L"%s() done for stream %s\n", WFUNCTION, sname.c_str());
    CloseHandle (h);
};

#define FULL_HASH_BUFSIZE 1024000

bool SHA512_of_file (wstring fname, string & rt)
{
    wstring stream_fname;
    if (fname.size()==1)
        stream_fname=L".\\"+fname;
    else
        stream_fname=fname;

    HANDLE h=CreateFile(fname.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (h==INVALID_HANDLE_VALUE)
    {
        DWORD err=GetLastError();
        wcerr << WFUNCTION << L"() can't open file " << fname << " GetLastError [" <<GetLastError_to_message (err)<< "]" << endl;
        return false; // throw exception?
    };

    FILETIME LastWriteTime;
    if (GetFileTime (h, NULL, NULL, &LastWriteTime)==FALSE)
    {
        wprintf (L"%s() can't get file times of file %s\n", WFUNCTION, fname.c_str());
        return false; // throw exception?
    };

    FILETIME ft_from_stream;
    bool b;
    b=NTFS_stream_get_info_if_exist (stream_fname+L":DDF_FULL_SHA512", ft_from_stream, rt);
    if (b)
    {
        //wprintf (L"%s(): Got full SHA512 from %s file\n", WFUNCTION, fname.c_str());
        if (ft_from_stream.dwLowDateTime==LastWriteTime.dwLowDateTime && ft_from_stream.dwHighDateTime==LastWriteTime.dwHighDateTime)
        {
            // timestamp correct - we'll use that info!
            return true;
        }
        else
        {
            // timestamp is not correct: probably, file was modified after stream info saved!
        };
    };

    struct sha512_ctx ctx;
    sha512_init_ctx (&ctx);

    uint8_t* buf=(uint8_t*)malloc(FULL_HASH_BUFSIZE);

    assert (buf!=NULL);

    memset (buf, 0, FULL_HASH_BUFSIZE);
    DWORD actually_read;

    do
    {
        if (ReadFile (h, buf, FULL_HASH_BUFSIZE, &actually_read, NULL)==FALSE)
        {
            wcerr << WFUNCTION << L"() can't read file " << fname << endl;
            free (buf);
            return false; // throw exception?
        };
        sha512_process_bytes (buf, actually_read, &ctx);
    }
    while (actually_read==FULL_HASH_BUFSIZE);

    CloseHandle (h);

    free (buf);
    rt=SHA512_finish_and_get_result (&ctx);
    NTFS_stream_save_info (stream_fname+L":DDF_FULL_SHA512", LastWriteTime, rt);
    return true;
};

void sha512_test()
{
    struct sha512_ctx ctx;
    string result;
    char *s1="The quick brown fox jumps over the lazy dog";

    sha512_init_ctx (&ctx);
    sha512_process_bytes (s1, strlen(s1), &ctx);
    result=SHA512_finish_and_get_result (&ctx);
    assert (result==string("07e547d9586f6a73f73fbac0435ed76951218fb7d0c8d788a309d785436bbb642e93a252a954f23912547d1e8a3b5ed6e1bfd7097821233fa0538f3db854fee6"));
};

void SHA512_process_string (struct sha512_ctx *ctx, string s)
{
    const char* tmp=s.c_str();
    sha512_process_bytes (tmp, s.size(), ctx);
};

void SHA512_process_wstring (struct sha512_ctx *ctx, wstring s)
{
    const wchar_t* tmp=s.c_str();
    sha512_process_bytes (tmp, s.size()*sizeof(wchar_t), ctx);
};

#define PARTIAL_HASH_BUFSIZE 512

bool partial_SHA512_of_file (wstring fname, string & out)
{
    wstring stream_fname;
    if (fname.size()==1)
        stream_fname=L".\\"+fname;
    else
        stream_fname=fname;
    
    HANDLE h=CreateFile(fname.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (h==INVALID_HANDLE_VALUE)
    {
        DWORD err=GetLastError();
        wcerr << WFUNCTION << L"() can't open file " << fname.c_str() << " " << GetLastError_to_message (err) << endl;
        return false; // throw exception?
    };

    FILETIME LastWriteTime;
    if (GetFileTime (h, NULL, NULL, &LastWriteTime)==FALSE)
    {
        wprintf (L"%s() can't get file times of file %s\n", WFUNCTION, fname.c_str());
        return false; // throw exception?
    };

    FILETIME ft_from_stream;
    bool b;
    b=NTFS_stream_get_info_if_exist (stream_fname+L":DDF_PART_SHA512", ft_from_stream, out);
    if (b)
    {
        //wprintf (L"%s(): Got partial SHA512 from %s file\n", WFUNCTION, fname.c_str());
        if (ft_from_stream.dwLowDateTime==LastWriteTime.dwLowDateTime && ft_from_stream.dwHighDateTime==LastWriteTime.dwHighDateTime)
        {
            //wprintf (L"timestamp correct - we will use that info!\n");
            return true;
        }
        else
        {
            // timestamp is not correct: probably, file was modified after stream info saved!
            //wprintf (L"timestamp wasn't correct: LastWriteTime.dwLowDateTime=%08X, ft_from_stream.dwLowDateTime=%08X\n", LastWriteTime.dwLowDateTime, ft_from_stream.dwLowDateTime);
            //wprintf (L"timestamp wasn't correct: LastWriteTime.dwHighDateTime=%08X, ft_from_stream.dwHighDateTime=%08X\n", LastWriteTime.dwHighDateTime, ft_from_stream.dwHighDateTime);
         };
    };

    struct sha512_ctx ctx;
    sha512_init_ctx (&ctx);

    uint8_t buf[PARTIAL_HASH_BUFSIZE];

    memset (buf, 0, PARTIAL_HASH_BUFSIZE);
    DWORD actually_read;

    FileSize filesize;
    if (get_file_size (fname, filesize)==false)
    {
        wprintf (L"%s(): get_file_size(%s) failed\n", WFUNCTION, fname.c_str());
        return false;
    };

    if (filesize<=512)
    {
        if (ReadFile (h, buf, PARTIAL_HASH_BUFSIZE, &actually_read, NULL)==FALSE)
        {
            wprintf (L"%s() can't read file %s\n", WFUNCTION, fname.c_str());
            return false;
        };
        sha512_process_bytes (buf, actually_read, &ctx);
    }
    else
    {
        if (ReadFile (h, buf, PARTIAL_HASH_BUFSIZE, &actually_read, NULL)==FALSE)
        {
            wprintf (L"%s() can't read file %s\n", WFUNCTION, fname.c_str());
            return false;
        };
        sha512_process_bytes (buf, actually_read, &ctx);

        LONG tmp=0;
        if (SetFilePointer (h, -512, &tmp, FILE_END)==INVALID_SET_FILE_POINTER && GetLastError()!=NO_ERROR)
        {
            DWORD err=GetLastError();
            wcerr << WFUNCTION L"() SetFilePointer failed for " << fname << " (" << GetLastError_to_message (err) << ")" << endl;
            return false;
        };

        if (ReadFile (h, buf, PARTIAL_HASH_BUFSIZE, &actually_read, NULL)==FALSE)
        {
            wprintf (L"%s() can't read file %s\n", WFUNCTION, fname.c_str());
            return false;
        };
        sha512_process_bytes (buf, actually_read, &ctx);
    };

    CloseHandle (h);

    out=SHA512_finish_and_get_result (&ctx);
    NTFS_stream_save_info (stream_fname+L":DDF_PART_SHA512", LastWriteTime, out);
    return true;
};

wstring size_to_string (FileSize i)
{
    if (i>1000000000)
        return wstrfmt (L"~%dG", i/1000000000);
    if (i>1000000)
        return wstrfmt (L"~%dM", i/1000000);
    if (i>1000)
        return wstrfmt (L"~%dk", i/1000);
    return wstrfmt (L"%d", i);
};

bool set_current_dir(wstring dir)
{
    BOOL B=SetCurrentDirectory (dir.c_str());
    if (B==FALSE)
    {
        DWORD err=GetLastError();
        wcerr << L"cannot change directory to [" << dir << L"]" << endl;
        wcerr << GetLastError_to_message (err) << endl;
    };
    return B==TRUE;
};

void SHA512_process (struct sha512_ctx *ctx, set<string> s)
{
    for (string st : s)
        SHA512_process_string (ctx, st);
};

void SHA512_process (struct sha512_ctx *ctx, set<wstring> s)
{
    for (wstring st : s)
        SHA512_process_wstring (ctx, st);
};

string SHA512_process (multiset<string> s)
{
    struct sha512_ctx ctx;
    sha512_init_ctx (&ctx);      
    for (string st : s)
        SHA512_process_string (&ctx, st);
    return SHA512_finish_and_get_result (&ctx);
};

string SHA512_process (list<string> s)
{
    struct sha512_ctx ctx;
    sha512_init_ctx (&ctx);      
    for (string st : s)
        SHA512_process_string (&ctx, st);
    return SHA512_finish_and_get_result (&ctx);
};

string SHA512_process (set<string> s)
{
    struct sha512_ctx ctx;
    sha512_init_ctx (&ctx);      
    SHA512_process (&ctx, s);
    return SHA512_finish_and_get_result (&ctx);
}

string SHA512_process (set<wstring> s)
{
    struct sha512_ctx ctx;
    sha512_init_ctx (&ctx);      
    SHA512_process (&ctx, s);
    return SHA512_finish_and_get_result (&ctx);
}

