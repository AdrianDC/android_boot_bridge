#!/sbin/sh
#
# Copyright (C) 2016 Adrian DC
#

# Parameters
INSTALL_TMP=${1};
INSTALL_ZIP=${2};

# Variables
TOYBOX=${INSTALL_TMP}/toybox;
UPDATE_BINARY_PATH=META-INF/com/google/android/update-binary;
UPDATE_BINARY_TMP=${INSTALL_TMP}/${UPDATE_BINARY_PATH};
RESULT=;

# Edify output, adapted from Chainfire
OUTFD=$(${TOYBOX} ps -w -o ARGS \
      | ${TOYBOX} grep -v 'grep' \
      | ${TOYBOX} grep -oE 'update(.*)' \
      | ${TOYBOX} cut -d ' ' -f 3);

# ui_print, adapted from Chainfire
ui_print()
{
  if [ ! -z ${OUTFD} ]; then
    if [ ! -z "${1}" ]; then
      ${TOYBOX} echo "ui_print ${1}" > /proc/self/fd/${OUTFD};
    fi;
    ${TOYBOX} echo "ui_print " > /proc/self/fd/${OUTFD};
  else
    ${TOYBOX} echo ${1};
  fi;
}

# Missing inputs
if [ ! -d "${INSTALL_TMP}" ] || [ ! -f "${INSTALL_TMP}/${INSTALL_ZIP}" ]; then
  ui_print ' ';
  ui_print ' Error in flash_zip: Missing inputs';
  ui_print ' ';
  return 1;
fi;

# ZIP extraction
if ! unzip -oq ${INSTALL_TMP}/${INSTALL_ZIP} ${UPDATE_BINARY_PATH} -d ${INSTALL_TMP}; then
  ui_print ' ';
  ui_print ' Error in flash_zip: unzip failed';
  ui_print ' ';
  return 2;
fi;

# Update preparation
chmod 755 ${UPDATE_BINARY_TMP};

# Update execution
${UPDATE_BINARY_TMP} 3 ${OUTFD:-0} ${INSTALL_TMP}/${INSTALL_ZIP};
RESULT=${?};

# Result output
return ${RESULT};

