/* em_sfx.h — one-shot sound effects mixed over the BGM stream: the native
 * mirror of the engine's SShd bank trigger path (FINDINGS.md "Audio — VAG
 * ADPCM" / "SShd bank format", 2026-06-10).
 *
 * Engine model being mirrored:
 *   - The PS2 triggers an SFX by SOUND ID through the bank's trigger-script
 *     table (A0 event scripts; func_001152D8 status 0xA0 ->
 *     func_00115850 voice setup). Gameplay code asks for ids — 0x162 weapon
 *     draw, 0x163 holster/reload-start, 0x164/0x165 fire, 0x168 reload mag
 *     action, 0x169 dry click, the footstep surface + gear layers,
 *     0x7D8 the canonical hurt-helper death (func_00153B50) — and the
 *     sequencer mixes the voices over the streamed BGM on the SPU2.
 *   - Natively the id -> script resolution is the EMSR registry
 *     assets/sfx/sfx_registry.emsr (tools/export_sfx_registry.py, from the
 *     user's own disc data). Every trigger script is A0 events (00115850),
 *     and each entry keeps, per A0 voice, the sequencer tick, the integer
 *     SPU pitch word (bend 0x40 -> 00117918 -> *44100/48000) and the
 *     001179E0 volume inputs, so the mixer applies the ORIGINAL Q14 volume
 *     words (docs/SFX_PITCH.md; WP-14, H19/AM-01/AM-02). The legacy
 *     sfx.txt WAV registry and its x1.531-sharp rates are retired.
 *   - Entries are scoped: (-1,-1) for group-1 global records, else the
 *     (area, sub) the id was resolved for. em_sfx_set_area selects the
 *     scope; an area-dependent id with no scope bound is silent and
 *     counted (em_sfx_unscoped_cues), never guessed from another area.
 *     Ids whose scripts need unreproduced driver features (controllers,
 *     looping samples, sustained key-off) are UNSUPPORTED: silent,
 *     counted, reported once.
 *   - The scoped EMSF bank separately preserves the original AREA11 panel
 *     cue with its verified steady envelope and takes precedence while
 *     (11,0) is selected. See docs/AREA11_PANEL_SFX.md.
 *   - Native boundaries: linear interpolation, no ADSR/reverb, sequencer
 *     ticks at the NTSC field rate, no 00117428 voice allocation.
 *
 * POSITIONAL AUDIO — the engine's play_sound and its 3-D volume/pan
 * solver. Re-verified 2026-07-31 against the BYTE-MATCHED decomp
 * src/func_001FBD50.c and src/func_001FBF50.c (both compile to the
 * original machine code; every constant below is quoted from them):
 *
 *   play_sound(obj, id, flat2d, float radius) = func_001FBD50, which
 *   calls func_001FBF50(obj, &gainA, &gainB, flat2d, radius, 4096.0f)
 *   — the 4096.0f is the volume scale, hence 1.0 = 0x1000 below. The
 *   pair is computed ONCE at trigger time (no per-frame re-pan) and
 *   submitted as func_001FB9F0(id, 0x1000, gainA, gainB). Downstream
 *   chain, all re-read this audit: func_001FB9F0 -> func_0011A218(voice,
 *   gainA, gainB) (BYTE-MATCHED) stores them at track +0x48/+0x4C only
 *   when BOTH lie in [-0x1000, 0x1000] (otherwise the 00119EA0 defaults
 *   0x1000/0x1000 stay); func_001179E0 then forms the two
 *   output volumes as (t * (panLUT >> 8) * ch[+0x48]) >> 19 and
 *   (t * (panLUT & 0xFF) * ch[+0x4C]) >> 19, where panLUT = voice +0x32
 *   = D_00242630[tone_pan >> 2] (00115850 via 00117BA0(4,0); the
 *   (short) result is then halved into the SPU word, see
 *   em_sfx_volume_words). So +0x48 pairs with the
 *   pan-LUT HIGH byte and +0x4C with the LOW byte; with the LUT's
 *   pan-0 entry (0x80,0x00) that pins gainA = LEFT. Decoded math,
 *   normalized to 1.0 = 0x1000:
 *
 *     DISTANCE (listener = the PLAYER position, D_00810360):
 *       d = |src - player|            (full 3-D Euclidean; the flat2d
 *                                      flag zeroes Y — no translated
 *                                      call site passes it)
 *       d >= radius -> INAUDIBLE: the engine does not submit at all
 *                      (play_sound returns -1)
 *       vol = sin(pi/2 * (radius - d)/radius)     quarter-sine ease:
 *             full at d=0, ~linear fade near the rim, 0 at d=radius.
 *             (func_001FBF50 writes 4096.0f * func_0011E2A8(1.5707964f
 *             * ((radius - d)/radius)); func_0011E2A8 disassembles as
 *             the fdlibm sinf — rem_pio2 quadrant 0/1/2/3 dispatching
 *             to +kernel_sin/+kernel_cos/-kernel_sin/-kernel_cos with
 *             the |x| < pi/4 kernel_sin fast path.)
 *       radius is a per-call-site constant: 300.0 at 281 of ~320
 *       constant sites incl. every id translated below (others:
 *       450/500/800/1000 — none translated yet). The earlier
 *       "vol 150/300" reading was the radius misread: the draw-sound
 *       site func_0016F530 also passes 300.0.
 *
 *     PAN (listener = the CAMERA: eye D_008105D0 + yaw D_0081027C =
 *     cam+0x9C; XZ plane only):
 *       delta = wrap_pi(atan2(src.x - eye.x, src.z - eye.z) - cam_yaw)
 *       c     = cos(delta)            (engine: dot of the yaw-rotated
 *                                      (0,0,1) with the normalized XZ
 *                                      eye->src vector — identical)
 *       k     = min(d / 18, 1)        d = the PLAYER distance above —
 *                                      pan ramps in over the first 18 u
 *                                      (player-attached sounds: k = 0)
 *       t     = c^5 * k + sign * (1 - k),   sign = +-1 from c^5 * k
 *       near channel (delta >= 0 -> LEFT) = vol
 *       far  channel                      = vol * t
 *     t = +1 ahead (center), 0 at 90 deg far side (full pan), -1
 *     behind: the far channel is PHASE-INVERTED at full amplitude (SPU2
 *     negative volume) — the engine's pseudo-surround rear cue. The
 *     port mixer carries signed float gains, so the inversion survives
 *     verbatim. The engine quantizes to ints 0..4096 (float_to_int);
 *     the port keeps floats (sub-1/4096 difference only). The engine
 *     MONO option (D_0028215B == 1 -> both channels = vol) has no port
 *     setting yet and is not modeled.
 *
 *   em_sfx_play(id) (no position) submits requests 0x1000/0x1000 —
 *   exactly the engine's non-positional submit func_001FB9F0(id, 0x1000,
 *   0x1000, 0x1000) — the water/footing one-shots use this form
 *   verbatim: FINDINGS "FOOTSTEP SURFACE TABLE" pins func_00187DE0 as
 *   func_001FB9F0(0xCA shallow / 0xDB deep, 0x1000 x3) and func_00187EA0
 *   as func_001FB9F0(0xA8, 0x1000 x3) — AND it is the exact limit of
 *   play_sound at the player (d=0 -> vol=1, k=0 -> t=+1, since
 *   func_001FBF50 takes the `k >= 0` branch at k == 0.0f and adds
 *   (1 - w) = 1):
 *   player-attached sounds (footsteps, weapon handling, shots, casing,
 *   hurt/death voice) are center/full BY THE ENGINE MATH, so their
 *   call sites stay on em_sfx_play.
 *   em_sfx_play_at (positional) instead submits float_to_int(4096 *
 *   gain) per channel (001FBF50 -> 001281C0, truncation toward zero).
 *
 * VOICE STEALING — RE-CORRECTED 2026-09 (WP-14). Every registry trigger
 * script consists of A0 events (the exporter rejects anything else), and
 * 001152D8 routes 0xA0 to func_00115850, which allocates through
 * func_00117428 (same-tone retrigger pass, free pass, priority-gated
 * oldest pass). The 2026-07-31 note below described func_001172B8, the
 * 0x90 note-on allocator, which these scripts never reach. Neither
 * allocator is reproduced: the port policy below is an approximation.
 * The superseded text is kept for its 001172B8 decode:
 *
 *   func_001172B8(tone_byte0) — src/func_001172B8.c, a NEARMISS (its
 *   logic is authoritative, its scheduling is not) — walks the 48-entry
 *   voice table D_0027CCC0 (stride 0x6A) round-robin from the shared
 *   cursor D_0027F740+0x30:
 *     1. FIRST busy voice (+0x00 != 0) whose state +0x1A == 3 is taken
 *        and reused in place;
 *     2. else, among +0x1A == 1 voices, the OLDEST — minimum +0x0A, the
 *        note-on serial stamped at trigger time from the D_0027F740+0x34
 *        counter (func_00115E50: voice.f0A = *(u16*)(D_0027F740+0x34))
 *        — with the +0x08 == 1 group ranked ahead of the +0x08 != 1
 *        group;
 *     3. else -1, and func_00115E50 breaks out of its tone loop: the
 *        note-on is silently dropped.
 *   There is NO priority gate and NO "same tone byte0 + bank handle"
 *   retrigger pass anywhere in func_001172B8. Both of those belong to
 *   func_00117428, which func_00115850 calls as
 *   func_00117428(tone[0], tone[1], handle) and which gates its steals
 *   on arg1 >= victim +0x1E. The old note that "byte0 is 0 on every
 *   shipped SFX tone, so retrigger never fires" is therefore moot on
 *   this path either way — the pass does not exist here. (The +0x1E =
 *   tone byte1 = priority and +0x20 = tone byte0 identifications do
 *   hold: func_00115E50 writes voice.f1E = tone[1] and voice.f20 =
 *   tone[0]. They just are not consulted by the note-on allocator.)
 *
 *   PORT POLICY (faithful-feasible): the engine's 48-voice budget over
 *   64 physical slots; when 48 voices are live the OLDEST live voice
 *   (minimum play serial) is killed — the same minimum-+0x0A rule as
 *   func_001172B8 pass 2, and with no priority gate, exactly like the
 *   engine on this path. Pass 1 (reuse a +0x1A == 3 voice) has no port
 *   analogue: port one-shots have no decaying/released state. The
 *   +0x08 sub-ranking inside pass 2 is a driver flag we do not model.
 *   The kill is honored by the audio thread at its next callback — the
 *   engine's own steal is likewise deferred to the next driver tick
 *   through the func_001157F0 command queue — and the 16 spare slots
 *   absorb that latency, so a play is dropped only when 64 slots are
 *   busy (unreachable in practice, still counted in em_sfx_drops).
 *
 * DEVICE OWNERSHIP: em_bgm owns the single em_audio device (em_audio.h pull
 * model). em_sfx NEVER opens a device — em_bgm's render callback calls
 * em_sfx_mix() to sum the one-shot voices into the same buffer, and
 * em_sfx_play() asks em_bgm to bring the shared device up (at the BGM
 * default 48 kHz) if music has not already done so.
 *
 * THREADING (per the em_audio.h contract): single producer (game thread) /
 * single consumer (the OS audio thread). Samples are preloaded PCM16 at
 * registry-load time and immutable until shutdown, so the audio thread only
 * ever reads memory. Each voice slot carries an atomic state word:
 *
 *      game thread                      audio thread (inside bgm_render)
 *      -----------                      --------------------------------
 *      CAS FREE -> STAGING
 *      write sound ptr, pos = 0
 *      store READY      (release) --->  load (acquire): READY -> adopt
 *                                       (PLAYING), resample-mix, sum;
 *                                       sample exhausted ->
 *                                 <---  store FREE (release)
 *
 * The game thread touches only FREE slots (the CAS), the audio thread only
 * non-FREE ones; no locks, no allocation, no I/O on the audio thread.
 * STEALING extends the protocol with one atomic `kill` flag per slot: the
 * game thread raises it on the chosen victim (never on a slot it already
 * raised it on); the audio thread, on seeing kill up on a READY/PLAYING
 * slot, lowers it and stores FREE instead of mixing. kill is lowered by
 * the claimer inside STAGING too (a victim can finish naturally and be
 * re-claimed before the audio thread ever saw the flag), and the READY
 * release-store publishes that clear with the other fields.
 *
 * Call ordering (game thread): em_sfx_init() at boot (after the manifest,
 * before plays), em_sfx_play() during gameplay, em_sfx_shutdown() AFTER
 * em_bgm_shutdown() (which tears down the device and guarantees the
 * callback can no longer fire — only then is the sample memory freeable).
 */
