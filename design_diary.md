# Design Diary - IE3090 RemoteOps

**Registration Number:** IT24101562  
**Project:** RemoteOps - Remote System Monitoring and Management Tool

## Initial Design

The project was designed using a client-server architecture written in C with BSD sockets. The Agent acts as the server and listens for Controller connections on the personalized TCP port 9410. The Controller connects to the Agent and provides a command-line interface for remote operations.

The personalized values used in the implementation are the authentication token OPS-1562 and session identifier SID 2651.

## TCP Communication and Authentication

The first stage was establishing TCP communication between the Agent and Controller. After confirming that the connection worked, authentication was implemented.

The Controller sends the authentication token to the Agent before any other command is accepted. Correct authentication returns a successful response containing SID:2651, while an incorrect token is rejected.

A newline-based protocol was used for text commands. Helper functions were implemented to handle sending complete data and receiving command lines correctly.

## Remote System Commands

After authentication was working, SYSINFO and LISTPROC were implemented.

SYSINFO retrieves information such as the hostname, operating system, kernel version, CPU cores, memory information, and uptime.

LISTPROC retrieves the currently running processes with their process IDs and command names.

The EXEC command was then implemented using a fixed whitelist. Only DATE, UPTIME, DISKFREE, HOSTNAME, and WHOAMI are allowed. This design prevents the Controller from executing arbitrary shell commands.

## File Transfer

PUT and GET were implemented using the existing TCP connection.

PUT uploads a file from the Controller to the Agent. Uploaded files are stored in the personalized directory:

./agentfiles/IT24101562/

GET downloads a stored file from the Agent to the Controller.

One important design consideration was that file data cannot be treated in the same way as newline-terminated text commands. Therefore, the file size is transmitted first and the exact number of file bytes is then transferred.

Filename validation was also added to reject path traversal patterns and directory separators.

## UDP Monitoring

A secondary UDP channel was implemented for periodic system monitoring.

The Controller sends MONITOR START to request monitoring. The Agent then periodically sends system information through UDP. Each UDP monitoring message includes SID:2651.

During development, continuously receiving UDP messages while also accepting commands from the Controller caused terminal interaction difficulties. The Controller was therefore designed to receive a limited set of monitoring updates before returning control to the command menu. Monitoring remains active on the Agent until MONITOR STOP is sent.

## Concurrent Controller Support

The initial Agent handled only one Controller connection. This was later redesigned using POSIX threads.

The Agent now continuously accepts new TCP connections and creates a separate worker thread for each Controller. This allows multiple Controllers to remain connected and execute commands independently.

The implementation was tested successfully with five simultaneous Controller connections.

UDP monitoring state was also maintained separately for each Controller session to avoid different Controllers sharing the same monitoring state.

## Logging and Graceful Disconnect

Thread-safe Agent logging was implemented using a mutex.

The personalized log file is:

remoteops_IT24101562.log

The log contains timestamps, Controller IP addresses, authentication results, commands, SID information, and disconnect events.

The authentication token itself is not stored in the log.

The QUIT command allows an individual Controller to disconnect gracefully while the Agent continues running and remains available for other Controllers.

## Build Process

A personalized Makefile named Makefile_562 was created to compile both applications.

The Agent is compiled with POSIX thread support using the -pthread option. The Controller is compiled separately. The -Wall and -Wextra options are used to identify compiler warnings.

The final Makefile successfully compiled both the Agent and Controller without errors or warnings.

## Final Design

The final RemoteOps implementation contains:

- TCP-based Agent and Controller communication
- Authentication using a personalized token
- Personalized SID in protocol responses
- SYSINFO system information retrieval
- LISTPROC process listing
- Whitelisted EXEC commands
- PUT and GET file transfer
- UDP system monitoring
- Concurrent Controller handling using POSIX threads
- Thread-safe Agent logging
- Graceful Controller disconnection
- Personalized Makefile and storage directory

The project was developed incrementally and each major feature was tested before moving to the next stage.
