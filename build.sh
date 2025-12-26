#!/bin/sh
gcc -I./include src/main.c src/plugin_manager.c src/server.c -o main.out -lmicrohttpd -llua