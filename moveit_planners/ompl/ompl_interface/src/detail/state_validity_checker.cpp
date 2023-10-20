/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2011, Willow Garage, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

/* Author: Ioan Sucan, Jeroen De Maeyer */

#include <moveit/ompl_interface/detail/state_validity_checker.h>
#include <moveit/ompl_interface/model_based_planning_context.h>
#include <ompl/base/spaces/constraint/ConstrainedStateSpace.h>
#include <rclcpp/logger.hpp>
#include <rclcpp/logging.hpp>

namespace ompl_interface
{
static const rclcpp::Logger LOGGER = rclcpp::get_logger("moveit.ompl_planning.state_validity_checker");

ompl_interface::StateValidityChecker::StateValidityChecker(const ModelBasedPlanningContext* pc)
  : ompl::base::StateValidityChecker(pc->getOMPLSimpleSetup()->getSpaceInformation())
  , planning_context_(pc)
  , group_name_(pc->getGroupName())
  , tss_(pc->getCompleteInitialRobotState())
  , verbose_(false)
{
  specs_.clearanceComputationType = ompl::base::StateValidityCheckerSpecs::APPROXIMATE;
  specs_.hasValidDirectionComputation = false;

  collision_request_with_distance_.distance = true;
  collision_request_with_cost_.cost = true;

  collision_request_simple_.group_name = planning_context_->getGroupName();
  collision_request_with_distance_.group_name = planning_context_->getGroupName();
  collision_request_with_cost_.group_name = planning_context_->getGroupName();

  collision_request_simple_verbose_ = collision_request_simple_;
  collision_request_simple_verbose_.verbose = true;

  collision_request_with_distance_verbose_ = collision_request_with_distance_;
  collision_request_with_distance_verbose_.verbose = true;
}

void ompl_interface::StateValidityChecker::setVerbose(bool flag)
{
  verbose_ = flag;
}

bool StateValidityChecker::isValid(const ompl::base::State* state, bool verbose) const
{
  assert(state != nullptr);
  // Use cached validity if it is available
  if (state->as<ModelBasedStateSpace::StateType>()->isValidityKnown())
  {
    return state->as<ModelBasedStateSpace::StateType>()->isMarkedValid();
  }

  if (!si_->satisfiesBounds(state))
  {
    if (verbose)
    {
      RCLCPP_INFO(LOGGER, "State outside bounds");
    }
    const_cast<ob::State*>(state)->as<ModelBasedStateSpace::StateType>()->markInvalid();
    return false;
  }

  moveit::core::RobotState* robot_state = tss_.getStateStorage();
  planning_context_->getOMPLStateSpace()->copyToRobotState(*robot_state, state);

  // check path constraints
  const kinematic_constraints::KinematicConstraintSetPtr& kset = planning_context_->getPathConstraints();
  if (kset && !kset->decide(*robot_state, verbose).satisfied)
  {
    const_cast<ob::State*>(state)->as<ModelBasedStateSpace::StateType>()->markInvalid();
    return false;
  }

  // check feasibility
  if (!planning_context_->getPlanningScene()->isStateFeasible(*robot_state, verbose))
  {
    const_cast<ob::State*>(state)->as<ModelBasedStateSpace::StateType>()->markInvalid();
    return false;
  }

  Eigen::VectorXd joint_positions; 
  robot_state->copyJointGroupPositions(planning_context_->getJointModelGroup(), joint_positions);
  
  //set collision as true by default
  res.collision = true;

  if(!joint_positions.hasNaN()) {
    // check collision avoidance
    collision_detection::CollisionResult res;
    planning_context_->getPlanningScene()->checkCollision(
        verbose ? collision_request_simple_verbose_ : collision_request_simple_, res, *robot_state);
    if (!res.collision)
    {
      const_cast<ob::State*>(state)->as<ModelBasedStateSpace::StateType>()->markValid();
    }
    else
    {
      const_cast<ob::State*>(state)->as<ModelBasedStateSpace::StateType>()->markInvalid();
    }
  }
  
  return !res.collision;
}

bool StateValidityChecker::isValid(const ompl::base::State* state, double& dist, bool verbose) const
{
  assert(state != nullptr);
  // Use cached validity and distance if they are available
  if (state->as<ModelBasedStateSpace::StateType>()->isValidityKnown() &&
      state->as<ModelBasedStateSpace::StateType>()->isGoalDistanceKnown())
  {
    dist = state->as<ModelBasedStateSpace::StateType>()->distance;
    return state->as<ModelBasedStateSpace::StateType>()->isMarkedValid();
  }

  if (!si_->satisfiesBounds(state))
  {
    if (verbose)
    {
      RCLCPP_INFO(LOGGER, "State outside bounds");
    }
    const_cast<ob::State*>(state)->as<ModelBasedStateSpace::StateType>()->markInvalid(0.0);
    return false;
  }

  moveit::core::RobotState* robot_state = tss_.getStateStorage();
  planning_context_->getOMPLStateSpace()->copyToRobotState(*robot_state, state);

  // check path constraints
  const kinematic_constraints::KinematicConstraintSetPtr& kset = planning_context_->getPathConstraints();
  if (kset)
  {
    kinematic_constraints::ConstraintEvaluationResult cer = kset->decide(*robot_state, verbose);
    if (!cer.satisfied)
    {
      dist = cer.distance;
      const_cast<ob::State*>(state)->as<ModelBasedStateSpace::StateType>()->markInvalid(dist);
      return false;
    }
  }

  // check feasibility
  if (!planning_context_->getPlanningScene()->isStateFeasible(*robot_state, verbose))
  {
    dist = 0.0;
    return false;
  }

  // check collision avoidance
  collision_detection::CollisionResult res;
  planning_context_->getPlanningScene()->checkCollision(
      verbose ? collision_request_with_distance_verbose_ : collision_request_with_distance_, res, *robot_state);
  dist = res.distance;
  return !res.collision;
}

double StateValidityChecker::cost(const ompl::base::State* state) const
{
  assert(state != nullptr);
  double cost = 0.0;

  moveit::core::RobotState* robot_state = tss_.getStateStorage();
  planning_context_->getOMPLStateSpace()->copyToRobotState(*robot_state, state);

  // Calculates cost from a summation of distance to obstacles times the size of the obstacle
  collision_detection::CollisionResult res;
  planning_context_->getPlanningScene()->checkCollision(collision_request_with_cost_, res, *robot_state);

  for (const collision_detection::CostSource& cost_source : res.cost_sources)
  {
    cost += cost_source.cost * cost_source.getVolume();
  }

  return cost;
}

double StateValidityChecker::clearance(const ompl::base::State* state) const
{
  assert(state != nullptr);
  moveit::core::RobotState* robot_state = tss_.getStateStorage();
  planning_context_->getOMPLStateSpace()->copyToRobotState(*robot_state, state);

  collision_detection::CollisionResult res;
  planning_context_->getPlanningScene()->checkCollision(collision_request_with_distance_, res, *robot_state);
  return res.collision ? 0.0 : (res.distance < 0.0 ? std::numeric_limits<double>::infinity() : res.distance);
}

/*******************************************
 * Constrained Planning StateValidityChecker
 * *****************************************/
bool ConstrainedPlanningStateValidityChecker::isValid(const ompl::base::State* wrapped_state, bool verbose) const
{
  assert(wrapped_state != nullptr);
  // Unwrap the state from a ConstrainedStateSpace::StateType
  auto state = wrapped_state->as<ompl::base::ConstrainedStateSpace::StateType>()->getState();

  // Use cached validity if it is available
  if (state->as<ModelBasedStateSpace::StateType>()->isValidityKnown())
  {
    return state->as<ModelBasedStateSpace::StateType>()->isMarkedValid();
  }

  // do not use the unwrapped state here, as satisfiesBounds expects a state of type ConstrainedStateSpace::StateType
  if (!si_->satisfiesBounds(wrapped_state))  // si_ = ompl::base::SpaceInformation
  {
    RCLCPP_DEBUG(LOGGER, "State outside bounds");
    const_cast<ob::State*>(state)->as<ModelBasedStateSpace::StateType>()->markInvalid();
    return false;
  }

  moveit::core::RobotState* robot_state = tss_.getStateStorage();
  // do not use the unwrapped state here, as copyToRobotState expects a state of type ConstrainedStateSpace::StateType
  planning_context_->getOMPLStateSpace()->copyToRobotState(*robot_state, wrapped_state);

  // check path constraints
  const kinematic_constraints::KinematicConstraintSetPtr& kset = planning_context_->getPathConstraints();
  if (kset && !kset->decide(*robot_state, verbose).satisfied)
  {
    const_cast<ob::State*>(state)->as<ModelBasedStateSpace::StateType>()->markInvalid();
    return false;
  }

  // check feasibility
  if (!planning_context_->getPlanningScene()->isStateFeasible(*robot_state, verbose))
  {
    const_cast<ob::State*>(state)->as<ModelBasedStateSpace::StateType>()->markInvalid();
    return false;
  }

  // check collision avoidance
  collision_detection::CollisionResult res;
  planning_context_->getPlanningScene()->checkCollision(
      verbose ? collision_request_simple_verbose_ : collision_request_simple_, res, *robot_state);
  if (!res.collision)
  {
    const_cast<ob::State*>(state)->as<ModelBasedStateSpace::StateType>()->markValid();
  }
  else
  {
    const_cast<ob::State*>(state)->as<ModelBasedStateSpace::StateType>()->markInvalid();
  }
  return !res.collision;
}

bool ConstrainedPlanningStateValidityChecker::isValid(const ompl::base::State* wrapped_state, double& dist,
                                                      bool verbose) const
{
  assert(wrapped_state != nullptr);
  // Unwrap the state from a ConstrainedStateSpace::StateType
  auto state = wrapped_state->as<ompl::base::ConstrainedStateSpace::StateType>()->getState();

  // Use cached validity and distance if they are available
  if (state->as<ModelBasedStateSpace::StateType>()->isValidityKnown() &&
      state->as<ModelBasedStateSpace::StateType>()->isGoalDistanceKnown())
  {
    dist = state->as<ModelBasedStateSpace::StateType>()->distance;
    return state->as<ModelBasedStateSpace::StateType>()->isMarkedValid();
  }

  // do not use the unwrapped state here, as satisfiesBounds expects a state of type ConstrainedStateSpace::StateType
  if (!si_->satisfiesBounds(wrapped_state))  // si_ = ompl::base::SpaceInformation
  {
    RCLCPP_DEBUG(LOGGER, "State outside bounds");
    const_cast<ob::State*>(state)->as<ModelBasedStateSpace::StateType>()->markInvalid(0.0);
    return false;
  }

  moveit::core::RobotState* robot_state = tss_.getStateStorage();

  // do not use the unwrapped state here, as copyToRobotState expects a state of type ConstrainedStateSpace::StateType
  planning_context_->getOMPLStateSpace()->copyToRobotState(*robot_state, wrapped_state);

  // check path constraints
  const kinematic_constraints::KinematicConstraintSetPtr& kset = planning_context_->getPathConstraints();
  if (kset && !kset->decide(*robot_state, verbose).satisfied)
  {
    const_cast<ob::State*>(state)->as<ModelBasedStateSpace::StateType>()->markInvalid();
    return false;
  }

  // check feasibility
  if (!planning_context_->getPlanningScene()->isStateFeasible(*robot_state, verbose))
  {
    dist = 0.0;
    return false;
  }

  // check collision avoidance
  collision_detection::CollisionResult res;
  planning_context_->getPlanningScene()->checkCollision(
      verbose ? collision_request_with_distance_verbose_ : collision_request_with_distance_, res, *robot_state);
  dist = res.distance;
  return !res.collision;
}
}  // namespace ompl_interface
