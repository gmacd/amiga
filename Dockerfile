# check=skip=InvalidDefaultArgInFrom
# Host-side tools for demo1, layered on the same cross-compiler image the
# build uses so `make tools-image` (run automatically by the format and lint
# targets) gives them with no host installs. IMAGE is passed in by the
# Makefile, pinned by digest, so the compiler and the tools always match.
# clang-format and clang-tidy are pinned to major version 21 and given
# their unversioned names: a new major version can reformat code.
# clang-format applies .clang-format (InsertBraces included); clang-tidy
# parses the sources the way .clangd does, through host-shim/.
ARG IMAGE
FROM ${IMAGE}
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends \
        clang-format-21 clang-tidy-21 \
    && ln -sf /usr/bin/clang-format-21 /usr/local/bin/clang-format \
    && ln -sf /usr/bin/clang-tidy-21 /usr/local/bin/clang-tidy \
    && rm -rf /var/lib/apt/lists/*
