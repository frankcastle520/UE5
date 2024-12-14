# Android Game Development Kit (AGDK)

## Introduction

This bundled package is provided as a convenience for developers using legacy build
methods, we encourage developers to use the gradle & maven approach if possible.

## Included libraries

The following libraries are included in this bundle:

* game-activity
* game-controller (paddleboat)
* game-text-input
* games-frame-pacing (swappy)
* games-performance-tuning (tuningfork)
* games-memory-advice

The source code for these libraries is available at the [gerrit
repo](https://android.googlesource.com/platform/frameworks/opt/gamesdk/)

## Bundle structure

The bundle contains the following:

* All the AARs published on maven are in the root directory
* Header files for the libraries are in the `include` directory
* Library files (both static & shared) are in the `libs` directory
* Per-library classes.jar files are in the `jar-classes` directory

## Additional information

For more details please visit the [AGDK
website](https://developer.android.com/games/agdk/overview)
