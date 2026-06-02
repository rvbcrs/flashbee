"""
Flash Bee enclosure — build123d parametric model.
v5: handheld with the antenna/sensor at the TOP (original look).

Layout (front view, display head at origin):
  ▲ antenna neck nub (protrusion)         +Y
  ╱╲ tapered sensor pod (AS3935 body)
  ◉  round display head (Ø46 board / Ø48.8 glass)
  ║  grip + battery HY103450 (10x34x50)
  o  lanyard tip                          −Y

Mount holes ASSUMED (owner away) — pocket/edge capture.
Run:  cad/.venv/bin/python cad/flashbee_case.py   (auto-shows in OCP viewer if open)
"""
from build123d import *
import os, math

# ── component dims (mm) ────────────────────────────────────────────
BOARD_D, GLASS_D, ACTIVE_D = 46.0, 48.8, 44.16     # glass overhangs PCB ~1.4/side
USBC_W, USBC_H = 9.2, 4.5
BAT_T, BAT_W, BAT_L = 10.0, 34.0, 50.0             # HY103450
SNS_NECK_W, SNS_NECK_L = 12.3, 15.2                # antenna neck
SNS_BODY_W, SNS_BODY_H = 28.2, 12.8                # wide body

# ── enclosure params ───────────────────────────────────────────────
WALL, CLEAR = 2.4, 0.5
R_HEAD = GLASS_D / 2 + CLEAR + WALL                 # ≈ 27.3
GRIP_W = BAT_W + 2 * CLEAR + 2 * WALL               # ≈ 39.8
THICK  = 22.0                                       # deep enough to STACK the 12.7 mm display + the
#                                                     sensor behind it (sensor stays on the floor; the
#                                                     display rises with THICK so its back clears it)
TIP_R  = GRIP_W / 2 - 1.0
OVERLAP = 14.0   # grip extends this far UP into the head; ≥~11 keeps the grip's
                 # rounded top corners (TIP_R) inside the head circle (no "ears")
BODY_LEN = (R_HEAD - WALL) + BAT_L + 10 + WALL      # head centre → grip tip
WINDOW_D, GLASS_POCKET_D, FRONT_LIP = ACTIVE_D - 0.4, GLASS_D + 2 * CLEAR, 1.6
PARTING = THICK - 4.0
# Raise ONLY the front shell's face + bezel by this. The printed lid jammed the
# glass against the thin bezel before the tongue seated (had to push on the glass).
# The back, tongue, display standoffs and USB stay referenced to THICK, so the
# glass gains REAL clearance (bezel up, standoffs put) AND the back-shell STL is
# unchanged — reprint the FRONT only. Bump to 0.3 if it still needs a hard press.
FRONT_RAISE = 0.6   # 0.2 wasn't enough — still needed too much force at the screen; the
                    # display/glass jams the bezel. Bigger clearance now (module floats up
                    # to the lip, held radially). If the glass sits too deep / plays, lower it.
TOP_F = THICK + FRONT_RAISE                          # front face / bezel datum (≈22.6)
# I/O connector centre-line (case Z): the USB-C + buttons sit on the module's PCB
# BACK, 8.07 mm behind the glass front (STEP-measured). Derived from THICK so all
# side openings track the front when the case depth changes.
IO_Z = (THICK - FRONT_LIP) - 8.07                   # ≈ 12.3 at THICK=22
LANYARD_R, LANYARD_HOLE = 6.0, 4.0

# sensor sits BEHIND the display; only the antenna neck pokes out a small
# nub/slot at the top of the head.
NUB_W = SNS_NECK_W + 2 * CLEAR + 2 * WALL            # ≈ 18 wide top nub
NUB_CY = R_HEAD - 2.0                                # nub straddles the head top
NUB_H = 16.0                                         # protrudes ~6 mm above the head
# sensor body behind the upper part of the display
SENSOR_BODY_CY = 9.0                                 # body centre (Y) behind display
SLOT_W = SNS_NECK_W + 1.0                            # antenna opening width

OUT = os.path.join(os.path.dirname(__file__), "out")
os.makedirs(OUT, exist_ok=True)


