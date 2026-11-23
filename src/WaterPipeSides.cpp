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

// Water pipe / sewer sides, companion of Water_Pipe_Sewer.dat (SimCity 4 1.1.641).
//
// Along every run of pipe (the stretch between junctions or dead ends) the water pipe stays on the left of a
// canonical travel direction, so curves never make the two pipes cross; on closed loops the water pipe is inside.
//
// Addresses (verified in the 1.1.641 EXE):
//  * cSC4UndergroundOccupant::CreateUndergroundModel builds the S3D instance as vfunc[0x1A4]() | pieceID
//    (call at 0x0065DC27, piece ID at occupant +0xFC). For pipes vfunc[0x1A4] returns 0x06000000. The call is
//    redirected to ModelBaseHook, which returns 0x06000000 | 0x80 | mask (mask bit e set = water sign -1 on
//    edge e: W, N, E, S). Water_Pipe_Sewer.dat contains a model for every piece ID | 0x80 | mask.
//  * cSC4PlumbingSimulator::DoPipeChanged (0x006DB9A0) is hooked to learn about network changes; the affected
//    runs are re-solved on the next model build and pieces whose mask changed are marked for rebuild
//    (occupant byte +0x261, checked by cSC4UndergroundOccupant::Draw).
//  * Neighbours: FindUndergroundOccupant(x, z, &out, classID) at 0x006584D0; connections: vfunc[0xBC](side, 4);
//    cell at +0x23C / +0x240; underground class ID at +0x264.
//  * Pipe-to-lot connection: CreatePieceModel (0x00655850) reads the static descriptor 0x00B0B6D0 (block S3D
//    +0xC, box S3D +0x10, riser vertex list +0x14, count +0x18). The descriptor is switched for the duration of
//    the call to the connection variants 0x06FE00xx of the DAT. The connection model is drawn with the
//    occupant matrix at +0xD0, whose rotation follows the camera, so the variant is chosen in that frame.
// All thiscall functions are declared __fastcall with an unused EDX argument (same stack layout and cleanup),
// which works with both MSVC and clang-cl.

#include "WaterPipeSides.h"
#include "Logger.h"
#include <Windows.h>
#include <cstdint>
#include <cstring>

