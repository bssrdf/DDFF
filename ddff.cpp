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
#include <boost/foreach.hpp>
#include <boost/range/adaptor/map.hpp>
#include <boost/range/adaptors.hpp>

#include "utils.hpp"
#include "sha512.h"

using namespace std;
using namespace std::tr1::placeholders;
using namespace boost::adaptors;

class Node;
typedef set<Node*> Node_group;
FileSize be_sure_all_Nodes_have_same_size_and_return_it(const Node_group & n);
wostream& operator<< (wostream &out, const Node &in);
wostream& operator<< (wostream &out, const Node_group &in);

typedef wstring Partial_hash;
typedef wstring Full_hash;
typedef wstring Dir_group_id;

//class Node : boost::noncopyable, public enable_shared_from_this<Node>
class Node : boost::noncopyable
{
    public:
        Partial_hash memoized_partial_hash; // hash level 2, (SHA512 of first and last 512 bytes) - may be empty
        Full_hash memoized_full_hash; // hash level 3, may be empty
        bool generate_partial_hash();
        bool generate_full_hash();

        Node* parent;
        wstring dir_name; // full path
        wstring file_name; // in case of files
        FileSize size; // always here
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
                return dir_name;
            else
                return dir_name + file_name;
        };

        Node_group children; // (dir only)
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

        bool get_full_hash(Full_hash & out)
        {
            if (memoized_full_hash.size()==0)
                if (generate_full_hash()==false)
                    return false;
            assert (memoized_full_hash.size()!=0);
            out=memoized_full_hash;
            return true;
        };

        bool is_partial_hash_present() const
        {
            return memoized_partial_hash.size()>0;
        };

        bool is_full_hash_present() const
        {
            return memoized_full_hash.size()>0;
        };

        // adding all nodes...
        void add_all_children (map<FileSize, Node_group> & out)
        {
            // there are no unique nodes yet

            if (parent!=NULL) // isn't root node?
            {
                //wcout << WFUNCTION << L"(): pushing info about " << get_name() << " (size " << size << ")" << endl;
                out[size].insert(this);
            };
            if (is_dir)
                for_each(children.begin(), children.end(), bind(&Node::add_all_children, _1, ref(out)));
        };

        // partial hashing occuring here
        // adding only nodes having size_unique=false, key of 'out' is partial hash
        void add_children_for_stage2 (map<Partial_hash, list<Node*>> & out)
        {
            if (!size_unique && parent!=NULL)
            {
                Partial_hash s;
                if (get_partial_hash(s))
                {
                    //wcout << WFUNCTION L"(): pushing info about " << get_name() << endl;
                    out[s].push_back(this);
                }
                else
                {
                    //wcout << WFUNCTION L"(): can't get partial hash for " << get_name() << endl;
                };
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
                Full_hash s;
                if (get_full_hash(s))
                {
                    out[s].insert(this);
                    //wcout << WFUNCTION L"(): pushing info about " << get_name() << endl;
                };
            };

            if (is_dir)
                for_each(children.begin(), children.end(), bind(&Node::add_children_for_stage3, _1, ref(out)));
        };

        void add_all_nonunique_full_hashed_children_only_files (map<Full_hash, Node_group> & out)
        {
            if (!size_unique && !partial_hash_unique && !full_hash_unique && parent!=NULL && !is_dir)
            {
                Full_hash s;
                if (get_full_hash(s))
                    out[s].insert(this);
            };

            if (is_dir)
                for_each(children.begin(), children.end(), bind(&Node::add_all_nonunique_full_hashed_children_only_files, _1, ref(out)));
        };

        void add_all_nonunique_full_hashed_children (map<Full_hash, Node_group> & out)
        {
            if (!size_unique && !partial_hash_unique && !full_hash_unique && parent!=NULL)
            {
                Full_hash s;
                if (get_full_hash(s))
                    out[s].insert(this);
            };

            if (is_dir)
                for_each(children.begin(), children.end(), bind(&Node::add_all_nonunique_full_hashed_children, _1, ref(out)));
        };
};

map<FileSize, set<Node_group>> _add_all_nonunique_full_hashed_children (Node* root)
{
    map<FileSize, set<Node_group>> rt;

    map<Full_hash, Node_group> tmp;
    root->add_all_nonunique_full_hashed_children (tmp);
    BOOST_FOREACH(auto &i, tmp | map_values)
    {
        if (i.size()==1)
        {
            // do not report. this "orphaned" nodes may be here after fuzzy directory comparisons
        }
        else
        {
            FileSize common_size=be_sure_all_Nodes_have_same_size_and_return_it (i);
            rt[common_size].insert (i);
        };
    };
    return rt;
};