def silo(shrink, with_loop):
    """Outer (shrink=0) or inset-by-WALL inner silhouette."""
    s = shrink
    with BuildSketch() as sk:
        Circle(R_HEAD - s)
        with Locations((0, (OVERLAP - BODY_LEN) / 2)):                       # grip
            RectangleRounded(GRIP_W - 2 * s, BODY_LEN + OVERLAP - 2 * s, max(TIP_R - s, 1))
        with Locations((0, NUB_CY)):                                         # small antenna nub on top
            RectangleRounded(NUB_W - 2 * s, NUB_H - 2 * s, max(4 - s, 1))     # shrink height too → closed top wall
        # blend the two head→grip shoulders: the straight grip wall is narrower
        # than the round head, so it emerges as a sharp fin on each side. Fillet
        # those junction vertices so the grip flows smoothly into the head.
        shoulders = [v for v in sk.vertices() if -27 < v.Y < -8 and abs(v.X) > 12]
        if shoulders:
            fillet(shoulders, min(8.0, GRIP_W / 2 - s - 4))
        if with_loop:
            with Locations((0, -BODY_LEN)):
                Circle(LANYARD_R)
    return sk.sketch


with BuildPart() as case:
    add(extrude(silo(0, True), amount=TOP_F))        # outer to the raised front face
    # Hollow: cavity from z=WALL to z=TOP_F-WALL, leaving a closed back floor
    # (z 0..WALL) AND a front wall (z TOP_F-WALL..TOP_F). NOTE: a sketch passed
    # straight to extrude() keeps its own z=0 plane — a Locations((0,0,WALL))
    # context does NOT lift it (that bug cut the cavity from z0 and ate the back
    # floor). So build the cavity solid and .moved() it up explicitly.
    add(extrude(silo(WALL, False), amount=TOP_F - 2 * WALL)
        .moved(Location((0, 0, WALL))), mode=Mode.SUBTRACT)

    # Screen RECESSED behind a front bezel lip so it can't fall out: the viewing
    # window is SMALLER than the glass, so the lip overlaps (captures) the glass
    # edge. The module is inserted from BEHIND; the back-tub ring presses it
    # forward against the lip. Stack (front→back): bezel lip → glass pocket → board.
    WIN_D   = ACTIVE_D + 1.5                 # ≈45.7 view hole: > active (full screen shows), < glass
    GLASS_T = 1.1                            # glass thickness
    with Locations((0, 0, TOP_F)):           # viewing window through the bezel lip (z TOP_F-FRONT_LIP..TOP_F)
        Cylinder(WIN_D / 2, FRONT_LIP,
                 align=(Align.CENTER, Align.CENTER, Align.MAX), mode=Mode.SUBTRACT)
    with Locations((0, 0, TOP_F - FRONT_LIP)):   # glass pocket behind the lip (recess for the Ø48.8 glass)
        Cylinder(GLASS_POCKET_D / 2, GLASS_T + 0.3,
                 align=(Align.CENTER, Align.CENTER, Align.MAX), mode=Mode.SUBTRACT)

    # USB-C cutout (−X edge, at IO_Z): the real USB-C PORT shape (a rounded slot for
    # the plug tip) set inside a WIDER, ~3 mm-deep recess that the cable's overmoulded
    # plug nestles into. The bare head wall is only ~2.4 mm, so a thin inward boss
    # gives the port-slot a wall. Connector front sits at x≈−23.8 (STEP), so the boss
    # stops at −24.1 (0.3 mm clear). Centred on the connector (STEP: Y≈0, Z=IO_Z).
    USB_Y      = 0.0
    USB_WALLX  = math.sqrt(max(R_HEAD ** 2 - USB_Y ** 2, 1.0))            # outer wall ≈ 27.3
    USB_WALLIN = math.sqrt(max((R_HEAD - WALL) ** 2 - USB_Y ** 2, 1.0))   # inner wall ≈ 24.9
    PORT_W, PORT_H = 9.0, 3.4                                             # USB-C port slot (rounded)
    REC_W,  REC_H  = 12.5, 7.0                                            # plug-overmould recess (wider)
    USB_BOSS_T = 0.8                                                      # inward boss → port-slot wall
    with Locations((-(USB_WALLIN - USB_BOSS_T / 2), USB_Y, IO_Z)):        # inward boss behind the recess
        Box(USB_BOSS_T, REC_W + 1.0, REC_H + 1.0)
    with BuildSketch(Plane.YZ.offset(-USB_WALLX - 1.0)):                  # WIDE recess for the plug body
        with Locations((USB_Y, IO_Z)):
            RectangleRounded(REC_W, REC_H, 2.0)
    extrude(amount=1.0 + (USB_WALLX - USB_WALLIN), mode=Mode.SUBTRACT)    # depth = head wall (≈2.4)
    with BuildSketch(Plane.YZ.offset(-USB_WALLX - 1.0)):                  # USB-C PORT slot (plug tip)
        with Locations((USB_Y, IO_Z)):
            RectangleRounded(PORT_W, PORT_H, min(PORT_W, PORT_H) / 2 - 0.05)
    extrude(amount=WALL * 3, mode=Mode.SUBTRACT)                          # through the boss into the cavity

    # lanyard through-hole in the tip bump
    with Locations((0, -BODY_LEN, THICK / 2)):
        Cylinder(LANYARD_HOLE / 2, THICK * 2, mode=Mode.SUBTRACT)

    # sensor nests just BEHIND the display PCB, in the shallow zone (depth map):
    # PCB back ≈ z 9.1, sensor 4.5 deep → z ≈ 3.5..8.0, clearing the case floor.
    SNS_BACK_Z, SNS_FRONT_Z = 3.5, 8.0
    # antenna nub is now CLOSED on all sides (silo shrinks its height too, so
    # the main hollow leaves a WALL-thick top wall) — like the original Flash
    # Bee, the neck lives inside and RF passes through the thin plastic wall.

    # PWR/BOOT buttons: RIGHT edge (+X), y=±11.31 (STEP), at the PCB-back I/O height
    for by in (11.31, -11.31):
        bwx = math.sqrt(max(R_HEAD ** 2 - by ** 2, 1.0))
        with Locations(Location((bwx, by, IO_Z), (0, 90, 0))):
            Cylinder(1.9, WALL * 4, mode=Mode.SUBTRACT)

    # ── internals ──
    PCB_T = 1.6
    # board back = front face − bezel lip − glass − PCB; ring top sits a hair
    # PROUD (preload) so the closed back tub presses the module onto the bezel.
    BOARD_BACK_Z = (THICK - FRONT_LIP) - GLASS_T - PCB_T + 0.15   # ≈ 13.85
    # Display board support is built AFTER the split (see "display support web"
    # below) so it lands cleanly in the back shell — the old full ring here was a
    # floating inward shelf (an overhang that needs print support over the antenna).
    BAT_TOP_Y = -(R_HEAD - WALL) - 1.0
    with Locations((0, BAT_TOP_Y - BAT_L - 2, WALL)):           # battery bottom stop rib
        # narrower than the cavity so its ends don't run into the side walls
        # (it only needs to catch the battery's bottom edge)
        Box(BAT_W - 10, 2.0, 7.0, align=(Align.CENTER, Align.CENTER, Align.MIN))

    # Sensor retention = PUSH-PIN / snap-rivet (no flexing PCB, no breaking clips):
    # a boss with a hole at each Ø1.9 mount hole; a separate printed pin (flat head
    # + shaft) is pushed through the PCB hole into the boss → friction holds it and
    # the head clamps the board down. Tool-less; a pin is cheap to reprint if loose/
    # tight. Mount holes from the STEP (cjmcu-3935): board frame (−9.74,−7.2)/
    # (+10.26,−7.2), 8 mm from the header edge. Board CENTRE at SENSOR_CY (antenna
    # tip ≈ +28.6, clears the closed nub). Boss tops at SNS_BACK_Z → PCB rests there.
    SENSOR_CY     = 13.5                          # board centre, case Y
    SENS_HOLE_DY  = -7.2                          # mount-hole Y offset from board centre (STEP)
    SNS_HOLE_X    = (-9.74, 10.26)                # mount-hole X (STEP)
    SNS_POST_Y    = SENSOR_CY + SENS_HOLE_DY      # boss row ≈ 6.3
    SNS_PCB_TOP_Z = SNS_BACK_Z + 1.6              # PCB top ≈ z5.1
    PIN_HOLE_D    = 1.6                           # hole Ø; the split Ø1.8 shaft springs out to grip it
    for px in SNS_HOLE_X:
        with Locations((px, SNS_POST_Y, WALL)):                  # boss — PCB rests on its top (z3.5)
            Cylinder(2.5, SNS_BACK_Z - WALL, align=(Align.CENTER, Align.CENTER, Align.MIN))
        with Locations((px, SNS_POST_Y, SNS_BACK_Z)):            # friction hole down into boss + floor
            Cylinder(PIN_HOLE_D / 2, SNS_BACK_Z - 1.0,
                     align=(Align.CENTER, Align.CENTER, Align.MAX), mode=Mode.SUBTRACT)

