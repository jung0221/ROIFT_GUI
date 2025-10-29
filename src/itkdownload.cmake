include(ExternalProject)

function(fetch_itk_if_missing)
    # If ITK already found, nothing to do
    find_package(ITK CONFIG QUIET)
    if (ITK_FOUND)
        include(${ITK_USE_FILE})
        message(STATUS "Found ITK: ${ITK_VERSION}")
        return()
    else()
        message(STATUS "ITK not found: configuring ExternalProject to download and build ITK (this can take a long time).")
    endif()

    if (NOT USE_ITK)
        message(STATUS "USE_ITK is OFF; skipping automatic ITK fetch/build")
        return()
    endif()


    # Use a short source path on Windows to avoid path-length issues.
    if (WIN32)
        set(ITK_SRC_DIR "C:/libs/itk_src")
        set(ITK_BUILD_DIR "C:/libs/itk_build")
        set(ITK_INSTALL_DIR "C:/libs/itk_install")
    else()
        set(ITK_SRC_DIR ${CMAKE_BINARY_DIR}/itk_src)
        set(ITK_BUILD_DIR ${CMAKE_BINARY_DIR}/itk_build)
        set(ITK_INSTALL_DIR ${CMAKE_BINARY_DIR}/itk_install)
    endif()
    ExternalProject_Add(itk_ep
        GIT_REPOSITORY https://github.com/InsightSoftwareConsortium/ITK.git
        GIT_TAG release
        SOURCE_DIR ${ITK_SRC_DIR}
        BINARY_DIR ${ITK_BUILD_DIR}
        CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX=${ITK_INSTALL_DIR}
            -DCMAKE_BUILD_TYPE=Release
            -DBUILD_SHARED_LIBS=ON
            -DBUILD_EXAMPLES=OFF
            -DBUILD_TESTING=OFF
            -DBUILD_DOCUMENTATION=OFF
            -DITK_LEGACY_REMOVE=ON
            -DModule_ITKReview=OFF
            -DZLIB_ROOT=${ZLIB_ROOT}
            -DCURL_ROOT=${CURL_ROOT}
        BUILD_BYPRODUCTS ${ITK_INSTALL_DIR}/lib/cmake/ITK-*/ITKConfig.cmake
        INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --target install
        UPDATE_COMMAND ""
        TIMEOUT 0
    )

    # Make the install folder visible to subsequent find_package calls.
    set(ITK_DIR ${ITK_INSTALL_DIR}/lib/cmake/ITK CACHE PATH "ITK install folder" FORCE)
    message(STATUS "Configured ExternalProject 'itk_ep'. ITK will be installed to: ${ITK_INSTALL_DIR}")

    # Record the EP so callers can add dependencies on it
    set_property(GLOBAL APPEND PROPERTY ROIFT_EXTERNAL_PROJECTS itk_ep)
endfunction()