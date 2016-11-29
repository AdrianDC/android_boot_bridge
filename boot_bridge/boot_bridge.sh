#!/sbin/sh
#
# Copyright (C) 2016 Adrian DC
#

# Variables
BOOTIMAGE_KERNEL=;
BOOTIMAGE_TEMPLATE=;
BRIDGE_CREATED=;
PARTITION_LINK=;
RESULT=;

# Bootimage detection based on the find_boot_image logic by Chainfire
for PARTITION in kern-a KERN-A Kernel kernel KERNEL boot BOOT lnx LNX; do
  PARTITION_LINK=/dev/block/by-name/${PARTITION};
  if [ -L ${PARTITION_LINK} ] && [ -e ${PARTITION_LINK} ]; then
    BOOTIMAGE_KERNEL=$(readlink ${PARTITION_LINK});
    break;
  fi;
  PARTITION_LINK=/dev/block/platform/*/by-name/${PARTITION};
  if [ -L ${PARTITION_LINK} ] && [ -e ${PARTITION_LINK} ]; then
    BOOTIMAGE_KERNEL=$(readlink ${PARTITION_LINK});
    break;
  fi;
  PARTITION_LINK=/dev/block/platform/*/*/by-name/${PARTITION};
  if [ -L ${PARTITION_LINK} ] && [ -e ${PARTITION_LINK} ]; then
    BOOTIMAGE_KERNEL=$(readlink ${PARTITION_LINK});
    break;
  fi;
done;

# Bootimage template linkage
BOOTIMAGE_TEMPLATE=$(find ${PARTITION_LINK%/*} -print -maxdepth 0)/android_boot;

# Bootimage not found
if [ -z "${BOOTIMAGE_KERNEL}" ] || [ -z "${BOOTIMAGE_TEMPLATE}" ]; then
  return -1;
fi;

# Bridge mode detection
if [ -z "${1}" ] && [ -L "${BOOTIMAGE_TEMPLATE}" ]; then
  BRIDGE_CREATED=true;
fi;

# Path setup
cd /tmp/;

# Bridge creation
if [ -z "${BRIDGE_CREATED}" ]; then

  # Remove template leftovers
  rm -f ${BOOTIMAGE_TEMPLATE}.img;
  rm -f ${BOOTIMAGE_TEMPLATE};

  # Template creation
  cp -f /tmp/boot_bridge/boot_template.img ${BOOTIMAGE_TEMPLATE}.img;
  ln -s ${BOOTIMAGE_TEMPLATE}.img ${BOOTIMAGE_TEMPLATE};
  chmod 777 ${BOOTIMAGE_TEMPLATE};

  # Transfer to template
  chmod 755 /tmp/boot_bridge/boot_bridge;
  /tmp/boot_bridge/boot_bridge --import="${BOOTIMAGE_KERNEL}" --export="${BOOTIMAGE_TEMPLATE}";
  RESULT=${?};

  # Rename fstab targets
  if [ ${RESULT} -eq 0 ]; then
    sed -i "s|${BOOTIMAGE_KERNEL}|${BOOTIMAGE_TEMPLATE}|" /etc/recovery.fstab*;
    sed -i "s|${BOOTIMAGE_KERNEL}|${BOOTIMAGE_TEMPLATE}|" /fstab.*;

  # Cleanup failures
  else
    rm -f ${BOOTIMAGE_TEMPLATE}.img;
    rm -f ${BOOTIMAGE_TEMPLATE};
    sed -i "s|${BOOTIMAGE_TEMPLATE}|${BOOTIMAGE_KERNEL}|" /etc/recovery.fstab*;
    sed -i "s|${BOOTIMAGE_TEMPLATE}|${BOOTIMAGE_KERNEL}|" /fstab.*;
  fi;

# Bridge restore
else

  # Transfer to bootimage
  chmod 755 /tmp/boot_bridge/boot_bridge;
  /tmp/boot_bridge/boot_bridge --import="${BOOTIMAGE_TEMPLATE}" --export="${BOOTIMAGE_KERNEL}";
  RESULT=${?};

  # Remove template
  rm -f ${BOOTIMAGE_TEMPLATE}.img;
  rm -f ${BOOTIMAGE_TEMPLATE};

  # Rename fstab targets
  sed -i "s|${BOOTIMAGE_TEMPLATE}|${BOOTIMAGE_KERNEL}|" /etc/recovery.fstab*;
  sed -i "s|${BOOTIMAGE_TEMPLATE}|${BOOTIMAGE_KERNEL}|" /fstab.*;

fi;

# Result output
return ${RESULT};

