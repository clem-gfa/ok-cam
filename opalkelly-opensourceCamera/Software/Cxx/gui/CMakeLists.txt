# wxWidgets camera GUI. Cross-platform via CMake's FindwxWidgets.
#   Windows : uses the prebuilt wxWidgets at %WXWIN% (3.2.x, e.g. C:\wx320), vc14x_x64_dll.
#   Linux   : apt install libwxgtk3.2-dev   (find_package picks it up via wx-config)
#   macOS   : brew install wxwidgets        (3.3.x as of 2026-08; 3.2 and 3.3 both build clean)
#
# The 3.2 and 3.3 series are both supported. macOS CI installs whatever Homebrew ships, which
# moved to 3.3 -- verified building clean, including the bundled/fixup_bundle release path.

# On Windows, point FindwxWidgets at the prebuilt tree (it has no CMake config package). WXWIN wins;
# otherwise use the prebuilt fetched into third_party/wxwidgets by scripts/setup-wxwidgets.ps1.
#
# WXWIN is only honoured if it actually looks like a wxWidgets tree. A stale or placeholder value
# would otherwise win over both the in-tree fallback and a user-supplied -DwxWidgets_ROOT_DIR, and
# the build would fail with a misleading "Could NOT find wxWidgets" rather than naming the cause.
if(WIN32)
    set(_wxroot "")
    if(DEFINED ENV{WXWIN})
        if(EXISTS "$ENV{WXWIN}/include/wx/version.h")
            set(_wxroot "$ENV{WXWIN}")
        else()
            # Warn and fall through to the in-tree fallback rather than failing outright, so a
            # stale WXWIN degrades to "ignored" instead of "build broken".
            message(WARNING
                "WXWIN is set to '$ENV{WXWIN}' but that is not a wxWidgets tree "
                "(no include/wx/version.h) -- ignoring it.")
        endif()
    endif()
    if(NOT _wxroot AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/wxwidgets/include/wx/version.h")
        set(_wxroot "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/wxwidgets")
    endif()
    # An explicit -DwxWidgets_ROOT_DIR from the caller is honoured if neither of the above found
    # anything, so a one-off build can point at any tree without touching the environment.
    if(NOT _wxroot AND wxWidgets_ROOT_DIR AND EXISTS "${wxWidgets_ROOT_DIR}/include/wx/version.h")
        set(_wxroot "${wxWidgets_ROOT_DIR}")
    endif()
    if(NOT _wxroot)
        message(FATAL_ERROR
            "wxWidgets not found. Run scripts/setup-wxwidgets.ps1 to fetch it into "
            "Software/Cxx/third_party/wxwidgets, set the WXWIN environment variable, or pass "
            "-DwxWidgets_ROOT_DIR=<path>.")
    endif()
    set(wxWidgets_ROOT_DIR "${_wxroot}")
    if(EXISTS "${_wxroot}/lib/vc14x_x64_dll")
        set(wxWidgets_LIB_DIR "${_wxroot}/lib/vc14x_x64_dll")
    endif()
    set(wxWidgets_CONFIGURATION mswu)
endif()

# Components the app actually uses: the XRC instantiates only core controls and RgbViewport paints
# via wxDC, so neither the adv nor the gl library is linked. Because only these components are linked,
# the release fixup (fixup_bundle, below) bundles only their runtime libraries -- the unused wx
# libraries (adv/gl/media/...) are never copied, with no skip-list to maintain.
find_package(wxWidgets REQUIRED COMPONENTS core base xrc html xml)
include(${wxWidgets_USE_FILE})

# Windows version resource, so the .exe carries the same version macOS puts in Info.plist.
# Without it the file properties are blank and neither a customer nor support can identify the
# build. Configured from the single PROJECT_VERSION above.
if(WIN32)
    configure_file(version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/version.rc" @ONLY)
    set(_okcam_rc "${CMAKE_CURRENT_BINARY_DIR}/version.rc")
endif()
add_executable(okcameragui WIN32 main_gui.cpp RgbViewport.cpp embedded_assets.cpp ${_okcam_rc})
# Customer-facing executable name (the docs + release archives refer to okCameraApp).
set_target_properties(okcameragui PROPERTIES OUTPUT_NAME okCameraApp)

# On macOS, build a .app bundle so the app launches from a Finder double-click (a bare Mach-O would
# open in Terminal instead). The executable goes in Contents/MacOS, editable data (camera_axi.lua,
# Bitfiles) in Contents/Resources, and the bundled libraries (wxWidgets + the FrontPanel runtime) in
# Contents/Frameworks -- Apple's idiomatic layout, which also lets the bundle code-sign cleanly.
if(APPLE)
    set_target_properties(okcameragui PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "okCameraApp"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.opalkelly.okCameraApp"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
endif()
# Link the shared backend (brings the FP6 SDK + NOMINMAX + include dirs transitively) + wxWidgets.
target_link_libraries(okcameragui PRIVATE okcamera_backend ${wxWidgets_LIBRARIES})

# RPATH so the executable (and the libraries bundled next to / inside it) resolve at runtime.
#   macOS : Contents/Frameworks for the bundled wx + FrontPanel libs; @loader_path is a hedge so the
#           bare-name dlopen("okimpl_fpoip.bundle") inside libokFrontPanel also finds the plugin.
#   Linux : $ORIGIN (libraries sit next to the exe). When bundling, emit DT_RPATH
#           (--disable-new-dtags) rather than DT_RUNPATH so the search also covers the wx libraries'
#           dependencies on one another.
# Bundling the wx runtime is a RELEASE-PACKAGING step (OKCAM_BUNDLE_WX, below); the BUILD_RPATH keeps
# a from-source build runnable in place, and INSTALL_RPATH is what the packaged app ships with.
if(APPLE)
    set_target_properties(okcameragui PROPERTIES
        BUILD_RPATH   "@loader_path;@executable_path/../Frameworks"
        INSTALL_RPATH "@executable_path/../Frameworks;@loader_path")
elseif(UNIX)
    set_target_properties(okcameragui PROPERTIES
        BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()

option(OKCAM_BUNDLE_WX "Bundle the wxWidgets runtime into the install tree (for the release packaging)" OFF)
if(OKCAM_BUNDLE_WX AND UNIX AND NOT APPLE)
    target_link_options(okcameragui PRIVATE "-Wl,--disable-new-dtags")
endif()

# Where editable data (camera_axi.lua, Bitfiles/) is staged in the BUILD tree. On macOS that is the
# bundle's Contents/Resources (Apple's layout, and required for the .app to code-sign cleanly); on
# Windows and Linux it sits flat next to the executable. main_gui.cpp's resourceDir() mirrors this
# split. (The installed/packaged tree gets the same data via the install() rules near the end.)
if(APPLE)
    set(_data_dir "$<TARGET_FILE_DIR:okcameragui>/../Resources")
    set(_fw_dir   "$<TARGET_FILE_DIR:okcameragui>/../Frameworks")
    add_custom_command(TARGET okcameragui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_data_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_fw_dir}")
else()
    set(_data_dir "$<TARGET_FILE_DIR:okcameragui>")
    set(_fw_dir   "$<TARGET_FILE_DIR:okcameragui>")
endif()

# Stage camera_axi.lua (the server-side scripted path for FPoIP). The UI assets (camera.xrc, the
# logo/icon images) are compiled into the executable (see embedded_assets.cpp), so they are not staged
# at all; only camera_axi.lua stays on disk, because it is an editable script. The GUI defaults
# OKCAM_LUA to <resourceDir>/camera_axi.lua.
set(_lua_src "${CMAKE_CURRENT_SOURCE_DIR}/../lua/camera_axi.lua")
add_custom_command(TARGET okcameragui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_lua_src}" "${_data_dir}/camera_axi.lua")

# Stage the FPGA bitfiles into Bitfiles/ (the app auto-discovers it; the release archive carries the
# same folder). OKCAM_BITFILES overrides the location at runtime for custom-gateware builds.
set(_bitfiles_src "${CMAKE_CURRENT_SOURCE_DIR}/../../bitfiles")
if(EXISTS "${_bitfiles_src}")
    add_custom_command(TARGET okcameragui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${_bitfiles_src}" "${_data_dir}/Bitfiles")
else()
    message(WARNING "Bitfiles not found at ${_bitfiles_src}; set OKCAM_BITFILES at runtime.")
endif()

# macOS: generate the .app icon (Contents/Resources/okCameraApp.icns) from the bundled PNG art so
# Finder/Dock show the real icon (Info.plist's CFBundleIconFile points at it). Best-effort -- skips if
# iconutil is unavailable.
if(APPLE)
    add_custom_command(TARGET okcameragui POST_BUILD
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/make-icns.sh"
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/icon-256.png"
                "${_data_dir}/okCameraApp.icns"
        VERBATIM)
endif()

# Stage the FrontPanel runtime libs (from the dir where the link library was found) so a from-source
# BUILD-tree run works. This MUST include okimpl_fpoip.* -- the FrontPanel-over-IP plugin that
# okFrontPanel loads at runtime via a bare-name dlopen; without it FPoIP silently fails (IsRemote()
# stays false, GetDeviceInfo/ConfigureFPGA error). macOS stages it into Contents/Frameworks (the
# idiomatic location, found via the exe's rpath); Windows/Linux next to the exe. In the RELEASE install
# tree, macOS bundles the FrontPanel runtime (fixup_bundle), while Windows/Linux keep it external (the
# customer supplies it from their SDK), so the install() rules below intentionally do NOT install it.
# CONFIGURE_DEPENDS so a newly-added plugin re-globs on the next build.
if(DEFINED OKFP_LIB_DIR)
    if(WIN32)
        file(GLOB _fpdlls CONFIGURE_DEPENDS "${OKFP_LIB_DIR}/*.dll")
    else()
        file(GLOB _fpdlls CONFIGURE_DEPENDS "${OKFP_LIB_DIR}/libokFrontPanel.so*"
             "${OKFP_LIB_DIR}/libokFrontPanel*.dylib" "${OKFP_LIB_DIR}/libokimpl_fpoip*"
             "${OKFP_LIB_DIR}/okimpl_fpoip*")  # macOS plugin has no lib prefix (okimpl_fpoip.bundle)
    endif()
    foreach(_d ${_fpdlls})
        add_custom_command(TARGET okcameragui POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_d}" "${_fw_dir}")
    endforeach()
endif()

# Windows: stage the wxWidgets runtime DLLs next to the exe for a from-source BUILD-tree run (the
# prebuilt wx is not on PATH). This is dev convenience only -- the RELEASE archive is built from the
# install tree, where the dependency resolver copies exactly the linked wx DLLs. Copy the whole
# release set and skip only the debug variants (their unicode marker is "ud", e.g. wxmsw32ud_core...,
# versus the release "u", wxmsw32u_core...). Matching by the trailing "u" alone would wrongly drop the
# component DLLs (…_core_…, …_xrc_…), which end in the component name, not "u".
if(WIN32 AND DEFINED wxWidgets_LIB_DIR)
    file(GLOB _wxdlls "${wxWidgets_LIB_DIR}/*_vc14x_x64.dll")
    foreach(_d ${_wxdlls})
        if(NOT _d MATCHES "[0-9]ud_")  # skip debug (…ud) builds; keep the release set
            add_custom_command(TARGET okcameragui POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_d}" "$<TARGET_FILE_DIR:okcameragui>")
        endif()
    endforeach()
endif()

# ============================================================================================
# INSTALL rules -- the authoritative RELEASE packaging. `cmake --install` produces a clean,
# self-contained tree (no build-system cruft); dist.yml archives it. fixup_bundle (gated on
# OKCAM_BUNDLE_WX) makes the app run without a system wxWidgets install.
# ============================================================================================
if(APPLE)
    # The built .app already contains the data (Contents/Resources), the .icns, and the FrontPanel
    # runtime (Contents/Frameworks) from the POST_BUILD steps above; installing the bundle copies it
    # wholesale, then fixup_bundle adds the wxWidgets closure into Contents/Frameworks.
    install(TARGETS okcameragui BUNDLE DESTINATION . COMPONENT app)
else()
    install(TARGETS okcameragui RUNTIME DESTINATION . COMPONENT app)
    install(FILES "${_lua_src}" DESTINATION . COMPONENT app)
    if(EXISTS "${_bitfiles_src}")
        install(DIRECTORY "${_bitfiles_src}/" DESTINATION Bitfiles COMPONENT app)
    endif()
    # FrontPanel runtime is intentionally NOT installed on Windows/Linux -- it stays external (the
    # customer provides it from their installed FrontPanel-Platform SDK).
endif()

# macOS ad-hoc signing identity (default "-" = ad-hoc). A real Developer ID can be passed to sign for
# real locally; the release CI job re-signs with Developer ID + notarizes (Phase 2). The scripts are
# shareable; the cert is never shared.
if(APPLE)
    set(OKCAM_CODESIGN_IDENTITY "-" CACHE STRING "codesign identity for the macOS .app ('-' = ad-hoc)")
endif()

if(OKCAM_BUNDLE_WX AND APPLE)
    # macOS: BundleUtilities fixup_bundle (driven by the configured Fixup.cmake). It copies the wx +
    # FrontPanel closure into Contents/Frameworks AND rewrites the dylib install names to @rpath -- the
    # part the modern resolver does NOT do, and the reason fixup_bundle is kept for macOS specifically.
    # okimpl_fpoip.bundle (loaded by a bare-name dlopen, so not in the link closure) is passed as a
    # plugin; the default classification bundles Homebrew's wx + the third_party FrontPanel runtime.
    set(_fixup_app "okCameraApp.app")
    set(_fixup_dirs "/opt/homebrew/lib;/usr/local/lib;${OKFP_LIB_DIR}")
    set(_fixup_plugins "\${CMAKE_INSTALL_PREFIX}/okCameraApp.app/Contents/Frameworks/okimpl_fpoip.bundle")
    set(_fixup_override_body "    # macOS: default classification bundles Homebrew wx + the third_party FrontPanel runtime.")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Fixup.cmake.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/Fixup.cmake" @ONLY)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/Fixup.cmake" COMPONENT app)

    # Re-sign inside-out AFTER fixup_bundle -- rewriting install names invalidates signatures, and
    # arm64 macOS SIGKILLs a process that loads a library with a broken signature. Ad-hoc by default
    # (OKCAM_CODESIGN_IDENTITY); the release CI job re-signs with Developer ID + notarizes (Phase 2).
    install(CODE "
        set(_id \"${OKCAM_CODESIGN_IDENTITY}\")
        set(_app \"\${CMAKE_INSTALL_PREFIX}/okCameraApp.app\")
        file(GLOB _items \"\${_app}/Contents/Frameworks/*\")
        foreach(_f \${_items})
            execute_process(COMMAND codesign --force --sign \"\${_id}\" \"\${_f}\")
        endforeach()
        execute_process(COMMAND codesign --force --sign \"\${_id}\" \"\${_app}\")
    " COMPONENT app)

elseif(OKCAM_BUNDLE_WX)
    # Windows + Linux: the modern resolver file(GET_RUNTIME_DEPENDENCIES), driven by the configured
    # BundleModern.cmake. It bundles ONLY the wxWidgets runtime next to the exe; FrontPanel stays
    # external (customer's SDK) and the system stack stays external. No install-name rewriting is
    # needed -- $ORIGIN rpath (Linux) / the default DLL search order (Windows) resolves the libraries.
    # (fixup_bundle is intentionally NOT used here: on Linux its verify_app step is unreliable for the
    # $ORIGIN strategy, and the modern resolver is the current, future-proof CMake mechanism.)
    if(WIN32)
        set(_grd_exe "okCameraApp.exe")
        set(_grd_dirs "${wxWidgets_LIB_DIR}")
        # dumpbin is not on PATH outside a VS dev prompt; locate a PE-import tool next to the compiler
        # at configure time and bake its path into the install script for the resolver to use.
        get_filename_component(_cxx_bindir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        find_program(OKCAM_DEPS_TOOL NAMES dumpbin llvm-objdump objdump HINTS "${_cxx_bindir}")
        if(OKCAM_DEPS_TOOL)
            set(_grd_tool "set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND \"${OKCAM_DEPS_TOOL}\")")
        else()
            set(_grd_tool "")
        endif()
    else()  # Linux
        set(_grd_exe "okCameraApp")
        set(_grd_dirs "/usr/lib/x86_64-linux-gnu;/usr/lib")
        set(_grd_tool "")
    endif()
    # Normalize Windows backslashes (e.g. WXWIN=C:\wx3210) to forward slashes so the configured CMake
    # script doesn't choke on "\w" as an invalid escape. List ';' separators are preserved.
    string(REPLACE "\\" "/" _grd_dirs "${_grd_dirs}")
    string(REPLACE "\\" "/" _grd_tool "${_grd_tool}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/BundleModern.cmake.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/BundleModern.cmake" @ONLY)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/BundleModern.cmake" COMPONENT app)
endif()