#ifndef EM_SFX_H
#define EM_SFX_H

#ifdef __cplusplus
extern "C" {
#endif

/* --- Engine sound ids (FINDINGS.md "WEAPON SYSTEM" section 8 sound list;
 *     "ENEMY AI" damage-pipeline consumption; reload + footsteps live-pinned
 *     2026-06-10 s29, "GAMEPLAY SOUND IDS PINNED LIVE") ------------------- */
#define EM_SFX_WPN_DRAW     0x162u  /* major-0 ENTER (func_0016F530;
                                     * play_sound radius 300 like the rest
                                     * — the old "vol 150" was a misread) */
#define EM_SFX_WPN_HANDLE   0x163u  /* SHARED weapon-handling foley (s29):
                                     * state 0x65 HOLSTER entry AND the
                                     * RELOAD START — not holster-specific    */
#define EM_SFX_WPN_FIRE     0x164u  /* per-shot block (anims 0x31/0x34)       */
#define EM_SFX_WPN_FIRE_ALT 0x165u  /* per-shot block (anims 0x32/0x35) —
                                     * stance-keyed; not selected natively
                                     * until the stance pairs are translated  */
#define EM_SFX_WPN_MAG      0x168u  /* RELOAD MAG ACTION, ~0.5 s into the
                                     * 0x33 reload anim (s29 live capture;
                                     * snd_0351, 39006 Hz, global bank)       */
