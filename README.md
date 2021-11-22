# Table of Contents

1.  [Retrocord Client](#orgdfd25fd)
    1.  [Dependencies](#org36a4f96)
        1.  [Installing Nelua on Arch Linux](#orgeb9f85e)
        2.  [Installing Nelua on Debian](#org673a9dd)
    2.  [Contributors](#orgf224edf)



<a id="orgdfd25fd"></a>

# Retrocord Client

This is a client for Retrocord that has been written in JS with Webview for C, and uses a Nelua program to house the interface. Though, the interface can be ran without the Nelua program, simply by opening the file `web/index.html` in a web browser such as Firefox, Chromium or even QuteBrowser if you wanted to.


<a id="org36a4f96"></a>

## Dependencies

There are a few dependencies required before you can go ahead and run the `make` command. Below are listed the dependencies for Debian and Arch, and distributions based on those, such as Ubuntu and Manjaro Linux


<a id="orgeb9f85e"></a>

### Installing Nelua on Arch Linux

Installing Nelua on Arch and Arch Distributions can be done either via the git repository (see the *Installing Nelua on Debian* section), or can be done via the Arch User Repository. You can do this withg either an AUR Helper, such as `yay` or `paru`, simply with the following command

    # Using the Yay AUR Helper
    yay -S nelua         # Nelua Stable
    yay -S nelua-git     # Nelua Git

    # Using the Paru AUR Helper
    paru -S nelua        # Nelua Stable
    paru -S nelua-git    # Nelua Git

1.  Additional Dependencies for Arch

    Arch Linux also requires some other dependencies, other than Nelua. These include the GNU C Compiler, GNU Make and, optionally for Windows builds, the GCC Windows Environment. Most of these can be installed with the `base-devel` package, or either individually.

        # Using 'base-devel'
        sudo pacman -Sy base-devel mingw-w64-gcc

        # Individually
        sudo pacman -Sy make gcc mingw-w64-gcc


<a id="org673a9dd"></a>

### Installing Nelua on Debian

Installing Nelua on Debian and Debian Based Distributions requires you to clone the Nelua Git Repository. However, this is very simple to do. Simply run the following commands

    git clone https://github.com/edubart/nelua-lang.git
    cd nelua-lang
    make
    sudo make install

If this ran correctly, you should now have Nelua installed, but just to make sure, you should run the following command

    whereis nelua

If the following command doesn&rsquo;t output something similar to the following, then you have done something wrong and should start again from recloning the repository.

    nelua: /usr/local/bin/nelua /usr/local/lib/nelua

1.  Additional Dependencies for Debian

    Debian also requires the Webkit2GTK package to be installed. This is done by running the following commands

        sudo apt update && sudo apt install libwebkit2gtk-4.0-dev -y

    To build, you also require the `build-essential` package installed, which has the `make` and GNU C Compiler, though you can install them individually.

        # With the 'build-essential' package
        sudo apt install build-essential

        # Individually
        sudo apt install make gcc

        # For Windows Builds
        sudo apt install mingw-w64


<a id="orgf224edf"></a>

## Contributors
<a href="https://github.com/Elpersonn/Retrocord-client/graphs/contributors">
	<img src="https://contrib.rocks/image?repo=Elpersonn/Retrocord-client" />
</a>
