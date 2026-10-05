# Reflection - IE3090 RemoteOps

**Registration Number:** IT24101562

The RemoteOps assignment gave me practical experience in developing a network-based client-server application using C and BSD sockets. Before starting this project, I understood the basic concepts of TCP and UDP communication, but this assignment helped me understand how these concepts are applied when building a complete network application.

I developed the project incrementally rather than implementing all features at once. I first established a basic TCP connection between the Agent and Controller using the personalized port 9410. After confirming the connection, I implemented authentication using my personalized token and session identifier. I then added SYSINFO, LISTPROC, and the EXEC command. For EXEC, I used a fixed whitelist containing DATE, UPTIME, DISKFREE, HOSTNAME, and WHOAMI. This helped me understand why remote command execution should be restricted rather than allowing arbitrary commands.

The PUT and GET features improved my understanding of transferring files through TCP. One important lesson was that file data must be handled differently from normal text commands. The file size must be known, and the exact number of bytes must be transmitted and received. I also learned the importance of validating filenames to reduce security risks such as path traversal.

UDP monitoring was one of the more challenging parts of the project. The initial approach caused monitoring output to interfere with command-line interaction. I adjusted the design so the Controller receives monitoring updates and then returns to the command menu while monitoring remains active until it is explicitly stopped. This demonstrated that a technically working solution may still need improvement to make it practical to use.

Another important improvement was concurrency. The first Agent implementation supported only one Controller. I redesigned it using POSIX threads so that each Controller connection is handled independently. I successfully tested the final implementation with five simultaneous Controller connections. Thread-safe logging was also implemented using a mutex to prevent concurrent threads from interfering with each other's log entries.

AI assistance was useful for understanding requirements, generating implementation ideas, and troubleshooting problems. However, I learned that AI-generated suggestions must be reviewed and tested rather than accepted automatically. Some approaches, particularly UDP monitoring, required modification after practical testing.

Overall, this assignment improved my understanding of TCP and UDP sockets, protocol design, concurrency, file transfer, authentication, command validation, logging, error handling, and systematic testing. It also demonstrated the importance of incremental development, testing each feature independently, and documenting design decisions throughout the development process.
