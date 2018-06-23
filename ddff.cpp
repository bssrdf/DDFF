#define BOOST_LIB_DIAGNOSTIC

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
#include <locale>
#include <fstream>
#include <algorithm>
#include <functional>

#include <windows.h>

#include <boost/utility.hpp>
#include <boost/variant.hpp>
#include <boost/range/adaptor/map.hpp>
#include <boost/range/adaptors.hpp>
#include <boost/flyweight.hpp>

#include "utils.hpp"
#include "sha512.h"

using namespace std;
using namespace std::tr1::placeholders;
using namespace boost::adaptors;

class Node;
typedef set<Node*> Node_group;
FileSize be_sure_all_Nodes_have_same_size_and_return_it(const Node_group & n);
wostream& operator<< (wostream &out, const Node &in); // FIXME: make if friend
wostream& operator<< (wostream &out, const Node_group &in);

typedef string Partial_hash;
typedef string Full_hash;
typedef string Dir_group_id;

class Node : boost::noncopyable
{
    public:
        Partial_hash memoized_partial_hash; // hash level 2, (SHA512 of first and last 512 bytes) - may be empty
        Full_hash memoized_full_hash; // hash level 3, may be empty
        Node* parent; // do you really need it? change to bool?
        boost::flyweight<wstring> dir_name; // full path
        boost::flyweight<wstring> file_name; // in case of files
        FileSize size; // always here
        bool is_dir:1; // false - file, true - dir
        bool size_unique:1;
        bool partial_hash_unique:1;
        bool full_hash_unique:1;
        bool already_dumped:1;
        Node_group children; // (for dir only)

        bool generate_partial_hash();
        bool generate_full_hash();

        Node::Node(Node* parent, wstring dir_name, wstring file_name, bool is_dir)
        {
            assert (dir_name[dir_name.size()-1]=='\\');
            this->parent=parent;
            this->dir_name=dir_name;
            this->file_name=file_name;
            this->is_dir=is_dir;
            size=0; // yet
            size_unique=partial_hash_unique=full_hash_unique=false;
            already_dumped=false;
        };

        wstring get_name() const
        {
            if (is_dir)
                return dir_name;
            else
                return wstring(dir_name) + wstring(file_name);
        };

        bool collect_info();
        FileSize get_size() const { return size; };

        bool get_partial_hash(Partial_hash & out)
        {
            if (memoized_partial_hash.size()==0)
                if (generate_partial_hash()==false)
                    return false;
            assert (memoized_partial_hash.size()!=0);
            out=memoized_partial_hash;
            return true;
        };

        Partial_hash get_partial_hash()
        {
            if (memoized_partial_hash.size()==0)
                if (generate_partial_hash()==false)
                {
                    assert (0);
                };
            assert (memoized_partial_hash.size()!=0);
            return memoized_partial_hash;
        };

       bool get_full_hash(Full_hash & out)
        {
            if (memoized_full_hash.size()==0)
                if (generate_full_hash()==false)
                    return false;
            assert (memoized_full_hash.size()!=0);
            out=memoized_full_hash;
            return true;
        };

        Full_hash get_full_hash()
        {
            if (memoized_full_hash.size()==0)
                if (generate_full_hash()==false)
                {
                    assert(0);
                };
            assert (memoized_full_hash.size()!=0);
            return memoized_full_hash;
        };
 
        bool is_partial_hash_present() const
        {
            return memoized_partial_hash.size()>0;
        };

        bool is_full_hash_present() const
        {
            return memoized_full_hash.size()>0;
        };

        // adding all nodes... there are no unique nodes yet
        void add_all_children (map<FileSize, Node_group> & out)
        {
            if (parent!=NULL) // isn't root node?
                out[size].insert(this);
            if (is_dir)
                for_each(children.begin(), children.end(), bind(&Node::add_all_children, _1, ref(out)));
        };

        // partial hashing occuring here
        // adding only nodes having size_unique=false, key of 'out' is partial hash
        void add_children_for_stage2 (map<Partial_hash, Node_group> & out)
        {
            if (!size_unique && parent!=NULL)
            {
                if (generate_partial_hash())
                    out[get_partial_hash()].insert(this);
            };

            if (is_dir)
                for_each(children.begin(), children.end(), bind(&Node::add_children_for_stage2, _1, ref(out)));
        };

