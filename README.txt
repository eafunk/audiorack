Updated Oct. 12, 2023

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

The old GUI applications have been replaced with a single web-base GUI.
This package included a node-js GUI server, which then hosts a javascript
GUI application to a client web browser. This is how you would usa and
manage audiorack.

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

The User Interface Server requires the following programs to be 
installed:

nodejs	The node.js runtime system, v.12.10 or newer.

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

First, you must have a MySQL or MariaDB database server running 
on a machine you have local network access to. Configuration of 
the database server is beyond the scope of this document.  You 
will need a database user configure with permission to create a 
new database, etc.

Second, you must have the jack-audio server (jackd) up and running 
on the computer that will be running arServer. There are many tools 
helpwith this, ranging from launching jackd from the commandline, 
to various GUI programs.  On a linux system, The UbuntuStudio tools 
are nice, and allow for launch at login, and auto-magic support for 
more than one audio device, with added latency for the extra devices 
of course. Recent Linux distributions are now using Pipewire (PW) as 
a media engine.  PW can provide emulation of a jackaudio server. See 
details later in this readme file for some help with using PW instead 
of jack.

Different audiorack compenents can be run on different computers, 
for example different arStudio instances can run on different 
machines, with yet another machine running the GUI server to bring 
control of all the pieces together, as long as all machines can 
see eachother on a local network. For starters, it will be assumed 
that everything exept possibly trhe database is running on one 
machine, the machine which audiorack was installed on.  

The easiest way to get things running once jack and a MySQL 
database server are running is to use the node-js GUI server 
that is part of this package.

You can manually run the GUI server from a command line shell 
with this command:
/opt/audiorack/bin/keepalive node /option/audiorack/bin/arui/server.js

The same command can be used to create a startup on login application 
on your particular OS.  For example, on Ubuntu 23.04, the program to 
configure startup items is called Startup Application Preferences, from 
which you would create a new startup item and with a name and comment 
to your liking, with the command set to the above comand.

Then, open http://localhost:3000 in a web browser to access the GUI. 
Use the following default credentials to log in for the first time:
default user name: admin
default password: configure

First task in the GUI is to configure the GUI server to talk to the
various components by clicking the "Configure" table on the left.
Click on "Users" bar to expand the Users setting where you should 
at least change the password for the "admin" user you logged in as, 
and you can add additional users with various permissions.

Next, expand the HTTP settings bar.  Here you can change the port 
number that the GUI server responds to web browser http requests on, 
and set up certificates and the port number for secure  http requests.  
Note that setting up sanctioned or self-signing web certificates is 
beyond the scope of this document, but it is recomended you place .pem 
(key file) and .crt (cert file) files in the .audiorack directory in 
the user account the GUI server is running in, and set the paths in 
this configuration accordingly. NOTE: that webmidi for client control 
surface use and WebRTC require secure http to be configured.

File settings is the next bar to expand and configure.  Here you must 
set up a "tmpMediaDir" which is accessible from the GUI server and any 
machines that will be running arStudio.  If diferent that the GUI 
server machine, a shared network disk must be used, and a means for 
automounting the shared disk at startup by the various machines must 
be implemented.  Everything is running on a single machine, you can 
use/create a directory on a local disk. When the GUI is used to add 
new media to the system, this is the directory it is uploaded to from 
the client web browser.

"tmpMediaAgeLimitHrs" determines how long (in hours) files are allowed 
to be in the tmpMediaDir directory befor being auto-deleted.

"mediaDir" is like the tmpMediaDir, but is perminent storage where 
media is moved to when added to the library.  The GUI will let a user 
upload media to the tmpMediaDir, from which items can then be added 
to the library if done befor the age limit times out.  

To aid in media file orginization, you can create additional mediaDirs 
to orginize special things.  For example, you can create mediaDir-ads, 
and mediaDir-ID to place advertisments and station ID in different 
location than music, etc.

Again, all these directories must be reachable in a multi-computer 
environment.

Library setting bar is where you configure the database server 
information.
type: must be "mysql" currently for MySQL or MariaDB server connection.

host: IP address of the database server, could be localhost if running 
on the same computer as the GUI server.

