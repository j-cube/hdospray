set(ARGS_OSPRAY_DOWNLOAD URL ${SUPERBUILD_OSPRAY_URL})
  if (OSPRAY_USE_GIT)
    set(ARGS_OSPRAY_DOWNLOAD 
    GIT_REPOSITORY ${SUPERBUILD_OSPRAY_URL}
    GIT_TAG ${SUPERBUILD_OSPRAY_VERSION}
    GIT_SHALLOW   ON)
  endif()

  set(EP_OSPRAY "OSPRay")
  ExternalProject_Add (
    ${EP_OSPRAY}
    PREFIX ${EP_OSPRAY}
    ${ARGS_OSPRAY_DOWNLOAD}
    STAMP_DIR     ${EP_OSPRAY}/stamp
    SOURCE_DIR    ${EP_OSPRAY}/source
    BINARY_DIR    ${EP_OSPRAY}/build
    CONFIGURE_COMMAND ${CMAKE_COMMAND} ${PROJECT_BINARY_DIR}/${EP_OSPRAY}/source/scripts/superbuild
      -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
      -DCMAKE_PREFIX_PATH=${INSTALL_PREFIX}
      -DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX}
      -DINSTALL_IN_SEPARATE_DIRECTORIES=OFF
      -DDOWNLOAD_ISPC=ON
      -DDOWNLOAD_TBB=OFF
      -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
      -DBUILD_OIDN=${SUPERBUILD_USE_DENOISER}
      -DTBB_PATH=${TBB_PATH}
      # -DOSPRAY_BUILD_ISA=ALL
      # -DOSPRAY_ENABLE_TESTING=OFF
      # -DOSPRAY_MODULE_BILINEAR_PATCH=OFF
      # -DOSPRAY_ENABLE_MODULES=OFF
      # -DOSPRAY_MODULE_DENOISER=ON
      # -DOSPRAY_STRICT_BUILD=OFF
      # -DOSPRAY_WARN_AS_ERRORS=OFF
      # -Dembree_DIR=${CMAKE_INSTALL_PREFIX}/lib/cmake/embree-3.13.0
      # -Drkcommon_DIR=${CMAKE_INSTALL_PREFIX}/lib/cmake/rkcommon-1.6.1
      # -Dopenvkl_DIR=${CMAKE_INSTALL_PREFIX}/lib/cmake/openvkl-0.13.0
    INSTALL_COMMAND ""
  )

  ExternalProject_Add_StepDependencies(${EP_OSPRAY}
    configure ${EP_USD}
  )