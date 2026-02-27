This project was developed in C for the Linux operating system as part of an Operating Systems curricular unit. It simulates a transport management platform composed of a central controller, multiple clients, and autonomous vehicle processes.

The controller manages service scheduling, client communication, and fleet execution. Clients interact with the controller through named pipes (FIFOs), sending requests such as login, service scheduling, and cancellation. At the scheduled simulated time, the controller launches autonomous vehicle processes using fork() and exec(), passing service data via command-line arguments. Communication from vehicles back to the controller is implemented through anonymous pipes with stdout redirection, allowing real-time telemetry monitoring (trip start, progress updates every 10%, completion, or cancellation).

The system demonstrates key Linux systems programming concepts including process creation, inter-process communication (IPC), signals handling, environment variables, file descriptors, thread synchronization (mutexes and condition variables), and concurrent programming with POSIX threads.

This project highlights practical experience in low-level system design, synchronization, resource management, and multi-process coordination in a Unix-based environment.