namespace
{
// ------------------------------------------------------------------ game addresses
constexpr uint32_t ADDR_MODEL_CALL   = 0x0065DC27;   // FF 92 A4 01 00 00  call [edx+0x1A4]
constexpr uint32_t ADDR_DOPIPECHANGE = 0x006DB9A0;   // 83 EC 14 53 55     sub esp,14h / push ebx / push ebp
constexpr uint32_t ADDR_FIND_UG      = 0x006584D0;   // static bool FindUndergroundOccupant(int x, int z, void** out, uint32 clsid)
constexpr uint32_t PIPE_VTABLE       = 0x00AA97B8;   // cSC4PipeOccupant primary vtable
constexpr uint32_t PIPE_SLOT105      = 0x006484C0;   // vtable[105] of pipes: mov eax, 0x06000000
constexpr uint32_t ADDR_CREATEPIECE  = 0x00655850;   // pipe connection CreatePieceModel, thiscall, 83 EC 58 53 55
constexpr uint32_t PIPE_CONN_DESC    = 0x00B0B6D0;   // static descriptor of the pipe-to-lot connection
constexpr int OFF_CONN_X = 0x150, OFF_CONN_Z = 0x154, OFF_CONN_DESC = 0x23C, OFF_CONN_MATRIX = 0xD0;
constexpr float WATER_OFFSET = 4.0f;                 // lateral offset of the two pipes (A in the DAT generator)
constexpr float FIT_OUT = 9.6f;                      // connection fitting centre: 1.6 m past the cell edge

constexpr int OFF_PIECE = 0xFC, OFF_X = 0x23C, OFF_Z = 0x240, OFF_CLSID = 0x264, OFF_DIRTY = 0x261;
constexpr uint32_t PIPE_NETWORK = 4;
constexpr uint32_t OUR_PIECES_MASK = 0xFFC00000, OUR_PIECES = 0x06C00000;   // pieces of Water_Pipe_Sewer.dat

typedef bool     (__cdecl *FindUGFn)(int, int, void**, uint32_t);
typedef bool     (__fastcall *ConnFn)(void*, void*, int, int);
typedef uint32_t (__fastcall *U32Fn)(void*, void*);
typedef void     (__fastcall *DoPipeChangedFn)(void*, void*, void*, int);
typedef void     (__fastcall *CreatePieceFn)(void*, void*);

inline uint32_t* VT(void* o) { return *(uint32_t**)o; }
inline int  Rd32(void* o, int off) { return *(int*)((char*)o + off); }
inline float RdF(void* o, int off) { return *(float*)((char*)o + off); }
inline bool IsPipe(void* o) { return o && *(uint32_t*)o == PIPE_VTABLE; }
inline bool Conn(void* o, int side) { return ((ConnFn)VT(o)[0xBC / 4])(o, nullptr, side, PIPE_NETWORK); }

// ------------------------------------------------------------------ state
constexpr int W = 258;                       // cities are at most 256 cells; +1 border on each side
static uint32_t gGen = 1, gTouch = 1, gClsid = 0;
static bool     gActive = false;
static uint32_t eGen[2][W * W];                 // per edge: generation of the cached sign (axis 0 = X edges, 1 = Z edges)
static int8_t   eSgn[2][W * W];
static void*    lastOcc[W * W];
static uint8_t  lastMask[W * W];
static uint32_t touchStamp[W * W];
static int      touched[W * W], nTouched;
static int      pendX[8192], pendZ[8192], nPend;
static bool     pendOverflow;

static const int DX[4] = { -1, 0, 1, 0 }, DZ[4] = { 0, -1, 0, 1 }, OPP[4] = { 2, 3, 0, 1 };

// Builds a 5-byte relative CALL (E8) / JMP (E9) located at address `at` that targets `to`.
static void MakeRel(uint8_t* b, uint8_t opcode, uint32_t at, uint32_t to) {
	b[0] = opcode;
	int32_t r = (int32_t)(to - (at + 5));
	memcpy(b + 1, &r, 4);
}
static inline bool InCity(int x, int z) { return x >= 0 && z >= 0 && x < 256 && z < 256; }
static inline int  CellIdx(int x, int z) { return (z + 1) * W + (x + 1); }

static void* Find(int x, int z) {
	if (!InCity(x, z) || !gClsid) return 0;
	void* out = 0;
	if (!((FindUGFn)ADDR_FIND_UG)(x, z, &out, gClsid) || !out) return 0;
	((U32Fn)VT(out)[2])(out, nullptr);         // Release: the occupant stays owned by the occupant manager
	return IsPipe(out) ? out : 0;
}

// Edge between a cell and its neighbour on `side`, normalised so both cells name the same edge.
static inline void EdgeKey(int x, int z, int side, int& axis, int& idx) {
	switch (side) {
		case 0: axis = 0; idx = CellIdx(x - 1, z); break;
		case 2: axis = 0; idx = CellIdx(x, z); break;
		case 1: axis = 1; idx = CellIdx(x, z - 1); break;
		default: axis = 1; idx = CellIdx(x, z); break;
	}
}
static inline void Mid2(int x, int z, int side, int& mx, int& mz) {   // edge midpoint, doubled cell units
	switch (side) {
		case 0: mx = 2 * x;     mz = 2 * z + 1; break;
		case 2: mx = 2 * x + 2; mz = 2 * z + 1; break;
		case 1: mx = 2 * x + 1; mz = 2 * z;     break;
		default: mx = 2 * x + 1; mz = 2 * z + 2; break;
	}
}
static void Touch(int x, int z) {
	if (!InCity(x, z)) return;
	int i = CellIdx(x, z);
	if (touchStamp[i] != gTouch) { touchStamp[i] = gTouch; if (nTouched < W * W) touched[nTouched++] = i; }
}

struct Step { int axis, idx, dir, mx, mz, x, z, side; };
static const int MAXRUN = 70000;
static Step fwdBuf[MAXRUN], backBuf[MAXRUN], pathBuf[2 * MAXRUN];

static int Degree(void* o) { int d = 0; for (int s = 0; s < 4; ++s) d += Conn(o, s) ? 1 : 0; return d; }
static int OtherSide(void* o, int notSide) { for (int s = 0; s < 4; ++s) if (s != notSide && Conn(o, s)) return s; return -1; }

// Walk from cell (x, z) leaving through `side`; stops at a dead end, a junction, or back at the start cell.
static int WalkFrom(int x, int z, int side, int x0, int z0, Step* out, bool& loop) {
	int n = 0; loop = false;
	while (n < MAXRUN) {
		Step& st = out[n++];
		EdgeKey(x, z, side, st.axis, st.idx);
		Mid2(x, z, side, st.mx, st.mz);
		st.dir = (side == 2 || side == 3) ? 1 : -1;
		st.x = x; st.z = z; st.side = side;
		int nx = x + DX[side], nz = z + DZ[side];
		void* nb = Find(nx, nz);
		if (!nb || !Conn(nb, OPP[side])) break;
		Touch(nx, nz);
		if (Degree(nb) != 2) break;                    // junction / dead end (also when back at a junction start)
		if (nx == x0 && nz == z0) { loop = true; break; }   // closed loop of degree-2 pieces only
		int s2 = OtherSide(nb, OPP[side]);
		if (s2 < 0) break;
		x = nx; z = nz; side = s2;
	}
	return n;
}

// Solve the run that contains edge (x, z, side): orientation, then the water sign of every edge on it.
static void WalkRun(void* o, int x, int z, int side) {
	Touch(x, z);
	bool loop;
	int nf = WalkFrom(x, z, side, x, z, fwdBuf, loop);
	int n = 0;
	if (!loop && Degree(o) == 2) {
		int other = OtherSide(o, side);
		bool l2;
		int nb = other >= 0 ? WalkFrom(x, z, other, x, z, backBuf, l2) : 0;
		for (int i = nb - 1; i >= 0; --i) { pathBuf[n] = backBuf[i]; pathBuf[n].dir = -pathBuf[n].dir; ++n; }
	}
	for (int i = 0; i < nf; ++i) pathBuf[n++] = fwdBuf[i];
	if (!n) return;

	bool rev;
	if (loop) {                         // water inside: travel counter-clockwise on screen (x right, -z up)
		long long a = 0;
		for (int i = 0; i < n; ++i) {
			const Step& p = pathBuf[i]; const Step& q = pathBuf[(i + 1) % n];
			a += (long long)p.mx * (-q.mz) - (long long)q.mx * (-p.mz);
		}
		rev = a < 0;
	} else {                            // open run: start from the end with the smaller (z, x) midpoint
		const Step& f = pathBuf[0]; const Step& l = pathBuf[n - 1];
		if (n == 1) rev = f.dir < 0;    // single edge: start and end coincide, use the positive direction
		else rev = (f.mz > l.mz) || (f.mz == l.mz && f.mx > l.mx);
	}
	for (int i = 0; i < n; ++i) {
		int d = rev ? -pathBuf[i].dir : pathBuf[i].dir;
		// water on the left of travel: X edge -> displacement along z = -d ; Z edge -> along x = +d
		eSgn[pathBuf[i].axis][pathBuf[i].idx] = (int8_t)(pathBuf[i].axis == 0 ? -d : d);
		eGen[pathBuf[i].axis][pathBuf[i].idx] = gGen;
	}
}

static int TileMask(void* o) {
	int x = Rd32(o, OFF_X), z = Rd32(o, OFF_Z), m = 0;
	for (int s = 0; s < 4; ++s) {
		if (!Conn(o, s)) continue;
		int axis, idx; EdgeKey(x, z, s, axis, idx);
		if (eGen[axis][idx] != gGen) WalkRun(o, x, z, s);
		if (eGen[axis][idx] == gGen && eSgn[axis][idx] < 0) m |= 1 << s;
	}
	return m;
}

static void ProcessCell(int x, int z) {
	void* o = Find(x, z);
	if (!o) return;
	Touch(x, z);
	TileMask(o);                        // walks every run through this cell (marks the cells it passes)
}

static void ProcessPending() {
	++gTouch; nTouched = 0;
	if (pendOverflow) {
		for (int z = 0; z < 256; ++z) for (int x = 0; x < 256; ++x) ProcessCell(x, z);
	} else {
		for (int i = 0; i < nPend; ++i) {
			ProcessCell(pendX[i], pendZ[i]);
			for (int s = 0; s < 4; ++s) ProcessCell(pendX[i] + DX[s], pendZ[i] + DZ[s]);
		}
	}
	nPend = 0; pendOverflow = false;
	for (int k = 0; k < nTouched; ++k) {
		int i = touched[k], x = i % W - 1, z = i / W - 1;
		void* o = Find(x, z);
		if (!o || o != lastOcc[i]) continue;                       // new pieces get their model built anyway
		uint32_t piece = (uint32_t)Rd32(o, OFF_PIECE);
		if ((piece & OUR_PIECES_MASK) != OUR_PIECES) continue;
		if ((uint8_t)TileMask(o) != lastMask[i]) *((uint8_t*)o + OFF_DIRTY) = 1;   // rebuilt on next Draw
	}
}


inline bool OurPiece(void* o) { return ((uint32_t)Rd32(o, OFF_PIECE) & OUR_PIECES_MASK) == OUR_PIECES; }
int gModelLogs = 0, gConnLogs = 0;

// ------------------------------------------------------------------ hooks
uint32_t __fastcall ModelBaseHook(void* o, void*) {
	uint32_t* vt = VT(o);
	if ((uint32_t)vt != PIPE_VTABLE) return ((U32Fn)vt[0x1A4 / 4])(o, nullptr);   // subway etc.: unchanged
	uint32_t base = 0x06000000;
	uint32_t piece = (uint32_t)Rd32(o, OFF_PIECE);
	int x = Rd32(o, OFF_X), z = Rd32(o, OFF_Z);
	if ((piece & OUR_PIECES_MASK) != OUR_PIECES || (piece & 0xFF) || !InCity(x, z)) return base;
	if (!gClsid) gClsid = (uint32_t)Rd32(o, OFF_CLSID);
	if (nPend || pendOverflow) ProcessPending();
	int m = TileMask(o);
	lastOcc[CellIdx(x, z)] = o; lastMask[CellIdx(x, z)] = (uint8_t)m;
	if (gModelLogs < 100) {
		++gModelLogs;
		Logger::GetInstance().WriteLineFormatted(LogLevel::Debug, "WaterPipeSides: piece 0x%08X at %d,%d -> mask %d", piece, x, z, m);
	}
	return base | 0x80 | (uint32_t)m;
}

DoPipeChangedFn gDoPipeChangedTramp;
void __fastcall DoPipeChangedHook(void* sim, void*, void* occ, int added) {
	gDoPipeChangedTramp(sim, nullptr, occ, added);
	if (!IsPipe(occ)) return;
	if (!gClsid) gClsid = (uint32_t)Rd32(occ, OFF_CLSID);
	int x = Rd32(occ, OFF_X), z = Rd32(occ, OFF_Z);
	if (!InCity(x, z)) return;
	++gGen;                                                        // every cached run may have changed
	if (nPend < (int)(sizeof(pendX) / sizeof(pendX[0]))) { pendX[nPend] = x; pendZ[nPend] = z; ++nPend; }
	else pendOverflow = true;
}

// ------------------------------------------------------------------ pipe-to-lot connection
CreatePieceFn gCreatePieceTramp;
uint8_t gRiser[12 * 0x18];

bool PipeArrival(int x, int z, int& axis, int& sign, int& side) {
	void* o = Find(x, z);
	if (o && OurPiece(o)) {                                    // pipe piece in the connection cell itself
		int m = TileMask(o);
		for (int s = 0; s < 4; ++s) if (Conn(o, s)) { axis = (s == 0 || s == 2) ? 0 : 1; sign = (m >> s & 1) ? -1 : 1; side = s; return true; }
	}
	for (int s = 0; s < 4; ++s) {                              // pipe ending at the edge of the connection cell
		void* n = Find(x + DX[s], z + DZ[s]);
		if (!n || !OurPiece(n) || !Conn(n, OPP[s])) continue;
		int m = TileMask(n);
		axis = (s == 0 || s == 2) ? 0 : 1; sign = (m >> OPP[s] & 1) ? -1 : 1; side = s;
		return true;
	}
	return false;
}

void __fastcall CreatePieceHook(void* conn, void*) {
	uint32_t* desc = *(uint32_t**)((char*)conn + OFF_CONN_DESC);
	int x = Rd32(conn, OFF_CONN_X), z = Rd32(conn, OFF_CONN_Z), axis = 0, sign = 1, side = 0;
	if ((uint32_t)desc != PIPE_CONN_DESC || !InCity(x, z) || !gClsid) { gCreatePieceTramp(conn, nullptr); return; }
	if (nPend || pendOverflow) ProcessPending();
	if (!PipeArrival(x, z, axis, sign, side)) { gCreatePieceTramp(conn, nullptr); return; }   // no DAT pipe: Maxis model
	// The connection model is drawn with the occupant matrix at +0xD0 (rows m00 m01 m02 tx / ... / m20 m21 m22 tz,
	// world = M * model) whose rotation follows the camera, so the wanted side and water offset (grid frame) are
	// brought into the model frame with M^T before picking the variant.
	float m00 = RdF(conn, OFF_CONN_MATRIX), m02 = RdF(conn, OFF_CONN_MATRIX + 8);
	float m20 = RdF(conn, OFF_CONN_MATRIX + 0x20), m22 = RdF(conn, OFF_CONN_MATRIX + 0x28);
	auto toModel = [&](float gx, float gz, float& ox, float& oz) { ox = m00 * gx + m20 * gz; oz = m02 * gx + m22 * gz; };
	float wgx = axis == 0 ? 0.0f : sign * WATER_OFFSET, wgz = axis == 0 ? sign * WATER_OFFSET : 0.0f;
	float nmx, nmz, wmx, wmz;
	toModel((float)DX[side], (float)DZ[side], nmx, nmz);
	toModel(wgx, wgz, wmx, wmz);
	int mside = side;
	for (int s2 = 0; s2 < 4; ++s2)
		if ((float)DX[s2] * nmx + (float)DZ[s2] * nmz > 0.5f) mside = s2;
	float lat = (mside == 0 || mside == 2) ? wmz : wmx;
	int msign = lat < 0 ? -1 : 1;
	uint32_t k = (uint32_t)(mside * 2 + (msign < 0 ? 1 : 0));
	uint32_t oldBlock = desc[3], oldBox = desc[4], oldList = desc[5], count = desc[6];
	if (count > 12) count = 12;
	float mx, mz;
	toModel(wgx + DX[side] * FIT_OUT, wgz + DZ[side] * FIT_OUT, mx, mz);
	std::memcpy(gRiser, (void*)oldList, count * 0x18);
	// The Maxis riser (a vertical half-tube built from this list) shows up as a band between the two pipes: every
	// vertex goes on one vertical line at the water fitting, so it has no area. The count is kept because the game
	// indexes into the riser vertices afterwards.
	for (uint32_t i = 0; i < count; ++i) { float* v = (float*)(gRiser + i * 0x18); v[0] = mx; v[2] = mz; }
	desc[3] = 0x06FE0010u + k; desc[4] = 0x06FE0000u + k; desc[5] = (uint32_t)gRiser;
	if (gConnLogs < 50) {
		++gConnLogs;
		Logger::GetInstance().WriteLineFormatted(LogLevel::Debug,
			"WaterPipeSides: connection %d,%d side %d sign %d -> model side %d sign %d, variant %u", x, z, side, sign, mside, msign, k);
	}
	gCreatePieceTramp(conn, nullptr);
	desc[3] = oldBlock; desc[4] = oldBox; desc[5] = oldList;
}

// ------------------------------------------------------------------ patching
bool BytesAre(uint32_t addr, const uint8_t* b, int n) {
	for (int i = 0; i < n; ++i) if (((uint8_t*)addr)[i] != b[i]) return false;
	return true;
}
void WriteCode(uint32_t addr, const uint8_t* b, int n) {
	DWORD old;
	VirtualProtect((void*)addr, n, PAGE_EXECUTE_READWRITE, &old);
	std::memcpy((void*)addr, b, n);
	VirtualProtect((void*)addr, n, old, &old);
	FlushInstructionCache(GetCurrentProcess(), (void*)addr, n);
}
uint32_t Target(uint32_t insn) { return insn + 5 + *(int32_t*)(insn + 1); }
}

