#pragma once

#define WIDEN2(x) L ## x
#define WIDEN(x) WIDEN2(x)
#define WFILE WIDEN(__FILE__)
#define WFUNCTION WIDEN(__FUNCTION__)

#include <wchar.h>

#include <string>
#include <set>
#include <list>

using namespace std;

typedef DWORD64 FileSize;

wstring wstrfmt (const wchar_t * szFormat, ...);
bool get_file_size (wstring name, FileSize & out);
wstring get_current_dir ();
bool set_current_dir(wstring dir);

void SHA512_process (struct sha512_ctx *ctx, string s);
void SHA512_process (struct sha512_ctx *ctx, set<string> s);
string SHA512_process (multiset<string> s);
string SHA512_process (list<string> s);
string SHA512_process (set<string> s);
string SHA512_process (set<wstring> s);
string SHA512_finish_and_get_result (struct sha512_ctx *ctx);
bool SHA512_of_file (wstring fname, string & out);
bool partial_SHA512_of_file (wstring name, string & out);

void sha512_test();
void sha1_test();
wstring size_to_string (FileSize i);
bool NTFS_stream_get_info_if_exist (wstring fname, FILETIME & ft_out, string & hash_out);
void NTFS_stream_save_info (wstring fname, FILETIME ft, string info);

