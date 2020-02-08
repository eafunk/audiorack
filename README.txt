Updated Nov. 11, 2019

*** WHAT IS AUDIORACK4? ***

AudioRack(1-3) was a broadcast radio automation system I wrote for Apple
OSX comprised of several components: 
1. arServer, and audio engine, media player and recorder/encoder.
2. ARStudio, and DJ GUI user interface program to control arServer.
3. ARManager, a libarary managment GUI program used to manage and 
   schedule audio content.
4. MySQL database backend

Details of the original project can be found at...
	http://redmountainradio.com/audiorack
... and documatnation for the original version is included in this
project in the docs directory.

Audio engines for other POSIX compliant operating systems matured in the
decade since I started coding AudioRack. I am now moving away from OSX, 
and it's proprietary CoreAudio API, to instead make use of the cross-
platform jack2 audio server system. 

This project is the start of a port of the entire AudioRack system to 
make use of jack2, gstreamer, and hopefully be buildable on any POSIX 
operating system.  It is a huge job, and it needs to start some where. 
That starting point is here with the arserver4, a CLI interface audio 
engine, automation system, and music database client.
 
arserver4 is the evolution the original OS X arserver. Rewriten from C++
back to good old plain C, because that is my preference. Much of the 
originbal design principles from the OS X arserver are being maintained 
with this new version, with some new functiuonality added as it relates 
to extra features available in jack2, and some original functionality, 
such as AudioUnit effects hosting, being removed since jack handles this
sort of things through interconnections to other applications. Some 
other original arserver features have been removed temporarily until I 
have time to re-implement them, such as IAX telephone interfacing. The 
TCP control session protocol of the original arserver has been 
maintained, so much of the old OS X ARStudio GUI continues to be able to
talk to and control this new arServer4 implementation, providing a 
temporary migration/use path until I get around to porting the ARStudio 
and ARManager GUIs from Apple Objective C programs to something more
cross platform (I haven't decided yet: Web app, Vala, etc.)

The original arserver program has been broken up into three programs:
arServer4, and two helper programs: arPlayer4, and arRecorder4.  
arServer4 continues to handle automation, scheduling, playlist queue 
management, and audio mixing functionality. However, arserver no longer 
plays or records any audio.  Instead it instantiats arPlayer instances, 
which connect themselves back to the arServer mixer, via jack, for playing 
file/stream media. On the other side, arServer4 instantiates arRecorder 
instances for capturing and encoding audio from arServer's mix buses, again 
via jack audio interconnections.  A custom control proptocol has been 
implementedon top of jack's midi system to allow arServer to control the 
attached players and recorders. This allows arSrevre to load, play, stop 
and unload players through jack interconnects even though the media 
players are separate programs.  As a bonus, arPlayer4 and arRecorder4 
are independent CLI programs that can be used with other jack programs 
independent of arServer if you wish.  And any jack audio 
source/destrinantion can be routed to/from the arServer mix engine as 
well.  

Documentation is nonexistant other than the source code, and the "help" 
command after you have started arServer4 running and you connect to it's
TCP control socket and issue the "help" command.  The best starting 
point is again the old AudioRack3 documentation, keeping in mind that 
arServer4 is largely an newer implementation of the old arserver3 
referenced in the AUdioRack3 documentation.

In order to handle differences in how various operating systems mounts 
disks, either physical or network shares, changes have been made to the 
underlying music library database structure to handle platform dependent
mount searches paths. A new settings property called "file_prefixes" 
specifies the search paths for database music items that might reside on
disks or network file shares, using BLOB wildcards for matching to the 
disk/mount name. The default value for this setting should be sufficient
for most use.  This is a departure from the original approach where the 
mount search path was specified in the database, not with a setting in 
the program on a partitular computer.  Since different OS platforms can 
have very different disk and share mount points, it make sense for this 
setting to go with the computer, not the database, which might be 
supporting many different operating systems.  The original version also 
didn't support BLOB wildcard searches, which is required to handle mount
locations which include user ID numbers in the mount path on some 
operating systems (I'm looking at you gnome).

So... An existing AudioRack3 database can be updated with the "dbinit" 
command in this new version to accomidate the changes, and allow for 
backward compatibility with existing music libraries which assumed the 
old Apple OS X mount points. You can create an populate a music library 
with the original OS X ARManager GUI, but you will need to issue the 
"dbinit" command to arServer4 after setting arServer4 up, to update the 
database structure for use by arServer4.  The old ARManager can still 
use an updated libraray/database.


*** REQUIRED BUILD LIBRARIES AND C HEADER FILES ***

arPlayer4, arRecorder4, and arServer4 depend on the following libraries 
being installed on your system.  You will ikely need the header files 
installed on your system to build the project as well. This usually 
implies the developer version of these packages must be intsalled:

gstreamer1		including all good, bad and ugly plugins, bad may 
					require manual install.
					
gstreamer-pbutils

jackd 			version 2, and any support libraries/programs for 
					you to set up and manageaudio on your system.

arServer4, in addition, depends on the following libraries:

libdbd-mysql

*** BUILDING ***

Makefiles are very basic at this point.  And there is a top level
Makefile for the whole project.  So to build a functioning system
you need to do the following from the top level project directory:

run the CLI "sudo make" command, and then if there are no errors, 
run "sudo make install" to place the compiled parts into their needed 
places. The install process creates (if needed) a /opt directory on 
the root file system, and a /opt/audiorack subdirectory. The three 
binary files are installed in /opt/audiorack/bin, and support files 
(help.txt, default configurations files, database init files, etc)
are installed in /opt/audiorack/support, from the support directory 
in this project.

	sudo make

	sudo make install

*** RUNNING ***

The following command will run arServer4 using default startup 
configuration found in the /opt/audiorack/support/ directory.

/opt/audiorack/bin/arServer4 

This default configuration will create a .audiorack directory in the 
user's account that runs the program.  This directory is used to store
program settings, and can optionally contain an alternate startup file.
An alternate startup file can contain pre-load configuration flags which
are treated as if the command line options were passed when the program 
was run. Copy the /opt/audiorack/support/user_startup.conf into your new
~/.audiorack directory as ars_startup.conf and edit it to customize this
startup file.

Note that this default launch of arServer4 will run arServer4 in the
background, keep-alive on, standard IO sockets closed.  For debugging, 
run with the -e and -n options to keep stderr open, and to run directly, 
without a running through a secondary "keep-alive/watchdog" process.

To see all the command line options for arServer:
/opt/audiorack/bin/arServer4 ?


Once running, you can connect to the TCP control socket to issue commands
and get responses from arServer4.  The default TCP socket is on port 9550.
So you could connect, for example, using telnet:

tellnet localhost 9550

The help command will show the extensive list of all the control socket
commands.  You will need to set up the database, audio input and output
groups, etc.  The old audiorack documentation should server as a starting
point, but keep in mind that the command for defining audio input and
output properties have been changes a bit from the original version to
work with jack2.  So please also consult the "help" command results to
see the required format in this version.

You will also need a working MySQL or MariaDB server on your computer
or on your network with a working MySQL account that can have sufficient
database priveleges to create and manipulate a datase for audiorack's 
use. You should already be familiar with MySQL enough to have such an 
account already set up, with a password and such, for audiorack to use.

Again, the old OS X GUI applications can be helpful to you if you have 
access to an OS X machine.  Keep in mind that all the audio config. 
panels in the old GUIs will not work properly with the new arServer4 due
to changes for jack2 uses.  And the database/library, if set up using the
old GUIs, will need to be "dbinit" updated by arServer4.


