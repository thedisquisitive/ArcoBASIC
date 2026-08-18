set(ARCO_GUI_BACKEND_AVAILABLE FALSE)
set(ARCO_GUI_BACKEND_SOURCE src/gui/stub_backend.cpp)

if(ARCO_ENABLE_GUI AND EMSCRIPTEN)
    # No GLFW/GTK/Cairo/Pango under Emscripten's sysroot -- and no reason to look, since none of
    # them have web ports arco_runtime could link against anyway. The canvas backend below is a
    # from-scratch implementation of the same arco::gui interface against HTML5 <canvas>, not a
    # port of glfw_backend.cpp's dependencies.
    set(ARCO_GUI_BACKEND_AVAILABLE TRUE)
    set(ARCO_GUI_BACKEND_SOURCE src/gui/canvas_backend.cpp)
elseif(ARCO_ENABLE_GUI AND UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GLFW3 QUIET IMPORTED_TARGET glfw3)
        pkg_check_modules(PANGOCAIRO QUIET IMPORTED_TARGET pangocairo)
        pkg_check_modules(GTK3 QUIET IMPORTED_TARGET gtk+-3.0)
    endif()
    find_package(OpenGL QUIET)

    if(GLFW3_FOUND AND PANGOCAIRO_FOUND AND GTK3_FOUND AND OpenGL_FOUND)
        set(ARCO_GUI_BACKEND_AVAILABLE TRUE)
        set(ARCO_GUI_BACKEND_SOURCE src/gui/glfw_backend.cpp)
    endif()
endif()

if(ARCO_ENABLE_NETWORK)
    find_package(CURL QUIET)
endif()
