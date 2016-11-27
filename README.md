# BootBridge for Sony ELF
 * <b>[See the project on XDA](http://forum.xda-developers.com/-/-/-t3506883)</b>

## What are Sony ELF bootimages

 * Devices like the Sony Xperia SP,
    T, TX, V or other from these generations
    use a bootimage structure different
    to the usual Android boot partitions

 * BootBridge is meant to simulate
    a normal bootimage, edit it
    then update the Sony ELF boot

 * With support for newer ELF bootimages
    like on the Sony Xperia Z2, BootBridge
    will perform the same tasks, but also
    recreate a regular Android bootimage
    that will allow the device to boot

## Where does the user have to do something

 * Simply open the zip, and replace the
    install/install.zip with the renamed
    flashable you want to install

## How the project works and what it does
 * Creates a new temporary bootimage
    using the regular Android structure
    and link it in the fstabs

 * Flash the zip chosen by the user,
    placed in install/install.zip,
    with support to systemless boot

 * Rebuild the original bootimage
    with the newly introduced changes
