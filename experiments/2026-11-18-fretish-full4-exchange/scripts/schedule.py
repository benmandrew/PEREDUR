"""The per-host schedule the driver runs (see PLAN.md, Hosts).

CPU allotments and times were set by peredur-extension-d5 for 2026-11-18;
every time is UTC. A lane takes no new item at or after stop_new.
"""

SCHEDULE = {
    "av2": {
        "build_cpus": "20-23",
        "hard_stop": "2026-11-18T11:59:30Z",
        "configs": "python3 scripts/gen_configs.py --schemes nsga2-apportion --weakening off --weights 0.1 0.2 0.7 --termination individuals --max-individuals 1000 --generations 500 --population-size 100 --pin-vintage --sweeps D --levels full4-exchange,full4-no-blame-exchange --parallel 2 --out-dir experiments/configs-fretish-full4-exchange",
        "blocks": [
            {
                "profile": "fretish-full4-exchange",
                "kind": "required",
                "seeds": [
                    0,
                    1,
                    2,
                    3,
                    4,
                    5,
                    6,
                    7,
                    8,
                    9,
                    10,
                    11,
                    12,
                    13,
                    14
                ]
            },
            {
                "profile": "fretish-full4-exchange-seeded",
                "kind": "required",
                "seeds": [
                    0,
                    1,
                    2
                ]
            },
            {
                "profile": "fretish-full4-no-blame-exchange-seeded",
                "kind": "optional",
                "seeds": [
                    0,
                    1,
                    2
                ]
            }
        ],
        "lanes": [
            {
                "cpu": 20,
                "start": "2026-11-18T06:00:00Z",
                "stop_new": "2026-11-18T08:30:00Z",
                "kinds": [
                    "required"
                ]
            },
            {
                "cpu": 21,
                "start": "2026-11-18T06:00:00Z",
                "stop_new": "2026-11-18T08:30:00Z",
                "kinds": [
                    "required"
                ]
            },
            {
                "cpu": 22,
                "start": "2026-11-18T06:00:00Z",
                "stop_new": "2026-11-18T08:30:00Z",
                "kinds": [
                    "required"
                ]
            },
            {
                "cpu": 23,
                "start": "2026-11-18T06:00:00Z",
                "stop_new": "2026-11-18T08:30:00Z",
                "kinds": [
                    "required"
                ]
            },
            {
                "cpu": 30,
                "start": "2026-11-18T09:00:00Z",
                "stop_new": "2026-11-18T11:59:00Z",
                "kinds": [
                    "required",
                    "optional"
                ]
            },
            {
                "cpu": 31,
                "start": "2026-11-18T09:00:00Z",
                "stop_new": "2026-11-18T11:59:00Z",
                "kinds": [
                    "required",
                    "optional"
                ]
            }
        ]
    },
    "av3": {
        "build_cpus": "28-31",
        "hard_stop": "2026-11-18T11:59:30Z",
        "configs": "python3 scripts/gen_configs.py --schemes nsga2-apportion --weakening off --weights 0.1 0.2 0.7 --termination individuals --max-individuals 1000 --generations 500 --population-size 100 --pin-vintage --sweeps D --levels full4-exchange,full4-no-blame-exchange --parallel 2 --out-dir experiments/configs-fretish-full4-exchange",
        "blocks": [
            {
                "profile": "fretish-full4-exchange",
                "kind": "required",
                "seeds": [
                    15,
                    16,
                    17,
                    18,
                    19,
                    20,
                    21,
                    22,
                    23,
                    24,
                    25,
                    26,
                    27,
                    28,
                    29
                ]
            },
            {
                "profile": "fretish-full4-exchange-seeded",
                "kind": "required",
                "seeds": [
                    3,
                    4
                ]
            },
            {
                "profile": "fretish-full4-no-blame-exchange-seeded",
                "kind": "optional",
                "seeds": [
                    3,
                    4
                ]
            }
        ],
        "lanes": [
            {
                "cpu": 28,
                "start": "2026-11-18T06:00:00Z",
                "stop_new": "2026-11-18T07:30:00Z",
                "kinds": [
                    "required"
                ]
            },
            {
                "cpu": 29,
                "start": "2026-11-18T06:00:00Z",
                "stop_new": "2026-11-18T07:30:00Z",
                "kinds": [
                    "required"
                ]
            },
            {
                "cpu": 30,
                "start": "2026-11-18T06:00:00Z",
                "stop_new": "2026-11-18T11:59:00Z",
                "kinds": [
                    "required",
                    "optional"
                ]
            },
            {
                "cpu": 31,
                "start": "2026-11-18T06:00:00Z",
                "stop_new": "2026-11-18T11:59:00Z",
                "kinds": [
                    "required",
                    "optional"
                ]
            }
        ]
    }
}
