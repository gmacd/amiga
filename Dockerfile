# Host-side tools for demo1, layered on the same cross-compiler image the
# build uses so one `make tools-image` gives format and lint targets with no
# host installs. clang-format applies .clang-format (InsertBraces included);
# clang-tidy parses the sources the way .clangd does, through host-shim/.
FROM stefanreinauer/amiga-gcc:gcc-v16.2
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends \
        clang-format clang-tidy \
    && rm -rf /var/lib/apt/lists/*