port: IP port number to connect to the server.  Leave blank to use 
MySQL default.

user: user name to connect to the server as.  This must have been 
configured already on the database server.

password: password associated with the above user name.

database: name of the existing or new (to be created) database for 
audiorack library storage on the database server.

prefix:  "ar_" is the recomended value
All database tables will have this prefix added to the table names.  
This is useful if you only have partial access to a database server 
and you have to use an existing database name (above).  This prifix 
will prevent name collitions with other tables that might be used 
by other applications in a shared database.

Finally, the Studio bar for connecting to studio instances, which 
hanle audio and automation for a station. The GUI can manage many 
independent studios as long as they all share a common library 
and have access to the same media directories.  This can be through 
multiple arServer instances running on one computer, of different 
computer for different instances, likely on a shared network.  
For each instance:

id: Name to use as an identifier in the GUI for this studio.

host: IP address of the machine running this studio instance. 
Can be "localhost" if the studio instance is/will be running 
on the same machine as the GUI server.

port: 9550 is the defult, but must be customized if multiple 
instances are running on the same machines, so each has it's 
own control port.

run: check if the GUI server should start the instance on the same 
machine when the GUI server is started. If the studio is running 
on another machine, the GUI server will not be able to start it. 
You will need to arrange for arServer to start up on that machine 
some other way.

startup: command line shell command to start the arServer instance 
on local machine. "arServer4 -k" will run /opt/audiorack/bin/arServer 
with the -k option (keep alive) which will auto-restart arServer if 
is should crash.  You can specify additional options such as -c to 
start with a custom configuration file if you like. But the 
"arServer4 -k" value will work for most single studio cases on a 
single machine. 

minpool: Minimum number of connections to make and hold to the 
studio's arServercontrol port.  2 is a good number.  If you have a 
lot of GUI uses accessing this studio at once, you might need to 
increase this number to keep the GUI from getting sluggish.

maxpool: Maximum number of connections to allow the GUI server to 
grow it's arServer control port connection pool too.  5 is a good 
number. This number must be larger than the minpool number. Each 
arServer supports a maximum of 20 connections without addition 
configuration, so make to never exceed this.

Clicking the "run" button will cause the run command above to be 
executed on the machine running the GUI server.  This lets you start 
a studio's arServer if it isn't running, yet and on the same machine 
as the GUI server.

*** Post GUI Configuration Tasks ***

Now that the GUI is configured, you will need to initialize the library 
database if it doesn't already exist.  Go to the Library tab, select the 
Manage sub-tab. In the Initialize/Update Library Database area on the top, 
with Current Library (from settings) option selected, click the execute 
button.  If all is coinfigure properly with your database server, a new 
empty library will be created, or if a libray already exists, it will 
be updated to the latest version. No harm is an existing database is
the latest version.

Next you can create a new location under the Mange Locations area. 
Generaly each studio/station will have it's own location, for which 
play logs and automation schedules are independently maintained. 
When you get a studio configured and running, you will set up the 
studio to use one of the locations set here.

The Stash tab gives you access to a local (to the computer/web browser your 
running the GUI client in) working list of items.  This is used for moving 
things around the GUI.

The File tab is where you get access to the temporary media directory.  Under 
this tab, you can upload music/items from your computer running the GUI client 
in a web browser to the temporary media directory.  Once in this idrectory, 
you can import items into the music library for perminent storage, or for a 
short period of time (tmpMediaAgeLimitHrs) the items will remain in this 
director for access/playout by the studios.

Using the Library tab, Browse sub-tab, you can now browse through items added
to the library, edit item properties, add and edit categories, playlists, etc.
This included creating scheduling automation rotations, pick tasks, and such
to build a programming schedule for a station.  Please have a look at the old
AudioRack 3x users guide for details on how automation work in the previous
version of audiorack.  The user interface is now different here on version 4
but the concepts are the same.

