# 逐日神弓

Item 1702 is a level-20 Scout greatbow (58–70 ranged damage), with equipped
passive 790. It drops from Shadow Trial boss and clear-reward pools at a base
5% per equipment roll for level-20 characters. Level-20 sets keep a base 60%
share of rolls. Item Find applies through the existing loot calculation.

Normal bow attacks snapshot equipped passive effects when fired. On a
successful hit, the target's pre-hit current HP contributes 5% additional
damage after armor, resistances, criticals and miss modifiers. Ordinary shields
still absorb damage. A piercing arrow rolls its 10% meteor chance once, on its
first successful hit. Double-shot arrows count as separate attacks.

Meteor power 791 uses an existing fireball sprite with a falling animation.
It becomes dangerous at frame 6 and hits only the original target if the target
is still in the impact area. Its damage is 5% of the target's current HP at
impact, using the engine's magic (ment) damage defenses. It cannot critically
strike, overhit, or trigger the bow's basic-attack effects. Scatter and the
channeling skill do not trigger these effects either.

`python3 tests/run_village_quests.py` exercises real equipment, projectile
creation, true-damage armor bypass, impact timing/current HP, magic resistance,
target locking, skill exclusion, one meteor roll per arrow and unequipping.
It also samples both loot pools at the equipment tier boundaries.
