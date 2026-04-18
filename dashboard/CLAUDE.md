# PacBot Dashboard

A web-based dashboard to monitor the state of a small Raspberry based robot.

## Overview

Both the robot and the webapp connect to a central server. The robot sends information about its state and sensor readings and receives movement instructions. The server broadcasts the sensor data to the connected webapps which can display it, and users interacting with the webapp can also send movement instructions.

## Robot information

The robot is a circular 4-omniwheel drive with an IMU and 8 IR time-of-flight sensors equally spread on the edge of the robot.

The robot maintains its heading and can move into the four cardinal directions. It only rotates to correct its heading.

The main state variable of the robot is the direction its instructed to move in. It also keeps internal PID counters.

## Dashboard surfaces and controls

The dashboard should be a simple black background high contrast "mission control" style dashboard with the following surfaces:

* Connection status (both web<->server and server<->robot)
* IMU Yaw/pitch/roll (graphs with numeric value)
* Top-down illustration of the robot with lines coming from the sensors to display distance + numeric value.
* 3D representation of the robot's orientation in space
* Controls to set the robot's speed, direction, stop it, manually adjust base yaw
* The movement controls should have associated keyboard shortcuts

## Server

For now, please write a placeholder server in Python that doesn't actually connect to a real robot but simulates one.

Communication between the server and webapp should use websockets.