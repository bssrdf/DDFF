#include <stdio.h>
#include <assert.h>
#include <io.h>
#include <fcntl.h>

#include <string>
#include <list>
#include <memory>
#include <set>
#include <map>
#include <iostream>
#include <algorithm>
#include <functional>

#include <windows.h>

#include <boost/utility.hpp>

#include "utils.hpp"
#include "sha512.h"

using namespace std;
using namespace std::tr1::placeholders;

class Node : boost::noncopyable, public enable_shared_from_this<Node>
{
    private:
        wstring memoized_partial_hash; // hash level 2, (filesize+SHA512 of first and last 512 bytes) - may be empty
        wstring memoized_full_hash; // hash level 3, may be empty
        bool generate_partial_hash();
        bool generate_full_hash();

    public:
        Node* parent;
        wstring dir_name; // full path
        wstring file_name; // in case of files
        DWORD64 size; // always here
        bool is_dir; // false - file, true - dir
        bool size_unique;
        bool partial_hash_unique;
        bool full_hash_unique;

        Node::Node(Node* parent, wstring dir_name, wstring file_name, bool is_dir)
        {
            if (dir_name[dir_name.size()-1]!='\\')
            {
                wcout << WFUNCTION << " dir_name=" << dir_name << " file_name=" << file_name << " is_dir=" << is_dir << endl;
                assert(0);
            };

            this->parent=parent;
            this->dir_name=dir_name;
            this->file_name=file_name;
            this->is_dir=is_dir;
            size=0; // yet
            size_unique=partial_hash_unique=full_hash_unique=false;
        };
        ~Node()
        {
        };

        wstring get_name() const
        {
            if (is_dir)
                return dir_name /* + L"\\" */;
            else
                return dir_name + file_name;
        };

        set<Node*> children; // (dir only)
        bool collect_info();
        DWORD64 get_size() const
        {
            return size;
        };

        bool get_partial_hash(wstring & out)
        {
            if (memoized_partial_hash.size()==0)
                if (generate_partial_hash()==false)
                    return false;
            assert (memoized_partial_hash.size()!=0);
            out=memoized_partial_hash;
            return true;
        };

        bool get_full_hash(wstring & out)
        {
            if (memoized_full_hash.size()==0)
                if (generate_full_hash()==false)
                    return false;
            assert (memoized_full_hash.size()!=0);
            out=memoized_full_hash;
            return true;
        };

        // adding all files...
        void add_children_for_stage1 (map<DWORD64, list<Node*>> & out)
        {
            // there are no unique nodes yet

            //wprintf (L"add_children_for_stage1 (). name=[%s]\n", name.c_str());
            if (parent!=NULL) // this is not root node!
            {
                wcout << WFUNCTION << L"(): pushing info about " << get_name() << " (size " << size << ")" << endl;
                out[size].push_back(this);
            };
            if (is_dir)
                for_each(children.begin(), children.end(), bind(&Node::add_children_for_stage1, _1, ref(out)));
        };

        // partial hashing occuring here
        // adding only nodes having size_unique=false, key of 'out' is partial hash
        void add_children_for_stage2 (map<wstring, list<Node*>> & out)
        {
            if (size_unique==false && parent!=NULL)
            {
                wstring s;
                if (get_partial_hash(s))
                {
                    wcout << WFUNCTION L"(): pushing info about " << get_name() << endl;
                    out[s].push_back(this);
                }
                else
                {
                    wcout << WFUNCTION L"(): can't get partial hash for " << get_name() << endl;
                };
            };

            if (is_dir)
                for_each(children.begin(), children.end(), bind(&Node::add_children_for_stage2, _1, ref(out)));
        };

        // the stage3 is where full hashing occured
        // add all nodes except...
        // ignore nodes with size_unique=true OR partial_hash_unique=true
        // key of 'out' is full hash
        void add_children_for_stage3 (map<wstring, list<Node*>> & out)
        {
            if (size_unique==false && partial_hash_unique==false)
            {
                //wprintf (L"(add_children_for_stage3) partial_hash_unique=false for [%s]\n", name.c_str());
                wstring s;
                if (get_full_hash(s))
                {
                    out[s].push_back(this);
                    wcout << WFUNCTION L"(): pushing info about " << get_name() << endl;
                };
            };

            if (is_dir)
                for_each(children.begin(), children.end(), bind(&Node::add_children_for_stage3, _1, ref(out)));
        };

