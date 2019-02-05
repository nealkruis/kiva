/* Copyright (c) 2012-2018 Big Ladder Software LLC. All rights reserved.
 * See the LICENSE file for additional terms and conditions. */

#include "Instance.hpp"

namespace Kiva {

Instance::Instance(Foundation fnd) {
  foundation = std::make_shared<Foundation>(fnd);
  create();
}

void Instance::create() {
  GroundOutput::OutputMap outputMap;

  outputMap.push_back(Surface::ST_SLAB_CORE);

  if (foundation->hasPerimeterSurface) {
    outputMap.push_back(Surface::ST_SLAB_PERIM);
  }

  if (foundation->foundationDepth) {
    outputMap.push_back(Surface::ST_WALL_INT);
  }

  ground = std::make_shared<Ground>(*foundation.get(), outputMap);
  ground->buildDomain();

}

} // namespace Kiva