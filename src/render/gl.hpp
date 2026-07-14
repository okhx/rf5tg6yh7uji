#pragma once

// Geode's cocos headers expose the ES2 declarations on iOS, while the
// recorder uses ES3 framebuffer and readback APIs. Make those declarations
// available when building the iOS target.
#ifdef GEODE_IS_IOS
#include <OpenGLES/ES3/gl.h>
#endif