        // add all nodes except...
        // ignore nodes with size_unique=true OR partial_hash_unique=true OR full_hash_unique=true
        // key of 'out' is full hash
        void add_children_for_stage4 (map<wstring, list<Node*>> & out)
        {
            if ((size_unique==false) && (partial_hash_unique==false) && (full_hash_unique==false))
            {
                //wprintf (L"(add_children_for_stage4) partial_hash_unique=false for [%s]\n", name.c_str());
                wstring s;
                if (get_full_hash(s))
                {
                    out[s].push_back(this);
                    wcout << WFUNCTION << L"(): pushing info about" << get_name() << endl;
                    assert (wcout.fail()==false);
                    assert (wcout.bad()==false);
                 };
            };

            if (is_dir)
                for_each(children.begin(), children.end(), bind(&Node::add_children_for_stage4, _1, ref(out)));
        };
};

bool Node::generate_partial_hash()
{
    if (is_dir)
    {
        struct sha512_ctx ctx;
        sha512_init_ctx (&ctx);
        set<wstring> hashes;

        for (auto i=children.begin(); i!=children.end(); i++)
        {
            wstring tmp;
            if ((*i)->get_partial_hash(tmp)==false)
            {
                wcout << WFUNCTION << L"() won't compute partial hash for dir or file [" << get_name() << L"]" << endl;
                return false; // this directory cannot be computed!
            };
            hashes.insert(tmp);
        };

        // here we use the fact set<wstring> is sorted...

        memoized_partial_hash=SHA512_process_set_of_wstrings (hashes);
        return true;
    }
    else
    {
        if (size_unique)
        {
            wcout << WFUNCTION << L"() won't compute partial hash for file [" << get_name() << L"] (because it's have unique size" << endl;
            return false;
        };

        wcout << L"computing partial hash for [" << get_name() << L"]" << endl;
        set_current_dir (dir_name);
        if (partial_SHA512_of_file (file_name, memoized_partial_hash)==false)
        {
            wcout << WFUNCTION << L"() can't compute partial hash for file [" << get_name() << L"] (file read error?)" << endl;
            return false; // file open error (not absent?)
        };
    };
    return true;
};

bool Node::generate_full_hash()
{
    //if (partial_hash_unique)
    //    return false;

    if (is_dir)
    {
        struct sha512_ctx ctx;
        sha512_init_ctx (&ctx);
        set<wstring> hashes;

        for (auto i=children.begin(); i!=children.end(); i++)
        {
            wstring tmp;
            if ((*i)->get_full_hash(tmp)==false)
                return false;
            hashes.insert(tmp);
        };

        // here we use the fact set<wstring> is sorted...

        memoized_full_hash=SHA512_process_set_of_wstrings (hashes);

        //wprintf (L"dir %s, T2 hash %s\n", name.c_str(), memoized_partial_hash.c_str());
    }
    else
    {
        if (size_unique || partial_hash_unique)
            return false;

        wcout << L"computing full hash for " << get_name() << "\n";
        set_current_dir (dir_name);
        if (SHA512_of_file (file_name, memoized_full_hash)==false)
            return false; // file read error (or absent)
         //wprintf (L"computed.\n");

        //wprintf (L"file %s, T2 hash %s\n", name.c_str(), memoized_partial_hash.c_str());
    };
    return true;
};