#define EM_SFX_WPN_DRY      0x169u  /* empty mag + empty reserve click        */
/* FIRE-CHAIN TAIL (live-pinned s29, FINDINGS "GAMEPLAY SOUND IDS PINNED
 * LIVE"): each shot is followed by the wall impact/ricochet ~2 frames
 * after the fire sound (office wall hit) and the shell casing hitting
 * the floor ~0.7 s after the shot. em_weapon schedules both off the
 * shot tick (the reload-mag-action pattern).
 * SURFACE-VARIANT FLAG: the soundmap's 0x188/0x18A/0x18B neighbors
 * (snd_0423/0422/0420 — consecutive tones 26..29 of the same program)
 * look like the per-surface impact family the engine's surface-keyed
 * impact resolver would pick from (the hit record's +0x1A attr — the
 * same byte FINDINGS "FOOTSTEP SURFACE TABLE" shows func_00175900
 * copying into actor +0x23A, where attrs 0x5A/0x5B/0x5C DO key decoded
 * one-shots: 0x86 splash, 0xCA/0xDB water entry, 0xA8). Re-checked
 * 2026-07-31: still true that NO surface -> IMPACT sound-id mapping is
 * pinned — 0x188/0x18A/0x18B appear nowhere in FINDINGS beyond this
 * conjecture, and 0x189 itself is only a live observation
 * (FINDINGS "0x189 impact/ricochet ~2 frames later (office wall hit)"),
 * not a decoded selection. The port plays 0x189 for every wall,
 * flagged. */
