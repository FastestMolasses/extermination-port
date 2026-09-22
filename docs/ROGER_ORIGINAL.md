# Original AREA11 Roger

`em_roger.c` translates the initialized controller at runtime address `008237E0`, its three story branches, the selector-0/class-10 candidate predicate, and the four-corner automatic trigger. It does not replace Roger with an examine prompt. The native scene must bind source owner `0082A500` to the real controller's status, class flags and armed byte before publishing him in the common interaction list.

The initial placement is model `47`, selector 0, class flags `AA`, subtype 1, position `(331.7, 290, 192.5)`, and descriptor `{10,20}`. Position and angles belong to the exported canonical scene record; they must not be copied from a later snapshot. Ordinary animation starts at clip 8/frame 0 in bank `4A`, with 21 source bones and the exported trailing identity palette slot. `em_roger_assets_load` requires the original mesh, all nine raw clips, all four programs, and the raw trigger polygon.

## Source and checks

The exporter compares the whole original model from `chunk15/f18_id94.bin +35000` with the saved actor's model at `01877740`. The bank header at `chunk15/f12_id44.bin +10E000` matches the resolved live bank-4A pointer. All nine clips have 21 nodes, self-loop sentinel -1, blend 0 and no events. Their durations are 240, 300, 240, 360, 300, 60, 60, 180, and 180 source frames.

`make test-roger-reference` executes original instruction words for 6,912 controller cases, 900 candidate cases and 324 polygon cases. The controller comparison checks worker order and arguments plus changed state. Its external services are deliberately injected. The polygon worker is compared separately, including edges and vertices through the original SDK zero-vector path. The predicate checks the score write after planar-radius success, before height and facing rejection.

The pose check decodes 6,258 raw keys through the original channel decoders. At both saved original states, clip 8/frame 28 and clip 8/frame 148, all 525 sampled node floats and all 63 channel cursors compare exactly. The native local hierarchy multiplied by the **captured original owner matrix** differs from the saved world matrices by at most `0.0001220703125`. This does not establish byte-identical native owner placement, matrix multiplication, or hardware arithmetic. All captured static node adjustments in this slice are identity.

`make test-roger-assets` runs the actual local resource loader and repeated pose loops under AddressSanitizer/UndefinedBehaviorSanitizer. It checks blend 20 at half speed using the original pre-step `remaining <= 1` completion rule: the 39th callback reinitializes the target and the 40th advances it by half a source frame. Missing resource loads leave cleared ownership.

Reproduce local exports with `../Extermination/.venv/bin/python tools/export_roger_resources.py`. Original bytes and generated assets remain ignored under `assets/scene_snow/roger`; evidence reports remain under `build/roger_reference`. No binary resources or disassembly belong in source control.

## Controller behavior and host services

Before story progress, suppression value 1 exits without face, pose, publication or draw. The alternate branch repeatedly runs `00828990`. The ordinary branch tests the original XZ polygon at `0082AB80`, starts `008283D0` on entry, stops streams, and advances the current animation at rate 1 on that same callback. Later script callbacks advance at rate 0.5. Completion restores bank 4A/clip 8, sets progress bit 0, resumes music and requests the original fade. Publication precedes its forced rendered byte, so forced visibility must not force interaction-list membership.

After progress and before bit 80, armed bit 4 starts `00828810`. Completion returns to clip 8 with blend 20 and clears the arm. Progress bit 80 starts departure `00828A10`; completion removes the original actor group and marks lifecycle 3. Cleanup and free happen on the next controller callback. Cleanup `001BA540` releases the extended face via `001CA770` and clears its auxiliary light through `001D8BF0(0)`; it is not a script-stop service.

`001BA580` is the face coordinator. Roger uses activity byte `008106D5`; values 1/2 set talking on/off and clear that activity byte before `001D0C70` advances blink/mouth state. Roger's initialized auxiliary kind is 0, so the `001DA6A0` call exits immediately. This is not a head-tracking behavior. The existing face kernel can be reused, but global RNG order and original allocation contents remain separate fidelity boundaries.

The automatic encounter uses bank **96**, whose runtime header resolves inside `chunk15/f12_id44.bin +41000`, with camera/player/Roger clips 0/1/2. This differs from the opening bank 98. Exporting four real script programs does not implement their camera, player, message, audio, fade, story and actor workers. Missing required workers must fail explicitly, never return fabricated completion. This checkpoint supplies controller/resources/proofs; live mesh, face, shared-script services and canonical publication bindings are a subsequent integration step.
