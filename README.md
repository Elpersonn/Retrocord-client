
# Table of Contents

1.  [Retrocord Client](#org5292416)
    1.  [Dependencies](#org44509a5)
    2.  [Contributors](#org0ec09df)



<a id="org5292416"></a>

# Retrocord Client

This is a client for Retrocord that has been written in JS with Webview for C, and uses a Nelua program to house the interface. Though, the interface can be ran without the Nelua program, simply by opening the file `web/index.html` in a web browser such as Firefox, Chromium or even QuteBrowser if you wanted to.


<a id="org44509a5"></a>

## Dependencies

There are a few dependencies that need to be installed before you can do a `make`, these are the GNU C Compiler (gcc), [Nelua (AUR)](https://aur.archlinux.org/packages/nelua) and, optionally for building a Windows build - the GCC Windows Compiler `mingw-w64-gcc` (Arch Based Distributions), `mingw-w64` (Debian/Ubuntu Based Distributions).

To install Nelua, you can use the AUR Package, and install it with an AUR helper such as `paru`, or manually. If you choose to install it manually, follow the commands below. This also requires you to have `fakeroot` installed. I recommend installing the `base-devel` package, as it includes all the tools required.

AUR Stable Package

    git clone https://aur.archlinux.org/nelua.git
    cd nelua
    makepkg -si

AUR Git Package

    git clone https://aur.archlinux.org/nelua-git.git
    cd nelua-git
    makepkg -si


<a id="org0ec09df"></a>

## Contributors

<a href="https://github.com/Elpersonn/Retrocord-client/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=Elpersonn/Retrocord-client" />
</a>

