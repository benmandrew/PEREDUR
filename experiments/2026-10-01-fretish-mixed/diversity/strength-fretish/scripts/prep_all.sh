#!/bin/bash
cd ~/fret-pooled
for s in takeoff fsm-combined fsm-timing rad-core-10 rad-core-68 rad-core-1-18 rad-core-45 rad-core-12-18 rad-core-17-18 rad-core-55 rad-core-65 rad-core-61 rad-core-33 lpc-mini-core1 liquid-mixer lpc-full-core1 valu3s-uc6 fsm-lmcps; do nice python3 pooled.py prep $s; done
echo PREP_ALL_DONE