#define EM_SFX_WPN_IMPACT   0x189u  /* bullet WALL impact/ricochet, fire
                                     * +2 frames (s29 live; snd_0421)         */
#define EM_SFX_WPN_CASING   0x16Au  /* shell casing floor bounce, ~0.7 s
                                     * (42 ticks) after each shot (s29
                                     * live; 2-event, snd_0347 — em_sfx
                                     * plays the first event until the
                                     * multi-event trigger scripts land)      */
/* KNIFE / MELEE (s36 decode — em_weapon.h "KNIFE / MELEE"): the swing/
 * impact sounds fire at each attack's impact gate, vol 300, hit or
 * whiff (func_001735C0 / func_00173E60; the heavy stab reuses 0x17F).
 * 0x179 is the armed-stance SQUARE sub-weapon toggle-ON sound
 * (func_0017A970 attachment-0 arm — the s29 "unidentified action"). */
#define EM_SFX_MELEE_HIT1   0x17Du  /* light combo hit 1 (damage 3)           */
#define EM_SFX_MELEE_HIT2   0x17Eu  /* light combo hit 2 (damage 3)           */
#define EM_SFX_MELEE_HIT3   0x17Fu  /* light hit 3 (damage 5) AND the
                                     * heavy stab (damage 15) — shared id     */
