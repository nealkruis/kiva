/* Copyright (c) 2012-2017 Big Ladder Software LLC. All rights reserved.
* See the LICENSE file for additional terms and conditions. */

#ifndef Ground_CPP
#define Ground_CPP

#undef PRNTSURF

#include "Ground.hpp"
#include "Errors.hpp"
//#include <unsupported/Eigen/SparseExtra>

namespace Kiva {

static const double PI = 4.0*atan(1.0);

static const bool TDMA = true;

Ground::Ground(Foundation &foundation) : foundation(foundation)
{
  pSolver = std::make_shared<Eigen::BiCGSTAB<Eigen::SparseMatrix<double>, Eigen::IncompleteLUT<double>>>();
}

Ground::Ground(Foundation &foundation, GroundOutput::OutputMap &outputMap)
  : foundation(foundation), groundOutput(outputMap)
{
  pSolver = std::make_shared<Eigen::BiCGSTAB<Eigen::SparseMatrix<double>, Eigen::IncompleteLUT<double>>>();
}

Ground::~Ground()
{
}

void Ground::buildDomain()
{
  // Create mesh
  foundation.createMeshData();

  // Build matrices for PDE term coefficients
  domain.setDomain(foundation);

  nX = domain.meshX.centers.size();
  nY = domain.meshY.centers.size();
  nZ = domain.meshZ.centers.size();
  num_cells = nX*nY*nZ;

  // Initialize matices
  if (foundation.numericalScheme == Foundation::NS_ADE)
  {
    U.resize(num_cells);
    V.resize(num_cells);
  }

  if ((foundation.numericalScheme == Foundation::NS_ADI ||
    foundation.numberOfDimensions == 1) && TDMA)
  {
    a1.resize(num_cells, 0.0);
    a2.resize(num_cells, 0.0);
    a3.resize(num_cells, 0.0);
    b_.resize(num_cells, 0.0);
    x_.resize(num_cells);
  }

  pSolver->setMaxIterations(foundation.maxIterations);
  pSolver->setTolerance(foundation.tolerance);
  tripletList.reserve(num_cells*(1+2*foundation.numberOfDimensions));
  Amat.resize(num_cells, num_cells);
  b.resize(num_cells);
  x.resize(num_cells);
  x.fill(283.15);

  TNew.resize(num_cells);
  TOld.resize(num_cells);

  link_cells_to_temp();
}

void Ground::calculateADE()
{
  // Solve for new values (Main loop)
  #pragma omp parallel sections num_threads(2)
  {
    #pragma omp section
      calculateADEUpwardSweep();
    #pragma omp section
      calculateADEDownwardSweep();
  }
  for (size_t index = 0; index < num_cells; ++index)
  {
        TNew[index] = 0.5*(U[index] + V[index]);

        // Update old values for next timestep
        TOld[index] = TNew[index];
  }
}

void Ground::calculateADEUpwardSweep()
{
  // Upward sweep (Solve U Matrix starting from 1, 1)
  for (size_t index = 0; index < num_cells; index++)
  {
    auto this_cell = domain.cell[index];
    this_cell->calcCellADEUp(timestep, foundation, bcs, U);
  }
}

void Ground::calculateADEDownwardSweep()
{
  // Downward sweep (Solve V Matrix starting from I, K)
  for (size_t index = num_cells - 1; /* i >= 0 && */ index < num_cells; index--)
  {
    auto this_cell = domain.cell[index];
    this_cell->calcCellADEDown(timestep, foundation, bcs, V);
  }
}

void Ground::calculateExplicit()
{
  for (size_t index = 0; index < num_cells; index++)
  {
    auto this_cell = domain.cell[index];
    TNew[index] = this_cell->calcCellExplicit(timestep, foundation, bcs);
  }
  TOld.assign(TNew.begin(), TNew.end());
}

void Ground::calculateMatrix(Foundation::NumericalScheme scheme)
{
  for (int index = 0; index < num_cells; index++)
  {
    auto this_cell = domain.cell[index];
    double A, Aip{0}, Aim{0}, Ajp{0}, Ajm{0}, Akp{0}, Akm{0}, bVal;
    this_cell->calcCellMatrix(scheme, timestep, foundation, bcs, A, Aip, Aim, Ajp, Ajm, Akp, Akm, bVal);
    setAmatValue(index, index, A);
    if (Aip != 0) { setAmatValue(index, this_cell->i_up_Ptr->index, Aip); }
    if (Aim != 0) { setAmatValue(index, this_cell->i_down_Ptr->index, Aim); }
    if (Ajp != 0) { setAmatValue(index, this_cell->j_up_Ptr->index, Ajp); }
    if (Ajm != 0) { setAmatValue(index, this_cell->j_down_Ptr->index, Ajm); }
    if (Akp != 0) { setAmatValue(index, this_cell->k_up_Ptr->index, Akp); }
    if (Akm != 0) { setAmatValue(index, this_cell->k_down_Ptr->index, Akm); }
    setbValue(index, bVal);
  }

  solveLinearSystem();

  // Read solution into temperature matrix
  TNew = getXvalues();
  // Update old values for next timestep
  TOld.assign(TNew.begin(), TNew.end());
  clearAmat();
}

void Ground::calculateADI(int dim) {
  double A, Ap, Am, bVal;

  auto dest_index = domain.dest_index_vector[dim-1].begin();
  auto cell_iter = domain.cell.begin();
  for ( ; dest_index<domain.dest_index_vector[dim-1].end(); ++cell_iter, ++dest_index) {
    A = 0.0;
    Ap = 0.0;
    Am = 0.0;
    bVal = 0.0;
    (*cell_iter)->calcCellADI(dim, foundation, timestep, bcs, Am, A, Ap, bVal);
    setValuesADI(*dest_index, Am, A, Ap, bVal);
  }

  solveLinearSystem();

  std::size_t index = 0;
  dest_index = domain.dest_index_vector[dim-1].begin();
  for ( ; dest_index<domain.dest_index_vector[dim-1].end(); ++index, ++dest_index) {
      TNew[index] = x_[*dest_index];
  }

  // Update old values for next timestep
  TOld.assign(TNew.begin(), TNew.end());

  clearAmat();
}

void Ground::calculate(BoundaryConditions& boundaryConditions, double ts)
{
  bcs = boundaryConditions;
  timestep = ts;
  // update boundary conditions
  setSolarBoundaryConditions();
  setInteriorRadiationBoundaryConditions();

  // Calculate Temperatures
  switch(foundation.numericalScheme)
  {
  case Foundation::NS_ADE:
    calculateADE();
    break;
  case Foundation::NS_EXPLICIT:
    calculateExplicit();
    break;
  case Foundation::NS_ADI:
    {
    if (foundation.numberOfDimensions > 1)
      calculateADI(1);
    if (foundation.numberOfDimensions == 3)
      calculateADI(2);
    calculateADI(3);
    }
    break;
  case Foundation::NS_IMPLICIT:
    calculateMatrix(Foundation::NS_IMPLICIT);
    break;
  case Foundation::NS_CRANK_NICOLSON:
    calculateMatrix(Foundation::NS_CRANK_NICOLSON);
    break;
  case Foundation::NS_STEADY_STATE:
    calculateMatrix(Foundation::NS_STEADY_STATE);
    break;
  }

}

void Ground::setAmatValue(const int i,const int j,const double val)
{
  if ((foundation.numericalScheme == Foundation::NS_ADI ||
    foundation.numberOfDimensions == 1) && TDMA)
  {
    if (j < i)
      a1[i] = val;
    else if (j == i)
      a2[i] = val;
    else
      a3[i] = val;
  }
  else
  {
    tripletList.emplace_back(i,j,val);
  }
}

void Ground::setbValue(const int i,const double val)
{
  if ((foundation.numericalScheme == Foundation::NS_ADI ||
    foundation.numberOfDimensions == 1) && TDMA)
  {
    b_[i] = val;
  }
  else
  {
    b(i) = val;
  }
}

void Ground::setValuesADI(const std::size_t & index, const double & Am, const double & A,
                          const double & Ap, const double & bVal) {
    a1[index] = Am;
    a2[index] = A;
    a3[index] = Ap;
    b_[index] = bVal;
};

void Ground::solveLinearSystem()
{
  if ((foundation.numericalScheme == Foundation::NS_ADI ||
    foundation.numberOfDimensions == 1) && TDMA)
  {
    solveTDM(a1,a2,a3,b_,x_);
  }
  else
  {
    int iters;
    double residual;

    bool success;

    Amat.setFromTriplets(tripletList.begin(), tripletList.end());
    pSolver->compute(Amat);
    x = pSolver->solveWithGuess(b,x);
    int status = pSolver->info();

//    Eigen::saveMarket(Amat, "Amat.mtx");
//    Eigen::saveMarketVector(b, "b.mtx");
    success = status == Eigen::Success;
    if (!success) {
      iters = pSolver->iterations();
      residual = pSolver->error();

      std::stringstream ss;
      ss << "Solution did not converge after " << iters << " iterations. The final residual was: (" << residual << ").";
      showMessage(MSG_ERR, ss.str());
    }
  }
}

void Ground::clearAmat()
{
  if ((foundation.numericalScheme == Foundation::NS_ADI ||
    foundation.numberOfDimensions == 1) && TDMA)
  {
    std::fill(a1.begin(), a1.end(), 0.0);
    std::fill(a2.begin(), a2.end(), 0.0);
    std::fill(a3.begin(), a3.end(), 0.0);
    std::fill(b_.begin(), b_.end(), 0.0);

  }
  else
  {
    tripletList.clear();
    tripletList.reserve(nX*nY*nZ*(1+2*foundation.numberOfDimensions));
  }
}

double Ground::getxValue(const int i)
{
  if ((foundation.numericalScheme == Foundation::NS_ADI ||
    foundation.numberOfDimensions == 1) && TDMA)
  {
    return x_[i];
  }
  else
  {
    return x(i);
  }
}

std::vector<double> Ground::getXvalues()
{
  if ((foundation.numericalScheme == Foundation::NS_ADI ||
    foundation.numberOfDimensions == 1) && TDMA)
  {
    return x_;
  }
  else
  {
    std::vector<double> v(x.data(), x.data() + x.rows());
    return v;
  }
}

double Ground::getConvectionCoeff(double Tsurf,
                  double Tamb,
                  double Vair,
                    double roughness,
                  bool isExterior,
                  double tilt)
{
  if (foundation.convectionCalculationMethod == Foundation::CCM_AUTO)
    return getDOE2ConvectionCoeff(tilt,0.0,0.0,Tsurf,Tamb,Vair,roughness);
  else //if (foundation.convectionCalculationMethod == Foundation::CCM_CONSTANT_COEFFICIENT)
  {
    if (isExterior)
      return foundation.exteriorConvectiveCoefficient;
    else
      return foundation.interiorConvectiveCoefficient;
  }
}

double Ground::getSurfaceArea(Surface::SurfaceType surfaceType)
{
  double totalArea = 0;

  // Find surface(s)
  for (size_t s = 0; s < foundation.surfaces.size(); s++)
  {
    if (foundation.surfaces[s].type == surfaceType)
    {
      Surface surface;
      surface = foundation.surfaces[s];

      totalArea += surface.area;
    }
  }

  return totalArea;
}

void Ground::calculateSurfaceAverages(){
  for (auto output : groundOutput.outputMap) {
    Surface::SurfaceType surface = output.first;
    std::vector<GroundOutput::OutputType> outTypes = output.second;

    double constructionRValue = 0.0;
    double surfaceArea = foundation.surfaceAreas[surface];

    if (surface == Surface::ST_SLAB_CORE) {
      constructionRValue = foundation.slab.totalResistance();
    }
    else if (surface == Surface::ST_SLAB_PERIM) {
      constructionRValue = foundation.slab.totalResistance();
    }
    else if (surface == Surface::ST_WALL_INT) {
      constructionRValue = foundation.wall.totalResistance();
    }

    double totalHeatTransferRate = 0.0;
    //double TA = 0;
    double HA = 0.0;
    double totalArea = 0.0;

    double& Tair = bcs.indoorTemp;

    if (foundation.hasSurface[surface]) {
      // Find surface(s)
      for (size_t s = 0; s < foundation.surfaces.size(); s++)
      {
        double tilt = foundation.surfaces[s].tilt;
        if (foundation.surfaces[s].type == surface)
        {

          #ifdef PRNTSURF
            std::ofstream output;
            output.open("surface.csv");
            output  << "x, T, h, q, dx\n";
          #endif

          for (auto index : foundation.surfaces[s].indices)
          {
            auto this_cell = domain.cell[index];
            double h = getConvectionCoeff(TNew[index],Tair,0.0,0.00208,false,tilt)
                 + getSimpleInteriorIRCoeff(this_cell->surfacePtr->emissivity,
                     TNew[index],Tair);

            double& A = this_cell->area;

            totalArea += A;
            totalHeatTransferRate += h*A*(Tair - TNew[index]);
            //TA += TNew[index]*A;
            HA += h*A;

            #ifdef PRNTSURF
              output <<
                domain.meshX.centers[i] << ", " <<
                TNew[index] << ", " <<
                h << ", " <<
                h*(Tair - TNew[index]) << ", " <<
                domain.meshX.deltas[i] << "\n";
            #endif

          }

          #ifdef PRNTSURF
            output.close();
          #endif

        }
      }
    }

    if (totalArea > 0.0) {
      double Tavg = Tair - totalHeatTransferRate/HA;
      double hAvg = HA/totalArea;

      groundOutput.outputValues[{surface,GroundOutput::OT_TEMP}] = Tavg;
      groundOutput.outputValues[{surface,GroundOutput::OT_FLUX}] = totalHeatTransferRate/totalArea;
      groundOutput.outputValues[{surface,GroundOutput::OT_RATE}] = totalHeatTransferRate/totalArea*surfaceArea;
      groundOutput.outputValues[{surface,GroundOutput::OT_CONV}] = hAvg;

      groundOutput.outputValues[{surface,GroundOutput::OT_EFF_TEMP}] = Tair - (totalHeatTransferRate/totalArea)*(constructionRValue+1/hAvg) - 273.15;
    }
    else {
      groundOutput.outputValues[{surface,GroundOutput::OT_TEMP}] = Tair;
      groundOutput.outputValues[{surface,GroundOutput::OT_FLUX}] = 0.0;
      groundOutput.outputValues[{surface,GroundOutput::OT_RATE}] = 0.0;
      groundOutput.outputValues[{surface,GroundOutput::OT_CONV}] = 0.0;

      groundOutput.outputValues[{surface,GroundOutput::OT_EFF_TEMP}] = Tair - 273.15;
    }
  }
}

double Ground::getSurfaceAverageValue(std::pair<Surface::SurfaceType, GroundOutput::OutputType> output)
{
  return groundOutput.outputValues[output];
}

void Ground::calculateBoundaryLayer()
{
  Foundation fd = foundation;

  BoundaryConditions preBCs;
  preBCs.localWindSpeed = 0;
  preBCs.outdoorTemp = 273.15;
  preBCs.indoorTemp = 293.15;
  fd.coordinateSystem = Foundation::CS_CARTESIAN;
  fd.numberOfDimensions = 2;
  fd.reductionStrategy = Foundation::RS_AP;
  fd.numericalScheme = Foundation::NS_STEADY_STATE;
  fd.farFieldWidth = 100;

  Ground pre(fd);
  pre.buildDomain();
  pre.calculate(preBCs);

  std::vector<double> x2s;
  std::vector<double> fluxSums;

  double fluxSum = 0.0;

  double x1_0 = 0.0;

  bool firstIndex = true;

  size_t i_min = pre.domain.meshX.getNearestIndex(boost::geometry::area(foundation.polygon)/
      boost::geometry::perimeter(foundation.polygon));

  size_t k = pre.domain.meshZ.getNearestIndex(0.0);

  size_t j = pre.nY/2;

  for (size_t i = i_min; i < pre.nX; i++)
  {
    std::size_t index = i + pre.nX*j + pre.nX*pre.nY*k;
    double Qz = pre.domain.cell[index]->calculateHeatFlux(pre.foundation.numberOfDimensions,
            pre.TNew, pre.nX, pre.nY, pre.nZ)[2];
    double x1 = pre.domain.meshX.dividers[i];
    double x2 = pre.domain.meshX.dividers[i+1];

    if (Qz > 0.0)
    {
      fluxSum += std::max(Qz,0.0)*(x2-x1);

      if (firstIndex)
        x1_0 = x1;
      x2s.push_back(x2);
      fluxSums.push_back(fluxSum);

      firstIndex = false;
    }

  }

  //std::ofstream output;
  //output.open("Boundary.csv");

  //output << 0.0 << ", " << 0.0 << "\n";

  boundaryLayer.push_back(std::make_pair(0,0));

  for (std::size_t i = 0; i < fluxSums.size() - 1; i++) // last cell is a zero-thickness cell, so don't include it.
  {
    //output << x2s[i] - x1_0 << ", " << fluxSums[i]/fluxSum << "\n";
    boundaryLayer.push_back(std::make_pair(x2s[i] - x1_0,fluxSums[i]/fluxSum));
  }

}

double Ground::getBoundaryValue(double dist)
{
  double val = 0.0;
  if (dist > boundaryLayer[boundaryLayer.size()-1].first)
    val = 1.0;
  else
  {
    for (std::size_t i = 0; i < boundaryLayer.size()-1; i++)
    {
      if (dist >= boundaryLayer[i].first && dist < boundaryLayer[i+1].first)
      {
        double m = (boundaryLayer[i+1].first - boundaryLayer[i].first)/
            (boundaryLayer[i+1].second - boundaryLayer[i].second);
        val = (dist - boundaryLayer[i].first)/m + boundaryLayer[i].second;
        continue;
      }
    }
  }
  return val;
}

double Ground::getBoundaryDistance(double val)
{
  double dist = 0.0;
  if (val > 1.0 || val < 0.0)
  {
    showMessage(MSG_ERR, "Boundary value passed not between 0.0 and 1.0.");
  }
  else
  {
    for (std::size_t i = 0; i < boundaryLayer.size()-1; i++)
    {
      if (val >= boundaryLayer[i].second && val < boundaryLayer[i+1].second)
      {
        double m = (boundaryLayer[i+1].second - boundaryLayer[i].second)/
            (boundaryLayer[i+1].first - boundaryLayer[i].first);
        dist = (val - boundaryLayer[i].second)/m + boundaryLayer[i].first;
        continue;
      }
    }
  }
  return dist;
}

void Ground::setNewBoundaryGeometry()
{
  double area = boost::geometry::area(foundation.polygon);
  double perimeter = boost::geometry::perimeter(foundation.polygon);
  double interiorPerimeter = 0.0;

  std::size_t nV = foundation.polygon.outer().size();
  for (std::size_t v = 0; v < nV; v++)
  {
    std::size_t vPrev, vNext, vNext2;

    if (v == 0)
      vPrev = nV - 1;
    else
      vPrev = v - 1;

    if (v == nV -1)
      vNext = 0;
    else
      vNext = v + 1;

    if (v == nV - 2)
      vNext2 = 0;
    else if (v == nV -1)
      vNext2 = 1;
    else
      vNext2 = v + 2;

    Point a = foundation.polygon.outer()[vPrev];
    Point b = foundation.polygon.outer()[v];
    Point c = foundation.polygon.outer()[vNext];
    Point d = foundation.polygon.outer()[vNext2];

    // Correct U-turns
    if (foundation.isExposedPerimeter[vPrev] && foundation.isExposedPerimeter[v] && foundation.isExposedPerimeter[vNext])
    {
      if (isEqual(getAngle(a,b,c) + getAngle(b,c,d),PI))
      {
        double AB = getDistance(a,b);
        double BC = getDistance(b,c);
        double CD = getDistance(c,d);
        double edgeDistance = BC;
        double reductionDistance = std::min(AB,CD);
        double reductionValue = 1 - getBoundaryValue(edgeDistance);
        perimeter -= 2*reductionDistance*reductionValue;
      }
    }

    if (foundation.isExposedPerimeter[vPrev] && foundation.isExposedPerimeter[v])
    {
      double alpha = getAngle(a,b,c);
      double A = getDistance(a,b);
      double B = getDistance(b,c);


      if (sin(alpha) > 0)
      {
        double f = getBoundaryDistance(1-sin(alpha/2)/(1+cos(alpha/2)))/sin(alpha/2);

        // Chamfer
        double d = f/cos(alpha/2);
        if (A < d || B < d)
        {
          A = std::min(A,B);
          B = std::min(A,B);
        }
        else
        {
          A = d;
          B = d;
        }
        double C = sqrt(A*A + B*B - 2*A*B*cos(alpha));

        perimeter += C - (A + B);

      }
    }

    if (!foundation.isExposedPerimeter[v])
    {
      interiorPerimeter += getDistance(b,c);
    }

  }

  foundation.reductionStrategy = Foundation::RS_CUSTOM;
  foundation.twoParameters = false;
  foundation.reductionLength2 = area/(perimeter - interiorPerimeter);

}

void Ground::setSolarBoundaryConditions()
{
  if (foundation.numberOfDimensions == 1) {
    return;
  }
  for (std::size_t s = 0; s < foundation.surfaces.size() ; s++)
  {
    if (foundation.surfaces[s].type == Surface::ST_GRADE
        || foundation.surfaces[s].type == Surface::ST_WALL_EXT)
    {

      double& azi = bcs.solarAzimuth;
      double& alt = bcs.solarAltitude;
      double& qDN = bcs.directNormalFlux;
      double& qDH = bcs.diffuseHorizontalFlux;
      double qGH = cos(PI/2 - alt)*qDN + qDH;
      double pssf;
      double q;

      double incidence = 0.0;
      double aziYPos = foundation.orientation;
      double aziXPos = PI/2 + foundation.orientation;
      double aziYNeg = PI + foundation.orientation;
      double aziXNeg = 3*PI/2 + foundation.orientation;

      double tilt = foundation.surfaces[s].tilt;
      if (foundation.surfaces[s].orientation == Surface::Z_POS)
      {
        incidence = cos(PI/2 - alt);
      }
      else if (foundation.surfaces[s].orientation == Surface::Z_NEG)
      {
        incidence = cos(PI/2 - alt - PI);
      }
      else
      {
        if (foundation.numberOfDimensions == 2)
        {
          // incidence is the average incidence on the exterior of a vertical cylinder
          // 2*(int(cos(alt)*cos(x),x,0,PI/2))/(2*PI)
          // 2*(integral of incidence over a quarter of the cylinder) = lit portion
          // divide by the total radians in the circle (2*PI)
          // = 2*(cos(alt))/(2*PI)
          // = cos(alt)/PI
          incidence = cos(alt)/PI;
        }
        else
        {
          double aziSurf;
          if (foundation.surfaces[s].orientation == Surface::Y_POS)
          {
            aziSurf = aziYPos;
          }
          else if (foundation.surfaces[s].orientation == Surface::X_POS)
          {
            aziSurf = aziXPos;
          }
          else if (foundation.surfaces[s].orientation == Surface::Y_NEG)
          {
            aziSurf = aziYNeg;
          }
          else //if (foundation.surfaces[s].orientation == Surface::X_NEG)
          {
            aziSurf = aziXNeg;
          }

          if (foundation.numberOfDimensions == 3 && !foundation.useSymmetry)
          {
            // incidence = cos(alt)*cos(azi-aziSurf)*sin(tilt)+sin(alt)*cos(tilt)
            // simplifies for tilt = PI/2 to = cos(alt)*cos(azi-aziSurf)
            incidence = cos(alt)*cos(azi-aziSurf);
          }
          else // if (foundation.coordinateSystem == Foundation::CS_3D_SYMMETRY)
          {
            // if symmetric, use average incidence (one side will be facing the sun,
            // the other won't).
            if (foundation.surfaces[s].orientation == Surface::Y_POS ||
                foundation.surfaces[s].orientation == Surface::Y_NEG)
            {
              if (foundation.isXSymm)
              {
                double incidenceYPos = cos(alt)*cos(azi-aziYPos);
                if (incidenceYPos < 0)
                  incidenceYPos = 0;

                double incidenceYNeg = cos(alt)*cos(azi-aziYNeg);
                if (incidenceYNeg < 0)
                  incidenceYNeg = 0;

                incidence = (incidenceYPos + incidenceYNeg)/2.0;

              }
              else
              {
                incidence = cos(alt)*cos(azi-aziSurf);
              }
            }

            if (foundation.surfaces[s].orientation == Surface::X_POS ||
                foundation.surfaces[s].orientation == Surface::X_NEG)
            {
              if (foundation.isYSymm)
              {
                double incidenceXPos = cos(alt)*cos(azi-aziXPos);
                if (incidenceXPos < 0)
                  incidenceXPos = 0;

                double incidenceXNeg = cos(alt)*cos(azi-aziXNeg);
                if (incidenceXNeg < 0)
                  incidenceXNeg = 0;

                incidence = (incidenceXPos + incidenceXNeg)/2.0;

              }
              else
              {
                incidence = cos(alt)*cos(azi-aziSurf);
              }
            }
          }
        }
      }

      // if sun is below horizon, incidence is zero
      if (sin(alt) < 0)
        incidence = 0;
      if (incidence < 0)
        incidence = 0;

      double Fsky = (1.0 + cos(tilt))/2.0;
      double Fg = 1.0 - Fsky;
      double rho_g = 1.0 - foundation.soilAbsorptivity;

      for (auto index: foundation.surfaces[s].indices)
      {
        auto this_cell = domain.cell[index];
        double alpha = this_cell->surfacePtr->absorptivity;

        if (qGH > 0.0)
        {
          pssf = incidence;
          q = alpha*(qDN*pssf + qDH*Fsky + qGH*Fg*rho_g);
        }
        else
        {
          q = 0;
        }
        this_cell->heatGain = q;

      }
    }
  }
}

void Ground::setInteriorRadiationBoundaryConditions()
{
  for (std::size_t s = 0; s < foundation.surfaces.size() ; s++)
  {
    if (foundation.surfaces[s].type == Surface::ST_SLAB_CORE
        || foundation.surfaces[s].type == Surface::ST_SLAB_PERIM
        || foundation.surfaces[s].type == Surface::ST_WALL_INT)
    {
      for (auto index: foundation.surfaces[s].indices)
      {
        auto this_cell = domain.cell[index];
        if (foundation.surfaces[s].type == Surface::ST_WALL_INT) {
          this_cell->heatGain = bcs.wallAbsRadiation;
        }
        else {
          this_cell->heatGain = bcs.slabAbsRadiation;
        }
      }
    }
  }
}

void Ground::link_cells_to_temp()
{
  for (auto this_cell : domain.cell) {
    this_cell->told_ptr = &TOld[this_cell->index];
  }
}


double getArrayValue(std::vector<std::vector<std::vector<double>>> Mat, std::size_t i, std::size_t j, std::size_t k)
{
  return Mat[i][j][k];
}

}

#endif
