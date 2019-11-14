#!/bin/bash

# Assignment 1
cp ./include/synch.h ../ops-class/os161/kern/include/
cp ./thread/synch.c ../ops-class/os161/kern/thread/
cp ./test/rwtest.c ../ops-class/os161/kern/test/
cp ./synchprobs/whalemating.c ../ops-class/os161/kern/synchprobs/
cp ./synchprobs/stoplight.c ../ops-class/os161/kern/synchprobs/

cp ./conf/conf.kern ../ops-class/os161/kern/conf/conf.kern
#Assignment 2.1
cp ./include/proc.h ../ops-class/os161/kern/include/proc.h
cp ./include/syscall.h ../ops-class/os161/kern/include/syscall.h
cp ./include/file.h ../ops-class/os161/kern/include/file.h
cp ./fs/file.c ../ops-class/os161/kern/fs/file.c
cp ./proc/proc.c ../ops-class/os161/kern/proc/proc.c
cp ./arch/mips/syscall/syscall.c ../ops-class/os161/kern/arch/mips/syscall/syscall.c
cp ./syscall/open_syscalls.c ../ops-class/os161/kern/syscall/open_syscalls.c
cp ./syscall/dup2_syscalls.c ../ops-class/os161/kern/syscall/dup2_syscalls.c
cp ./syscall/close_syscalls.c ../ops-class/os161/kern/syscall/close_syscalls.c
cp ./syscall/read_syscalls.c ../ops-class/os161/kern/syscall/read_syscalls.c
cp ./syscall/write_syscalls.c ../ops-class/os161/kern/syscall/write_syscalls.c
cp ./syscall/lseek_syscalls.c ../ops-class/os161/kern/syscall/lseek_syscalls.c
