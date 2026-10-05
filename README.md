# IE3090 RemoteOps

## Student Information

Registration Number: IT24101562

## Project Description

RemoteOps is a client-server remote system monitoring and management tool developed in C using BSD sockets.

The system consists of:

- Agent - TCP server that receives and processes commands.
- Controller - TCP client used to connect to and manage the Agent.
- UDP monitoring - provides periodic system monitoring information.

## Personalized Configuration

- TCP Port: 9410
- Session ID (SID): 2651
- Authentication Token: OPS-1562
- Agent Source: agent_562.c
- Controller Source: controller_562.c
- Makefile: Makefile_562
- Log File: remoteops_IT24101562.log
- Storage Directory: ./agentfiles/IT24101562/

## Compilation

Compile both programs using:

make -f Makefile_562

Clean compiled files using:

make -f Makefile_562 clean

## Running the Agent

./agent_562

## Running the Controller

Open another terminal and run:

./controller_562

Enter the authentication token when requested.

## Supported Commands

SYSINFO

LISTPROC

EXEC DATE

EXEC UPTIME

EXEC DISKFREE

EXEC HOSTNAME

EXEC WHOAMI

PUT <filename>

GET <filename>

MONITOR START

MONITOR STOP

QUIT

## Main Features

- TCP client-server communication
- Token-based authentication
- Personalized session identifier
- System information retrieval
- Process listing
- Whitelisted remote command execution
- TCP file upload and download
- UDP system monitoring
- Concurrent Controller support using POSIX threads
- Thread-safe activity logging
- Graceful Controller disconnection

## File Storage

Files uploaded using PUT are stored in:

./agentfiles/IT24101562/

## Logging

Agent activity is recorded in:

remoteops_IT24101562.log

The log records connection events, authentication results, commands, session identifiers, and disconnect events.