FileSize be_sure_all_Nodes_have_same_size_and_return_it(const Node_group & n)
{
    assert (n.size()>0);
    FileSize rt=(*n.begin())->size;
    for (auto &i : n)
        if (i->size!=rt)
        {
            wcout << WFUNCTION << " check failed. all nodes:" << endl;
            wcout << n;
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
        out << " memoized_partial_hash=" << in.memoized_partial_hash;
    };

    if (in.is_full_hash_present())
    {
        out << " memoized_full_hash=" << in.memoized_full_hash;
    };
    
    out << endl;

    return out;
};

bool Node::generate_partial_hash()
{
    if (is_dir)
    {
        struct sha512_ctx ctx;
        sha512_init_ctx (&ctx);
        multiset<Partial_hash> hashes;

        for (auto &i : children)
        {
            Partial_hash tmp;
            if (i->get_partial_hash(tmp)==false)
            {
                //wcout << WFUNCTION << L"() won't compute partial hash for dir or file [" << get_name() << L"]" << endl;
                return false; // this directory cannot be computed!
            };
            hashes.insert(tmp);
        };

        // here we use the fact set<wstring> is already sorted...
        memoized_partial_hash=SHA512_process (hashes);

        //wcout << WFUNCTION << " dir=" << dir_name << " hashes.size()=" << hashes.size() << " memoized_partial_hash=" << memoized_partial_hash << endl;
    }
    else
    {
        if (size_unique)
        {
            //wcout << WFUNCTION << L"() won't compute partial hash for file [" << get_name() << L"] (because it's have unique size" << endl;
            return false;
        };

        //wcout << L"computing partial hash for [" << get_name() << L"]" << endl;
        set_current_dir (dir_name);
        if (partial_SHA512_of_file (file_name, memoized_partial_hash)==false)
        {
            //wcout << WFUNCTION << L"() can't compute partial hash for file [" << get_name() << L"] (file read error?)" << endl;
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
        multiset<Full_hash> hashes;

        for (auto &i : children)
        {
            Full_hash tmp;
            if (i->get_full_hash(tmp)==false)
                return false;
            hashes.insert(tmp);
        };

        // here we use the fact set<wstring> is sorted...
        memoized_full_hash=SHA512_process (hashes);

        //wcout << WFUNCTION << " dir=" << dir_name << " hashes.size()=" << hashes.size() << " memoized_full_hash=" << memoized_full_hash << endl;
    }
    else
    {
        if (size_unique || partial_hash_unique)
            return false;

        //wcout << L"computing full hash for " << get_name() << "\n";
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
        return get_file_size (file_name, size);
    }
    else
    {
        WIN32_FIND_DATA ff;
        HANDLE hfile;

        if (set_current_dir (dir_name)==false)
        {
            wcout << L"cannot change directory to [" << dir_name << "]" << endl;
            return false;
        };

        if ((hfile=FindFirstFile (L"*", &ff))==INVALID_HANDLE_VALUE)
        {
            wcout << L"FindFirstFile() failed" << endl;
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
    
FileSize set_of_Nodes_sum_size(const Node_group & s)
{
    FileSize rt=0;

    for (auto &i : s)
        rt+=i->size;
    return rt;
};

void mark_nodes_having_unique_sizes (Node* root)
{
    //wcout << WFUNCTION << endl;
    map<FileSize, Node_group> stage1; // key is file size

    root->add_all_children (stage1);

    //wcout << L"stage1.size()=" << stage1.size() << endl;

    BOOST_FOREACH(auto &i, stage1 | map_values)
        if (i.size()==1)
        {
            (*i.begin())->size_unique=true;
            //wcout << WFUNCTION << L"() marking as unique: [" << to_mark->get_name() << L"] (unique size " << to_mark->size << L")" << endl;
        };
};

void mark_nodes_having_unique_partial_hashes (Node* root)
{
    map<Partial_hash, list<Node*>> stage2; // key is partial hash
    root->add_children_for_stage2 (stage2);
    //wcout << L"stage2.size()=" << stage2.size() << endl;

    BOOST_FOREACH(auto &i, stage2 | map_values)
        if (i.size()==1)
        {
            (*i.begin())->partial_hash_unique=true;
            //wcout << WFUNCTION << L"() marking as unique (because partial hash is unique): [" << to_mark->get_name() << L"]" << endl;
        };
};

void mark_nodes_with_unique_full_hashes (Node* root)
{
    map<Full_hash, Node_group> stage3; // key is full hash
    root->add_children_for_stage3 (stage3);
    //wcout << L"stage3.size()=" << stage3.size() << endl;

    BOOST_FOREACH(auto &i, stage3 | map_values)
        if (i.size()==1)
        {
            (*i.begin())->full_hash_unique=true;
            //wcout << WFUNCTION << L"() marking as unique: [" << to_mark->get_name() << "] (because full hash is unique)" << endl;
        };
};

void cut_children_for_non_unique_dirs (Node* root)
{
    map<Full_hash, Node_group> stage3; // key is full hash
    root->add_children_for_stage3 (stage3);
    BOOST_FOREACH(auto &node_group, stage3 | map_values)
    {
        if (node_group.size()>1 && (*node_group.begin())->is_dir)
        {
            // * cut unneeded (directory type) nodes for keys with more than only 1 value 
            // (e.g. nodes to be dumped)
            // we just remove children at each node here!
            for (auto &node : node_group)
            {
                //wcout << L"cutting children of node [" << (*l)->get_name() << L"]" << endl;
                node->children.clear();
            };
        };
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

void output_set_wstring_as_multiline (wostream &out, const set<wstring> &in)
{
    std::copy(in.begin(), in.end(), std::ostream_iterator<wstring, wchar_t>(wcout, L"\n"));
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
            out << L"* similar " << (is_dir ? wstring(L"directories") : wstring (L"files"))
                << L" (size " << size_to_string (size) << ")" << endl;
            output_set_wstring_as_multiline (out, equal_files);
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

void work_on_fuzzy_equal_dirs (Node *root, map<FileSize, set<Result*>> & results) 
{
    map<Full_hash, Node_group> groups_of_similar_files;
    root->add_all_nonunique_full_hashed_children_only_files (groups_of_similar_files);

    // key is dir_group, value is set of Nodes for corresponding files
    map<Dir_group_id, set<wstring>> dir_groups_files, dir_groups_names;
    map<Dir_group_id, FileSize> group_size;
    map<Dir_group_id, Node_group> dir_groups_links;

    BOOST_FOREACH(auto &node_group, groups_of_similar_files | map_values)
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

        wstring dir_group=SHA512_process (directories);
        dir_groups_files[dir_group].insert (files.begin(), files.end());
        
        //wcout << WFUNCTION << " inserting these files to dir_groups_files:" << endl;
        //output_set_wstring_as_multiline (wcout, files);

        dir_groups_names[dir_group].insert (directories.begin(), directories.end());
        group_size[dir_group]=group_size[dir_group] + set_of_Nodes_sum_size(node_group);
        dir_groups_links[dir_group].insert (links.begin(), links.end());
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
    BOOST_FOREACH(auto &node_groups, stage4 | map_values)
    {
        for (auto &node_group : node_groups)
        {
            Node* first_node=*(node_group.begin());
            //if (first_node->already_dumped)
            //    continue;

            set<wstring> full_dirfilenames;

            for (auto &node : node_group)
            {
                if (node->already_dumped==false)
                    full_dirfilenames.insert(node->get_name());
            };

            if (full_dirfilenames.size()>1)
            {
                //wcout << WFUNCTION << " ** inserting as a pack:" << endl;
                //output_set_wstring_as_multiline (wcout, full_dirfilenames);
                results[first_node->size].insert (new Result(new Result_equal_files_dirs (first_node->is_dir, first_node->size, full_dirfilenames)));
            };
        };
    };
};

void do_all(set<wstring> dirs)
{
    wstring dir_at_start=get_current_dir();
    Node* root=new Node(NULL, L"\\", L"", true);
 
    for (auto &dir : dirs)
    {
        Node* node=new Node(root, dir, L"", true);
        node->collect_info();
        root->children.insert (node);
    };

    //wcout << L"all info collected" << endl;

    // stage 1: remove all (file) nodes having unique file sizes
    mark_nodes_having_unique_sizes (root);

    // stage 2: remove all file/directory nodes having unique partial hashes
    mark_nodes_having_unique_partial_hashes (root);

    // stage 3: remove all file/directory nodes having unique full hashes
    mark_nodes_with_unique_full_hashes (root);

    cut_children_for_non_unique_dirs (root);

    map<FileSize, set<Result*>> results; // automatically sorted map!

    work_on_fuzzy_equal_dirs (root, results);

    map<FileSize, set<Node_group>> stage4; // here will be size-sorted nodes. key is file/dir size
    stage4=_add_all_nonunique_full_hashed_children (root);
    //wcout << "stage4.size()=" << stage4.size() << endl;
    add_exact_results (stage4, results); 
    wcout << "* results:" << endl;

    // dump results
    BOOST_FOREACH(auto &result_group, results | map_values | reversed)
        for (auto &result : result_group)
            result->dump(wcout);

    // cleanup
    set_current_dir (dir_at_start);

    // we do not free any allocated memory
};

void tests()
{
    sha512_test();

    try
    {
        wstring out1;
        FileSize out4;

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
    set<wstring> dirs;

    if (argc==1)
        dirs.insert (get_current_dir()); // ?
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
        cerr << "bad_alloc caught: " << ba.what() << endl;
    }
    catch (std::exception &s)
    {
        wcout << "std::exception: " << s.what() << endl;
    };

    return 0;
};
