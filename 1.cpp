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
#include <boost/variant.hpp>

#include "utils.hpp"
#include "sha512.h"

using namespace std;
using namespace std::tr1::placeholders;

class Node;
uint64_t be_sure_all_Nodes_have_same_size_and_return_it(const set<Node*> & n);
wostream& operator<< (wostream &out, const Node &in);
wostream& operator<< (wostream &out, const set<Node*> &in);

class Node : boost::noncopyable, public enable_shared_from_this<Node>
{
    public:
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
        bool already_dumped;

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
            already_dumped=false;
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

        bool is_full_hash_present()
        {
            return memoized_full_hash.size()>0;
        };

        // adding all files...
        void add_all_children (map<DWORD64, list<Node*>> & out)
        {
            // there are no unique nodes yet

            if (parent!=NULL) // this is not root node!
            {
                wcout << WFUNCTION << L"(): pushing info about " << get_name() << " (size " << size << ")" << endl;
                out[size].push_back(this);
            };
            if (is_dir)
                for_each(children.begin(), children.end(), bind(&Node::add_all_children, _1, ref(out)));
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
            if (size_unique==false && partial_hash_unique==false && parent!=NULL)
            {
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

        void add_all_unique_full_hashed_children_only_files (map<wstring, set<Node*>> & out)
        {
            if (!size_unique && !partial_hash_unique && !full_hash_unique && parent!=NULL && !is_dir)
            {
                wstring s;
                if (get_full_hash(s))
                {
                    out[s].insert(this);
                    wcout << WFUNCTION L"(): pushing info about " << get_name() << endl;
                };
            };

            if (is_dir)
                for_each(children.begin(), children.end(), bind(&Node::add_all_unique_full_hashed_children_only_files, _1, ref(out)));
        };

        void add_all_unique_full_hashed_children (map<wstring, set<Node*>> & out)
        {
            wcout << WFUNCTION L"(map<wstring, set<Node*>>): begin" << endl;
            if (!size_unique && !partial_hash_unique && !full_hash_unique && parent!=NULL)
            {
                wstring s;
                if (get_full_hash(s))
                {
                    out[s].insert(this);
                    wcout << WFUNCTION L"(map<wstring, set<Node*>>): pushing info about " << get_name() << endl;
                    assert (out[s].size()>0);
                };
            };

            if (is_dir)
                for_each(children.begin(), children.end(), bind(&Node::add_all_unique_full_hashed_children, _1, ref(out)));
        };
};

void _add_all_unique_full_hashed_children (Node* root, map<DWORD64, set<set<Node*>>> & out)
{
    wcout << WFUNCTION L"(): begin" << endl;

    map<wstring, set<Node*>> tmp;
    root->add_all_unique_full_hashed_children (tmp);
    wcout << WFUNCTION << "(map<DWORD64, set<set<Node*>>>) tmp.size()=" << tmp.size() << endl;
    wcout << "info about root:" << endl;
    wcout << *root;
    for (auto i=tmp.begin(); i!=tmp.end(); i++)
    {
        assert ((*i).second.size()>0);
        uint64_t common_size=be_sure_all_Nodes_have_same_size_and_return_it ((*i).second);
        out[common_size].insert ((*i).second);
        wcout << WFUNCTION << "(map<DWORD64, set<set<Node*>>>) inserting pack, common_size=" << common_size << endl; 
    };
};

uint64_t be_sure_all_Nodes_have_same_size_and_return_it(const set<Node*> & n)
{
    assert (n.size()>0);
    uint64_t rt=(*n.begin())->size;
    for (auto i=n.begin(); i!=n.end(); i++)
    {
        if ((*i)->size!=rt)
        {
            wcout << WFUNCTION << " check failed. all nodes:" << endl;
            wcout << n;
            exit(0);
        };
    };
    return rt;
};

wostream& operator<< (wostream &out, const Node &in)
{
    out << "Node. size=" << in.size << " ";

    if (in.is_dir)
        out << "directory. dir_name=" << in.dir_name;
    else
        out << "file. dir_name=" << in.dir_name << " file_name=" << in.file_name;
    
    out << " size_unique=" << in.size_unique << " partial_hash_unique=" << in.partial_hash_unique << 
        " full_hash_unique=" << in.full_hash_unique;

    if (in.memoized_partial_hash.size()>0)
        out << " memoized_partial_hash=" << in.memoized_partial_hash;
    if (in.memoized_full_hash.size()>0)
        out << " memoized_full_hash=" << in.memoized_full_hash;
    
    out << endl;

    return out;
};

bool Node::generate_partial_hash()
{
    if (is_dir)
    {
        struct sha512_ctx ctx;
        sha512_init_ctx (&ctx);
        multiset<wstring> hashes;

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

        // here we use the fact set<wstring> is already sorted...
        memoized_partial_hash=SHA512_process_multiset_of_wstrings (hashes);

        wcout << WFUNCTION << " dir=" << dir_name << " hashes.size()=" << hashes.size() 
            << " memoized_partial_hash=" << memoized_partial_hash << endl;
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
    if (is_dir)
    {
        struct sha512_ctx ctx;
        sha512_init_ctx (&ctx);
        multiset<wstring> hashes;

        for (auto i=children.begin(); i!=children.end(); i++)
        {
            wstring tmp;
            if ((*i)->get_full_hash(tmp)==false)
                return false;
            hashes.insert(tmp);
        };

        // here we use the fact set<wstring> is sorted...
        memoized_full_hash=SHA512_process_multiset_of_wstrings (hashes);

        wcout << WFUNCTION << " dir=" << dir_name << " hashes.size()=" << hashes.size() 
            << " memoized_full_hash=" << memoized_full_hash << endl;
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
                n=new Node (this, dir_name + wstring(ff.cFileName) + wstring(L"\\"), L"", is_dir); 
            }
            else
            { // it is file
                is_dir=false;
                n=new Node (this, dir_name, wstring(ff.cFileName), is_dir);
            };

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

void mark_nodes_with_unique_sizes (Node* root)
{
    wcout << WFUNCTION << endl;
    map<DWORD64, list<Node*>> stage1; // key is file size

    root->add_all_children (stage1);

    wcout << L"stage1.size()=" << stage1.size() << endl;

    for (auto i=stage1.begin(); i!=stage1.end(); i++)
        if ((*i).second.size()==1)
        {
            Node* to_mark=(*i).second.front();
            wcout << WFUNCTION << L"() marking as unique: [" << to_mark->get_name() << L"] (unique size " << to_mark->size << L")" << endl;
            to_mark->size_unique=true;
        };
};

void mark_nodes_with_unique_partial_hashes (Node* root)
{
    wcout << WFUNCTION << endl;
    map<wstring, list<Node*>> stage2; // key is partial hash
    root->add_children_for_stage2 (stage2);
    wcout << L"stage2.size()=" << stage2.size() << endl;

    for (auto i=stage2.begin(); i!=stage2.end(); i++)
        if ((*i).second.size()==1)
        {
            Node* to_mark=(*i).second.front();
            wcout << WFUNCTION << L"() marking as unique (because partial hash is unique): [" << to_mark->get_name() << L"]" << endl;
            to_mark->partial_hash_unique=true;
        };
};

void mark_nodes_with_unique_full_hashes (Node* root)
{
    wcout << WFUNCTION << endl;
    map<wstring, list<Node*>> stage3; // key is full hash
    root->add_children_for_stage3 (stage3);
    wcout << L"stage3.size()=" << stage3.size() << endl;

    for (auto i=stage3.begin(); i!=stage3.end(); i++)
        if ((*i).second.size()==1)
        {
            Node* to_mark=(*i).second.front();
            wcout << WFUNCTION << L"() marking as unique: [" << to_mark->get_name() << "] (because full hash is unique)" << endl;
            to_mark->full_hash_unique=true;
        };
};

void cut_children_for_non_unique_dirs (Node* root)
{
    map<wstring, list<Node*>> stage3; // key is full hash
    root->add_children_for_stage3 (stage3);
    for (auto i=stage3.begin(); i!=stage3.end(); i++)
    {
        if (i->second.size()>1 && i->second.front()->is_dir)
        {
            // * cut unneeded (directory type) nodes for keys with more than only 1 value 
            // (e.g. nodes to be dumped)
            // we just remove children at each node here!
            for (auto l=i->second.begin(); l!=i->second.end(); l++)
            {
                wcout << L"cutting children of node [" << (*l)->get_name() << L"]" << endl;
                (*l)->children.clear();
            };
        };
    };
};

wostream& operator<< (wostream &out, const set<wstring> &in)
{
    std::copy(in.begin(), in.end(), std::ostream_iterator<wstring, wchar_t>(wcout, L" "));
    return out;
};

wostream& operator<< (wostream &out, const set<Node*> &in)
{
    out << "set<Node*>:" << endl;
    for (auto i=in.begin(); i!=in.end(); i++)
        out << *(*i) << endl;
    out << "*** end ***" << endl;
    return out;
};

void output_set_wstring_as_multiline (wostream &out, const set<wstring> &in)
{
    std::copy(in.begin(), in.end(), std::ostream_iterator<wstring, wchar_t>(wcout, L"\n"));
};

class Result_fuzzy_equal_dirs
{
    private:
        set<wstring> directories;
        set<wstring> files;
        DWORD64 size;
    public:
        Result_fuzzy_equal_dirs (set<wstring> & directories, set<wstring> & files, DWORD64 size)
        {
            this->directories=directories;
            this->files=files;
            this->size=size;
        };
        void dump(wostream & out)
        {
            out << L"* common files in directories (" << size_to_string(size) << L")" << endl;
            out << L"** directories:" << endl;
            output_set_wstring_as_multiline (out, directories);
            out << L"** files:" << endl;
            output_set_wstring_as_multiline (out, files);
            out << endl;           
        };
};

class Result_equal_files_dirs
{
    private:
        bool is_dir;
        DWORD64 size;
        set<wstring> strings;
    public:
        Result_equal_files_dirs (bool is_dir, DWORD64 size, set<wstring> strings)
        {
            this->is_dir=is_dir;
            this->size=size;
            this->strings=strings;
        };
        void dump(wostream & out)
        {
            out << L"* similar " << (is_dir ? wstring(L"directories") : wstring (L"files"))
                << L" (size " << size_to_string (size) << ")" << endl;
            output_set_wstring_as_multiline (out, strings);
            out << endl;
        };
};

class Result
{
    private:
        boost::variant<Result_fuzzy_equal_dirs*, Result_equal_files_dirs*> result;
    public:
        Result (Result_fuzzy_equal_dirs*in)
        {
            result=in;
        };
        Result (Result_equal_files_dirs* in)
        {
            result=in;
        };
        void dump(wostream & out)
        {
            if (result.which()==0)
            {
                Result_fuzzy_equal_dirs* tmp=boost::get<Result_fuzzy_equal_dirs*>(result);
                tmp->dump(out);
            }
            else if (result.which()==1)
            {
                Result_equal_files_dirs* tmp=boost::get<Result_equal_files_dirs*>(result);
                tmp->dump(out);
            }
            else
            {
                assert (0);
            };
        };
};

void work_on_fuzzy_equal_dirs (Node *root, map<DWORD64, set<Result*>> & out) 
{
    map<wstring, set<Node*>> groups_of_similar_files;
    root->add_all_unique_full_hashed_children_only_files (groups_of_similar_files);

    // key is dir_group, value is set of Nodes for corresponding files
    map<wstring, set<wstring>> dir_groups_files;
    map<wstring, set<wstring>> dir_groups_names;
    map<wstring, uint64_t> group_size;
    map<wstring, set<Node*>> dir_groups_links;

    for (auto i=groups_of_similar_files.begin(); i!=groups_of_similar_files.end(); i++)
    {
        set<wstring> directories;
        set<wstring> files;
        set<Node*> links;

        // here we work with ONE file laying in different directories
        for (auto l=i->second.begin(); l!=i->second.end(); l++)
        {
            directories.insert ((*l)->dir_name);
            files.insert ((*l)->file_name);
            links.insert (*l);
        };
        
        if (directories.size()==1) // this mean, some similar files withine ONE directory, do not report
            continue;

        wstring dir_group=SHA512_process_set_of_wstrings (directories);
        dir_groups_files[dir_group].insert (files.begin(), files.end());
        dir_groups_names[dir_group].insert (directories.begin(), directories.end());
        group_size[dir_group]=group_size[dir_group] + set_of_Nodes_sum_size(i->second);
        dir_groups_links[dir_group].insert (links.begin(), links.end());
    };
    
    for (auto g=dir_groups_files.begin(); g!=dir_groups_files.end(); g++)
    {
        if (g->second.size()>2 && group_size[g->first]>0)
        {
            for (auto q=dir_groups_links[g->first].begin(); q!=dir_groups_links[g->first].end(); q++)
                (*q)->already_dumped=true;
            /*
            wcout << L"* common files in directories (" << size_to_string(group_size[g->first]) << ")" << endl;
            wcout << L"** directories:" << endl;
            output_set_wstring_as_multiline (wcout, dir_groups_names[g->first]);
            wcout << L"** files:" << endl;
            output_set_wstring_as_multiline (wcout, g->second);
            wcout << endl;
            */
            DWORD64 group_sz=group_size[g->first];
            Result_fuzzy_equal_dirs* n=new Result_fuzzy_equal_dirs(dir_groups_names[g->first], g->second, group_sz);
            out[group_sz].insert (new Result (n));
        };
    };
};

void dump_info_stage4 (map<DWORD64, set<set<Node*>>> & stage4, map<DWORD64, set<Result*>> & out) 
{
    for (auto m=stage4.rbegin(); m!=stage4.rend(); m++)
    {
        wcout << WFUNCTION << " workout size=" << (*m).first << endl;
        for (auto i=(*m).second.begin(); i!=(*m).second.end(); i++)
        {
            Node* first=*((*i).begin());
            if (first->already_dumped)
            {
                wcout << WFUNCTION << " already dumped" << endl;
                continue;
            };

            set<wstring> full_dirfilenames;

            //for_each((*i).second.begin(), (*i).second.end(), [](Node* e){ strings.insert(e->get_name()); });
            for (auto j=(*i).begin(); j!=(*i).end(); j++)
                full_dirfilenames.insert((*j)->get_name());

            wcout << WFUNCTION << " ** inserting as a pack:" << endl;
            output_set_wstring_as_multiline (wcout, full_dirfilenames);
            out[first->size].insert (new Result(new Result_equal_files_dirs (first->is_dir, first->size, full_dirfilenames)));

            //wcout << L"* similar " << (first->is_dir ? wstring(L"directories") : wstring (L"files"))
            //    << L" (size " << size_to_string (first->size) << ")" << endl;
            //for_each((*i).second.begin(), (*i).second.end(), [](Node* e){ wcout << e->get_name() << endl; });
            //wcout << endl;
        };
    };
};

void do_all(wstring dir1)
{
    wstring dir_at_start=get_current_dir();
    Node* root=new Node(NULL, L"\\", L"", true);
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
    Node* n2=new Node(root, dir1, L"", true);
    n2->collect_info();
    //root.children.insert (shared_ptr<Node>(&n2)); 
    root->children.insert (n2);

    wcout << L"all info collected" << endl;

    // stage 1: remove all (file) nodes having unique file sizes
    mark_nodes_with_unique_sizes (root);

    // stage 2: remove all file/directory nodes having unique partial hashes
    mark_nodes_with_unique_partial_hashes (root);

    // stage 3: remove all file/directory nodes having unique full hashes
    mark_nodes_with_unique_full_hashes (root);

    cut_children_for_non_unique_dirs (root);

    map<DWORD64, set<Result*>> results; // automatically sorted map!

    work_on_fuzzy_equal_dirs (root, results);

    map<DWORD64, set<set<Node*>>> stage4; // here will be size-sorted nodes. key is file/dir size
    _add_all_unique_full_hashed_children (root, stage4);
    wcout << "stage4.size()=" << stage4.size() << endl;
    dump_info_stage4 (stage4, results); // FIXME: fn to be renamed

    wcout << "* results:" << endl;

    // dump results
    //for_each(results.begin(), results.end(), [](Result* r){ (*r).second->dump(wcout); });
    for (auto i=results.rbegin(); i!=results.rend(); i++)
        for (auto r=(*i).second.begin(); r!=(*i).second.end(); r++)
            (*r)->dump(wcout);

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