You should already have a studio set up in the GUI settings. Now is the time
to get that studio running (via the GUI settings), and configure it.
Configuration can be done via the STudio tab of the GUI, and selecting the
you want to manage by selecting it's name in the sub-tab list (you may 
have configured more than one). If you are logged into the GUI with as a 
user with full administrator priveleges (as set up in the GUI setting Users
panel), then you will see a box of 8 tabs near the bottom.  This is the 
configuration box.  The Console tab give you access to set command to the 
studio's arServer control port and see the responses; useful for low level 
configuration.

The Output tab allows you to configure each of the 5 (default)
output groups the arServer mixer has. Give each output you desire to use a 
unique one-word name, set up jack audio routing for where you want the
output to go (device and ports for each stereo channel), which mixer bus 
is routed to the output, and what level ducking should be applied when 
cue or one of three mutes are activated. You likely will want to create 
Monitor, Cue, Headphones, and Main outputs:

Monitor from the monitor bus going to your studio speakers and fully muting
on MuteAand ducking apx. 6 to 12 dB on Cue. Cue going to the

Cue from the cue bus going to a set of cue speakers, or maybe you studio 
monitors fully muting on MuteA as well.

Headphones from the monitor bus going to your headphones with no muting.

Main from the main bus going to some analog output which is your actual 
broadcast audio potentially to a transmitter, etc.

The LiveInputs tab allows you to configure as many input groups as 
you like.  Input groups show up under the Live Input tab and can be 
placed in a mixer input, in the automation queue, etc., to place live
input from an audio device on the air. Much like outputs, you will name
and define what audio device sorce and ports will be routed to the 
arServer mixer when the input is loaded. You can also assign what mixer
busses this signal will be routed to when playing, and what mute group 
(if any) will be activated to duck corrisponding output volumes when 
live.  Each input can also have a mix-minus feed, for implementing
phone interfaces, talkbacks, etc.


Library setting can mostly be filled in by clicking the Copy & Apply 
from Library Settings button (that would be copy from the GUI settings 
for the library).  You will need to set a Library location for the
station location list you created previously. This is the schedule 
and logs this studio will use for automation.

Mixer settings control behavior of the arServer studio mixer.  Default
Bus settings select which mixer busses are selected for source (such
as music players) that do not have bus assignments associated with them. 
Live Input (as above) do have bus assignments, which are used when 
a Live Input is loaded rather than the default bus assignments set here.
Selecting Monitor, Main, and Alternate busses is a good startring point.
You should also specify a full file path to a directory that will be 
used to store file recordings that the mixer might make when a user 
creates a recorder. Finally, there are some settings for silence 
detection.  Silence Detection can monitor the selected of the mix bus,
and if the audio level is below the specified threshold for more then 
the specified time, will trigger two possible actions:
First timeout will cause a segue of all playing items, forceing the
next track to play.
Second timeout with continued silence between will force a total 
restart of arServer.  If arServer was started with the 0k (keep alive)
option, it will automatically restart.
You can customize these actions by creating silence.detect (first) and
silence.fault (second) files in the .audiorack/triggers directory which 
would contain a serries of arServer control commands you want to execure
instead of the above default behaviors.

Automation settings control how the queue, schedule filler and schedule 
inserter behave. First setting box determines if arServer should start 
up with automation on, or off.  If off, a user will need to either fill
the queue with items to play, or will need to start automation before 
anything will play. On is therefore the recommended setting. Next are
options for Automation live mode behavior. Live mode is a reduced 
automation mode intended for use when a live DJ is present (called 
live-assist in the radio buisiness).  Here you select which full 
automation mode functions continue to be active in live mode, and which
functions are turned off when live. You also set a timeout, where the 
automation mode will go from live back to full-auto mode if a user
doesn't do anything in the studio for more than this timeout period.
Finally, queue behavior is set, including the minimum number of items 
the automation queue filler should always keepin the queue, the default 
segue overlap time from end of a playing item to the start of the next, 
and a level base segue holdoff.  The default segue time is used for ietms
that do not already have a segue time set.  If an item already has a time
set in it's library properties, that time is used instead.  The level 
based segue holdoff works with the segue time (either default of set for 
an item) to keep the segue form happening beyond the segue time until the 
audio level of the playing item drops below the specified level. Recomended
settings are 8 minimum queue item, 7 second segue overlap, and -26 dB segue
holdoff level.

