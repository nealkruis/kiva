/* Copyright (c) 2012-2019 Big Ladder Software LLC. All rights reserved.
 * See the LICENSE file for additional terms and conditions. */

#ifndef Simulator_HPP
#define Simulator_HPP

#include <iostream>

#include <mgl2/mgl.h>

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <boost/lexical_cast.hpp>

#include <boost/filesystem/operations.hpp>

#include "BoundaryConditions.hpp"
#include "Geometry.hpp"
#include "Ground.hpp"
#include "GroundOutput.hpp"
#include "GroundPlot.hpp"
#include "Input.hpp"
#include "WeatherData.hpp"

using namespace Kiva;

class Simulator {
public:
  // Constructor
  Simulator(WeatherData &weatherData, Input &input, std::string outputFileName);

  virtual ~Simulator();
  void simulate();

  WeatherData &weatherData;
  Input &input;

  double annualAverageDryBulbTemperature;

  double percentComplete;

private:
  Ground ground;
  BoundaryConditions bcs;

  std::vector<GroundPlot> plots;
  std::ofstream outputFile;
  boost::filesystem::path outputDir;
  void initializePlots();
  void initializeConditions();

  void printStatus(boost::posix_time::ptime t);

  std::string printOutputHeaders();
  std::string printOutputLine();

  void plot(boost::posix_time::ptime t);

  boost::posix_time::ptime prevStatusUpdate;
  boost::posix_time::ptime prevOutputTime;
  bool initPeriod;

  double getInitialTemperature(boost::posix_time::ptime t, double z);

  void updateBoundaryConditions(boost::posix_time::ptime t);
};

#endif // Simulator_HPP
