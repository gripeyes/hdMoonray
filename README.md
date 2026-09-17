# hdMoonray - part of the [MoonRay](https://github.com/OpenMoonRay/openmoonray) project
Policies concerning [Governance](https://github.com/OpenMoonRay/openmoonray/blob/main/GOVERNANCE.md), [Code of Conduct](https://github.com/OpenMoonRay/openmoonray/blob/main/CODE_OF_CONDUCT.md), and [Contribution](https://github.com/OpenMoonRay/openmoonray/blob/main/CONTRIBUTING.md) are available in the overarching MoonRay project, defined in the [`OpenMoonRay/openmoonray` GitHub repository superproject](https://github.com/OpenMoonRay/openmoonray).

This repository contains the Hydra plugin (delegate) for MoonRay, HdMoonRay.
This allows Moonray to be used to render the viewer of DCC applications,
and as part of the command-line tool to render USD using Moonray.

## Houdini 22 timeline updates

Motion-sampled point updates must write the complete vertex arrays to RDL's
`vertex_list_0` and `vertex_list_1`. Previously, this path passed one vertex instead
of an array; the resulting attribute type mismatch left the previous geometry in
place when scrubbing the timeline. The fix changes only those two assignments.
It does not change AOVs, convergence, viewport/product classification, Arras
sessions, or the transport protocol.

With `BUILD_TESTING=ON`, run `ctest -R hdmoonray_primvar_sampling --output-on-failure`
from the build directory, using the same Houdini runtime library environment as
the build. The regression exercises the production points dispatch for
`1 -> 10 -> 1`, verifies every vertex in both motion samples, and checks empty
arrays. It fails against the previous implementation.

Validated on Houdini 22.0.440 with the normal Arras-backed **Moonray** delegate:
timeline changes and rapid scrubbing update geometry, an unchanged frame does
not restart rendering, and a light edit updates IPR. An installed-delegate USD
render produces non-black pixels and its test-created MCRT process exits after
teardown. The shading tests also pass. After installing the rebuilt shared Hydra
library and normal plugin, restart Houdini to load the updated binaries.

## Render Settings
There are a number of switches that control the Render. In usdview these are under
"View/Hydra Settings".  In Houdini the "eye" button in the lower-right of the Viewer
brings these up, look on the first tab and select Moonray. In Maya the empty checkbox
next to Moonray in the Renderer menu does this. Both Houdini and Maya have the
advantage that you can edit the settings before running Moonray, and your settings
are saved for the next time.

To use the render farm, set the number of Hosts first, then turn on "Use Remote
Hosts". Messages from the remote renders are controlled by "Log Level", this is
independent of the "Debug" and "Info" settings.

"Restart" and "Reload Textures" are toggles as Hydra does not support any kind of
"button". Changing the value (on or off) triggers the action.

"Debug mode" is an in-process renderer. If Moonray crashes the host app will crash as
well. The Restart toggle does not work (the Houdini menu item to Restart Render does,
and in usdview you can switch to GL and back).  Currently this mode is not working
with Houdini-19 and locks up Houdini until the render finishes.

### Environment variables

You can set some environment variables to get arras processor allocations, these are not
available as settings:

    HDMOONRAY_STACK=env-dc use "dc" and "env" as args to requestArrasUrl()
    HDMOONRAY_STACK=env same as HDMOONRAY_STACK=env-gld
    HDMOONRAY_PRODUCTION=name set production name

A couple environment variables are provided to debug and recover from crashes that
happen at startup, and (especially for usdview) to get the first render to be with
the desired settings:

    HDMOONRAY_DEBUG=1 turn on debug messages (not debug mode!)
    HDMOONRAY_INFO=1 turn on info messages
    HDMOONRAY_RDLA_OUTPUT=filename dump rdla/rdlb before rendering
    HDMOONRAY_DISABLE=1 do not run the renderer
    HDMOONRAY_DEBUG_MODE=1 run debug (in-process) renderer
    HDMOONRAY_HOSTS=5 enable remote hosts and set count
    HDMOONRAY_LOGLEVEL=5 set remote logging level to N (1 is default, 5 is max)
    HDMOONRAY_DISABLE_LIGHTING=1 Turn off all the lights (and turn on the default dome light)
    HDMOONRAY_DOUBLESIDED=1 Make all geometry doublesided unless moonray:side_type=1
