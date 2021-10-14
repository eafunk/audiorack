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
referenced in the AudioRack3 documentation.

In order to handle differences in how various operating systems mounts 
disks, either physical or network shares, changes have been made to the 
underlying music library database structure to handle platform dependent
mount searches paths. A new arServer settings property called "file_prefixes" 
specifies the search paths for database music items that might reside on
disks or network file shares, using BLOB wildcards for matching to the 
disk/mount name. The default value for this setting should be sufficient
for most use. This is a departure from the original approach where the 
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


*** REQUIRED BUILD LIBRARIES, PROGRAMS, AND C HEADER FILES ***

arPlayer4, arRecorder4, and arServer4 depend on the following libraries 
being installed on your system.  You will likely need the header files 
installed on your system to build the project as well. This usually 
implies the developer version of these packages must be intsalled:

gstreamer1		including all good, bad and ugly plugins, bad may 
					require manual install.
					
gstreamer-pbutils

jackd 			version 2, and any support libraries/programs for 
					you to set up and manage audio on your system. I
					recommend you have the Carla program installed to 
					manage audio processing with jack (Carla is cross-
					platform).

arServer4, in addition, depends on the following libraries:

libmysqlclient

The User Interface Server requires the following programs to be installed:

nodejs	The node.js runtime system

npm		The node.js package manager

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

First, you must have the jack-audio server (jackd) up and running on the
computer that will be running arServer. There are many tools help
with this, ranging from launching jackd from the commandline, to various
GUI programs.  On a linux system, The UbuntuStudio tools are nice, and
allow for launch at login, and auto-magic support for more than one audio
device, with added latency for the extra devices of course.

The following command will run arServer4 using default startup 
configuration found in the /opt/audiorack/support/ directory.

/opt/audiorack/bin/arServer4 

This default configuration will create a .audiorack directory in the 
user's account that runs the program.  This directory is used to store
program settings, and on first run, will have a copy of the startup file
/opt/audiorack/support/user_startup.conf copied into it as ars_startup.conf.
You can edit the .audiorack/ars_startup.conf file to customize it. At 
startup, an empty triggers directory is created if it doesn't already 
exist in the .audiorack directory, in which you can place command files 
that are executed at start and stop of mute busses, inputs with matching 
names, etc. Also at startup, a templates directory is created, if it 
doesn't already exist, and filled with copies of all the recorder creation 
template files found in /opt/audiorack/support/templates.  Existing files
are not overwritten, so you can customized these recorder templates. These
files are specified by name when the newrec command is executed to pre-load
a newly created recorder instance with some base settings for the desired
recorder type, such as mp3file.rec, wavfile.rec, etc.

An alternate startup file can contain pre-load configuration flags which
are treated as if the command line options were passed when the program 
was run. 

Note that this default launch of arServer4 will run arServer4 in the
background, keep-alive off, standard IO sockets closed.  For debugging, 
run with the -e and -n options to keep stderr open, and to run directly, 
without a running through a secondary "keep-alive/watchdog" process.

Add the -k option when you run the program, or in the above mentioned
ars_startup.conf file to make arServer run in keep-alive mode:  If the 
arServer crashes, it's watchdog launcher will restart it automagically.

To see all the command line options for arServer:
/opt/audiorack/bin/arServer4 ?

Once running, you can connect to the TCP control socket to issue commands
and get responses from arServer4.  The default TCP socket is on port 9550.
So you could connect, for example, using telnet on the same computer:

tellnet localhost 9550

The help command will show the extensive list of all the control socket
commands.  You will need to set up the database, audio input and output
groups, etc.  The old audiorack documentation should server as a starting
point, but keep in mind that the command for defining audio input and
output properties have been changed from the original version to work
with jack2.  So please also consult the "help" command results to see 
the required format in this new version.

You will also need a working MySQL or MariaDB server on your computer
or on your network with a working MySQL account that can has sufficient
database priveleges to create and manipulate a database for audiorack's 
use. You should already be familiar with MySQL enough to have such an 
account already set up, with a password and such, for audiorack to use.

Again, the old OS X GUI applications can be helpful to you if you have 
access to an OS X machine.  Keep in mind that all the audio config. 
panels in the old GUIs will not work properly with the new arServer4 due
to changes for jack2 uses.  And the database/library, if set up using the
old GUIs, will need to be "dbinit" updated by arServer4.