void WaterPipeSides::Install()
{
	Logger& logger = Logger::GetInstance();
	static const uint8_t expModel[6] = { 0xFF, 0x92, 0xA4, 0x01, 0x00, 0x00 };
	static const uint8_t expDPC[5]   = { 0x83, 0xEC, 0x14, 0x53, 0x55 };
	static const uint8_t expCP[5]    = { 0x83, 0xEC, 0x58, 0x53, 0x55 };
	if (!BytesAre(ADDR_MODEL_CALL, expModel, 6) || !BytesAre(ADDR_DOPIPECHANGE, expDPC, 5)
		|| *(uint32_t*)(PIPE_VTABLE + 0x1A4) != PIPE_SLOT105) {
		logger.WriteLine(LogLevel::Error, "WaterPipeSides: patch sites differ from 1.1.641, not installing.");
		return;
	}
	uint8_t* tramp = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!tramp) {
		logger.WriteLine(LogLevel::Error, "WaterPipeSides: VirtualAlloc failed.");
		return;
	}
	// trampoline 1: original DoPipeChanged prologue + jump back
	std::memcpy(tramp, expDPC, 5);
	MakeRel(tramp + 5, 0xE9, (uint32_t)tramp + 5, ADDR_DOPIPECHANGE + 5);
	gDoPipeChangedTramp = (DoPipeChangedFn)tramp;
	uint8_t j[5]; MakeRel(j, 0xE9, ADDR_DOPIPECHANGE, (uint32_t)&DoPipeChangedHook);
	WriteCode(ADDR_DOPIPECHANGE, j, 5);
	// model ID call: call ModelBaseHook; nop
	uint8_t c[6]; MakeRel(c, 0xE8, ADDR_MODEL_CALL, (uint32_t)&ModelBaseHook); c[5] = 0x90;
	WriteCode(ADDR_MODEL_CALL, c, 6);
	// trampoline 2: pipe-to-lot connection
	if (BytesAre(ADDR_CREATEPIECE, expCP, 5) && *(uint32_t*)(PIPE_CONN_DESC + 0x0C) == 0x6A089741u) {
		uint8_t* t2 = tramp + 16;
		std::memcpy(t2, expCP, 5);
		MakeRel(t2 + 5, 0xE9, (uint32_t)t2 + 5, ADDR_CREATEPIECE + 5);
		gCreatePieceTramp = (CreatePieceFn)t2;
		uint8_t j2[5]; MakeRel(j2, 0xE9, ADDR_CREATEPIECE, (uint32_t)&CreatePieceHook);
		WriteCode(ADDR_CREATEPIECE, j2, 5);
	} else {
		logger.WriteLine(LogLevel::Error, "WaterPipeSides: connection patch site differs, connection hook skipped.");
	}
	gActive = *(uint8_t*)ADDR_MODEL_CALL == 0xE8 && Target(ADDR_MODEL_CALL) == (uint32_t)&ModelBaseHook
		&& *(uint8_t*)ADDR_DOPIPECHANGE == 0xE9 && Target(ADDR_DOPIPECHANGE) == (uint32_t)&DoPipeChangedHook;
	if (!gActive) {
		logger.WriteLine(LogLevel::Error, "WaterPipeSides: hook verification failed.");
	}
}
