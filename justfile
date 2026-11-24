set positional-arguments

_default:
    @just --list

# Initialize the pinned SDK and project packages.
setup:
    bash tools/build_posix.sh setup

# Build firmware. Add SCons arguments such as APPS=hello or PREINSTALL=hello.
build *options:
    bash tools/build_posix.sh firmware "$@"

# Build selected .sap packages for the existing firmware.
apps ids *options:
    bash tools/build_posix.sh apps "$@"