#define EM_SFX_SUB_TOGGLE   0x179u  /* D_00810D3C toggle-ON (SQUARE while
                                     * armed, attachment 0; snd_0436)         */
#define EM_SFX_ENEMY_DEATH  0x7D8u  /* canonical hurt-helper death
                                     * (func_00153B50 HP<=0; the crawler's
                                     * own gore set — burst 0x434 etc. — is
                                     * not pinned per-state yet)              */

/* FOOTSTEPS (s37 static decode — decomp FINDINGS.md "FOOTSTEP SURFACE
 * TABLE"; replaces the s29 fixed-pair reading): each step submits TWO
 * ids back-to-back, both with an independent random variant —
 *
 *   surface_id = BLOCK(attr) + GAIT_SUB(gait) + rand5()
 *   gear_id    = EM_SFX_STEP_GEAR_BASE       + rand5()
 *
 * There is NO id table in the engine: the mapping is compiled-in
 * immediates in the func_00182430 mapper (the per-material 17-id BLOCK
 * bases live in em_game.c footstep_block; attr 0/unmapped -> 0x10, the
 * office floor), GAIT_SUB is gait 3 -> +0xA, gait 2 -> +5, else +0,
 * and rand5 = (rand() & 7) with 5..7 folded to 0..2. The s29 "floor A
 * 0x15/0x16 vs floor B 0x1A/0x1B pairs, alternating L/R" were the SAME
 * material at walk vs run gait with the rand bias toward 0..2; neither
 * layer alternates. The office registry ships the full variant sets
 * 0x15..0x19 (walk), 0x1A..0x1E (run), 0x138..0x13C (gear). */
#define EM_SFX_STEP_GEAR_BASE 0x138u  /* gear/cloth foley (snd_0311/0308/
                                       * 0309/0310/0306 = base + 0..4)  */

