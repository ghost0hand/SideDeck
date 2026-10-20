# Third-party components

SideDeck's optional input-isolation package uses:

- HidHide 1.5.230.0, Nefarius Software Solutions and contributors, MIT.
  Official source: https://github.com/nefarius/HidHide/tree/v1.5.230.0
- ViGEmClient native x64 DLL distributed in Nefarius.ViGEm.Client 1.21.256,
  Nefarius Software Solutions and contributors, MIT.
  Official source: https://github.com/nefarius/ViGEmClient
- ViGEmBus 1.22.0, Nefarius Software Solutions and contributors, BSD-3-Clause.
  Official source: https://github.com/nefarius/ViGEmBus/tree/v1.22.0
  Its original signed installer retains its license and notices.

The package includes the original driver installers without modification. Drivers
are installed only through their own installation flow after an explicit user
action. ViGEmBus is end-of-life; basic Xbox 360 input/rumble is the supported target
of this experimental integration. No DS5 adaptive triggers, motion or touchpad
forwarding is promised. See the bundled licenses directory for license texts.

Inno Setup compiles the installer; it is a build tool and is not installed by the
SideDeck package. Its runtime license notice is retained by the generated setup.
