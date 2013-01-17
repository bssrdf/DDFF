LIBS=kernel32.lib user32.lib libboost_wserialization-vc110-mt-s-1_52.lib
BOOST_LIBS=$(BOOST)stage/lib

# compiling for WinXP: http://blogs.msdn.com/b/vcblog/archive/2012/10/08/10357555.aspx
CL_OPTIONS=/Fo$@ /EHsc /DUNICODE /D_USING_V110_SDK71_ /c /Ox /Zi /I$(BOOST)
LINK_OPTIONS=/SUBSYSTEM:CONSOLE,5.01 /OUT:$@ $(LIBS) /LIBPATH:$(BOOST_LIBS)

ddff.obj: ddff.cpp
	cl.exe ddff.cpp $(CL_OPTIONS)

utils.obj: utils.cpp
	cl.exe utils.cpp $(CL_OPTIONS)

sha512.obj: sha512.cpp
	cl.exe sha512.cpp $(CL_OPTIONS)

u64.obj: u64.c
	cl.exe u64.c $(CL_OPTIONS)

ddff.exe: ddff.obj utils.obj sha512.obj u64.obj
#	link.exe $** /SUBSYSTEM:CONSOLE /DEBUG /PDB:1.pdb $(LINK_OPTIONS)
	link.exe $** /SUBSYSTEM:CONSOLE /DEBUG $(LINK_OPTIONS)

all: ddff.exe

clean:
	del *.exe *.obj *.pdb
