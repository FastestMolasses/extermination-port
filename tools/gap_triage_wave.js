export const meta = {
  name: 'port-gap-triage',
  description: 'Triage byte-matched engine functions the port never implements — which are real behaviour gaps?',
  phases: [{ title: 'Triage', detail: 'read each recovered function, classify, and cost the port' }],
}

const SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    findings: {
      type: 'array',
      items: {
        type: 'object', additionalProperties: false,
        properties: {
          func: { type: 'string' },
          what_it_does: { type: 'string' },
          klass: { type: 'string' },
          port_has_equivalent: { type: 'string' },
          player_visible: { type: 'string' },
          recommendation: { type: 'string' },
        },
        required: ['func', 'what_it_does', 'klass', 'port_has_equivalent',
                   'player_visible', 'recommendation'],
      },
    },
    notes: { type: 'string' },
  },
  required: ['findings', 'notes'],
}

const PORT = '/Users/abe/Documents/Extermination.nosync/extermination-port'
const DECOMP = '/Users/abe/Documents/Extermination.nosync/Extermination'

const PROMPT = (t) => `Triaging the Extermination native port's BEHAVIOUR GAPS.

Five audit rounds have verified what the port already implements. This is the other
direction: engine functions we have recovered as BYTE-MATCHED C, which are called by
functions the port DOES model, but which the port never mentions. Some are real missing
behaviour; many are engine plumbing a native port has no reason to mirror.

YOUR FUNCTIONS (each is byte-matched C in ${DECOMP}/src/<name>.c):
${t.funcs.map(f => `  ${f.func}   (called by ${f.callers} port-known function(s))`).join('\n')}

FOR EACH, read the recovered C and classify:

klass — one of:
  GAMEPLAY   affects simulation the player experiences (damage, AI, movement, triggers)
  VISUAL     affects what is drawn (effects, HUD elements, lighting, sprites)
  AUDIO      sound/music behaviour
  PLUMBING   engine-internal: memory ops, matrix/vector math, list management, DMA/GS
             packet building, allocator. A native port re-implements these its own way
             and should NOT mirror them function-for-function.
  UNCLEAR    you could not tell

port_has_equivalent — search ${PORT}/src for an equivalent by BEHAVIOUR, not by name
  (the port uses em_* naming and its own idioms). Answer "yes: <symbol>", "partial:
  <symbol> — <what differs>", or "no".

player_visible — would a player notice if this is missing? Answer concretely
  ("no — internal list bookkeeping", "yes — the hit spark never spawns"). Be honest;
  most PLUMBING is invisible.

recommendation — "port it" / "already covered" / "skip (plumbing)" / "needs more decode".
  Recommend porting ONLY where there is real player-visible behaviour the port lacks.

RULES:
* Read the actual recovered C. Do not infer from the name — several of these have
  misleading splat-derived names.
* Do NOT write any code this round. This is triage; a later pass implements.
* Do NOT copy PS2 disassembly into the port repo.
* Be conservative on GAMEPLAY: if the port already achieves the same observable result
  by different means, that is "already covered", not a gap.

Return the schema.`

const cands = (typeof args === 'string' ? JSON.parse(args) : args)

const results = await parallel(cands.map(t => () =>
  agent(PROMPT(t), { label: `triage:${t.id}`, phase: 'Triage', schema: SCHEMA, effort: 'high' })
))

const ok = results.filter(Boolean)
const all = ok.flatMap(r => r.findings || [])
const byClass = {}
for (const f of all) byClass[f.klass] = (byClass[f.klass] || 0) + 1
log(`triaged ${all.length} functions: ${JSON.stringify(byClass)}`)
const port = all.filter(f => /port it/i.test(f.recommendation))
log(`recommended to port: ${port.length}`)
for (const f of port) log(`  ${f.func}: ${f.what_it_does.slice(0, 90)}`)
return { findings: all }