*** OPTIONAL SIP VoIP integartion ***

Audiorack4 can make use of the "baresip" sip client for integrations with 
SIP based VoIP telephone systems.  If you want make use of this 
functionality, you need to install baresip, and configure it to work
with both your telephone system, and to talk to audiorack4. Note that
Audiorack4 requires basesip version 1.0 or later, as changes were made to
baresip in this version to help it play nicely with audiorack. Configuring 
baresip to work with your telephone system is beyond the scope of this 
document. Baresip must be running on the same computer as arServer in order
for audio to be shared via jack-audio. Configuration for audiorack4 integration 
involves the following:

1. Ensure the folowing baresip modules are enabled (not commented out).  
Edit the baresip configuration file (usually found at ~/.baresip/config) 
either changing the settings show, or creating them if they do not already 
exist in the file. See baresip documentation for details.

module			jack.so
module			cons.so

2. Ensure baresip has the following audio setting to work with arServer:

audio_player		jack
audio_source		jack
ausrc_srate			48000		# Agree with jack-audio session sample rate
auplay_srate		48000		# Agree with jack-audio session sample rate
ausrc_channels		2			# The arServer input channel width (2 for stereo)
auplay_channels	2			# The arServer input channel width (2 for stereo)
ausrc_format		float		# s16, float, ..
auplay_format		float		# s16, float, ..

3. Ensure that the baresip console module is configured to be reachable
on the local computer so arServer4 can connect to it.  Note the port
used for connection, in the example below (the default), port 5555:

cons_listen		0.0.0.0:5555

4. Set baresip to NOT auto-connect calls to the default sound devices.
We want arServer to manage jack connections.

jack_connect_ports	no

5. It's a good idea to limit the number of calls that baresip will accept
at a any one time.

call_max_calls		2	# two calls at a time max.

6. Set the arServer setting "sip_ctl_port" to the port noted above.
This is the TCP port on the local computer that arServer will connect 
to to manage jack-audio connections and calls from baresip. As mentioned
above, baresip must be running on the same computer with arServer, therefore, 
"localhost" is used as the TCP address, and only a port number needs to be 
specified. The example commands below are arServer console commands to set 
the setting, and then save all the settings in the .audiorack/ars_setting 
file, for recall on next arServer startup as well.

set sip_ctl_port 5555
saveset

7. Run baresip.  arServer is already assumed to be running.  You can uncomment 
a line in the ars_startup.conf which will cause arServer to run baresip via 
the runifnot script, which is commented out by default, to have arServer run 
baresip when arServer starts up.  Just search for baresip in the file to find it. 
The run path for baresip specified in the line may need to be adjusted to the
install location of baresip in your particular operating system.

Note that this will run baresip via the runifnot script, which assumes that if 
baresip is already running, it was run via that script.  So if you ran baresip 
manually, you will want to quit it before you restart arServer and let arServer
run it.


Calls received by baresip should now be answered by arServer and routed to
an free arServer mixer input. The arServer console command "stat" will show 
you if arServer is connected to baresip, and baresip's registeration status 
with the SIP server you (presumably) have configured baresip to talk to. Again, 
configuration of baresip to talk to your telephone system is beyond the scope 
of this document.

*** OPTIONAL Carla integartion ***

Carla is a nice, cross platform jack-audio GUI and audio processing host.

To configuring Carla after installing it, (1) get arServer up and running. 
(2) run Carla. (3) set up all your processing as desired with processing ports 
wired to/from arServer ports. (4) Disconnect any Carla to arServer connections 
to the arServer mixer inputs. (5) Save Carla settings to ~/.audiorack/fx.carxp. 
(6) (Re)Define arServer input groups to use the Carla outputs that you disconnected 
prior to saving. 

This sequency is needed to allow arServer to control connections from Carla sources
to arServer mixer inputs, such as a microphone processing chain. Otherwise Carla will 
make the connections that were saved, even if arServer hasn't loaded the desired input 
group yet, or if some other source is using the input that was wired at the time of the 
carla save.  We want arServer to control the jack connections for inputs when loaded,
 and not Carla.

To have arServer run carla next time it is started, uncommented the associated 
line in the .audiorack.ars_startup.conf file.  Note that this will run Carla
via the runifnot script, which assumes that if Carla is already running, it was 
run via that script.  So if you ran Carla manually, you will want to quit it
befor you restart arServer, and let it run Carla.
