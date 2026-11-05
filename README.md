# Linux Elevator Module

A Linux kernel elevator simulation extracted from Group 37's operating systems project. Original source attribution is preserved.

## Features

- Five floors, passenger queues, and four passenger types
- Capacity limits of five passengers and 70 load units
- Kernel thread, mutex synchronization, and linked-list queues
- Start, request, and stop system-call interfaces
- Status through `/proc/elevator`

## Files

- `src/elevator.c`, `src/elevator.h`: loadable module
- `src/Makefile`, `Makefile`: kernel module build
- `syscalls.c`: kernel-side syscall definitions and exported stub pointers
- `wrappers.h`: user-space syscall wrappers
- `consumer.c`: start/stop utility
- `producer.c`: random passenger request generator

## Requirements

This project requires the modified Linux kernel used for the original assignment, with matching build headers. `syscalls.c` must be integrated into that kernel's build and syscall table. The module requires its exported `STUB_*` symbols. The wrappers assume syscall numbers 548, 549, and 550; confirm these match the target kernel before running the utilities.

The complete kernel patch and syscall-table changes are not included. This is not a standalone module for an unmodified stock kernel, and it does not run natively on Windows.

## Build and run

On the matching assignment Linux environment, from the repository root:

```bash
make
gcc -Wall -o consumer consumer.c
gcc -Wall -o producer producer.c
sudo insmod src/elevator.ko
./consumer --start
./producer 10
cat /proc/elevator
./consumer --stop
# Allow passengers to finish before unloading.
sudo rmmod elevator
make clean
```

Precompiled binaries and generated kernel build files are excluded. Source was preserved during extraction; the kernel module has not been built or run as part of this repository packaging.
