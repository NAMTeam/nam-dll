/*
 * This file is part of nam-dll, a DLL Plugin for SimCity 4
 * that improves interoperability with the Network Addon Mod.
 *
 * nam-dll is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * nam-dll is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with nam-dll.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

// Water pipe / sewer sides (companion of Water_Pipe_Sewer.dat).
//
// The DAT draws every water pipe piece as a drinking-water pipe plus a sewer pipe and contains one model
// variant per combination of water sides on the piece edges. This patch picks, for every piece, the variant
// whose sides follow the pipe network, so the two pipes never cross along a run, and places the
// pipe-to-lot connection (under pumps and buildings) on the two pipes.
// Without Water_Pipe_Sewer.dat the patch has no visible effect.
namespace WaterPipeSides
{
	void Install();
}
