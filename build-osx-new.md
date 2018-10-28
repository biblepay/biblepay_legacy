Mac OS X Build Instructions and Notes
====================================

This is a newer guide to compile Biblepay on MacOS High Sierra (headless and GUI).

Notes
-----

* Tested on OS X 10.13 (High Sierra) and 10.12 (Sierra) on 64-bit Intel processors only.

* All of the commands should be executed in a Terminal application. The
built-in one is located in `/Applications/Utilities`.

Preparation
-----------

- Install XCode

    xcode-select --install

- Install Homebrew

- Install dependencies

    brew install autoconf automake berkeley-db4 libtool boost miniupnpc openssl pkg-config protobuf libevent

Patching QT
-----------    
    
A big problem comes from QT library. Latest version is now 5.11.0. It has this issue:

- MacOS has a default of 8MB for main thread and 512KB for other threads. This prevents biblepay-qt from loading the 31102 Bible verses used for PoBH. 
See https://bugreports.qt.io/browse/QTBUG-49607

To fix this we need to patch QT and recompile from source. 

One possible way is:

- Install QT normally

    brew install qt

- Find the compressed QT source file in Brew's cache folder

    brew --cache

- Unpack the file (qt-5-11-0.tar.xz in my case) it and find and patch qt5/qtbase/src/corelib/thread/qthread_unix.cpp 

    --- qthread_unix_org.cpp	
    +++ qthread_unix.cpp	
    @@ -65,6 +65,10 @@
     #include <cxxabi.h>
     #endif
     
    +#ifdef Q_OS_MAC
    +#include <sys/resource.h>  // get/setrlimit
    +#endif
    +
     #include <sched.h>
     #include <errno.h>
     
    @@ -702,6 +706,45 @@
         }
     #endif // QT_HAS_THREAD_PRIORITY_SCHEDULING
     
    +#if defined(Q_OS_MAC) && !defined(Q_OS_IOS)
    +    if (d->stackSize == 0) {
    +                // Fix the default (too small) stack size for threads on OS X,
    +                // which also affects the thread pool.
    +                // See also:
    +                // https://bugreports.qt.io/browse/QTBUG-2568
    +                // This fix can also be found in Chromium:
    +                // https://chromium.googlesource.com/chromium/src.git/+/master/base/threading/platform_thread_mac.mm#186
    +        
    +                // The Mac OS X default for a pthread stack size is 512kB.
    +                // Libc-594.1.4/pthreads/pthread.c's pthread_attr_init uses
    +                // DEFAULT_STACK_SIZE for this purpose.
    +                //
    +                // 512kB isn't quite generous enough for some deeply recursive threads that
    +                // otherwise request the default stack size by specifying 0. Here, adopt
    +                // glibc's behavior as on Linux, which is to use the current stack size
    +                // limit (ulimit -s) as the default stack size. See
    +                // glibc-2.11.1/nptl/nptl-init.c's __pthread_initialize_minimal_internal. To
    +                // avoid setting the limit below the Mac OS X default or the minimum usable
    +                // stack size, these values are also considered. If any of these values
    +                // can't be determined, or if stack size is unlimited (ulimit -s unlimited),
    +                // stack_size is left at 0 to get the system default.
    +                //
    +                // Mac OS X normally only applies ulimit -s to the main thread stack. On
    +                // contemporary OS X and Linux systems alike, this value is generally 8MB
    +                // or in that neighborhood.
    +                size_t default_stack_size = 0;
    +                struct rlimit stack_rlimit;
    +                if (pthread_attr_getstacksize(&attr, &default_stack_size) == 0 &&
    +                                 getrlimit(RLIMIT_STACK, &stack_rlimit) == 0 &&
    +                                 stack_rlimit.rlim_cur != RLIM_INFINITY) {
    +                        default_stack_size =
    +                                std::max(std::max(default_stack_size,
    +                                static_cast<size_t>(PTHREAD_STACK_MIN)),
    +                                static_cast<size_t>(stack_rlimit.rlim_cur));
    +                    }
    +                d->stackSize = default_stack_size;
    +            }
    +    #endif
     
         if (d->stackSize > 0) {
     #if defined(_POSIX_THREAD_ATTR_STACKSIZE) && (_POSIX_THREAD_ATTR_STACKSIZE-0 > 0)

- Pack again into the same file qt-5-11-0.tar.xz and replace the one at Brew's cache folder
- Obtain the new sha256 of the file with

    shasum -a 256 qt-5-11-0.tar.xz

- Edit qt brew file replacing the old SHA256 number with this new one.

    brew edit qt
    
- Uninstall "unpatched" QT or it won't be able to recompile, and then install with full compile

    brew uninstall qt
    brew install -build-from-source qt
    
- Wait a few hours to finish building and installing. Brew will read our patched source XZ file from cache and it will not download a new one.  

Building Biblepay (command line and QT GUI)
-------------------------------------------

After this, we have QT patched and ready to build Biblepay-qt. As QT 5.6+ requires C++11 flags we will add an export.

Also, MacOS picks dynamic versions of the libraries by default. We will leave this behavior for QT libraries, but we will force 
the other libs to link statically.

To do so, we need to create a new folder, for example "StaticLibsCopy/" and copy the following libraries there:
libevent.a
libevent_pthreads.a
libminiupnpc.a
libcrypto.a
libssl.a
libdb_cxx-4.8.a
libprotobuf.a
libboost_chrono-mt.a
libboost_thread-mt.a
libboost_program_options-mt.a
libboost_filesystem.a
libboost_system.a

Then we must instruct the linker to search for libraries in that folder. Otherwise, Mac linker will perform a dynamic link.

Finally, we set up the environment variable MACOSX_DEPLOYMENT_TARGET to allow compatibility up to MacOS 10.11 (El Capitan)

    git clone https://github.com/Biblepaypay/Biblepay.git
    cd Biblepay
    export CXXFLAGS=-std=c++11
    export MACOSX_DEPLOYMENT_TARGET=10.11
    ./autogen.sh
    ./configure LDFLAGS="-L/Users/mippl/StaticLibsCopy/"
    make
      
This will compile and link the executables
src/biblepayd  -> biblepay daemon
src/biblepay-cli -> biblepay client (RPC)
src/qt/biblepay-qt -> QT-based GUI client 
      
Packaging, signing and bundling (DMG file)
-------------------------------------------

Finally, to prepare a bundle we need to follow these steps

a. Create an application icon file 
b. Create Info.plist file
c. Create .app bundle structure.
d. All all necessary QT libraries 
e. Sign all binaries with your apple developer certificate for MacOS application.
f. Create dmg file.

All of them are performed automatically by shell script 

chmod +x macOS_bundle.sh
./macOS_bundle.sh

placed at the root of the repository.
You would probably need to edit it if the certificate name or developer changes.

Steps d and e are performed using macdeployqt tool, present in all QT bundles. 