        // the stage3 is where full hashing occured
        // add all nodes except...
        // ignore nodes with size_unique=true OR partial_hash_unique=true
        // key of 'out' is full hash
        void add_children_for_stage3 (map<Full_hash, Node_group> & out)
        {
            if (!size_unique && !partial_hash_unique && parent!=NULL)
            {
                if (generate_full_hash())
                    out[get_full_hash()].insert(this);
            };

            if (is_dir)
                for_each(children.begin(), children.end(), bind(&Node::add_children_for_stage3, _1, ref(out)));
        };

        void add_all_nonunique_full_hashed_children_only_files (map<Full_hash, Node_group> & out)
        {
            if (!size_unique && !partial_hash_unique && !full_hash_unique && parent!=NULL && !is_dir)
            {
                if (generate_full_hash())
                    out[get_full_hash()].insert(this);
            };

            if (is_dir)
                for_each(children.begin(), children.end(), bind(&Node::add_all_nonunique_full_hashed_children_only_files, _1, ref(out)));
        };

        void add_all_nonunique_full_hashed_children (map<Full_hash, Node_group> & out)
        {
            if (!size_unique && !partial_hash_unique && !full_hash_unique && parent!=NULL)
            {
                if (generate_full_hash())
                    out[get_full_hash()].insert(this);
            };

            if (is_dir)
                for_each(children.begin(), children.end(), bind(&Node::add_all_nonunique_full_hashed_children, _1, ref(out)));
        };
};

struct is_Node_group_have_size_1
{
    bool operator()( Node_group n ) const { return n.size()==1; }
};

struct is_Node_group_dont_have_size_1
{
    bool operator()( Node_group n ) const { return n.size()!=1; }
};

FileSize set_of_Nodes_sum_size(const Node_group & group);
struct is_Node_group_size_not_zero
{
    bool operator()( Node_group n ) const { return set_of_Nodes_sum_size(n)!=0; }
};

struct is_Node_group_type_dir
{
    bool operator()( Node_group n ) const 
    { 
        bool rt_is_dir=(*n.begin())->is_dir;
        // just to be sure
        for (auto &node : n)
        {
            if (rt_is_dir != node->is_dir)
            {
                wcout << WFUNCTION << L"() not all nodes in Node_group has same is_dir" << endl;
                wcout << n;
                exit(0);
            };
        };
        return rt_is_dir; 
    }
};

map<FileSize, set<Node_group>> _add_all_nonunique_full_hashed_children (Node* root)
{
    map<FileSize, set<Node_group>> rt;

    map<Full_hash, Node_group> tmp;
    root->add_all_nonunique_full_hashed_children (tmp);

    // do not add 1-sized node groups. these "orphaned" nodes may be here after fuzzy directory comparisons
    for(auto &node_group : tmp | map_values | filtered(is_Node_group_dont_have_size_1()))
    {
        FileSize common_size=be_sure_all_Nodes_have_same_size_and_return_it (node_group);
        rt[common_size].insert (node_group);
    };
    return rt;
};

