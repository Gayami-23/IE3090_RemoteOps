# AI Prompt Log - IE3090 RemoteOps

**Registration Number:** IT24101562 
**Project:** RemoteOps - Remote System Monitoring and Management Tool

## AI Tool Used

ChatGPT was used as a development support tool during Part 1 of the RemoteOps assignment. AI assistance was mainly used to understand requirements, develop C socket-programming components, troubleshoot problems, and plan testing. Generated code and suggestions were compiled and tested before being accepted.

---

## 1. Assignment Requirement Analysis

**Prompt / Request:** 
Asked ChatGPT to guide me through the RemoteOps assignment and explain how to implement the project step by step.

**AI Assistance:** 
ChatGPT helped identify the personalized project values, including TCP port 9410, authentication token OPS-1562, SID 2651, source file names, log file name, and personalized storage directory.

**Evaluation / Action Taken:** 
The calculated values were checked against my registration number IT24101562 and then used throughout the implementation.

---

## 2. TCP Agent and Controller

**Prompt / Request:** 
Asked for guidance and C code to establish the initial connection between the Agent and Controller.

**AI Assistance:** 
ChatGPT suggested using BSD TCP sockets with socket(), bind(), listen(), accept(), and connect(). It also provided a basic Agent and Controller structure.

**Evaluation / Action Taken:** 
The code was compiled using GCC and tested using localhost. The Controller successfully connected to the Agent on TCP port 9410 before additional functionality was added.

---

## 3. Authentication

**Prompt / Request:** 
Asked how to implement the required authentication mechanism.

**AI Assistance:** 
ChatGPT suggested making AUTH the first protocol command and validating the personalized token OPS-1562.

**Evaluation / Action Taken:**
Both valid and invalid authentication were tested. The valid token returned OK AUTHENTICATED with SID:2651, while an invalid token returned an authentication error.

---

## 4. SYSINFO and Persistent Command Processing

**Prompt / Request:** 
Asked how to implement the SYSINFO command and allow multiple commands after authentication.

**AI Assistance:** 
ChatGPT suggested adding a persistent command-processing loop and using Linux system calls and structures to obtain hostname, operating system, kernel, CPU, memory, and uptime information.

**Evaluation / Action Taken:**  
SYSINFO was compiled and tested. The Controller successfully displayed the requested system information and received the correct SID.

---

## 5. LISTPROC

**Prompt / Request:**  
Asked how to implement the process-listing requirement.

**AI Assistance:**  
ChatGPT suggested using popen() with a fixed ps command to retrieve process IDs and command names.

**Evaluation / Action Taken:**  
LISTPROC was tested through the Controller. The Agent successfully returned a process list and terminated the response with OK LISTPROC SID:2651.

---

## 6. EXEC Command and Whitelist

**Prompt / Request:**  
Asked how to safely implement remote command execution according to the assignment.

**AI Assistance:** 
ChatGPT suggested mapping the allowed command names DATE, UPTIME, DISKFREE, HOSTNAME, and WHOAMI to fixed shell commands instead of executing arbitrary Controller input.

**Evaluation / Action Taken:** 
Allowed commands were tested successfully. An unsupported command such as EXEC LS was also tested and correctly rejected. The whitelist approach was retained because it prevents arbitrary command execution.

---

## 7. PUT File Upload

**Prompt / Request:** 
Asked for help implementing file upload from the Controller to the Agent.

**AI Assistance:** 
ChatGPT suggested sending the filename and file size first, followed by the exact file bytes. It also suggested storing uploaded files in ./agentfiles/IT24101562/ and validating filenames.

**Evaluation / Action Taken:** 
A test file was uploaded using PUT. The uploaded file was compared with the original file to verify that the content and size were correct.

---

## 8. GET File Download

**Prompt / Request:** 
Asked for help implementing file download from the Agent to the Controller.

**AI Assistance:** 
ChatGPT suggested that the Agent send the file size before transmitting the exact file bytes and that the Controller save the received file locally.

**Evaluation / Action Taken:** 
The previously uploaded test file was downloaded. File size and content were compared with the Agent copy, confirming successful transfer.

---

## 9. UDP Monitoring

**Prompt / Request:** 
Asked how to implement the secondary UDP monitoring channel and later requested help when UDP output interfered with Controller terminal interaction.
**AI Assistance:** 
ChatGPT initially suggested periodic UDP monitoring using a separate monitoring mechanism. During testing, the terminal interaction was difficult because monitoring output and command input interfered with each other. The design was adjusted so that the Controller receives a limited number of UDP updates and then returns to the command menu while monitoring remains active on the Agent.

**Evaluation / Action Taken:** 
MONITOR START and MONITOR STOP were tested. Periodic UDP system information containing SID:2651 was successfully received, and monitoring could be stopped without terminating the TCP connection.

---

## 10. Concurrent Controller Support

**Prompt / Request:** 
Asked how to satisfy the requirement for at least five simultaneous Controller connections.

**AI Assistance:** 
ChatGPT suggested changing the Agent from a single accept-and-process design to a continuous accept loop with a separate POSIX thread for each Controller.

**Evaluation / Action Taken:** 
The Agent was tested using five Controller processes connected simultaneously. All five authenticated successfully and different commands could be processed while the connections remained active.

---

## 11. Thread-Safe Logging

**Prompt / Request:** 
Asked for help implementing the personalized Agent log file.

**AI Assistance:** 
ChatGPT suggested creating remoteops_IT24101562.log and protecting log writes with a pthread mutex because multiple Controller threads may attempt to write simultaneously.

**Evaluation / Action Taken:** 
Logging was tested with authentication, SYSINFO, EXEC WHOAMI, and QUIT. The generated log contained timestamps, Controller IP addresses, commands, SID information, and graceful disconnect events. The authentication token itself was not written to the log.

---

## 12. Makefile and Repository Cleanup

**Prompt / Request:** 
Asked for guidance on creating the personalized Makefile and cleaning the repository before submission.

**AI Assistance:**
ChatGPT suggested a Makefile that builds both the Agent and Controller using GCC, with -pthread for the Agent and -Wall -Wextra warning options. It also suggested ignoring generated binaries, runtime logs, and temporary test files using .gitignore.

**Evaluation / Action Taken:** 
The Makefile was tested using clean and build commands. Both programs compiled successfully without compiler errors or warnings.

---

## Critical Evaluation of AI Assistance

AI was useful for explaining socket-programming concepts, suggesting implementation structures, and troubleshooting errors. However, the generated suggestions were not assumed to be correct automatically. Each major feature was compiled and tested individually.

Some AI-generated approaches required adjustment during development. In particular, the UDP monitoring implementation required changes because continuous asynchronous output affected command-line interaction. The concurrency design also required changing the original single-Controller Agent into a thread-based architecture.

The final implementation was therefore produced through an iterative process of AI-assisted suggestions, manual testing, observation of errors or limitations, and modification of the design.
