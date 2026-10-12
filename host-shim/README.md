# host-shim

Prototype-only replacements for the NDK's `proto/*.h` headers, for host
tools that read the sources without the Amiga compiler (clangd, VS Code
IntelliSense, cppcheck). The real `proto/` headers pull in `inline/`
wrappers written as GCC register-asm, which host clang cannot parse. These
include the plain `clib/*_protos.h` prototypes instead, which is all an
editor or linter needs. Put this directory on the include path *before*
`ndk-include`. The Docker build never sees it.