VoIP settings control how arServer integrates with the baresip VoIP client.
See the later section in this document called OPTIONAL SIP VoIP integartion 
for details on how to set up baresip to work with arServer.  This panel 
controls teh arServer side of this integration. The first item sets the 
control port that arServer uses to connect to baresip to monitor and control
calles that come into baresip.  Again, see the OPTIONAL SIP VoIP integartion
section for details.  All the other ists in this settings tab set up specific
properties for how a caller is connected to the mixer, live the default 
input levels, mix-minus feed levels, mixer and feed bus assignments, and feed 
cue and/or talkback behavior.

Wire settings control persisten jack audio connection between various audio
devices and port to allow you to route audio through processing and such 
outside of and beyond the Live Inputs and Output settings above. 
Any connections you make in this list will be persistent: when arServer runs, 
it will make sure the connections are made if not already present. If a 
device goes away breaking the connection, arServer will reconnect them when 
the device comes back. If a connection is removed from this list, arServer 
will disconnect the devices and ports that were specified and remove the 
connection from it's Spersistent connection list.

Example of use: a arServer Output group may have been 
created to route the main mix bus to two output channels of an audio device
to feed an air chain. You might want to insert an audio level comprerssor 
plugin into that path before it goes to the audio devive.  You would need
run the program that hosts the compressor plugin (such as Carla), and then 
set the arServer output to route to the two (L&R) inputs of the compressor 
instead of the audio device.  Then in this Wire list you would add two 
connections for the left output of the compressor to the desired audio 
interface channel, and from the right output to another audio interface 
channel.

*** RUNNING arServer low level details ***

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

If yoiu woulkd like to runn multiple arServer instances on a single machine,
you can create copies of the .audiorack directory with a unique name for 
each directory to go with each instance. Edit the ars_prefs.conf file
in each new directory you created changing all the references to .
audiorack to be the new directory name you created that contain the 
particular ars_prefs.conf you are editing. Next, the ars_startup.conf 
file also in each directory changing the "-p 9550" to something like
"-p 955x" where x is a unique control port number for each arServer 
instance.  For example, 9551, 9552, 9553, etc. Also changing all the 
references to .audiorack to be the new directory name you created that 
contain the particular ars_prefs.conf you are editing.

When you start the corrisponding arServer instance, add the -c option to
the shell command and specify the file path to that instance's 
ars_startup.conf file. This arServer instance will now load the specoified
config file, with the unique port change. For example:

/opt/audiorack/bin/arServer4 -k -c /home/someuser/studioB/ars_startup.conf

Where someuser if the account you are running the instance from, and studioB 
is the customized copy of .audiorack directory you created.

You will also need a working MySQL or MariaDB server on your computer
or on your network with a working MySQL account that can has sufficient
database priveleges to create and manipulate a database for audiorack's 
use. You should already be familiar with MySQL enough to have such an 
account already set up, with a password and such, for audiorack to use.

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

*** Using Pipewire to emulate Jack-Audio ***

Pipewire has a replacement jack library file which provides application all of the 
functions of jack up front, but use pipewire as the audio backend when the usually 
optional pipewire-jack package is installed along with pipewire itself. The pw-jack 
program then allows the launch of programs with the library change made via run 
environmental variables.  Unfortuantly, arServer itself runs additonal programs 
(like arPlayer and arRecorder) which do not inherit the environmental variable set 
by pw-jack.  So here is how you can get most linux OSs to change the library from jack
to the pipewir implementation by default, with out actually removing the native jack 
libraries:

This is for pipewire version 0.3, and would likely need to be modified for future 
versions of pipewire, and assumes your OS uses glibc at it's base for library loading.

You need to create the file /etc/ld.so.conf.d/pipewire-jack-x86_64-linux-gnu.conf as 
the root user, with the following contents for Ubuntu 22.10.  Other operating systems 
may have the library files for jack in a different location:

/usr/lib/x86_64-linux-gnu/pipewire-0.3/jack/

Then run sudo ldconfig. This overrides the default linker search path so that
every program that runs and tries to use the jack client libraries, will instead 
load the pipewire implimentation.
