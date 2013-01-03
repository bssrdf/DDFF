#pragma once

#define WIDEN2(x) L ## x
#define WIDEN(x) WIDEN2(x)
#define WFILE WIDEN(__FILE__)
#define WFUNCTION WIDEN(__FUNCTION__)

#include <wchar.h>

#include <string>
#include <set>

using namespace std;

typedef DWORD64 FileSize;

wstring wstrfmt (const wchar_t * szFormat, ...);
bool get_file_size (wstring name, FileSize & out);
wstring get_current_dir ();
bool set_current_dir(wstring dir);
void SHA512_process_wstring (struct sha512_ctx *ctx, wstring s);
void SHA512_process_set_of_wstrings (struct sha512_ctx *ctx, set<wstring> s);
wstring SHA512_process_multiset_of_wstrings (multiset<wstring> s);
wstring SHA512_process_set_of_wstrings (set<wstring> s);
wstring SHA512_finish_and_get_result (struct sha512_ctx *ctx);
bool SHA512_of_file (wstring fname, wstring & out);
void sha512_test();
void sha1_test();
bool partial_SHA512_of_file (wstring name, wstring & out);
wstring size_to_string (FileSize i);
bool NTFS_stream_get_info_if_exist (wstring fname, FILETIME & ft_out, wstring & hash_out);
void NTFS_stream_save_info (wstring fname, wstring info);

