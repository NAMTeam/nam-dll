# nam-dll

A DLL Plugin for SimCity 4 that improves interoperability with the Network Addon Mod.

## Features

### Additional Network Tool Shortcuts

Adds keyboard shortcuts for the monorail, one way road, dirt road and ground highway network tools.
This is something that Maxis left partially implemented, this DLL adds the missing features.

If you are using a mod that alters the city keyboard shortcuts such as CasperVG's AutoHistorical DLL mod, you will need to edit the KEYCFG
file included in that mod.
Otherwise, create a new patch that overrides the city KEYCFG file in SimCity_1.dat, TGI 0xA2E3D533, 0x6A231EAA, 0x6A9362EF.    

Then add the following data to the end of the new KEYCFG file:

```
; Network tool keyboard shortcuts
Control Y               = 0x8BE098F4 "Monorail"
Control E               = 0x6BE098FA "Dirt Road"
Shift E                 = 0x4BE098F7 "One-way Road"
Alt E                   = 0x4BE098FD "Ground Highway"
```

You can customize the shortcuts to use different keys, see the documentation at the top of the KEYCFG file.

## System Requirements

* Windows 10 or later

The plugin may work on Windows 7 or later with the [Microsoft Visual C++ 2022 x86 Redistribute](https://aka.ms/vs/17/release/vc_redist.x86.exe) installed, but I do not have the ability to test that.

## Installation

1. Close SimCity 4.
2. Copy `NAM.dll` into the Plugins folder in the SimCity 4 installation directory.
3. Start SimCity 4.

## Troubleshooting

The plugin should write a `NAM.log` file in the same folder as the plugin.    
The log contains status information for the most recent run of the plugin.

# License

This project is licensed under the terms of the GNU Lesser General Public License version 3.0.    
See [LICENSE.txt](LICENSE.txt) for more information.

## 3rd party code

[gzcom-dll](https://github.com/nsgomez/gzcom-dll/tree/master) Located in the vendor folder, MIT License.    
[Windows Implementation Library](https://github.com/microsoft/wil) MIT License    
[SC4Fix](https://github.com/nsgomez/sc4fix) MIT License

# Source Code

## Prerequisites

* Visual Studio 2022

## Building the plugin

* Open the solution in the `src` folder
* Update the post build events to copy the build output to you SimCity 4 application plugins folder.
* Build the solution

## Debugging the plugin

Visual Studio can be configured to launch SimCity 4 on the Debugging page of the project properties.
I configured the debugger to launch the game in a window with the following command line:    
`-intro:off -CPUcount:1 -w -CustomResolution:enabled -r1920x1080x32`

You may need to adjust the resolution for your screen.

## Building the DLL on Linux

It is possible to compile the DLL on Linux using `clang` as a cross-compiler.
Check the [Makefile](Makefile) for details.
