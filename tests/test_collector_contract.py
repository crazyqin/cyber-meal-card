"""Feed Python collector output to the production firmware C parser, offline."""
import json
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'collector'))
import collect
from common import window

config = {'accounts': [{'id':'one','name':'One','provider':'custom'},
                       {'id':'two','name':'Two','provider':'custom'}]}
good = {'identity':'good','windows':[window(25,300,1893456000)]}
cases = [([good, {'identity':'bad','windows':[{'remaining_percent':999}]}],2),
         ([good, good],1),
         ([good, {'identity':'points','points':{'balance':'10','spent_today':'2','added_today':'0'}}],2),
         ([good, {'identity':'bad','balances':[{'currency':'CNY','total':'9'*25}]}],2),
         ([good, {'identity':'bad','windows':'wrong-type'}],2)]
for responses, expected in cases:
    iterator = iter(responses)
    module = SimpleNamespace(LABEL='Custom',collect=lambda *_:next(iterator))
    with patch.object(collect, 'collector_id', return_value='contract-pc'), \
         patch.object(collect.importlib,'import_module',return_value=module):
        report, _ = collect.collect_report(config,{})
    result = subprocess.run([sys.argv[1]],input=json.dumps(collect.public_report(report)),
                            capture_output=True,text=True,check=True)
    assert int(result.stdout) == expected
print('Collector -> production C contract: PASS (5 cases)')