bool Node::collect_info()
{
    if (is_dir==false)
    {
        set_current_dir (dir_name);
        bool rt=get_file_size (file_name, size);
        //wprintf (L"file size for [%s] is %I64d, rt=%d\n", name.c_str(), size, rt);
        return rt;
    }
    else
    {
        WIN32_FIND_DATA ff;
        HANDLE hfile;

        if (set_current_dir (dir_name)==false)
        {
            wcout << L"cannot change directory to [" << dir_name << "]\n";
            return false;
        };

        if ((hfile=FindFirstFile (L"*", &ff))==INVALID_HANDLE_VALUE)
        {
            wprintf (L"FindFirstFile() failed\n");
            return false;
        };

        do
        {
            DWORD att=ff.dwFileAttributes;
            bool is_dir;
            wstring new_name;

            if (att & FILE_ATTRIBUTE_REPARSE_POINT) // do not follow symlinks
                continue;

            Node* n;

            if (att & FILE_ATTRIBUTE_DIRECTORY) 
            { // it's dir
                if (ff.cFileName[0]=='.') // skip subdirectories links
                    continue;

                is_dir=true;
                //new_name=name + wstring(ff.cFileName) + wstring(L"\\");
                n=new Node (this, dir_name + wstring(ff.cFileName) + wstring(L"\\"), L"", is_dir); 
            }
            else
            { // it is file
                is_dir=false;
                //new_name=name + wstring(ff.cFileName);
                n=new Node (this, dir_name, wstring(ff.cFileName), is_dir);
            };

            //Node* n=new Node(this, new_name, is_dir);
            if (n->collect_info())
            {
                children.insert (n);
                size+=n->get_size();
            };
        } 
        while (FindNextFile (hfile, &ff)!=0);

        FindClose (hfile);
        return true;
    };
};
    
uint64_t set_of_Nodes_sum_size(const set<Node*> & s)
{
    uint64_t rt=0;

    for (auto i=s.begin(); i!=s.end(); i++)
        rt+=(*i)->size;
    return rt;
};

