To run from terminal:

	cd /opt/audiorack/bin/
	./arserver ?

You should get a list of command line options that arserver accepts.

	./arserver -k

You should see a message indicating that arserver has started.  It is running under the user account that the "./arserver -k" command was issued from.  The -k option sets arserver keep-alive mode active.  In this mode, arserver will immediately relaunch itself if it crashes, is killed, or otherwise stops improperly (silence or watchdog time timeouts).  If it is stopped by the system or via it's shutdown command, it will not relaunch itself.  Next, from the terminal, open a telnet session to the arserver control port:

	telnet localhost 9550

You should see a disclaimer message and the "ars>" prompt.  Type "help" for a list of commands, "shutdown" to shutdown arserver, or "exit" to leave the telnet session with out shutting down arserver.

OR JUST RUN THE ARStudio application.

Some useful information:

When arserver starts running, it looks for a startup configuration file.  If the -c command line option was used when it was run, it first tries to use the config file at the location specified after the -c option.  In all other cases, it will start by looking for a config file in the users home directory at ~/Library/Preference/ars_startup.conf (OSX) or ~/.audiorack/ars_startup.conf where the user is the user on the computer who caused it to run.  If no file is found, it will next try /opt/audiorack/support/startup.conf.  

This approach allows a default config file to reside in /opt/audiorack/support/, and still have a per-user custom config file in that users home/preferences folder. Note that the StartupItems script (which can run arserver as a daemon at boot time) uses the -c option to specify a special startup config file at /etc/opt/audiorack/ars_startup.conf. This script also runs arserver as root.  When arserver is run as root, it automatically drops privileges to run under an "arserver" user and group, which should already exist on your system. If you are running arserver in this mode, please ensure that that the arserver user and group have file permission access to the music library files!

*** THE FOLLOWING IS OUT OF DATE - CHANGES HAVE SINCE BEEN MADE FOR AUDIORACK4 ****
Regarding music library file path resolution:  If a file in the music library database has been moved or renamed, arserver will not be able to locate the file. However, he music library stores the volume name along with other file information which can be useful in locating missing files.  When arserver is forced to use the volume and inode to find a file, it takes the volume name and appends it each of a list of possible mounting locations.  By default the list of locations is /Volumes/ (OS X usual disk location) and /private/var/automount/Network/ (OS X static network mount location).  You can set the "File mount prefix list" in the Library database Properties (Using Manage tab in ARManager or by directly editing the "info" table in the database) to override this default list.  For example the default list would be specified as follows in the File mount prefix list field in ARManager or as the "Value" string for the "MountList" property of the "info" database table:

/Volumes,/private/var/automount/Network

The idea behind this is to allow multiple computers to share the same music library database even if they mount the disks that contain the music in different ways.  As long as the file mount list contains all the possible mount locations, all will be well.

The arserver function dbfilesearch can also be used to traverse a file path hierarchy or to search all the volumes in the library looking for files with matching Hash codes that may have been moved or renamed.
