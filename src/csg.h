#pragma once
#include "geometry.h"

namespace cad {

Solid csgUnion(const Solid& a, const Solid& b);
Solid csgSubtract(const Solid& a, const Solid& b);     // a minus b
Solid csgIntersect(const Solid& a, const Solid& b);

} // namespace cad