bb = case.part.bounding_box().size
print(f"size {bb.X:.1f} x {bb.Y:.1f} x {bb.Z:.1f}  vol={case.part.volume:.0f}")

# ── round the outer front/back rims for the moulded look ───────────
# Fillet the lowest + highest ring of edges (z≈0 back face, z≈THICK front
# face incl. the board opening). OCC can choke near the grip/nub junctions,
# so fall back to a smaller radius and, failing that, leave the rims sharp.
shell = case.part
# Round the FRONT rim (the bezel around the glass) — the face you see + hold.
# Only the top ring: OCC reliably fillets it, whereas the back ring (antenna
# slot / lanyard cut-throughs) makes the solver fail or hang. r=1.0 is proven
# safe; larger radii can self-intersect at the board opening. FB_FILLET=0 off.
if os.environ.get("FB_FILLET", "1") != "0":
    try:
        shell = fillet(shell.edges().group_by(Axis.Z)[-1], 1.0)
        print("front rim filleted r=1.0")
    except Exception as _e:
        print(f"⚠ fillet skipped ({type(_e).__name__}) — front rim left sharp")

# ── snap-fit lip: tongue (back rim) into a groove (front lid) ──────────────
LIP_H, LIP_T, LIP_CLR = 3.0, 1.0, 0.25
with BuildPart() as _lip:                                   # 1 mm tongue, inner half of wall
    add(extrude(silo(WALL - LIP_T, False), amount=LIP_H))
    extrude(silo(WALL, False), amount=LIP_H, mode=Mode.SUBTRACT)
