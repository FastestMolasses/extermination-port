export const meta = {
  name: 'port-claim-audit',
  description: 'Verify or refute every DECODED/VERIFIED claim in the port against the decomp\'s recovered C',
  phases: [{ title: 'Audit', detail: 'one subsystem per agent: check each claim, fix the code' }],
}

const SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    subsystem: { type: 'string' },
    verdicts: {
      type: 'array',
      items: {
        type: 'object', additionalProperties: false,
        properties: {
          where: { type: 'string' },
          claim_was: { type: 'string' },
          verdict: { type: 'string' },
          evidence: { type: 'string' },
          code_changed: { type: 'boolean' },
          change: { type: 'string' },
        },
        required: ['where', 'claim_was', 'verdict', 'evidence', 'code_changed', 'change'],
      },
    },
    claims_seen: { type: 'number' },
    claims_total: { type: 'number' },
    builds: { type: 'boolean' },
    notes: { type: 'string' },
  },
  required: ['subsystem', 'verdicts', 'claims_seen', 'claims_total', 'builds', 'notes'],
}

const PORT = '/Users/abe/Documents/Extermination.nosync/extermination-port'
const DECOMP = '/Users/abe/Documents/Extermination.nosync/Extermination'

const PROMPT = (t) => `You are auditing the Extermination NATIVE PORT for FAITHFULNESS to the original game.

THE PROBLEM. The port's comments assert things — "DECODED", "VERIFIED", "CONFIRMED",
"LIVE-VERIFIED". Most were written by EARLIER, WEAKER sessions, BEFORE the decompilation
recovered the functions they cite. In the sibling decomp those same sessions produced seven
"compiler walls" that turned out to be defects in our own inputs, and three "proven
impossible" verdicts that were simply false. **Assume nothing in these comments is true
until you check it.** The user's report: the port plays and behaves incorrectly in many
places. Your job is to find and fix those places.

YOUR SUBSYSTEM: ${t.files.join(', ')}
CLAIMS TO CHECK (${t.claims.length} of them, from tools/audit_claims.py):
${t.claims.slice(0, 60).map(c => `  ${c.where}  [${c.bucket}]  cites: ${c.cites || '-'}\n     "${c.text}"`).join('\n')}
${t.claims.length > 60 ? `  ... and ${t.claims.length - 60} more; enumerate them yourself with:\n     cd ${PORT} && python3 tools/audit_claims.py --file ${t.files[0].split('/').pop()}` : ''}

GROUND TRUTH. ${DECOMP}/src/<func>.c is our recovered C.
  * First line NOT "// NEARMISS" => BYTE-MATCHED: it compiles to the original machine
    code. It is authoritative, full stop.
  * "// NEARMISS" => body-correct readable C; its LOGIC is authoritative, only its
    instruction scheduling is not byte-exact.
  * "INCLUDE_ASM" => still undecompiled. It CANNOT support a "decoded" claim.
Follow callees (grep ${DECOMP}/src for the name). ${DECOMP}/docs/FINDINGS.md has decoded
formats. Do NOT copy PS2 disassembly into the port — the decomp's C may inform you, raw
disassembly must never land here.

PER CLAIM, decide one verdict:
  CONFIRMED  — the recovered C says exactly what the comment says. Tighten the citation to
               name the byte-matched function so the next reader can re-check it fast.
  CORRECTED  — the recovered C says something DIFFERENT. **Fix the CODE**, then fix the
               comment. This is the point of the exercise; a wrong constant, threshold,
               frame count, ordering or state transition is a behaviour bug.
  DOWNGRADED — the claim cites a still-undecompiled stub, or cites nothing. It was never
               decoded. Reword it honestly ("observed", "port stand-in") so nobody
               downstream trusts it as source-derived. Leave behaviour alone unless you
               have real evidence.
  UNRESOLVED — you could not settle it. Say why. This is a fine answer.

RULES:
* NEVER invent. If the decomp does not settle a question, UNRESOLVED or DOWNGRADED — not a
  plausible-sounding guess. A wrong "fix" is worse than an honest unknown.
* NEVER claim the decomp says something it does not. Quote the concrete constant/branch you
  read, and name the file you read it in.
* Prefer CHECKABLE claims first — they are where real behaviour bugs hide.
* Keep the port's own idioms and naming. You are correcting behaviour and provenance, not
  transliterating MIPS. This is native C for modern platforms.
* Do NOT move functions between files or restructure — a separate pass owns layout.
* ONLY touch: ${t.files.join(', ')}. Another agent owns every other file.
* Verify before finishing: cd ${PORT} && make 2>&1 | tail -20   (it MUST compile)

Return the schema. claims_seen = how many you actually examined; claims_total = ${t.claims.length}.
Be honest if you did not get through them all — partial coverage truthfully reported is far
more useful than a false "all done".`

const cands = (typeof args === 'string' ? JSON.parse(args) : args)

const results = await parallel(cands.map(t => () =>
  agent(PROMPT(t), { label: `audit:${t.id}`, phase: 'Audit', schema: SCHEMA, effort: 'high' })
))

const ok = results.filter(Boolean)
let corrected = 0, confirmed = 0, downgraded = 0, unresolved = 0, seen = 0
for (const r of ok) {
  seen += r.claims_seen || 0
  for (const v of r.verdicts || []) {
    if (v.verdict === 'CORRECTED') corrected++
    else if (v.verdict === 'CONFIRMED') confirmed++
    else if (v.verdict === 'DOWNGRADED') downgraded++
    else unresolved++
  }
}
log(`subsystems: ${ok.length}/${cands.length}   claims examined: ${seen}`)
log(`CORRECTED ${corrected}  CONFIRMED ${confirmed}  DOWNGRADED ${downgraded}  UNRESOLVED ${unresolved}`)
for (const r of ok) log(`  ${r.subsystem}: ${r.claims_seen}/${r.claims_total} seen, builds=${r.builds}`)
return { results: ok }
