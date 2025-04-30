# Phase 4a - Terminal and Sleep System Calls

## Overview

This phase implements basic kernel services for:

- **Sleeping processes** (`Sleep` syscall)
- **Terminal input** (`TermRead` syscall)
- **Terminal output** (`TermWrite` syscall)

It launches a clock driver and a terminal driver per terminal unit, allowing user-mode programs to write to and read from terminal devices.

## Implemented System Calls

- `Sleep(seconds)`
  - Puts the calling process to sleep for the specified number of seconds.
  - Internally waits for the clock device to tick (~100ms intervals).

- `TermWrite(buffer, size, unit, &bytesWritten)`
  - Writes `size` characters from `buffer` to the specified terminal `unit`.
  - Characters are sent one-by-one to the hardware, waiting for a device interrupt after each character.

- `TermRead(buffer, size, unit, &bytesRead)`
  - Reads a full line from the specified terminal `unit` into `buffer`.
  - Blocks until a line (ending with a newline `\n`) is available.

## Testcases

- Testcase 0-5, 8: all work as expected. however there is an error on gradescope that says therer are binary files, 
    but when you run them in the terminal it produces no such error and has a similar output. Additionally for 0-2 the sleep values are in range. 
- Testcase 7: issue where the binary files dont open. in terminal you can type "cat term0.out" and it will output the correct strings.