with BuildPart() as _groove:                                # matching groove (with clearance)
    add(extrude(silo(WALL - LIP_T - LIP_CLR, False), amount=LIP_H + 0.6))
    extrude(silo(WALL + LIP_CLR, False), amount=LIP_H + 0.6, mode=Mode.SUBTRACT)

# ── split front + back ─────────────────────────────────────────────
front = shell & (Pos(0, 0, PARTING) * Box(400, 400, THICK - PARTING + 5,
                 align=(Align.CENTER, Align.CENTER, Align.MIN)))
back = shell & (Pos(0, 0, PARTING) * Box(400, 400, PARTING + 5,
                align=(Align.CENTER, Align.CENTER, Align.MAX)))
back = back + _lip.part.moved(Location((0, 0, PARTING)))            # tongue on the back rim
# OCC collapses the front to an empty solid when cutting the perfectly concentric
# groove ring; it only succeeds once the front's symmetry is broken. Break it
# WITHOUT a through-hole: thin the wall on the INNER side at the −X (USB) seam,
# leaving a ~1 mm outer skin (radius-based, so the skin is uniform → no gap).
_skin, _rtop = 1.0, THICK - 1.0   # keep a ~1 mm outer skin (no through-hole); reach the groove zone
_relief = (Pos(0, 0, PARTING - 1) * Cylinder(R_HEAD - _skin, _rtop - (PARTING - 1),
                                             align=(Align.CENTER, Align.CENTER, Align.MIN))) \
          & (Pos(-R_HEAD, 0, PARTING - 1) * Box(WALL * 4, 14.0, _rtop - (PARTING - 1),
                                                align=(Align.CENTER, Align.CENTER, Align.MIN)))
front = front - _relief
front = front - _groove.part.moved(Location((0, 0, PARTING)))      # groove in the lid

# Sensor is now held by the 2 push-pins (boss + pin, built above) — the breaking
# cantilever snap-clips are gone.

# ── display board support: standoffs at the screw-mount keep-outs ─────────────
# The display module's back carries components right up to the edge, so it can
# only be supported at its 3 screw-mount holes (clean keep-outs). Their XY come
# straight from the module STEP. The apex mount (0,+20.5) sits over the AS3935
# sensor, so we plant standoffs only at the two GRIP-side mounts; the full-ring
# front bezel holds the top edge. Built in algebra mode (detached) → unioned in.
#   rest plane = mount-boss underside. The glass seats top-against the bezel lip
#   (z = THICK − FRONT_LIP), so its BOTTOM is GLASS_T lower; the boss underside is
#   9.2 mm below the glass bottom (user-measured on the real module). So:
GLASS_BOTTOM_Z = (THICK - FRONT_LIP) - GLASS_T      # ≈ 15.3
BOARD_REST_Z   = GLASS_BOTTOM_Z - 9.2               # ≈ 6.1  (boss underside / board seat)
DISP_MOUNTS  = [(13.75, -14.70), (-13.75, -14.70)]  # grip-side mounts (from STEP, case coords)
POST_OD = 4.5                                       # rest-shoulder Ø — the module's own (brass) standoff
# rests ON TOP of this flat post. NO spigot: the module standoff is solid, so a
# spigot would just collide with it (XY is located by the glass in the bezel).
for mx, my in DISP_MOUNTS:
    post = Pos(mx, my, WALL) * Cylinder(POST_OD / 2, BOARD_REST_Z - WALL,
                                        align=(Align.CENTER, Align.CENTER, Align.MIN))
    back = back + post

