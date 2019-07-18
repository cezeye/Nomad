/*
 * ReferenceTrajectoryGenerator.cpp
 *
 *  Created on: July 16, 2019
 *      Author: Quincy Jones
 *
 * Copyright (c) <2019> <Quincy Jones - quincy@implementedrobotics.com/>
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software
 * is furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

// Primary Include
#include <Controllers/ReferenceTrajectoryGen.hpp>

// C System Includes

// C++ System Includes
#include <iostream>
#include <string>
#include <sstream>
#include <chrono>

// Third-Party Includes
#include <Eigen/Dense>

// Project Includes
#include <Controllers/RealTimeTask.hpp>
#include <Controllers/StateEstimator.hpp>
#include <Controllers/Messages.hpp>

// TODO: Static Variable in "Physics" Class somewhere

double kGravity = 9.81;

namespace Controllers
{
namespace Locomotion
{
//using namespace RealTimeControl;

ReferenceTrajectoryGenerator::ReferenceTrajectoryGenerator(const std::string &name, const unsigned int N, const double T) : 
                               RealTimeControl::RealTimeTaskNode(name, 20000, RealTimeControl::Priority::MEDIUM, -1, PTHREAD_STACK_MIN),
                               sequence_num_(0),
                               num_states_(13),
                               T_(T),
                               N_(N)
{
    
    // Sample Time
    T_s_ = T_ / (N_);

    // Reference State Trajectory
    X_ref_ = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>(num_states_, N_);

    // Create Ports
    zmq::context_t *ctx = RealTimeControl::RealTimeTaskManager::Instance()->GetZMQContext();

    // Reference Output Port
    // TODO: Independent port speeds.  For now all ports will be same speed as task node
    RealTimeControl::Port *port = new RealTimeControl::Port("REFERENCE", ctx, "reference", rt_period_);
    output_port_map_[OutputPort::REFERENCE] = port;

    port = new RealTimeControl::Port("STATE_HAT", ctx, "state", rt_period_);
    input_port_map_[InputPort::STATE_HAT] = port;

    port = new RealTimeControl::Port("SETPOINT", ctx, "setpoint", rt_period_);
    input_port_map_[InputPort::SETPOINT] = port;
    
}

void ReferenceTrajectoryGenerator::Run()
{

    // Get Inputs
    GetInputPort(0)->Receive((void *)&x_hat_in_, sizeof(x_hat_in_)); // Receive State Estimate
    GetInputPort(1)->Receive((void *)&setpoint_in_, sizeof(setpoint_in_)); // Receive Setpoint

    // Get Timestamp
    // TODO: "GetUptime" Static function in a time class
    uint64_t time_now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    // TODO: Move to Operator Input Node
    // Messages::Controllers::Locomotion::TrajectorySetpoint setpoint_in_;
    // setpoint_in_.timestamp = time_now;
    // setpoint_in_.sequence_number = 1;
    // setpoint_in_.x_dot = 1.0;
    // setpoint_in_.y_dot = 0.0;
    // setpoint_in_.yaw_dot = 1.0;
    // setpoint_in_.z_com = 0.5;

    std::cout << "X: " << x_hat_in_.x[3] << std::endl;
    std::cout << "[ReferenceTrajectoryGenerator]: Received State: " << x_hat_in_.x[3] << " : " << sequence_num_ << std::endl;

    // Compute Trajectory
    X_ref_(0,0) = x_hat_in_.x[0]; // X Position
    X_ref_(1,0) = x_hat_in_.x[1]; // Y Position
    X_ref_.row(2).setConstant(setpoint_in_.z_com); // Z Position

    X_ref_.row(3).setConstant(setpoint_in_.x_dot); // X Velocity
    X_ref_.row(4).setConstant(setpoint_in_.y_dot); // Y Velocity
    X_ref_.row(5).setConstant(0); // Z Velocity

    X_ref_.row(6).setConstant(0); // Roll Orientation
    X_ref_.row(7).setConstant(0); // Pitch Orientation
    X_ref_.row(8).setConstant(x_hat_in_.x[8]); // Yaw Orientation

    X_ref_.row(9).setConstant(0); // Roll Rate
    X_ref_.row(10).setConstant(0); // Pitch Rate

    // Since we linearize and assume pitch and roll are 0.  omega_z = yaw_rate
    //Eigen::Vector3d orientation_dot(0,0,setpoint.yaw_dot);
    //Eigen::Matrix3d R_z = Eigen::AngleAxisd(x_hat.x[8], Eigen::Vector3d(0,0,1)).toRotationMatrix();
    //Eigen::Vector3d omega = R_z * orientation_dot;
    //std::cout << orientation_dot << std::endl;
    //std::cout << R_z << std::endl;
    //std::cout << omega;

    X_ref_.row(11).setConstant(setpoint_in_.yaw_dot); // Yaw Rate
    X_ref_.row(12).setConstant(kGravity); // Gravity

    for(int i = 0;i < N_-1; i++)
    {
        X_ref_(0,i+1) = X_ref_(0,i) + setpoint_in_.x_dot * T_s_;
        X_ref_(1,i+1) = X_ref_(1,i) + setpoint_in_.y_dot * T_s_;
        X_ref_(8,i+1) = X_ref_(8,i) + setpoint_in_.yaw_dot * T_s_;
    }
    std::cout << X_ref_ << std::endl;

    // Update Publish Buffer
    reference_out_.timestamp = time_now;
    reference_out_.sequence_number = sequence_num_;
    memcpy(reference_out_.X_ref, X_ref_.data(), sizeof(double) * X_ref_.size());

    // Publish Trajectory
    GetOutputPort(0)->Send(&reference_out_, sizeof(reference_out_));

    std::cout << "[ReferenceTrajectoryGenerator]: Publishing: " << reference_out_.sequence_number << std::endl;

    // Update our Sequence Counter
    sequence_num_++;
}

void ReferenceTrajectoryGenerator::Setup()
{

    // State Estimate INPUT
    GetInputPort(0)->Connect();

    // Setpoint INPUT
    GetInputPort(1)->Connect();

    // Reference OUTPUT
    GetOutputPort(0)->Bind();
    std::cout << "[ReferenceTrajectoryGenerator]: " << "Reference Trajectory Publisher Running!" << std::endl;
}

} // namespace Locomotion
} // namespace Controllers
