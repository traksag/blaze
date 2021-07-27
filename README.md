# Blaze

Handmade game server for Minecraft Java Edition.

Currently a principal goal of this project is to make it easy to update it to newer versions of the game. Other goals are speed and stability: the server shouldn't have CPU spikes, memory spikes, a very slow and expensive start up, and should contain as little failure cases as possible (i.e. stuff Java can't). Users should be able to determine and control the maximum memory usage easily in advance.

Some side effects of these goals are the following: we don't implement world generation, we don't support world upgrading, and we don't implement entity AI. Maybe it will be possible to write plugins for these kinds of things, if this project ever gets that far.

## Licence

All files in this repository are in the public domain. You can do whatever you want with them. No warranty is implied; use files in this repository at your own risk.

## System Requirements

This software is developed on macOS, but it should work fine on most UNIX systems. Maybe it works fine on Windows too. You must have zlib installed to build and run this program.

Your compiler must satisfy the following:

* Integers are represented using two's complement representation and bitwise operations such as AND, work as expected on signed integers.
* Signed right shift works by sign extension.
* Casting to an unsigned or signed integer type works as expected.

GCC satisfies these requirements as can be seen [here](https://gcc.gnu.org/onlinedocs/gcc/Integers-implementation.html). Hence Clang likely does as well (there is no accessible documentation of implementation-defined behaviour though).

## Usage

Build the server by running `./build.sh` if you're on Unix. It should be easy enough to adopt the build script on other systems. Note that there are a few configuration options at the top of 'build.sh' you may wish to modify.

To start the server, simply run `./blaze`. Currently the 'blaze' binary needs to be in the repository's root directory so it can read the data from the data files such as 'entitytags.txt' and 'itemtags.txt'. The server listens on localhost port 25565. Alter this in the code if you want.

Blaze can load chunks from Anvil region files. Create a folder called 'world' in the repository root and copy paste the 'region' folder from some other place into it. Note that Blaze only loads chunks from the latest Minecraft version, hence you may need to optimise your world before copy pasting the 'region' folder.

As of writing this, Blaze only runs in offline mode and has the following features:

1. Load chunks from region files with support for all block states.
2. Chunk streaming to clients.
3. Players can see each other in the world and in the tab list.
4. Chat messages.
5. Very basic inventory management.
6. In-memory block placing and breaking, and sending changes to clients.
7. Spawning items in from the creative mode inventory.
8. Server list ping with a sample of the online players.
9. Basic block update system.
10. Non-player entity movement and block collisions.

## Contributing

Contributions are welcome, provided you agree to put your contribution in the public domain.

A fair warning: I may deny a pull request if it doesn't fall in line with my goals (e.g. too complex). I may also deny a pull request and use parts of it to stitch together something myself.

If you have questions, remarks and otherwise, feel free to contact me by email via the email address I use to commit, or on Discord at traks#2633.