# anti-tip supports: 2 short posts on the +Y (antenna) side that just touch the
# PCB BACK at its rim (z≈9.5) so the screen can't rock back about the 2 grip
# standoffs. Positions STEP-verified clear of the back-side components AND the
# sensor (the marked +Y spots themselves had components/buttons underneath).
ANTITIP_PTS = [(-21.0, 7.6), (21.0, 8.0)]   # left / right of the antenna, case coords
PCB_BACK_Z  = (THICK - FRONT_LIP) - 6.9      # seated PCB back plane (6.9 behind the glass front)
# tapered: wide Ø4 foot (won't snap) → narrow Ø2 tip where it meets the PCB (the
# Ø2 tip is the STEP-verified clear width; the foot widens into the empty floor).
for mx, my in ANTITIP_PTS:
    back = back + Pos(mx, my, WALL) * Cone(2.0, 1.0, PCB_BACK_Z - WALL,
                                           align=(Align.CENTER, Align.CENTER, Align.MIN))

# the groove/relief booleans can leave `front` as a 2-solid Compound (a degenerate
# ~0-volume sliver) that rejects further booleans → flatten to real solids first.
_fr = [s for s in front.solids() if s.volume > 1.0]
if _fr:
    front = _fr[0]
    for _s in _fr[1:]:
        front = front + _s

# ── snap detents: ball bumps on the tongue click into dimples in the groove ──
# Gives a positive, tool-less "click" that holds the halves shut (the lip alone
# is just a friction fit). Bump/dimple sizes are the tuning knobs for snap force.
SNAP_R, SNAP_DIMPLE_R = 0.7, 0.95
SNAP_Z = PARTING + LIP_H / 2
_hy = 15.0                                                   # upper-head, clear of USB(-X,y0) + buttons(+X,y±11.31)
_hx = math.sqrt((R_HEAD - (WALL - LIP_T)) ** 2 - _hy ** 2)
_snap_pts = [(_hx, _hy), (-_hx, _hy),                                              # head upper L/R
             (GRIP_W / 2 - (WALL - LIP_T), -55.0), (-(GRIP_W / 2 - (WALL - LIP_T)), -55.0)]  # grip L/R
for (sx, sy) in _snap_pts:
    back = back + Pos(sx, sy, SNAP_Z) * Sphere(SNAP_R)
    front = front - Pos(sx, sy, SNAP_Z) * Sphere(SNAP_DIMPLE_R)

# ── battery hold-down: a rib on the FRONT lid presses the cell to the floor ──
# (the bottom rib stops Y-slide; the grip walls hug X; this kills Z rattle).
BAT_MID_Y = BAT_TOP_Y - BAT_L / 2
# rib runs from BAT_RIB_Z UP through the front wall to the face (z=THICK) so it
# fuses solidly to the lid. The real cell does NOT sit on the floor — its leads run
# underneath (a few mm), so it floats up: user measured the cell TOP at 1.4 mm below
# the parting tongue (z18) → z16.6. So the rib only needs to bridge that 1.4 mm: it
# reaches z16.6 to just touch the lifted cell (a short 1.4 mm nub, not the old 5.8 mm
# slab). Lower BAT_RIB_Z slightly (e.g. 16.3) for a firmer press if it rattles.
# Built via BuildPart().part (the lip/clips pattern) — the Pos*Box algebra fuse
# failed on this thin overlap. The groove/dimple booleans can leave a degenerate
# ~0-volume sliver, making `front` a 2-solid Compound that won't accept further
# unions — keep real solids.
_real = [s for s in front.solids() if s.volume > 1.0]
front = _real[0]
for _s in _real[1:]:
    front = front + _s
BAT_RIB_Z = 16.6
with BuildPart() as _batrib:
    Box(12, 40, THICK - BAT_RIB_Z, align=(Align.CENTER, Align.CENTER, Align.MIN))
front = front + _batrib.part.moved(Location((0, BAT_MID_Y, BAT_RIB_Z)))