/* LOCKED DOOR — re-verified 2026-07-31 against FINDINGS "DOOR SCRIPTS
 * DECODED": the locked-try script is D_0024DEC0 (queued by
 * func_001BBE40 mode 1) and its itemization lists literally
 * "@24DD80 op17 sub0  positional sound id 0x3F2 (locked rattle)", with
 * the preceding wait putting it at the 60-frame mark where the
 * lock-fixture jiggle peaks (FINDINGS: "rot-from-rest peaks 24.0 deg
 * around f60-110 ... times the script's 60-frame wait -> 0x3F2 rattle
 * SFX"). snd_0533 in every exported area's bank (gen_sfx_registry).
 * NOTE this is an op-0x17 POSITIONAL record — em_door should be feeding
 * em_sfx_play_at at the door, not em_sfx_play (em_door is not this
 * file's to change; flagged for the em_door pass).
 * The locked "VO" that follows is a TEXT-ONLY radio message: FINDINGS
 * "Every locked-door line record's voice_cue is -1 ... the locked 'VO'
 * plays NO audio — it is a ... RADIO SUBTITLE" — no id to define;
 * em_door plays the optional scene.txt `lockedvo` registry id if one
 * ever resolves (none do). */
#define EM_SFX_DOOR_RATTLE  0x3F2u  /* locked-door handle rattle */

/* --- PLACEHOLDER ids (flagged — NOT engine-documented; chosen far above
 *     the observed bank id range so they can never collide).
 *     DOOR ids: the REAL door sounds are decoded — re-verified
 *     2026-07-31 against FINDINGS "Door sounds — D_0024DB80": a
 *     [front, back] halfword PAIR table indexed by the door LINK
 *     halfword's HIGH byte (link +0x56 >> 8), family ids 0x3FB..0x40E,
 *     patched into the op-0x0B sub-6 record by func_001BBD60 (the
 *     direct variant func_001BBD20 tail-jumps play_sound at radius
 *     300). The dumped table's selector 2 is [0x3FD, 0x3FE] and both
 *     OFFICE door placements carry link 0x0200/0x0280 -> selector 2 ->
 *     0x3FD front / 0x3FE back, exactly as stated. em_door consumes the pair through the
 *     optional scene.txt "doorsfx <front> <back>" line (em_door.h,
 *     generated by the decomp repo's tools/gen_sfx_registry.py); the
 *     two defines below remain only as the LEGACY fallback when that
 *     line is absent (silent no-ops unless mapped in the registry).
 *     (The old 0xF002 reload placeholder is GONE: s29 pinned the real
 *     reload pair — start 0x163 + mag action 0x168 — and em_weapon
 *     plays those directly.) ------------------------------------------- */
#define EM_SFX_DOOR_OPEN    0xF000u /* LEGACY fallback — real id = the
                                     * doorsfx pair, played per side      */
#define EM_SFX_DOOR_CLOSE   0xF001u /* LEGACY fallback — the engine's open
                                     * script has NO close sound record   */

/* Load the EMSR registry and the independent AREA11 EMSF bank. Either
 * file that is missing or invalid is unavailable as a whole (reported).
 * Returns the audible registry entries plus one for the panel bank. Game
 * thread, once at boot. */
int em_sfx_init(void);

/* Select the original (area, sub) scope after loading its scene (the
 * D_00810700/701 pair 001FB9F0 reads). (11,0) requires the complete
 * audited panel bank and returns zero (scope cleared) if it is missing.
 * Other pairs select their registry scope and return one; (-1,-1) clears.
 * Already-playing voices retain immutable data through completion. */
int em_sfx_set_area(int area, int sub);

/* Current scope: 0 unavailable, 1 audible asset loaded, 2 intentional
 * original FF remap (accepted as silence, does not allocate a voice). */
int em_sfx_cue_state(unsigned id);
int em_sfx_absent_cues(void);
int em_sfx_unsupported_cues(void); /* UNSUPPORTED registry plays refused */
int em_sfx_unscoped_cues(void);    /* area-dependent plays whose scope
                                    * (or lack of one) was not exported  */

/* Fire one one-shot voice for the engine sound id at CENTER/FULL — the
 * engine's non-positional submit form AND the exact play_sound result for
 * a player-attached source (see "POSITIONAL AUDIO" above). Unknown/
 * unloaded id or disabled module = silent no-op. Brings the shared audio
 * device up through em_bgm if no music has started yet. Game thread
 * only. */
