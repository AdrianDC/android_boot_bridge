#!/sbin/sh
#
# Copyright (C) 2016 Adrian DC
#

# Variables
BOOTIMAGE_ORIGINAL=;
BOOTIMAGE_PATH=;
BRIDGE_CREATED=;
RESULT=${2};

# Bootimage detection based on the find_boot_image logic by Chainfire
for PARTITION in kern-a KERN-A Kernel kernel KERNEL boot BOOT lnx LNX; do
  BOOTIMAGE_PATH=/dev/block/by-name/${PARTITION};
  if [ -L ${BOOTIMAGE_PATH} ] && [ -e ${BOOTIMAGE_PATH} ]; then
    break;
  fi;
  BOOTIMAGE_PATH=/dev/block/platform/*/by-name/${PARTITION};
  if [ -L ${BOOTIMAGE_PATH} ] && [ -e ${BOOTIMAGE_PATH} ]; then
    break;
  fi;
  BOOTIMAGE_PATH=/dev/block/platform/*/*/by-name/${PARTITION};
  if [ -L ${BOOTIMAGE_PATH} ] && [ -e ${BOOTIMAGE_PATH} ]; then
    break;
  fi;
done;

# Bootimage template linkage
BOOTIMAGE_PATH=$(find ${BOOTIMAGE_PATH} -print -maxdepth 0 | head -n 1)
BOOTIMAGE_ORIGINAL=${BOOTIMAGE_PATH%/*}/boot_original.img;

# Bootimage not found
if [ -z "${BOOTIMAGE_PATH}" ] || [ -z "${BOOTIMAGE_ORIGINAL}" ]; then
  return 1;
fi;

# Bridge mode detection
if [ ! "${1}" = 'init' ] && [ -f "${BOOTIMAGE_ORIGINAL}" ]; then
  BRIDGE_CREATED=true;
fi;

# Path setup
cd /tmp/;

# Bridge creation
if [ -z "${BRIDGE_CREATED}" ]; then

  # ELF bootimage backup
  rm -f "${BOOTIMAGE_ORIGINAL}";
  dd if="${BOOTIMAGE_PATH}" of="${BOOTIMAGE_ORIGINAL}";

  # Template bootimage creation
  dd if=/dev/zero of="${BOOTIMAGE_PATH}";
  dd if=/tmp/boot_bridge/boot_template.img of="${BOOTIMAGE_PATH}";

  # Transfer to template bootimage
  chmod 755 /tmp/boot_bridge/boot_bridge;
  /tmp/boot_bridge/boot_bridge --import="${BOOTIMAGE_ORIGINAL}" --export="${BOOTIMAGE_PATH}";
  RESULT=${?};

  # Cleanup failures
  if [ ${RESULT} -ne 0 ]; then
    dd if=/dev/zero of="${BOOTIMAGE_PATH}";
    dd if="${BOOTIMAGE_ORIGINAL}" of="${BOOTIMAGE_PATH}";
    rm -f "${BOOTIMAGE_ORIGINAL}";
  fi;

# Bridge restore
else

  # Transfer to ELF bootimage
  if [ ! "${1}" = 'failed' ]; then
    chmod 755 /tmp/boot_bridge/boot_bridge;
    /tmp/boot_bridge/boot_bridge --import="${BOOTIMAGE_PATH}" --export="${BOOTIMAGE_ORIGINAL}";
    RESULT=${?};
  fi;

  # ELF bootimage restore
  dd if=/dev/zero of="${BOOTIMAGE_PATH}";
  dd if="${BOOTIMAGE_ORIGINAL}" of="${BOOTIMAGE_PATH}";
  rm -f "${BOOTIMAGE_ORIGINAL}";

fi;

# Result output
return ${RESULT};