# ── battery snap-over lips (BACK half): capture the cell during assembly ───────
# So the pouch stays seated in the open back half before the front lid's press
# rib clamps it. Each long edge gets 2 small lips overhanging the cell top by
# ~0.5 mm with a lead-in ramp on top (a soft LiPo pushes past it; pull-out is
# resisted by the flat underside). X=grip walls, Y−=bottom rib, Z=these + rib.
BAT_X    = BAT_W / 2 + CLEAR          # inner grip wall face ≈ 17.5
BAT_TOPZ = WALL + BAT_T              # cell top ≈ 12.4  (< PARTING, so it's in back)
with BuildPart() as _batclips:
    for sgn in (-1, 1):                                          # left / right long edge
        ix = sgn * BAT_X
        with BuildSketch(Plane.XZ):                             # wedge cross-section, extruded in Y
            with BuildLine():
                Polyline((ix, BAT_TOPZ), (ix - sgn * 1.0, BAT_TOPZ),   # 1 mm inward overhang
                         (ix, BAT_TOPZ + 1.4), close=True)             # lead-in ramp up to z≈13.8
            make_face()
        extrude(amount=4.0, both=True)                          # 8 mm long
for _ly in (BAT_MID_Y + 13, BAT_MID_Y - 13):                    # two per side along the cell
    back = back + _batclips.part.moved(Location((0, _ly, 0)))

# ── re-clear the button openings THROUGH the snap-lip ──────────────────
# At THICK=22 the I/O row (z≈IO_Z=12.3) sits well below the parting (z18), so
# the lip/groove no longer refill these — but re-cut the buttons on both halves
# as a cheap safeguard. (The USB-C is NOT re-cut here: a plain Box would square
# off the new rounded port-slot + recess; it's fully cut in the case build.)
for _by in (11.31, -11.31):
    _bwx = math.sqrt(R_HEAD ** 2 - _by ** 2)
    _bc = Location((_bwx, _by, IO_Z), (0, 90, 0)) * Cylinder(1.9, WALL * 4)
    back = back - _bc
    front = front - _bc

# ── button plungers (separate printed parts, captive once installed) ──
# Two side keys on the +X edge at Y=±11.31, axis along X at z≈PLG_Z. Each
# plunger: actuator tip → flange (Ø6, can't exit the Ø3.8 hole = captive) →
# stem (slides in the hole, tip proud outside). Drop in from inside before
# closing. PLG_REACH = how far the tip reaches past the inner wall toward the
# switch — tune after a test print (the exact key height isn't in the STEP).
PLG_Z = IO_Z
PLG_REACH, PLG_FL_T, PLG_PROUD = 2.0, 1.2, 0.8
def _wallx(rad, y):
    return math.sqrt(max(rad ** 2 - y ** 2, 1.0))
def plunger(by):
    xin, xout = _wallx(R_HEAD - WALL, by), _wallx(R_HEAD, by)
    act_len = PLG_REACH - PLG_FL_T
    stem_len = xout + PLG_PROUD - xin
    A = (Align.CENTER, Align.CENTER, Align.MIN)
    loc = Location((xin - PLG_REACH, by, PLG_Z))
    place = lambda s: s.rotate(Axis.Y, 90).moved(loc)                  # local +z → case +x
    act  = place(Cylinder(1.4, act_len, align=A))                      # actuator tip (Ø2.8)
    flng = place(Pos(0, 0, act_len) * Cylinder(3.0, PLG_FL_T, align=A))            # flange (Ø6)
    stem = place(Pos(0, 0, act_len + PLG_FL_T) * Cylinder(1.6, stem_len, align=A)) # stem (Ø3.2)
    # the Ø6 flange must FOLLOW the curved inner wall — a flat disc jams its
    # corners into the wall and won't seat. Clip it to the inner-wall cylinder so
    # its wall-side face is concave (the actuator + stem are left untouched).
    flng = flng & (Pos(0, 0, -THICK) * Cylinder(R_HEAD - WALL, THICK * 3, align=A))
    return act + flng + stem
plungers = [plunger(11.31), plunger(-11.31)]
export_step(plungers[0], f"{OUT}/plunger.step"); export_stl(plungers[0], f"{OUT}/plunger.stl")

