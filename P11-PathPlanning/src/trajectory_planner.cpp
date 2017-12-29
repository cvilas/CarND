#include "trajectory_planner.h"
#include "helpers.h"
#include "integrator.h"
#include "spline.h"

#include <limits>
#include <Eigen/Core>
#include <Eigen/Dense>
#include <iostream>
#include <cmath>

namespace sdcnd_t3p1
{

//---------------------------------------------------------------------------------------------------------------------
TrajectoryPlanner::TrajectoryPlanner(const WaypointList& wps)
  : trackWaypoints_(wps), model_(SIM_DELTA_TIME)
//---------------------------------------------------------------------------------------------------------------------
{
}


//---------------------------------------------------------------------------------------------------------------------
std::array<double, 6> TrajectoryPlanner::computePolynomialCoefficients(const PolynomialConstraint& initial, const PolynomialConstraint& final)
//---------------------------------------------------------------------------------------------------------------------
{
  // Solve for ceofficients of jerk minimising polynomial as described in term3 - trajectory generation lessons

  std::array<double, 6> coeffs = {initial.q, initial.qDot, initial.qDotDot/2.0, 0, 0, 0};

  const double dt = final.t - initial.t;
  const double dt2 = dt*dt;
  const double dt3 = dt2*dt;
  const double dt4 = dt3*dt;
  const double dt5 = dt4*dt;

  Eigen::Matrix3d A;
  A << dt3, dt4, dt5,
       3*dt2, 4*dt3, 5*dt4,
       6*dt, 12*dt2, 20*dt3;

  Eigen::FullPivLU<Eigen::Matrix3d> lu(A);
  if( lu.isInvertible() )
  {
    Eigen::Matrix3d Ai = A.inverse();

    Eigen::Vector3d Bs;
    Bs << final.q - (initial.q + initial.qDot * dt + 0.5 * initial.qDotDot * dt2),
          final.qDot - (initial.qDot + initial.qDotDot * dt),
          final.qDotDot - initial.qDotDot;

    Eigen::Vector3d X = Ai * Bs;
    coeffs[3] = X[0];
    coeffs[4] = X[1];
    coeffs[5] = X[2];
  }
  else
  {
    std::cerr << "WARNING: Trajectory not solvable for given constraints" << std::endl;
  }
  return coeffs;
}

//---------------------------------------------------------------------------------------------------------------------
void TrajectoryPlanner::updateTrajectory(double longSpeed, double latPos, double timeDelta, size_t nPointsToAdd,
                                         CartesianPoseList& coords)
//---------------------------------------------------------------------------------------------------------------------
{
  Integrator integrator(SIM_DELTA_TIME);

  // set initial conditions
  PolynomialConstraint sInitial;
  const auto& firstPointIt = history_.back();
  double s0 = firstPointIt.s;
  double t0 = firstPointIt.time;
  sInitial.q = firstPointIt.sv;
  sInitial.qDot = firstPointIt.sa;
  sInitial.qDotDot = firstPointIt.sj;
  sInitial.t = firstPointIt.time;

  PolynomialConstraint dInitial;
  dInitial.q = firstPointIt.d;
  dInitial.qDot = firstPointIt.dv;
  dInitial.qDotDot = firstPointIt.da;
  dInitial.t = firstPointIt.time;

  // set final conditions
  PolynomialConstraint sFinal;
  sFinal.q = longSpeed;
  sFinal.qDot = 0;
  sFinal.qDotDot = 0;
  sFinal.t = sInitial.t + timeDelta;

  PolynomialConstraint dFinal;
  dFinal.q = latPos;
  dFinal.qDot = 0;
  dFinal.qDotDot = 0;
  dFinal.t = dInitial.t + timeDelta;

  // Fit polynomial between end points
  const std::array<double, 6> sCoeffs = computePolynomialCoefficients(sInitial, sFinal);
  const std::array<double, 6> dCoeffs = computePolynomialCoefficients(dInitial, dFinal);

  // Generate intermediate waypoints
  double dt = SIM_DELTA_TIME;
  for(size_t i = 0; i < nPointsToAdd; ++i)
  {
    const double dt2 = dt*dt;
    const double dt3 = dt2*dt;
    const double dt4 = dt3*dt;
    const double dt5 = dt4*dt;

    State fs;
    fs.sv = sCoeffs[0] + sCoeffs[1]*dt + sCoeffs[2]*dt2 + sCoeffs[3]*dt3 + sCoeffs[4]*dt4 + sCoeffs[5]*dt5;
    fs.sa = sCoeffs[1] + 2*sCoeffs[2]*dt + 3*sCoeffs[3]*dt2 + 4*sCoeffs[4]*dt3 + 5*sCoeffs[5]*dt4;
    fs.sj = 2*sCoeffs[2] + 6*sCoeffs[3]*dt + 12*sCoeffs[4]*dt2 + 20*sCoeffs[5]*dt3;

    fs.d = dCoeffs[0] + dCoeffs[1]*dt + dCoeffs[2]*dt2 + dCoeffs[3]*dt3 + dCoeffs[4]*dt4 + dCoeffs[5]*dt5;
    fs.dv = dCoeffs[1] + 2*dCoeffs[2]*dt + 3*dCoeffs[3]*dt2 + 4*dCoeffs[4]*dt3 + 5*dCoeffs[5]*dt4;
    fs.da = 2*dCoeffs[2] + 6*dCoeffs[3]*dt + 12*dCoeffs[4]*dt2 + 20*dCoeffs[5]*dt3;
    fs.dj = 6*dCoeffs[3] + 24*dCoeffs[4]*dt + 60*dCoeffs[5]*dt2;

    fs.time = t0 + dt;
    dt += SIM_DELTA_TIME;

    if(i == 0)
    {
      integrator.reset(s0, sInitial.q);
    }
    fs.s = integrator.integrate(fs.sv);

    if(fs.s > MAX_TRACK_LENGTH)
    {
      fs.s -= MAX_TRACK_LENGTH;
      integrator.reset(fs.s, fs.sv);
    }

    // reference trajectory coordinates
    const CartesianPose refPose = getCartesianFromFrenet(fs.s, fs.d, trackWaypoints_);

    // push into control and get updated vehicle position
    fs.pose = model_.predict(refPose, fs.sv);

    // push into buffer that's passed to simulator
    history_.push_back(fs);
    coords.push_back(fs.pose);
  }
}

//---------------------------------------------------------------------------------------------------------------------
CartesianPoseList TrajectoryPlanner::computePlan(Behaviour behaviour, const Vehicle& me,
                                                 const VehicleList& others, const CartesianPoseList& myPrevPath)
//---------------------------------------------------------------------------------------------------------------------
{
  const size_t myPrevPathSz = myPrevPath.size();
  size_t nPointsToAdd = SIM_NUM_WAYPOINTS - myPrevPathSz;
  CartesianPoseList path = myPrevPath;

  // set initial boundary conditions
  if(myPrevPathSz < 2)
  {
    history_.clear();

    // Add two points to set initial direction of motion. first point just behind current car position
    State initial;
    initial.s = me.frenet.s - 1;
    initial.sv = me.speed;
    initial.sa = 0;
    initial.sj = 0;
    initial.d = me.frenet.d;
    initial.dv = 0;
    initial.da = 0;
    initial.dj = 0;
    initial.time = 0;
    initial.pose = getCartesianFromFrenet(initial.s, initial.d, trackWaypoints_);

    path.push_back(initial.pose);
    history_.push_back(initial);
    nPointsToAdd -= 1;

    // set the second point to car coords
    initial.s = me.frenet.s;
    initial.sv = me.speed;
    initial.sa = 0;
    initial.sj = 0;
    initial.d = me.frenet.d;
    initial.dv = 0;
    initial.da = 0;
    initial.dj = 0;
    initial.time = 0;
    initial.pose.x = me.position.x;
    initial.pose.y = me.position.y;
    initial.pose.heading = me.position.heading;

    path.push_back(initial.pose);
    history_.push_back(initial);
    nPointsToAdd -= 1;
  }
  else
  {
    // clear history of points already processed by sim
    history_.erase(history_.begin(), history_.begin() + nPointsToAdd);
  }

  // default targets for the trajectory generator
  const double targetD = getFrenetDFromLaneNumber(behaviour.targetLane) ;
  double targetSpeed = Behaviour::MAX_SPEED;

  // if behind and close to another vehicle, set safe final boundary conditions
  auto vehicles = findNearestVehicles(behaviour.targetLane, me.position, others);
  if( vehicles.ahead != others.end() )
  {
    const double deltaDist = distance(vehicles.ahead->position, me.position);
    targetSpeed = std::min(Behaviour::MAX_SPEED, std::max(0., (deltaDist-1)/Behaviour::MIN_RESPONSE_TIME) );
  }

  // Add waypoints to trajectory
  //std::cout << "Targets : " << targetSpeed << ", " << targetD << std::endl;
  updateTrajectory(targetSpeed, targetD, (history_.back().time < 15 ? 15 : Behaviour::MIN_RESPONSE_TIME), nPointsToAdd, path);

  return path;
}

}

