
Welcome to have a try of EPump library. Any questions about EPump, please
contact author via kehengzhong@hotmail.com. 

The library EPump can run on most Unix-like system and Windows OS, especially work
better on Linux.

If you get the copy of EPump package on Unix-like system and find the configure
scripts in the top directory have no execute permission, please type the following
commands before getting the library running:

[kehengzhong@localhost epump]$ chmod +x autogen.sh configure ltmain.sh config.* depcomp install-sh

then start the script configure to generate Makefile

[kehengzhong@localhost epump]$ ./configure

After you see the Makefile in current and src directory, make the library:

[kehengzhong@localhost epump]$ make && make install

The new generated EPump libraries will be installed into the default directory /usr/local/lib,
and the header file epump.h is copied to the location /usr/local/include.

After including the header "epump.h", your program can call the APIs provided in it.
Adding the compiler options: -I/usr/local/include -L/usr/local/lib -lepump
you'll be ready to go!

Please refer to the test program for your coding. Further tutorial or documentation
will be coming later. 

Hope you enjoy it!
