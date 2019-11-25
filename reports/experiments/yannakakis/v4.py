#! /usr/bin/env python

import math
import os
import platform

from suites import OPTIMAL_SUITE, EXCLUDED_DOMAINS, CYCLIC_SCHEMAS

from lab.environments import LocalEnvironment, BaselSlurmEnvironment
from lab.experiment import Experiment
from lab.reports import Attribute, geometric_mean

from downward import suites
from downward.reports.absolute import AbsoluteReport
from downward.reports.scatter import ScatterPlotReport

from common_setup import Configuration
from exp_utils import *

"""
Test memory and time to instantiate initial state.
"""

# Create custom report class with suitable info and error attributes.
class BaseReport(AbsoluteReport):
    INFO_ATTRIBUTES = []
    ERROR_ATTRIBUTES = [
        'domain', 'problem', 'algorithm', 'unexplained_errors', 'error', 'node']

NODE = platform.node()
REMOTE = NODE.endswith(".scicore.unibas.ch") or NODE.endswith(".cluster.bc2.ch")
BENCHMARKS_DIR = os.environ["DOWNWARD_BENCHMARKS"]
POWER_LIFTED_DIR = os.environ["POWER_LIFTED_SRC"]

if REMOTE:
    SUITE = OPTIMAL_SUITE
    ENV = BaselSlurmEnvironment(
        partition='infai_2',
        memory_per_cpu="6G",
        extra_options='#SBATCH --cpus-per-task=3',
        setup='',
        export=["PATH", "DOWNWARD_BENCHMARKS", "POWER_LIFTED_DIR"])
else:
    SUITE = ['gripper:prob01.pddl',
             'miconic:s1-0.pddl']
    ENV = LocalEnvironment(processes=4)

TIME_LIMIT = 1800
MEMORY_LIMIT = 16384

ATTRIBUTES=[Attribute('closed_list_size', functions=geometric_mean),
            'cost',
            'coverage',
            'generated',
            'initial_state_size',
            Attribute('peak_memory', functions=geometric_mean),
            'search_time',
            'expansions',
            'time_cyclic',
            'visited']

# Create a new experiment.
exp = Experiment(environment=ENV)

# Add custom parser for Power Lifted.
exp.add_parser('power-lifted-parser.py')

CONFIGS = [Configuration('blind-full-reducer', ['naive', 'blind', 'full_reducer']),
           Configuration('blind-ordered_join', ['naive', 'blind', 'ordered_join']),
           Configuration('blind-join', ['naive', 'blind', 'join']),
           Configuration('blind-yannakakis', ['naive', 'blind', 'yannakakis']),
           Configuration('blind-random-1', ['naive', 'blind', 'random_join']),
           Configuration('goalcount-full-reducer', ['gbfs', 'goalcount', 'full_reducer']),
           Configuration('goalcount-ordered_join', ['gbfs', 'goalcount', 'ordered_join']),
           Configuration('goalcount-join', ['gbfs', 'goalcount', 'join']),
           Configuration('goalcount-yannakakis', ['gbfs', 'goalcount', 'yannakakis']),
           Configuration('goalcount-random-1', ['gbfs', 'goalcount', 'random_join'])]

# Create one run for each instance and each configuration
for config in CONFIGS:
    for task in suites.build_suite(BENCHMARKS_DIR, SUITE):
        if task.domain in EXCLUDED_DOMAINS:
            continue
        run = exp.add_run()
        run.add_resource('domain', task.domain_file, symlink=True)
        run.add_resource('problem', task.problem_file, symlink=True)
        run.add_command(
            'run-translator',
            [POWER_LIFTED_DIR+'/builds/release/translator/translate.py',
             task.domain_file, task.problem_file],
            time_limit=TIME_LIMIT,
            memory_limit=MEMORY_LIMIT)
        run.add_command(
            'run-search',
            [POWER_LIFTED_DIR+'/builds/release/search/search', 'output.lifted'] +
            config.arguments,
            time_limit=TIME_LIMIT,
            memory_limit=MEMORY_LIMIT)
        run.set_property('domain', task.domain)
        run.set_property('problem', task.problem)
        run.set_property('algorithm', config.name)
        run.set_property('id', [config.name, task.domain, task.problem])

        # Add step that writes experiment files to disk.
exp.add_step('build', exp.build)

# Add step that executes all runs.
exp.add_step('start', exp.start_runs)

# Add step that collects properties from run directories and
# writes them to *-eval/properties.
exp.add_fetcher(name='fetch')

# Make a report.
exp.add_report(
    BaseReport(attributes=ATTRIBUTES),
    outfile='report.html')

def filter_non_cyclic(run):
    if 'time_cyclic' not in run or math.isnan(run['time_cyclic']):
        run['time_cyclic'] = 0.0
    return run

def is_cyclic(run):
    cyclic = ['agricola-opt18-strips',
    'barman-opt11-strips',
    'barman-opt14-strips',
    'caldera-split-opt18-adl',
    'data-network-opt18-strips',
    'elevators-opt08-strips',
    'elevators-opt11-strips',
    'freecell',
    'hiking-opt14-strips',
    'nomystery-opt11-strips',
    'organic-synthesis-opt18-strips',
    'parcprinter-08-strips',
    'parcprinter-opt11-strips',
    'pipesworld-notankage',
    'pipesworld-tankage',
    'rovers',
    'satellite',
    'settlers-opt18-adl',
    'spider-opt18-strips',
    'termes-opt18-strips',
    'tetris-opt14-strips',
    'tidybot-opt11-strips',
    'tidybot-opt14-strips',
    'tpp']
    if run['domain'] in cyclic:
        if run['coverage'] == 1 and run['search_time'] >= 1.0:
            return True
        return False
    return False

# Make a report.
exp.add_report(
    BaseReport(attributes=ATTRIBUTES,
    filter_algorithm=['blind-full-reducer', 'blind-join', 'blind-ordered_join'],
    filter=[is_cyclic, filter_non_cyclic]),
    outfile='compare-bfs.html')


for attr in ['peak_memory', 'search_time']:
    for alg in ['blind-join', 'blind-ordered_join']:
        exp.add_report(
            ScatterPlotReport(
                attributes=[attr],
                filter_algorithm=[alg, "blind-full-reducer"],
                filter=[discriminate_org_synt],
                get_category=domain_as_category,
                format='tex'
            ),
            outfile='{}-{}-vs-{}'.format(attr, alg, "blind-full-reducer") + '.tex'
        )

for attr in ['visited', 'search_time']:
    for alg in ['blind-yannakakis']:
        exp.add_report(
            ScatterPlotReport(
                attributes=[attr],
                filter_algorithm=[alg, "blind-full-reducer"],
                filter=[discriminate_org_synt],
                get_category=domain_as_category,
                format='tex'
            ),
            outfile='{}-{}-vs-{}'.format(attr, alg, "blind-full-reducer") + '.tex'
        )

# Parse the commandline and run the specified steps.
exp.run_steps()