FileSize be_sure_all_Nodes_have_same_size_and_return_it(const Node_group & n)
{
    assert (n.size()>0);
    FileSize rt=(*n.begin())->size;
    if (any_of(n.cbegin(), n.cend(), [&](Node* n){ return n->size!=rt; }))
        {
            wcerr << WFUNCTION << " check failed. all nodes:" << endl;
            wcerr << n;
            exit(0);
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

    if (in.is_partial_hash_present())
    {
        out << " memoized_partial_hash=" << in.memoized_partial_hash.c_str();
    };

    if (in.is_full_hash_present())
    {
        out << " memoized_full_hash=" << in.memoized_full_hash.c_str();
    };
    
    out << endl;

    return out;
};

bool Node::generate_partial_hash()
{
    if (memoized_partial_hash.size()>0)
        return true;

    if (is_dir)
    {
        // can't do set here (there can be multiple files with same content)
        // can't do multiset here (transform() wouldn't work)
        list<Partial_hash> hashes;

        // can partial hash be generated for each children?
        if (all_of (children.cbegin(), children.cend(), [](Node *n) -> bool { return n->generate_partial_hash(); }))
        {
            hashes.insert (hashes.begin(), children.size(), "");
            transform(children.begin(), children.end(), hashes.begin(), [](Node* n) -> Partial_hash { return n->get_partial_hash(); });

            multiset<Partial_hash> sorted_hashes;
            sorted_hashes.insert (hashes.begin(), hashes.end());
            // here we use the fact multiset<Partial_hash> is already sorted...
            memoized_partial_hash=SHA512_process (sorted_hashes);
        }
        else
            return false;
    }
    else
    {
        if (size_unique)
            return false;

        set_current_dir (dir_name);
        return (partial_SHA512_of_file (file_name, memoized_partial_hash));
    };
    return true;
};

bool Node::generate_full_hash()
{
    if (memoized_full_hash.size()>0)
        return true;

    if (is_dir)
    {
        // can't do set here (there can be multiple files with same content)
        // can't do multiset here (transform() wouldn't work)
        list<Full_hash> hashes;

        // can partial hash be generated for each children?
        if (all_of (children.cbegin(), children.cend(), [](Node *n) -> bool { return n->generate_full_hash(); }))
        {
            hashes.insert (hashes.begin(), children.size(), "");
            transform(children.begin(), children.end(), hashes.begin(), [](Node* n) -> Full_hash { return n->get_full_hash(); });

            multiset<Full_hash> sorted_hashes;
            sorted_hashes.insert (hashes.begin(), hashes.end());
            // here we use the fact multiset<Full_hash> is already sorted...
            memoized_full_hash=SHA512_process (sorted_hashes);
        }
        else
            return false;
    }
    else
    {
        if (size_unique || partial_hash_unique)
            return false;

        set_current_dir (dir_name);
        return SHA512_of_file (file_name, memoized_full_hash);
    };
    return true;
};

bool Node::collect_info()
{
    if (is_dir==false)
    {
        set_current_dir (dir_name);
        return get_file_size (file_name, size);
    }
    else
    {
        WIN32_FIND_DATA ff;
        HANDLE hfile;

        if (set_current_dir (dir_name)==false)
            return false;

        if ((hfile=FindFirstFile (L"*", &ff))==INVALID_HANDLE_VALUE)
        {
            wcerr << L"FindFirstFile() failed" << endl;
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
                n=new Node (this, wstring(dir_name) + wstring(ff.cFileName) + wstring(L"\\"), L"", is_dir); 
            }
            else
            { // it is file
                is_dir=false;
                n=new Node (this, wstring(dir_name), wstring(ff.cFileName), is_dir);
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

FileSize set_of_Nodes_sum_size(const Node_group & group)
{
    FileSize rt=0;

    for_each (group.begin(), group.end(), [&](Node *n) { rt=rt+n->size; });
    return rt;
};

void mark_nodes_having_unique_sizes (Node* root)
{
    map<FileSize, Node_group> stage1;
    root->add_all_children (stage1);

    for(auto &node_group : stage1 | map_values | filtered(is_Node_group_have_size_1()))
        (*node_group.begin())->size_unique=true;
};

void mark_nodes_having_unique_partial_hashes (Node* root)
{
    map<Partial_hash, Node_group> stage2; 
    root->add_children_for_stage2 (stage2);
    
    for(auto &node_group : stage2 | map_values | filtered(is_Node_group_have_size_1()))
        (*node_group.begin())->partial_hash_unique=true;
};

void mark_nodes_with_unique_full_hashes (Node* root)
{
    map<Full_hash, Node_group> stage3;
    root->add_children_for_stage3 (stage3);

    for (auto &node_group : stage3 | map_values | filtered(is_Node_group_have_size_1()))
        (*node_group.begin())->full_hash_unique=true;
};

void cut_children_for_non_unique_dirs (Node* root)
{
    map<Full_hash, Node_group> stage3;
    root->add_children_for_stage3 (stage3);

    for(auto &node_group : stage3 | map_values | 
            filtered(is_Node_group_dont_have_size_1()) | 
            filtered(is_Node_group_size_not_zero()) | // should be evaluated before next filtered()
            filtered(is_Node_group_type_dir()))
    {
        // * cut unneeded (directory type) nodes for keys with more than only 1 value 
        // (e.g. nodes to be dumped)
        // we just remove children at each node here!
        for_each (node_group.begin(), node_group.end(), [](Node *n) { n->children.clear(); });
    };
};

wostream& operator<< (wostream &out, const Node_group &in)
{
    out << "Node_group:" << endl;
    for (auto i=in.begin(); i!=in.end(); i++)
        out << *(*i) << endl;
    out << "*** end ***" << endl;
    return out;
};

wstring set_to_string (const set<wstring> & in, wstring sep)
{
    wstring rt;
    for_each (in.begin(), in.end(), [&](wstring s) { rt=rt+s+sep; });
    return rt;
};

class Result_fuzzy_equal_dirs
{
    private:
        set<wstring> directories;
        set<wstring> files;
        FileSize size;
    public:
        Result_fuzzy_equal_dirs (set<wstring> & directories, set<wstring> & files, FileSize size)
        {
            this->directories=directories;
            this->files=files;
            this->size=size;
        };
        void dump(wostream & out)
        {
            out << L"* common files in directories (" << size_to_string(size) << L")" << endl;
            out << L"** directories:" << endl;
            out << set_to_string (directories, L"\n");
            out << L"** files:" << endl;
            out << set_to_string (files, L"\n");
            out << endl;           
        };
};

class Result_equal_files_dirs
{
    private:
        bool is_dir;
        FileSize size;
        set<wstring> equal_files;
    public:
        Result_equal_files_dirs (bool is_dir, FileSize size, set<wstring> equal_files)
        {
            this->is_dir=is_dir;
            this->size=size;
            this->equal_files=equal_files;
        };
        void dump(wostream & out)
        {
            out << L"* equal " << (is_dir ? wstring(L"directories") : wstring (L"files"))
                << L" (size " << size_to_string (size) << ")" << endl;
            out << set_to_string (equal_files, L"\n");
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
                boost::get<Result_fuzzy_equal_dirs*>(result)->dump(out);
            else if (result.which()==1)
                boost::get<Result_equal_files_dirs*>(result)->dump(out);
            else
            {
                assert (0);
            };
        };
};

void work_on_fuzzy_equal_dirs (Node *root, map<FileSize, set<Result*>> & results) 
{
    map<Full_hash, Node_group> groups_of_similar_files;
    root->add_all_nonunique_full_hashed_children_only_files (groups_of_similar_files);

    map<Dir_group_id, set<wstring>> dir_groups_files, dir_groups_names;
    map<Dir_group_id, FileSize> group_size;
    map<Dir_group_id, Node_group> dir_groups_links;

    for (auto &node_group : groups_of_similar_files | map_values)
    {
        set<wstring> directories, files;
        Node_group links;

        // here we work with ONE file laying in different directories
        for (auto &node : node_group)
        {
            directories.insert (node->dir_name);
            files.insert (node->file_name);
            links.insert (node);
        };
        
        if (directories.size()==1) // this mean, some similar files withine ONE directory, do not report
            continue;

        Dir_group_id dir_group=SHA512_process (directories);
        dir_groups_files[dir_group].insert (files.begin(), files.end());
        
        dir_groups_names[dir_group].insert (directories.begin(), directories.end());
        dir_groups_links[dir_group].insert (links.begin(), links.end());
        group_size[dir_group]+=set_of_Nodes_sum_size(node_group);
    };
    
    for (auto &group : dir_groups_files)
    {
        Dir_group_id dir_group_id;
        set<wstring> files;
        tie(dir_group_id, files)=group;

        if (files.size()>2 && group_size[dir_group_id]>0)
        {
            for (auto &node : dir_groups_links[dir_group_id])
                node->already_dumped=true;
            FileSize v_group_size=group_size[dir_group_id];
            Result_fuzzy_equal_dirs* n=new Result_fuzzy_equal_dirs(dir_groups_names[dir_group_id], files, v_group_size);
            results[v_group_size].insert (new Result (n));
        };
    };
};

void add_exact_results (map<FileSize, set<Node_group>> & stage4, map<FileSize, set<Result*>> & results) 
{
    for (auto &node_groups : stage4 | map_values)
        for (auto &node_group : node_groups)
        {
            Node* first_node=*(node_group.begin());

            set<wstring> full_dirfilenames;

            for (auto &node : node_group)
                if (node->already_dumped==false)
                    full_dirfilenames.insert(node->get_name());

            if (full_dirfilenames.size()>1 && first_node->size>0)
                results[first_node->size].insert (new Result(new Result_equal_files_dirs (first_node->is_dir, first_node->size, full_dirfilenames)));
        };
};

#include <boost/filesystem.hpp>
#include <boost/serialization/serialization.hpp>

#include <boost/archive/add_facet.hpp>
#include <boost/archive/detail/utf8_codecvt_facet.hpp>

void do_all(set<wstring> dirs)
{
    const string result_filename="ddff_results.txt";
    locale old_loc;
    locale* utf8_locale = boost::archive::add_facet(
            old_loc, new boost::archive::detail::utf8_codecvt_facet);
   
    wstring dir_at_start=get_current_dir();
    Node* root=new Node(NULL, L"\\", L"", true);
 
    wcout << L"starting with these directories:" << endl;
    wcout << set_to_string (dirs, L"\n");

    wcout << L"(Stage 1/3) Scanning file tree" << endl;
    for (auto &dir : dirs)
    {
        Node* node=new Node(root, dir, L"", true);
        node->collect_info();
        root->children.insert (node);
    };

    // stage 1: remove all (file) nodes having unique file sizes
    mark_nodes_having_unique_sizes (root);

    wcout << L"(Stage 2/3) Computing partial filehashes" << endl;

    // stage 2: remove all file/directory nodes having unique partial hashes
    mark_nodes_having_unique_partial_hashes (root);

    wcout << L"(Stage 3/3) Computing full filehashes" << endl;

    // stage 3: remove all file/directory nodes having unique full hashes
    mark_nodes_with_unique_full_hashes (root);

    cut_children_for_non_unique_dirs (root);

    map<FileSize, set<Result*>> results; // implicitly sorted map!

    work_on_fuzzy_equal_dirs (root, results);

    map<FileSize, set<Node_group>> stage4; // size-sorted nodes
    stage4=_add_all_nonunique_full_hashed_children (root);
    add_exact_results (stage4, results);

    set_current_dir (dir_at_start);
    
    wofstream fout;
    fout.open (result_filename, ios::out);
    fout.imbue(*utf8_locale);

    fout << "* results:" << endl;

    // dump results
    for (auto &result_group : results | map_values | reversed)
        for (auto &result : result_group) 
            result->dump(fout);

    wcout << L"Results saved into " << result_filename.c_str() << " file" << endl; // FIXME .c_str()

    // we do not free any allocated memory
};

void tests()
{
    sha512_test();

    try
    {
        wstring out1;
        FileSize out4;

        //assert (SHA512_of_file (L"tst.mp3", out1)==true);
        //assert (out1==L"3007efc65d0eb370731d770b222e4b7c89f02435085f0bc4ad20c0356fadf562227dffb80d3e1a7a38ef1acad3883504a80ae2f8e36471d87a3e8dfb7c2114c1");

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
    _setmode(_fileno(stderr), _O_U16TEXT);
    locale::global(locale(""));

    //tests();
    set<wstring> dirs;

    wcout << L"Duplicate Directories and Files Finder" << endl;
    wcout << L"-- <dennis@yurichev.com> (" << WDATE << L")" << endl;

    if (argc==1)
    {
       wcout << "Usage: ddff.exe <directory1> <directory2> ... " << endl;
       wcout << "For example: ddff.exe C:\\ D:\\ E:\\" << endl;
       return 0;
    }
    else 
    {
        for (int i=0; i<(argc-1); i++)
        {
            wstring dir=wstring (argv[i+1]);

            if (dir[dir.size()-1]!=L'\\')
                dir+=wstring(L"\\");

            dirs.insert (dir);
        };
    };

    try
    {
        do_all(dirs);
    }
    catch (bad_alloc& ba)
    {
        wcerr << "bad_alloc caught: " << ba.what() << " (out of memory)" << endl;
    }
    catch (exception &s)
    {
        wcerr << "exception: " << s.what() << endl;
    };

    return 0;
};

/* vim: set expandtab ts=4 sw=4 : */
