# Welcome to ttyplot!

This file documents how to contribute to ttyplot and
things to know before facing them.  It will likely
never be complete, but if something important
turns out to be missing here, let's add it!


## Problem: The CI complains that two images are too dissimilar

The CI does basic UI testing by making screenshots
of the running application and comparing them
against pre-recorded screenshots `recordings/expected-*.png`.
When you make changes to ttyplot that change the *runtime appearance* of
ttyplot, the CI will hopefully catch these changes
and reject them as a regression.  In a case where these
changes are made with full intention, the images
that the CI is comparing to at `recordings/expected-*.png`
will then need to be regenerated.
For your convenience, an easy way to do that is:

```console
$ ./recordings/get_back_in_sync.sh
```

The script will re-render these images and even create a Git commit
for you.

For all that to work well on macOS, you would need to install:

```console
$ brew tap homebrew/cask-fonts
$ brew install \
    agg \
    asciinema \
    coreutils \
    font-liberation
```

On a Debian-based Linux including Ubuntu, you would need to install…

```console
$ sudo apt-get update
$ sudo apt-get install --no-install-recommends -V \
    asciinema \
    ca-certificates \
    cargo \
    fonts-liberation
$ cargo install --git https://github.com/asciinema/agg
```

…and put the install location of `agg` into `${PATH}`:

```console
$ export PATH="${PATH}:${HOME}/.cargo/bin"
```
