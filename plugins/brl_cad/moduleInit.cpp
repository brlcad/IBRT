// Copyright (c) 2026 BRL-CAD Visualizer contributors
// SPDX-License-Identifier: MIT

/*! \file ospray/moduleInit \brief Defines the module initialization callback */

#include <iostream>
#include "geometry/brlcad.h"
#include "ospray/version.h"

/*! _everything_ in the ospray core universe should _always_ be in the
  'ospray' namespace. */
namespace ospray {

namespace brlcad {

/*! the actual module initialization function. This function gets
    called exactly once, when the module gets first loaded through
    'ospLoadModule'. Notes:

    a) this function does _not_ get called if the application directly
    links to libospray_module_<modulename> (which it
    shouldn't!). Modules should _always_ be loaded through
    ospLoadModule.

    b) it is _not_ valid for the module to do ospray _api_ calls
    inside such an intiailzatoin function. Ie, you can _not_ do a
    ospLoadModule("anotherModule") from within this function (but
    you could, of course, have this module dynamically link to the
    other one, and call its init function)

    c) in order for ospray to properly resolve that symbol, it
    _has_ to have extern C linkage, and it _has_ to correspond to
    name of the module and shared library containing this module
    (see comments regarding library name in CMakeLists.txt)
*/

extern "C" OSPError OSPRAY_DLLEXPORT ospray_module_init_brl_cad(
    int16_t versionMajor, int16_t versionMinor, int16_t /*versionPatch*/)
{
  auto status = moduleVersionCheck(versionMajor, versionMinor);

  if (status == OSP_NO_ERROR) {
    /*! Register the BRLCAD geometry class under the ospray
        geometry type name of 'brlcad'. This name is used to create
        geometries:

        OSPGeometry geom = ospNewGeometry("brlcad");
    */
    Geometry::registerType<BRLCAD>("brlcad");
    std::cout << "Initializing BRL-CAD module for OSPRay" << std::endl;
  }

  return status;
}

} // namespace brlcad
} // namespace ospray
