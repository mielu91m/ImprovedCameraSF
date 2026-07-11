Improved Camera SF - Pseudo FPP Camera - Vesion Beta


> ⚠ **Beta version 4**

-Three functions for controlling the pseudo-camera position have been added to the 
ImprovedCameraSF.ini file:
fUpOffset – camera height/vertical position
fForwardOffset – moves the camera forward/backward
fSideOffset – centers the camera if it drifts away from the middle
The provided values are my personal preferences and may or may not suit your taste, but you can always adjust them to your liking.
I haven't found an effective way to hide the head yet.

New in Beta Version 4

- Bug fixes;
- Fixed sitting and standing up animations for furniture;
- Added and optimized vehicle logic;
- Added the option to toggle the pseudo-camera on and off in the ImprovedCameraSF.ini file;
- Changed the logic for animations like SnuSnuField (the camera now follows the head, and positional adjustment works so you can set it to your liking);
- ADS from FPP;
- Added invisible headgear thanks to HooliG4N83 (it is automatically equipped and unequipped by the pseudo-camera; I didn't add the helmet to the code because it conflicted with spacesuits that have integrated helmets – if you need it, just type player.additem 04000900 1 in the console);
- Performance optimizations.

To Do:
- Spaceship/ship logic;
- Hiding the head via code;
- Fixing any encountered bugs.

## Overview

Improved Camera SF brings back the classic third-person-to-first-person hybrid camera — also known as **pseudo-FPP** or **first-person with visible body**. Instead of the game's standard first-person mode (which hides your character), this mod places the camera at your character's head while keeping the full body rendered, animated, and visible. The result is an immersive first-person view where you can see your own body, gear, and shadows.

Toggle between normal third-person and the enhanced first-person view with a hotkey.

## Features

- **Full body in first person** — See your character's body, equipment, and armor while in first-person view
- **Head tracking** — Camera follows the head bone during standard animations (walking, running, sprinting, jumping, aiming)
- **Player rotation on look** — Your character model rotates naturally when you look left or right in pseudo-FPP mode
- **Toggle on/off** — Default: Numpad /  (can be changed in the .ini file.)
- **Compatible with third-person animations** — Body animations play normally while in pseudo-FPP mode
- **INI files** — for manual tuning of the pseudo-camera.

## Requirements

- **Starfield** version 1.16.244 (or compatible)
- **SFSE** (Starfield Script Extender)
- **Address Library** (for SFSE)

## Installation
Mod Manager or manually:
1. Install SFSE and Address Library if you haven't already
2. Extract the archive
3. Copy `ImprovedCameraSF.dll`and `ImprovedCameraSF.ini` to `YourGameFolder\Data\SFSE\Plugins\`
4. Launch the game through SFSE

## Usage

The toggle hotkey is configurable. By default:
- **Press the toggle key** while in third-person to activate pseudo-FPP mode
- **Press again** to return to normal third-person


 ⚠ This is a **beta release**. The mod is functional but not yet polished. Expect imperfections.



## Technical Notes

Built with CommonLibSF and MinHook. The mod hooks into `ThirdPersonState::Update` and `TESCamera::Update` to override the camera position without modifying game files.

## Credits
- HooliG4N83 - for invisible headwear
- Libxe dla CommonLibSF, without which creating this type of plugin for Starfield would have been much more difficult. This file uses CommonLibSF, which is licensed under the GNU GPLv3 .
- Tsuda Kageyu for MinHook
- meh321﻿ for Address Library for SFSE Plugins.
- Ian Patterson﻿ for Starfield Script Extender.
- Inspired by Improved Camera for Skyrim SE
- 