void do_all(wstring dir1)
{
    wstring dir_at_start=get_current_dir();
    Node root(NULL, L"\\", L"", true);
    /*
       Node* n=new Node(&root, L"C:\\Users\\Administrator\\Music\\", true);
       n->collect_info();
       root.children.insert (shared_ptr<Node>(n)); // there might be method like "add_child()" 

       Node* n2=new Node(&root, L"C:\\Users\\Administrator\\-cracker's things\\", true);
       n2->collect_info();
       root.children.insert (shared_ptr<Node>(n2)); 
       */

    //Node n2(&root, L"C:\\Users\\Administrator\\Projects\\dupes_locator\\testdir\\", L"", true);
    //Node n2(&root, L"C:\\Users\\Administrator\\-cracker's things\\", true);
    //Node n2(&root, L"C:\\Users\\Administrator\\Music\\queue\\", true);
    //Node n2(&root, L"C:\\Users\\Administrator\\Music\\", L"", true);
    Node n2(&root, dir1, L"", true);
    n2.collect_info();
    //root.children.insert (shared_ptr<Node>(&n2)); 
    root.children.insert (&n2);

    wcout << L"all info collected" << endl;

    // stage 1: remove all (file) nodes having unique file sizes
    wcout << L"stage1" << endl;
    map<DWORD64, list<Node*>> stage1; // key is file size

    root.add_children_for_stage1 (stage1);

    wcout << L"stage1.size()=" << stage1.size() << endl;

    for (auto i=stage1.begin(); i!=stage1.end(); i++)
        if ((*i).second.size()==1)
        {
            Node* to_mark=(*i).second.front();
            wcout << L"(stage1) marking as unique: [" << to_mark->get_name() << L"] (unique size " << to_mark->size << L")" << endl;
            to_mark->size_unique=true;
        };

    // stage 2: remove all file/directory nodes having unique partial hashes
    wcout << L"stage2" << endl;
    map<wstring, list<Node*>> stage2; // key is partial hash
    root.add_children_for_stage2 (stage2);
    wcout << L"stage2.size()=" << stage2.size() << endl;

    for (auto i=stage2.begin(); i!=stage2.end(); i++)
        if ((*i).second.size()==1)
        {
            Node* to_mark=(*i).second.front();
            wcout << L"(stage2) marking as unique (because partial hash is unique): [" << to_mark->get_name() << L"]" << endl;
            to_mark->partial_hash_unique=true;
        };
    wcout << endl;

    // stage 3: remove all file/directory nodes having unique full hashes
    wcout << L"stage3" << endl;
    map<wstring, list<Node*>> stage3; // key is full hash
    root.add_children_for_stage3 (stage3);
    wcout << L"stage3.size()=" << stage3.size() << endl;

    wcout << L"beginning scaning stage3 tbl" << endl;
    for (auto i=stage3.begin(); i!=stage3.end(); i++)
        if ((*i).second.size()==1)
        {
            Node* to_mark=(*i).second.front();
            wcout << L"(stage3) marking as unique: [" << to_mark->get_name() << "] (because full hash is unique)" << endl;
            to_mark->full_hash_unique=true;
        }
        else if ((*i).second.size()>1 && (*i).second.front()->is_dir)
        {
            // * cut unneeded (directory type) nodes for keys with more than only 1 value 
            // (e.g. nodes to be dumped)
            // we just remove children at each node here!
            for (auto l=(*i).second.begin(); l!=(*i).second.end(); l++)
            {
                wcout << L"cutting children of node [" << (*l)->get_name() << L"]" << endl;
                (*l)->children.clear();
            };
        };
    wcout << endl;

    // stage 4: dump what left
    wcout << L"stage4" << endl;
    map<wstring, list<Node*>> stage4; // key is full hash
    root.add_children_for_stage4 (stage4); // add only nodes with all uniques (size, partial hash, full hash)
    wcout << L"stage4.size()=" << stage4.size() << endl;

    // collect all "directory groups"

    // TODO

    map<DWORD64, list<Node*>> stage5; // here will be size-sorted nodes. key is file/dir size

    for (auto i=stage4.begin(); i!=stage4.end(); i++)
    {
        if ((*i).second.front()->size==0) // skip zero-length files and directories
            continue;

        // check if all elements has equal size and same type!
        for (auto l=(*i).second.begin();  l!=(*i).second.end(); l++)
        {
            assert ((*l)->is_dir==(*i).second.front()->is_dir);
            assert ((*l)->size==(*i).second.front()->size);
        };

        stage5[(*i).second.front()->size]=(*i).second;
    };

    for (auto i=stage5.rbegin(); i!=stage5.rend(); i++)
    {
        Node* first=(*i).second.front();

        wcout << L"* similar " << (first->is_dir ? wstring(L"directories") : wstring (L"files"))
            << L" (size " << size_to_string (first->size) << ")" << endl;

        for (auto l=(*i).second.begin();  l!=(*i).second.end(); l++)
            wcout << L"[" << (*l)->get_name() << L"]" << endl;
    };

    // cleanup
    set_current_dir (dir_at_start);
};

void tests()
{
    sha512_test();
    sha1_test();

    try
    {
        wstring out1;
        DWORD64 out4;

        assert (SHA512_of_file (L"tst.mp3", out1)==true);
        assert (out1==L"3007efc65d0eb370731d770b222e4b7c89f02435085f0bc4ad20c0356fadf562227dffb80d3e1a7a38ef1acad3883504a80ae2f8e36471d87a3e8dfb7c2114c1");

        get_file_size (L"10GB_empty_file", out4);
        assert (out4==10737418240);
    }
    catch (bad_alloc& ba)
    {
        cerr << "bad_alloc caught: " << ba.what() << endl;
    }
    catch (std::exception &s)
    {
        cerr << "std::exception: " << s.what() << endl;
    };
};

int wmain(int argc, wchar_t** argv)
{
    _setmode(_fileno(stdout), _O_U16TEXT);

    //tests();
    wstring dir1;

    if (argc==1)
        dir1=get_current_dir();
    else if (argc==2)
        dir1=wstring (argv[1]);

    if (dir1[dir1.size()-1]!=L'\\')
        dir1+=wstring(L"\\");

    wcout << L"dir1=" << dir1 << endl;

    try
    {
        do_all(dir1);
    }
    catch (bad_alloc& ba)
    {
        cerr << "bad_alloc caught: " << ba.what() << endl;
    }
    catch (std::exception &s)
    {
        wcout << "std::exception: " << s.what() << endl;
    };

    return 0;
};
