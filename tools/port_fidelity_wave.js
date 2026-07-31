export const meta = {
  name: 'port-fidelity-wave',
  description: 'Replace guessed port behaviour with the decomp\'s recovered C, one subsystem per agent',
  phases: [{ title: 'Fidelity', detail: 'per-subsystem: read decomp C, correct the port' }],
}

const SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    file: { type: 'string' },
    fixes: {
      type: 'array',
      items: {
        type: 'object', additionalProperties: false,
        properties: {
          ps2_func: { type: 'string' },
          what_was_wrong: { type: 'string' },
          what_the_decomp_says: { type: 'string' },
          change_made: { type: 'string' },
          confidence: { type: 'string' },
        },
        required: ['ps2_func', 'what_was_wrong', 'what_the_decomp_says', 'change_made', 'confidence'],
      },
    },
    left_alone: { type: 'array', items: { type: 'string' } },
    builds: { type: 'boolean' },
    notes: { type: 'string' },
  },
  required: ['file', 'fixes', 'left_alone', 'builds', 'notes'],
}

const PORT = '/Users/abe/Documents/Extermination.nosync/extermination-port'
const DECOMP = '/Users/abe/Documents/Extermination.nosync/Extermination'

const PROMPT = (id, target) => `You are correcting the Extermination NATIVE PORT against the matching decompilation.

THE SITUATION. The port at ${PORT} was written BEFORE the decompilation recovered these
functions. Its architecture is faithful, but many individual behaviours were filled in from
observation and are marked "placeholder", "approximate", "invented" or "guess" in comments.
The sibling decomp at ${DECOMP} has since recovered the very same functions as C that
compiles to the original bytes. Your job: replace guesses with what the original actually does.

YOUR FILE: ${target.file}
FLAGGED PS2 FUNCTIONS (all have recovered C in the decomp):
${target.refs.map(r => `  ${r.name}  [${r.status}]  referenced at ${r.file} line(s) ${r.lines}`).join('\n')}

METHOD, per flagged function:
1. Read ${DECOMP}/src/<func>.c — that IS the original's behaviour.
   * A file whose first line is NOT "// NEARMISS" is BYTE-MATCHED: it compiles to the
     original machine code. Treat it as ground truth, full stop.
   * A "// NEARMISS" file is body-correct readable C that differs only in codegen. Its
     LOGIC is still authoritative; only its instruction scheduling is not byte-exact.
   * Follow callees you need (grep ${DECOMP}/src for their names) and read the decomp's
     docs/FINDINGS.md for the subsystem's decoded structure.
2. Find where ${target.file} stands in for that function, read the surrounding comment
   (the port documents which PS2 function each part mirrors), and compare behaviour:
   constants, thresholds, frame counts, ordering, state transitions, edge cases.
3. Where the port GUESSED and the decomp KNOWS, correct the port. Keep the port's own
   idioms and naming — you are fixing behaviour, not transliterating MIPS. The port is
   native C for modern platforms; it must stay readable, not become a decomp copy.
4. Update the comment to say the value/behaviour is now DECODED from <func>, and drop the
   stale "placeholder"/"approximate" wording you just made untrue.

HARD RULES:
* Do NOT paste PS2 disassembly into the port. The decomp's C is our own code and may inform
  you; raw disassembly must never land in this repo.
* Do NOT add third-party dependencies. Clean-room, platform APIs only.
* Do NOT restructure the file or move functions between files — another pass owns layout.
  Change behaviour only.
* If the decomp does NOT actually settle a question, LEAVE THE PORT ALONE and list it in
  left_alone with why. An invented "fix" is worse than an honest placeholder. Never claim
  the decomp says something it does not.
* Verify with: cd ${PORT} && make 2>&1 | tail -20
  It MUST compile before you finish. Only your own file should change.

Return the schema. In what_the_decomp_says, cite the concrete evidence (the decomp function
and the specific constant/branch you read). confidence = "certain" only when the byte-matched
C states it outright; use "likely" when you inferred across functions.`

const cands = (typeof args === 'string' ? JSON.parse(args) : args)

const results = await parallel(cands.map(t => () =>
  agent(PROMPT(t.id, t), { label: `fidelity:${t.file.split('/').pop()}`, phase: 'Fidelity', schema: SCHEMA })
))

const ok = results.filter(Boolean)
log(`subsystems processed: ${ok.length}/${cands.length}`)
for (const r of ok) log(`  ${r.file}: ${r.fixes.length} fix(es), ${r.left_alone.length} left alone, builds=${r.builds}`)
return { results: ok }
