// Windows-only shim. ImgWindow's SystemGL.h includes <GL/glext.h>, which MSVC's SDK doesn't ship.
// ImgWindow uses only OpenGL 1.1 symbols (all declared by <GL/gl.h>), so no extension
// declarations are needed. Added to the include path by native/CMakeLists.txt on WIN32 only.
#pragma once