# ── sensor push-pin (separate printed part, print 2) ──────────────────
# GLUE-IN pin: friction fits kept failing (FDM hole ±0.1 mm → too tight OR fell out).
# So this slides in EASILY (straight Ø1.4 grip body, slightly under the printed hole)
# and is meant to be set with a tiny drop of CA glue in the hole → rock solid, no
# tolerance fight. Flat head clamps the PCB; a lead-in tip starts it; long enough to
# grip the boss (2.2 mm into the 2.5-deep hole). Origin = head bottom (z0).
PIN_HEAD_D, PIN_SHAFT_D, PIN_HEAD_T = 4.5, 1.4, 1.2
PIN_SHAFT_L = (SNS_PCB_TOP_Z - SNS_BACK_Z) + 2.2    # through the PCB (1.6) + 2.2 into the boss hole
def sensor_pin():
    with BuildPart() as p:
        Cylinder(PIN_HEAD_D / 2, PIN_HEAD_T, align=(Align.CENTER, Align.CENTER, Align.MIN))         # flat head (clamps PCB)
        Cylinder(PIN_SHAFT_D / 2, PIN_SHAFT_L - 0.8, align=(Align.CENTER, Align.CENTER, Align.MAX)) # straight grip body Ø1.4
        with Locations((0, 0, -(PIN_SHAFT_L - 0.8))):                                               # lead-in tip Ø1.4→Ø0.9
            Cone(0.45, PIN_SHAFT_D / 2, 0.8, align=(Align.CENTER, Align.CENTER, Align.MAX))
    return p.part
pin = sensor_pin()
export_step(pin, f"{OUT}/sensor_pin.step"); export_stl(pin, f"{OUT}/sensor_pin.stl")

export_step(front, f"{OUT}/front_shell.step"); export_stl(front, f"{OUT}/front_shell.stl")
export_step(back, f"{OUT}/back_shell.step");   export_stl(back, f"{OUT}/back_shell.stl")
export_step(shell, f"{OUT}/assembly.step"); export_stl(shell, f"{OUT}/case.stl")
print(f"front {front.volume:.0f}  back {back.volume:.0f}  exported.")

# ── component ghosts (screen / sensor / battery) ───────────────────
# Transform imported STEPs with .moved(Rotation/Location) (the pattern OCP
# renders correctly). Loaded only when we're going to show or export them.
import sys
HERE = os.path.dirname(__file__)
SCREEN_DZ = (THICK - FRONT_LIP) - 2.77   # seats the module glass front at the bezel (tracks THICK)
SENSOR_DY, SENSOR_DZ = SENSOR_CY, 3.6  # board centre = SENSOR_CY (kept in sync with the pegs)
BAT_DX, BAT_DY, BAT_DZ = -17.0, -76.0, 3.0

SHOW = os.environ.get("FB_SHOW", "1") != "0"
SOLID = "solid" in sys.argv          # show the CLOSED case only — no see-through ghosts
# Fast by default: a normal run only shows the (lightweight) viewer. The heavy
# ~50 MB assembly_ghosts.step (detailed component STEPs) is written ONLY when you
# pass "export" — e.g. `flashbee_case.py export`.
EXPORT_ASM = "export" in sys.argv

if SHOW and SOLID:                    # unambiguous "is it closed?" view
    from ocp_vscode import show, set_port, Camera
    set_port(3939)
    show(shell, *plungers, names=["case (closed)", "plunger_L", "plunger_R"],
         colors=["#c8a200", "#d83a3a", "#d83a3a"],
         reset_camera=Camera.CENTER)    # re-frame on each push, keep the angle
    print("→ shown CLOSED case + plungers (no ghosts). Only the screen window is open.")
