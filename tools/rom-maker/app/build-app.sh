#!/bin/sh
# Builds "Pac-Land MD ROM Maker.exe": a .NET Framework 4.x Windows Forms app
# (built into Windows 10 and 11) with the source-only maker ZIP embedded.
# Needs the Mono C# compiler (mcs) and its 4.5 reference assemblies.
set -eu
cd -- "$(dirname -- "$0")/../../.."
python3 tools/rom-maker/maker.py --package build/rom-maker-kit/Pac-Land-MD-ROM-Maker.zip
# Compile against the exact .NET Framework 4.5 API (Mono's 4.5-api reference set),
# so nothing newer than Windows provides can slip in.
L=${MONO_API:-/usr/lib/mono/4.5-api}
mcs -nostdlib -noconfig -target:winexe -r:$L/mscorlib.dll -optimize+ -out:"build/rom-maker-kit/Pac-Land MD ROM Maker.exe" \
  -r:$L/System.Windows.Forms.dll -r:$L/System.Drawing.dll -r:$L/System.IO.Compression.dll \
  -r:$L/System.IO.Compression.FileSystem.dll -r:$L/System.dll -r:$L/System.Core.dll \
  -resource:build/rom-maker-kit/Pac-Land-MD-ROM-Maker.zip,maker.zip tools/rom-maker/app/Maker.cs