void em_sfx_play(unsigned id);

/* Per-frame listener mirror: player position (the engine's DISTANCE
 * listener D_00810360), camera eye + yaw (the PAN listener D_008105D0 /
 * D_0081027C). Call once per gameplay frame after the camera commit.
 * Until first called, em_sfx_play_at degrades to center/full with no
 * distance cull (exactly em_sfx_play) — keeps headless/early-boot
 * behavior identical. Game thread only. */
void em_sfx_listener(const float player_pos[3], const float cam_eye[3],
                     float cam_yaw);

/* POSITIONAL one-shot — the native play_sound(obj, id, 0, radius). The
 * stereo gain pair from the BYTE-MATCHED src/func_001FBF50.c is computed
 * ONCE here and baked into the voice; "no per-frame re-pan" re-verified
 * 2026-07-31: the pair lands in channel +0x48/+0x4C via func_0011A218
 * and nothing else in the driver rewrites those two words (they are
 * otherwise only initialised to 0x1000 by func_00119650/func_00119EA0),
 * so it is static for the life of the voice. A source at d >= radius is
 * culled exactly like the engine — func_001FBF50 does
 * `if (!(dist < radius)) return 0;` BEFORE submitting anything, and
 * func_001FBD50 turns that into its -1 return. Counted in
 * em_sfx_culls. Game thread only. */
void em_sfx_play_at(unsigned id, const float pos[3], float radius);

/* The pure gain solver behind em_sfx_play_at, exposed for tests/tools:
 * writes the LEFT/RIGHT gains (1.0 = engine 0x1000; the far channel may
 * be NEGATIVE = the engine's behind-the-camera phase inversion) and
 * returns 1, or returns 0 when the source is out of range (engine
 * play_sound -1). Uses the em_sfx_listener state. */
int em_sfx_compute_gains(const float pos[3], float radius,
                         float *gain_l, float *gain_r);

/* AUDIO-THREAD mixer half: SUM all live one-shot voices into the
 * interleaved stereo buffer (which already holds the BGM frames), each
 * A0 voice at its SPU pitch (4096 = 48 kHz) and Q14 volume words. Called by
 * em_bgm's render callback only — real-time safe per the em_audio.h
 * contract (no locks/allocation/IO). A no-op while no voices are live. */
void em_sfx_mix(float *out_interleaved_stereo, int frames, int device_rate);

/* Free the preloaded samples. Game thread, AFTER em_bgm_shutdown() (the
 * device-teardown guarantee is what makes the sample memory safe to
 * free). Prints the mixed-voice counters if any one-shot ever played. */
/* Stop every live voice immediately (engine func_001FBC50 — the audio
 * reset/stop-all, mislabelled "Subsystem init" in the decomp). Call at a
 * game-over, a scripted cut, or the script's op-0x17 sub-3 stop. This is a
 * RUNTIME stop; em_sfx_shutdown() below is the process-exit teardown and must
 * not be used for it. */
void em_sfx_stop_all(void);

void em_sfx_shutdown(void);

/* Introspection (EM_SFX_TEST / debugging; game thread). */
int  em_sfx_sound_count(void);    /* audible registry entries loaded    */
int  em_sfx_plays(void);          /* accepted em_sfx_play(_at) calls    */
int  em_sfx_drops(void);          /* plays dropped (64 physical busy)   */
int  em_sfx_steals(void);         /* oldest-voice kills at the 48 budget*/
int  em_sfx_culls(void);          /* play_at sources culled at >=radius */
long em_sfx_frames_mixed(void);   /* summed voice frames mixed so far   */
int  em_sfx_max_concurrent(void); /* peak simultaneous live voices      */

#ifdef __cplusplus
}
#endif

#endif /* EM_SFX_H */
