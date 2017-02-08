/* Copyright (c) 2012-2016 Big Ladder Software. All rights reserved.
* See the LICENSE file for additional terms and conditions. */

#ifndef BESTEST_HPP_
#define BESTEST_HPP_

#include <gtest/gtest.h>
#include "Ground.hpp"

using namespace Kiva;

class BESTEST : public testing::Test {
protected:
  void SetUp() {

    fnd.reductionStrategy = Foundation::RS_AP;
    Material soil(1.9,1490.0,1800.0);
    double length(12.0);
    double width(12.0);

    fnd.deepGroundBoundary = Foundation::DGB_CONSTANT_TEMPERATURE;
    fnd.deepGroundTemperature = 283.15;

    fnd.soil = soil;
    fnd.soilAbsorptivity = 0.0;
    fnd.soilEmissivity = 0.0;

    fnd.hasSlab = false;

    fnd.polygon.outer().push_back(Point(-length/2.0,-width/2.0));
    fnd.polygon.outer().push_back(Point(-length/2.0,width/2.0));
    fnd.polygon.outer().push_back(Point(length/2.0,width/2.0));
    fnd.polygon.outer().push_back(Point(length/2.0,-width/2.0));

    Layer tempLayer;
    tempLayer.thickness = 0.24;
    tempLayer.material = soil;

    fnd.wall.layers.push_back(tempLayer);

    fnd.wall.heightAboveGrade = 0.0;
    fnd.wall.depthBelowSlab = 0.0;
    fnd.wall.interiorEmissivity = 0.0;
    fnd.wall.exteriorEmissivity = 0.0;
    fnd.wall.exteriorAbsorptivity = 0.0;
    fnd.wallTopBoundary = Foundation::WTB_ZERO_FLUX;

    fnd.convectionCalculationMethod = Foundation::CCM_CONSTANT_COEFFICIENT;
    fnd.interiorConvectiveCoefficient = 99999;
    fnd.exteriorConvectiveCoefficient = 99999;

    fnd.numericalScheme = Foundation::NS_STEADY_STATE;

    bcs.localWindSpeed = 0;
    bcs.outdoorTemp = 283.15;
    bcs.indoorTemp = 303.15;


    outputMap[Surface::ST_SLAB_CORE] = {
      GroundOutput::OT_RATE
    };
  }

  double calcQ(){
    Ground ground(fnd,outputMap);
    ground.buildDomain();
    ground.calculate(bcs);
    ground.calculateSurfaceAverages();
    return ground.getSurfaceAverageValue({Surface::ST_SLAB_CORE,GroundOutput::OT_RATE});

  }

  std::map<Surface::SurfaceType, std::vector<GroundOutput::OutputType>> outputMap;

  BoundaryConditions bcs;
  Foundation fnd;
};

#endif /* BESTEST_HPP_ */