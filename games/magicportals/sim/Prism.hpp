#pragma once

// A converted level's collision polygons, as solids the engine can collide.
//
// The engine builds convex hulls from mesh FILES only - ConvexHullCache loads
// an asset - so each 2D polygon reaches the solver as an OBJ prism: the polygon
// in the XY plane, in metres with y flipped, extruded along z and centred on
// z = 0. That is the readiness doc's S2 route. There are 13 distinct polygons
// across all 128 levels, and a file is named by a hash of its own text, so the
// same polygon written twice is one file and 13 is the most there will be.
//
// Every one of them is convex - 956 chamfered boxes and 16 triangles - and the
// writer refuses a reflex corner rather than hand the hull builder a shape it
// would silently fill in. The winding is normalised, so the mesh is closed and
// faces outward whatever order the converter listed the points in. That is
// what lets ConvexDecomposition measure its volume, and so lets a test assert
// that the collider invents none.

#include <filesystem>
#include <string>
#include <vector>

namespace MagicPortals::Prism {

// OBJ text for the prism of `polygonPx` - x0, y0, x1, y1, ... in the remake's
// pixels, local to its body - `depthMetres` deep. Empty, with `error` set, for
// anything that is not a convex polygon of at least three points.
std::string ObjText(const std::vector<double>& polygonPx, double depthMetres, std::string& error);

// The same, written under `directory` (created if need be). Returns the file's
// path, or empty with `error` set.
std::string Write(const std::vector<double>& polygonPx, double depthMetres,
                  const std::filesystem::path& directory, std::string& error);

// The polygon's area in square pixels, so a test can hold the prism's volume
// to it.
double AreaPx(const std::vector<double>& polygonPx);

} // namespace MagicPortals::Prism
