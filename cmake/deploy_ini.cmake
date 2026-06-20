# Seeds an INI into a deploy folder only if one is not already present, so a
# user's tuned/bound INI survives rebuilds. Delete the destination INI to
# re-seed from the repo default on the next build.
#
# Invoked in script mode: cmake -DSRC=<default ini> -DDST=<deployed ini> -P deploy_ini.cmake
if(NOT EXISTS "${DST}")
  message(STATUS "[deploy] Seeding INI -> ${DST}")
  configure_file("${SRC}" "${DST}" COPYONLY)
else()
  message(STATUS "[deploy] Preserving existing INI -> ${DST}")
endif()
