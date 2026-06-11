/* em_sfx.h — one-shot sound effects mixed over the BGM stream: the native
 * mirror of the engine's SShd bank trigger path (FINDINGS.md "Audio — VAG
 * ADPCM" / "SShd bank format", 2026-06-10).
 *
 * Engine model being mirrored:
 *   - The PS2 triggers an SFX by SOUND ID through the bank's trigger-script
 *     table (note-on event scripts; func_001152D8 status 0x90 ->
 *     func_00115E50 voice setup). Gameplay code asks for ids — 0x162 weapon
 *     draw, 0x163 holster/reload-start, 0x164/0x165 fire, 0x168 reload mag
 *     action, 0x169 dry click, the footstep surface + gear layers,
 *     0x7D8 the canonical hurt-helper death (func_00153B50) — and the
 *     sequencer mixes the voices over the streamed BGM on the SPU2.
 *   - Natively the bank's id -> sample resolution is a small text registry,
 *     assets/sfx/sfx.txt: one "<id-hex> <wav-path>" line per sound (the
 *     WAVs are the user's own local audio_export.py decodes; '#' starts a
 *     comment). NO registry file = the whole module is a silent no-op —
 *     byte-for-byte the pre-SFX behavior. An id that is not listed (or
 *     whose WAV failed to load) is likewise a silent no-op per play.
 *   - Per-sound playback rate: the engine repitches per trigger note
 *     (44100 * 2^(dnote/12)); the per-sound center notes are NOT decoded
 *     yet, so each WAV plays at its own stored rate, resampled to the
 *     device rate in the mixer. When the decomp pins the tone records, the
 *     registry grows a pitch column.
 *
 * POSITIONAL AUDIO (2026-06-11 decode of func_001FBD50/func_001FBF50 —
 * the engine's play_sound and its 3-D volume/pan solver):
 *
 *   play_sound(obj, id, flat2d, float radius) computes a STEREO GAIN
 *   PAIR once at trigger time (no per-frame re-pan) and submits
 *   func_001FB9F0(id, 0x1000, gainA, gainB) -> sequencer channel
 *   +0x48/+0x4C, multiplied per voice with the bank pan LUT D_00242630
 *   (pan 0 -> pair (0x80,0x00): the +0x48 chain carries the LEFT byte,
 *   pinning gainA = LEFT). Decoded math, normalized to 1.0 = 0x1000:
 *
 *     DISTANCE (listener = the PLAYER position, D_00810360):
 *       d = |src - player|            (full 3-D Euclidean; the flat2d
 *                                      flag zeroes Y — no translated
 *                                      call site passes it)
 *       d >= radius -> INAUDIBLE: the engine does not submit at all
 *                      (play_sound returns -1)
 *       vol = sin(pi/2 * (radius - d)/radius)     quarter-sine ease:
 *             full at d=0, ~linear fade near the rim, 0 at d=radius.
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
 *   em_sfx_play(id) (no position) keeps BOTH channels at 1.0 — exactly
 *   the engine's non-positional submit func_001FB9F0(id, 0x1000,
 *   0x1000, 0x1000) (the water/footing one-shots use this form) AND
 *   the limit of play_sound at the player (d=0 -> vol=1, k=0 -> t=+1):
 *   player-attached sounds (footsteps, weapon handling, shots, casing,
 *   hurt/death voice) are center/full BY THE ENGINE MATH, so their
 *   call sites stay on em_sfx_play.
 *
 * VOICE STEALING (2026-06-11 decode of the engine allocator
 * func_00117428 over the 48-voice table D_0027CCC0):
 *
 *   The engine DOES steal. Selection order per note-on:
 *     1. retrigger-reuse: an active voice with the same tone byte0 and
 *        bank handle restarts in place — but byte0 is 0 on EVERY
 *        shipped SFX tone (data-verified, 556/556 in the global bank),
 *        so this never fires for gameplay SFX;
 *     2. any free voice (state 0, not locked);
 *     3. steal: the first busy-but-released voice, else the OLDEST
 *        voice (minimum note-on serial, voice+0x0A sampled from the
 *        D_0027F740+0x34 counter) whose priority (+0x1E = tone byte1)
 *        <= the new tone's — byte1 is uniformly 10 across shipped SFX
 *        tones, so among gameplay sounds the gate always passes;
 *     4. else the note-on is silently dropped.
 *
 *   PORT POLICY (faithful-feasible): the engine's 48-voice budget over
 *   64 physical slots; when 48 voices are live the OLDEST live voice
 *   is killed (released-first cannot apply — port one-shots have no
 *   released state; priorities are engine-equal per the data above).
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
 * impact resolver would pick from (the hit record's +0x1A attr; the
 * decoded FX side keys 0x5A/0x5B/0x5C the same way), but NO surface ->
 * sound-id mapping is pinned in FINDINGS yet — the port plays 0x189
 * (the one live-observed variant) for every wall, flagged. */
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

