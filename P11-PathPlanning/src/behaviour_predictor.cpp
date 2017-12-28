#include "behaviour_predictor.h"
#include "constants.h"
#include "helpers.h"
#include "trajectory_planner.h"

#include <iostream>

namespace sdcnd_t3p1
{

constexpr double BehaviourPredictor::PREDICTION_TIME;

//---------------------------------------------------------------------------------------------------------------------
BehaviourPredictor::PredictionMap BehaviourPredictor::predict(const Vehicle& me, const VehicleList& others)
//---------------------------------------------------------------------------------------------------------------------
{
  PredictionMap pmap;
  Prediction prediction;
  pmap[0] = prediction;
  pmap[1] = prediction;
  pmap[2] = prediction;

  const int myLane = getLaneNumberFromFrenetD(me.frenet.d);
  for(int lane = 0; lane < 3; ++lane)
  {
    auto& prediction = pmap.at(lane);
    prediction.laneNumber = lane;
    prediction.laneSpeed = me.speed;
    prediction.freeDistance = 0;

    NearestVehicles nearest = findNearestVehicles(lane, me.position, others);
    double freeDistRear = MAX_TRACK_LENGTH;
    if(nearest.behind != others.end())
    {
      // Vehicle behind us in the same lane is responsible for it's own speed. ignore.
      // For vehicles on other lanes, consider them only if they are too close to be
      // a collision hazard if we change into their lane.
      const int theirLane = getLaneNumberFromFrenetD(nearest.behind->frenet.d);
      if(theirLane != myLane)
      {
        const double dist = distance(me.position, nearest.behind->position);
        if( dist < TrajectoryPlanner::SAFE_MANOEUVRE_DISTANCE)
        {
          // assumed worst case: we continue at current speed but vehicle behind speeds up by 50%
          freeDistRear = dist - (1.5*nearest.behind->speed - me.speed) * PREDICTION_TIME;
        }
      }
    }

    double freeDistAhead = MAX_TRACK_LENGTH;
    double laneSpeed = SPEED_LIMIT;
    if(nearest.ahead != others.end())
    {
      // assumed worst case: we continue at current speed but vehicle in front reduces speed by 50%
      freeDistAhead = distance(me.position, nearest.ahead->position)
          - (me.speed - 0.5*nearest.ahead->speed) * PREDICTION_TIME;
      laneSpeed = nearest.ahead->speed;
    }

    if((freeDistRear > 0) && // vehicle behind does not overtake us
       (freeDistAhead > 0) ) // we don't overtake vehicle ahead
    {
      prediction.freeDistance = freeDistAhead; // how much free space we've got. Only consider space up ahead.
      prediction.laneSpeed = std::min(laneSpeed, SPEED_LIMIT);
    }
    std::cout << "l: [" << prediction.laneNumber << "] d: [" << prediction.freeDistance
              << "] v: " << prediction.laneSpeed << std::endl;
  }

  std::cout << "=====" << std::endl;
  return pmap;
}

}