elif SHOW or EXPORT_ASM:
    DETAIL = "detail" in sys.argv          # show the full ~666-solid component STEPs
    screen = sensor = battery = None

    if EXPORT_ASM or DETAIL:               # import the detailed manufacturer STEPs (slow)
        screen = import_step(f"{HERE}/ESP32-S3-Touch-AMOLED-1_75.stp") \
            .moved(Rotation(90, 0, 0)).moved(Location((0, 0, SCREEN_DZ)))
        sensor = import_step(f"{HERE}/cjmcu-3935.step") \
            .moved(Rotation(90, 0, 0)).moved(Location((0, SENSOR_DY, SENSOR_DZ)))
        battery = import_step(f"{HERE}/Battery v4.step") \
            .moved(Rotation(90, 0, 0)).moved(Location((BAT_DX, BAT_DY, BAT_DZ)))

    if EXPORT_ASM:                         # combined STEP with the detailed ghosts
        for o, lbl, col in [(back, "back_shell", Color(0.18, 0.18, 0.18)),
                            (front, "front_shell", Color(0.88, 0.70, 0.00)),
                            (screen, "screen", Color(0.23, 0.51, 0.96)),
                            (sensor, "sensor", Color(0.66, 0.33, 0.97)),
                            (battery, "battery", Color(0.13, 0.77, 0.37))]:
            o.label = lbl
            o.color = col
        for i, pl in enumerate(plungers):
            pl.label = f"plunger_{i}"
            pl.color = Color(0.85, 0.23, 0.23)
        asm = Compound(children=[back, front, screen, sensor, battery, *plungers])
        asm.label = "FlashBee_Assembly"
        print("exporting assembly_ghosts.step (heavy)…")
        export_step(asm, f"{OUT}/assembly_ghosts.step")
        print("→ wrote assembly_ghosts.step")

    if SHOW:                               # direct (blocking) push to the OCP CAD Viewer
        # FAST viewer: lightweight primitive ghosts built from known dims — NO STEP
        # import (the board STEP is ~666 solids → minutes of tessellation + a bogged,
        # non-refreshing viewer tree). Same envelope/position for a fit check.
        # 'detail' shows the full component STEPs instead.
        if DETAIL:
            objs   = [back, front, screen, sensor, battery, *plungers]
            names  = ["back_shell", "front_shell", "screen", "sensor", "battery",
                      "plunger_L", "plunger_R"]
            colors = ["#3a3a3a", "#c8a200", "#3a6ea5", "#8e44ad", "#27ae60",
                      "#d83a3a", "#d83a3a"]
            alphas = [1.0, 1.0, 0.55, 0.6, 0.6, 1.0, 1.0]
        else:
            # Rich but light: import the display STEP and keep only the SIGNIFICANT
            # solids (vol > 1 mm³ → ~35 of 666). That preserves the REAL shapes —
            # glass, PCB, AMOLED panel, the round USB-C, the 8-pin IO header, the
            # battery/speaker connectors, the ICs, the 3 mount bosses and the 2
            # buttons — while dropping the hundreds of tiny SMD parts so it still
            # renders fast. ('detail' shows all 666 solids.)
            _draw = import_step(f"{HERE}/ESP32-S3-Touch-AMOLED-1_75.stp") \
                .moved(Rotation(90, 0, 0)).moved(Location((0, 0, SCREEN_DZ)))
            _ds = [s for s in _draw.solids() if s.volume > 1.0]
            _glass = max(_ds, key=lambda s: s.volume)            # Ø49 cover glass (biggest)
            display_glass = _glass
            display_board = Compound(children=[s for s in _ds if s is not _glass])
            sensor = import_step(f"{HERE}/cjmcu-3935.step") \
                .moved(Rotation(90, 0, 0)).moved(Location((0, SENSOR_CY, SENSOR_DZ)))
            battpack = Pos(0, BAT_TOP_Y - BAT_L / 2, WALL + BAT_T / 2) * Box(BAT_W, BAT_L, BAT_T)
            # highlight the case display supports (same geometry as in `back`) so
            # they're easy to spot: 2 grip-side mount standoffs + 2 anti-tip posts.
            standoffs = None
            for mx, my in DISP_MOUNTS:
                _p = Pos(mx, my, WALL) * Cylinder(POST_OD / 2, BOARD_REST_Z - WALL,
                                                  align=(Align.CENTER, Align.CENTER, Align.MIN))
                standoffs = _p if standoffs is None else standoffs + _p
            for mx, my in ANTITIP_PTS:
                standoffs = standoffs + Pos(mx, my, WALL) * Cone(2.0, 1.0, PCB_BACK_Z - WALL,
                                                  align=(Align.CENTER, Align.CENTER, Align.MIN))
            pins = None                                  # the 2 push-pins, seated head-on-PCB
            for px in SNS_HOLE_X:
                _pp = pin.moved(Location((px, SNS_POST_Y, SNS_PCB_TOP_Z)))
                pins = _pp if pins is None else pins + _pp
            objs   = [back, front, display_board, display_glass, standoffs, sensor, pins, battpack, *plungers]
            names  = ["back_shell", "front_shell", "display_board", "glass", "display_standoffs",
                      "sensor", "sensor_pins", "battery", "plunger_L", "plunger_R"]
            colors = ["#3a3a3a", "#c8a200", "#3a6ea5", "#8fd6e8", "#ff6a00",
                      "#8e44ad", "#2fd07b", "#b9bcc2", "#d83a3a", "#d83a3a"]
            alphas = [0.55, 0.45, 0.85, 0.30, 1.0, 0.80, 1.0, 0.60, 1.0, 1.0]
        from ocp_vscode import show, set_port, Camera
        set_port(3939)
        show(*objs, names=names, colors=colors, alphas=alphas,
             reset_camera=Camera.CENTER)    # re-frame on each push, keep the angle
        print("→ shown in OCP CAD Viewer (real STEP, significant solids; pass 'detail' for all 666)")
else:
    print("FB_SHOW=0 — viewer/ghosts skipped (case still built + exported).")