/* LOCKED DOOR (FINDINGS "DOOR SCRIPTS DECODED" s23 — the locked-try
 * script D_0024DEC0's op-0x17 sub-0 record at its 60-frame mark; the
 * lock-fixture jiggle clip peaks right there). snd_0533 in every
 * exported area's bank (gen_sfx_registry). The locked "VO" that
 * follows is a TEXT-ONLY radio message in the engine (voice-cue -1,
 * 2026-06-11 decode) — no id to define; em_door plays the optional
 * scene.txt `lockedvo` registry id if one ever resolves. */
#define EM_SFX_DOOR_RATTLE  0x3F2u  /* locked-door handle rattle */

/* --- PLACEHOLDER ids (flagged — NOT engine-documented; chosen far above
 *     the observed bank id range so they can never collide).
 *     DOOR ids: the REAL door sounds are decoded (FINDINGS "DOOR SCRIPTS
 *     DECODED" s23) — a [front, back] halfword pair from D_0024DB80,
 *     indexed by the door link's high byte (door family ids
 *     0x3FB..0x40E; the office doors' links are 0x02xx -> pair[2] =
 *     0x3FD front / 0x3FE back). em_door consumes the pair through the
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

/* Load the id -> WAV registry (assets/sfx/sfx.txt) and preload every
 * listed PCM16 WAV. Missing registry = SFX disabled (silent no-ops
 * everywhere, zero behavior change); a bad line/WAV skips that entry with
 * a stderr note. Returns the number of sounds loaded. Game thread, once,
 * at boot. */
int em_sfx_init(void);

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

/* POSITIONAL one-shot — the native play_sound(obj, id, 0, radius): the
 * decoded func_001FBF50 stereo gain pair is computed ONCE here (engine
 * semantics: no per-frame re-pan) and baked into the voice. A source at
 * d >= radius is culled exactly like the engine (nothing submitted;
 * counted in em_sfx_culls). Game thread only. */
void em_sfx_play_at(unsigned id, const float pos[3], float radius);

/* The pure gain solver behind em_sfx_play_at, exposed for tests/tools:
 * writes the LEFT/RIGHT gains (1.0 = engine 0x1000; the far channel may
 * be NEGATIVE = the engine's behind-the-camera phase inversion) and
 * returns 1, or returns 0 when the source is out of range (engine
 * play_sound -1). Uses the em_sfx_listener state. */
int em_sfx_compute_gains(const float pos[3], float radius,
                         float *gain_l, float *gain_r);

/* AUDIO-THREAD mixer half: SUM all live one-shot voices into the
 * interleaved stereo buffer (which already holds the BGM frames),
 * resampling each voice from its WAV rate to `device_rate`. Called by
 * em_bgm's render callback only — real-time safe per the em_audio.h
 * contract (no locks/allocation/IO). A no-op while no voices are live. */
void em_sfx_mix(float *out_interleaved_stereo, int frames, int device_rate);

/* Free the preloaded samples. Game thread, AFTER em_bgm_shutdown() (the
 * device-teardown guarantee is what makes the sample memory safe to
 * free). Prints the mixed-voice counters if any one-shot ever played. */
void em_sfx_shutdown(void);

/* Introspection (EM_SFX_TEST / debugging; game thread). */
int  em_sfx_sound_count(void);    /* registry entries loaded            */
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
