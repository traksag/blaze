# Blaze

Handmade game server for Minecraft Java Edition 1.21.3.

Currently the principal goal of this project is to make an efficient server for the latest version of Minecraft.
I've considered using Java for this, but I feel C is a lot more interesting in terms of optimisation potential.
Many vanilla game mechanics will be implemented, though the goal is not to recreate the full vanilla experience.
Some things are not planned to be implemented: world generation, world upgrading and entity AI.
Eventually it might be possible to write plugins for these kinds of things, if this project ever gets that far.

Many thanks to the authors of [https://wiki.vg](https://wiki.vg) for their documentation of the Minecraft protocol!

## Licence

All files in this repository are in the public domain. You can do whatever you want with them. No warranty is implied; use files in this repository at your own risk.

## System Requirements

This software can be compiled and run with Clang or GCC on mainstream hardware running macOS or Linux. You must have zlib installed to build and run this software.

If you're using a different compiler, a different operating system or exotic hardware, make sure the following are satisfied:

* Integers are represented using two's complement representation and bitwise operations such as 'and', work as expected on signed integers.
* Signed right shift works by sign extension.
* Casting one integer type to another integer type works by modulo reduction to the range of the target type.
* Several preprocessor macros and intrinsics must be available. These may give you compilation errors.
* Floats and doubles are encoded as IEEE 754 binary32 and binary64 respectively.

## Usage

Build the server by running `./build.sh` if you're on Unix. It should be easy enough to adopt the build script on other systems. Note that there are a few configuration options at the top of 'build.sh' you may wish to modify.

To start the server, simply run `./blaze`. The server listens on localhost port 25565. Alter this in the code if you want.

Blaze can load chunks from Anvil region files. Create a folder called 'world' in your working directory and copy paste the 'region' folder from some other place into it. Note that Blaze only loads chunks from the latest Minecraft version, hence you may need to optimise your world before copy pasting the 'region' folder.

As of writing this, Blaze runs in offline mode and has the following features:

1. Async chunk loading from region files with support for all block states.
2. Chunk streaming to clients.
3. Players can see each other in the world and in the tab list.
4. Chat messages.
5. Very basic inventory management.
6. In-memory block placing and breaking, and sending changes to clients.
7. Spawning items in from the creative mode inventory.
8. Server list ping with a sample of the online players.
9. Basic block update system.
10. Non-player entity movement and block collisions.
11. Basic light engine.

## Contributing

Contributions are welcome, provided you agree to put your contribution in the public domain.

A fair warning: I may deny a pull request if it doesn't fall in line with my goals (e.g. too complex). I may also deny a pull request and use parts of it to stitch together something myself.

If you have questions, remarks and otherwise, feel free to contact me by email via the email address I use to commit. You can also contact me on Discord, my username is traksag.
