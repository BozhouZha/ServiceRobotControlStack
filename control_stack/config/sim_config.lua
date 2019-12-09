-- Copyright 2019 kvedder@seas.upenn.edu
-- School of Engineering and Applied Sciences,
-- University of Pennsylvania
--
-- This software is free: you can redistribute it and/or modify
-- it under the terms of the GNU Lesser General Public License Version 3,
-- as published by the Free Software Foundation.
--
-- This software is distributed in the hope that it will be useful,
-- but WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
-- GNU Lesser General Public License for more details.
--
-- You should have received a copy of the GNU Lesser General Public License
-- Version 3 in the file COPYING that came with this distribution.
-- If not, see <http://www.gnu.org/licenses/>.
-- ========================================================================

sim = {
  kLaserStdDev = 0.015;
  kArcExecStdDev = 0.4;
  kArcReadStdDev = 0.2;
  kRotateExecStdDev = 0.001;
  kRotateReadStdDev = 0.001;
  kStartPositionX = 4;
  kStartPositionY = 0;
  kStartPositionTheta = 0;

  kMap = "./src/ServiceRobotControlStack/control_stack/maps/loop_small_bumps.map";
};