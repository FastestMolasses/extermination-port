# Shared AREA11 Use dispatch

The original player scans the previous frame's published actor list once,
at its action-specific Use edge. Pickups, the panel, elevator, Roger and
the distant door compete in that same list. Neither the legacy door-first
scan nor a separate nearest-pickup scan establishes the original winner.

`em_interaction_scene_scan_checked` refreshes each canonical owner's live
status, class flags and armed byte, preserving the original published order.
It uses the verified original scan on temporary armed bytes. Valid predicate
results preserve score writes even on rejected candidates, first-winner
ties and immediate result2. A native predicate result-1 represents a missing
required worker, not an original eligible actor; that failure commits no
arm or scan state. The caller must stop the affected dispatch on failure.
Global gates still leave the prior score untouched and evaluate no owners.

The scene regression exercises failure at every position in the actual
11-owner list, including after an earlier eligible candidate, and checks
that no owner is armed. It also checks immediate-result short circuiting,
first-published ties, live status refresh and the inhibited path.

`em_door_candidate` implements the original `00183EF0` selector0/class5
branch. Subtypes3 and15 measure distance from a doorway center shifted by
the original sine/cosine functions. The front/back test still measures its
bearing from the owner's origin. Radius success publishes the planar score
before the height or facing test, even if either later rejects. The final
facing window is pi/4; action2D rejects this class before the door branch.

`tools/test_door_candidate_reference.py` executes the original predicate and
SDK bodies for 3,725 cases, including 75 using the actual AREA11 placement
and descriptor. Return values and the shared score bits agree throughout.
This establishes the predicate. The distant door's live publication,
controller and transit script remain separate integration requirements;
the helper does not validate the legacy native door implementation.
