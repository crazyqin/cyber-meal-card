"""Offline collector protocol and failure tests; no credentials or provider calls."""
from copy import deepcopy
import importlib
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import MagicMock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "collector"))
import collect
from common import CollectionError, atomic_json, scrub, window
from plugins.chatgpt import normalize
from plugins import teleagent, tokenhub


class CollectorTests(unittest.TestCase):
    def setUp(self):
        self.config = {"accounts": [{"id": "main", "provider": "custom", "name": "main", "api_key_ref": "API"}]}
        self.adapter = SimpleNamespace(LABEL="Custom", collect=MagicMock(return_value={"identity": "stable-account",
            "windows": [window(25, 300, 1792195200)], "details": {"business": 42, "api_key": "secret-value"}}))

    def report(self, config=None, previous=None):
        with patch.object(collect, "collector_id", return_value="pc-one"), patch.object(collect.importlib, "import_module", return_value=self.adapter):
            return collect.collect_report(config or self.config, {"API": "secret-value"}, previous)

    def test_only_enabled_plugins_loaded_and_unknown_fields_preserved(self):
        self.config["accounts"].append({"id": "disabled", "provider": "never_import_me", "enabled": False})
        report, details = self.report()
        self.assertEqual(len(report["accounts"]), 1)
        self.assertEqual(report["accounts"][0]["account_id"], "stable-account")
        self.assertEqual(details["accounts"][0]["source_data"], {"business": 42})
        self.assertNotIn("_binding", json.dumps(collect.public_report(report)))
        self.assertEqual(self.adapter.collect.call_count, 1)

    def test_failure_preserves_source_time_and_rotation_invalidates_cache(self):
        first, _ = self.report()
        self.adapter.collect.side_effect = RuntimeError("Bearer secret-value")
        failed, detail = self.report(previous=first)
        row = failed["accounts"][0]
        self.assertEqual(row["collected_at"], first["accounts"][0]["collected_at"])
        self.assertEqual(row["windows"], first["accounts"][0]["windows"])
        self.assertEqual(row["status"], "error")
        self.assertNotIn("secret-value", json.dumps(detail))
        with patch.dict(os.environ, {"API": "rotated-key"}):
            changed, _ = self.report(previous=first)
        self.assertIsNone(changed["accounts"][0]["collected_at"])
        self.assertNotIn("windows", changed["accounts"][0])

    def test_bad_summary_becomes_error_without_rejecting_other_accounts(self):
        self.config['accounts'].append({'id':'other','provider':'custom','name':'other'})
        self.adapter.collect.side_effect = [
            {'identity':'bad','windows':[{'window_minutes':300,'used_percent':25,'remaining_percent':999}]},
            {'identity':'good','windows':[window(25,300,1792195200)]}]
        report, _ = self.report()
        self.assertEqual([r['status'] for r in report['accounts']], ['error','ok'])
        self.assertEqual(report['accounts'][0]['error_code'], 'invalid_summary')
        self.assertNotIn('windows',report['accounts'][0])

    def test_same_remote_account_emitted_once(self):
        self.config['accounts'].append({'id':'alias','provider':'custom','name':'alias'})
        report, detail = self.report()
        self.assertEqual(len(report['accounts']),1)
        self.assertEqual(len(detail['accounts']),1)

    def test_tokenhub_retries_transient_response_once_but_not_auth(self):
        with patch.object(tokenhub,'_collect',side_effect=[ValueError('synthetic'),{'identity':'good'}]) as adapter, \
             patch.object(tokenhub.time,'sleep'):
            self.assertEqual(tokenhub.collect({},{}),{'identity':'good'})
        self.assertEqual(adapter.call_count,2)
        for code in ('auth','sandbox_unavailable','credentials_missing'):
            with patch.object(tokenhub,'_collect',side_effect=CollectionError(code)) as adapter, \
                 self.assertRaises(CollectionError):
                tokenhub.collect({},{})
            self.assertEqual(adapter.call_count,1)

    def test_invalid_summary_preserves_valid_previous_data(self):
        previous, _ = self.report()
        self.adapter.collect.return_value = {'points':{'balance':'x','spent_today':'0','added_today':'0'}}
        report, _ = self.report(previous=previous)
        self.assertEqual(report['accounts'][0]['status'],'error')
        self.assertEqual(report['accounts'][0]['windows'],previous['accounts'][0]['windows'])

    def test_unavailable_reset_count_and_detail_limit_semantics(self):
        empty = normalize({"rateLimitResetCredits": None})
        self.assertIsNone(empty["reset_credits"]["available_count"])
        self.assertFalse(empty["reset_credits"]["details_known"])
        value = normalize({"rateLimitResetCredits": {"availableCount": 9, "credits": [
            {"status": "available", "grantedAt": 100, "expiresAt": 400},
            {"status": "available", "grantedAt": 200, "expiresAt": 500},
            {"status": "redeemed", "grantedAt": 300, "expiresAt": 50}]}})
        reset = value["reset_credits"]
        self.assertEqual(reset["available_count"], 9)  # Not len(capped detail rows).
        self.assertEqual(reset["latest_expires_at_unix"], 500)
        self.assertEqual(reset["next_expires_at_unix"], 400)
        self.assertIsNone(normalize({"rateLimitResetCredits":{"availableCount":True}})["reset_credits"]["available_count"])

    def test_all_limit_buckets_and_usage_retained(self):
        value = normalize({"rateLimitsByLimitId": {"codex": {"planType":"plus", "primary": {
            "usedPercent":125, "windowDurationMins":300, "resetsAt":1792195200}}, "other": {"primary":None}}})
        self.assertEqual(value["windows"][0]["remaining_percent"], 0)
        self.assertEqual(set(value["limits_by_id"]), {"codex", "other"})

    def test_credentials_and_pii_redacted_recursive_without_losing_token_usage(self):
        value = scrub({"operationToken":"private", "user_token":"private", "access_token":"private", "user_id":"private", "credits":{"count":3},
                       "nested":[{"email":"private", "text":"Bearer secret-value", "lifetimeTokens":123}]}, ["secret-value"])
        self.assertEqual(value["credits"]["count"], 3)
        self.assertNotIn("secret-value", json.dumps(value))
        self.assertNotIn("private", json.dumps(value))
        self.assertEqual(value["nested"][0]["lifetimeTokens"], 123)

    def test_teleagent_matches_portal_points_and_today_all_clients(self):
        responses = [{"code":0, "data":{"pointsBalance":"12345.67", "groups":[{"balance":45.67,"effectiveEnd":"2026-10-17"}]}},
                     {"code":200, "data":{"todayPointsUsed":12.34,"todayPointsAdded":100,"todayTaskCount":4,"todayCallCount":4}}]
        with patch.object(teleagent,"read_json",return_value={"operation_token":"example-private-token"}), \
             patch.object(teleagent,"get_json",side_effect=responses) as request:
            result=teleagent.collect({"id":"main","auth_file":"unused"},{})
        self.assertEqual(result["points"],{"balance":"12345.67","spent_today":"12.34","added_today":"100"})
        self.assertEqual(result["details"]["usage"]["todayCallCount"],4)
        urls=[c.args[0] for c in request.call_args_list]
        self.assertEqual(urls[0],teleagent.BASE+"/points/detail")
        from urllib.parse import urlparse,parse_qs
        query=parse_qs(urlparse(urls[1]).query)
        self.assertEqual(set(query),{"startDate","endDate"})
        self.assertEqual(query["startDate"],query["endDate"])
        self.assertNotIn("example-private-token",json.dumps(result))
        self.adapter.collect.return_value=result
        report,_=self.report()
        self.assertEqual(report["accounts"][0]["points"],result["points"])

    def test_teleagent_rejects_auth_errors_missing_values_and_invalid_numbers(self):
        for value in (True,None,"NaN","Infinity","-1",{},"1e1000000"):
            with self.subTest(value=value),self.assertRaises(CollectionError): teleagent.amount(value)
        self.assertEqual(teleagent.amount(0),"0")
        self.assertEqual(teleagent.amount("0.00001"),"0.00001")
        for responses,code in (([{"code":401,"message":"private"}],"auth"),
                               ([{"code":200,"data":{"pointsBalance":10}},{"code":200,"data":{"todayPointsAdded":0}}],"invalid_response")):
            with patch.object(teleagent,"read_json",return_value={"operation_token":"example-private-token"}), \
                 patch.object(teleagent,"get_json",side_effect=responses),self.assertRaises(CollectionError) as caught:
                teleagent.collect({"id":"main","auth_file":"unused"},{})
            self.assertEqual(caught.exception.code,code)

    def test_copied_state_gets_new_machine_id(self):
        with tempfile.TemporaryDirectory() as folder:
            config = {"collector_id":"auto", "_state":Path(folder)}
            with patch("collect.machine_identity", return_value="machine-one"):
                first = collect.collector_id(config)
                self.assertEqual(collect.collector_id(config), first)
            with patch("collect.machine_identity", return_value="machine-two"):
                second = collect.collector_id(config)
                self.assertNotEqual(first, second)

    def test_relative_paths_duplicate_ids_and_disabled_unknown_provider(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/"config.json"
            config={"schema_version":2, "interval_seconds":300,"accounts":[
                {"id":"main","provider":"uninstalled","enabled":False,"auth_file":"auth.json"}]}
            atomic_json(path,config)
            loaded=collect.load_config(path)
            self.assertEqual(Path(loaded["accounts"][0]["auth_file"]), (Path(folder)/"auth.json").resolve())
            config["accounts"] *= 2
            atomic_json(path,config)
            with self.assertRaises(ValueError): collect.load_config(path)

    def test_uploads_to_independent_machine_paths_and_never_sets_acl(self):
        config={"cos":{"bucket":"example-1250000000","region":"ap-shanghai","prefix":"usage",
                "secret_id_ref":"COS_ID","secret_key_ref":"COS_KEY"}}
        sdk=MagicMock(); http=MagicMock()
        with patch.dict(sys.modules,{"qcloud_cos":sdk,"requests":http}):
            for pc in ("pc-a","pc-b"):
                report={"schema_version":2,"collector_id":pc,"accounts":[]}
                collect.publish(config,{"COS_ID":"example-id","COS_KEY":"example-key"},report,report)
        calls=sdk.CosS3Client.return_value.put_object.call_args_list
        self.assertEqual([c.kwargs["Key"] for c in calls], ["usage/pc-a/details.json","usage/pc-a/latest.json",
            "usage/pc-b/details.json","usage/pc-b/latest.json"])
        for call in calls:
            self.assertNotIn("ACL",call.kwargs)
            self.assertEqual(call.kwargs["CacheControl"],"no-store")

    def test_cos_environment_session_token_and_file_fallback(self):
        config={'cos':{'bucket':'example-1250000000','region':'ap-shanghai','prefix':'usage',
                'secret_id_ref':'COS_ID','secret_key_ref':'COS_KEY','token_ref':'COS_SESSION'}}
        sdk=MagicMock(); http=MagicMock()
        saved={'COS_ID':'example-id','COS_KEY':'example-key','COS_SESSION':'example-file-token'}
        report={'collector_id':'pc-a','accounts':[]}
        with patch.dict(sys.modules,{'qcloud_cos':sdk,'requests':http}),patch.dict(os.environ,{'COS_SESSION':'example-env-token'}):
            collect.publish(config,saved,report,report)
        self.assertEqual(sdk.CosConfig.call_args.kwargs['Token'],'example-env-token')
        with patch.dict(sys.modules,{'qcloud_cos':sdk,'requests':http}),patch.dict(os.environ,{},clear=True):
            collect.publish(config,saved,report,report)
        self.assertEqual(sdk.CosConfig.call_args.kwargs['Token'],'example-file-token')

    def test_teleagent_passes_explicit_proxy_to_both_requests(self):
        responses=[{'code':0,'data':{'pointsBalance':1}},
                   {'code':0,'data':{'todayPointsUsed':0,'todayPointsAdded':0}}]
        with patch.object(teleagent,'read_json',return_value={'operation_token':'example-token'}), \
             patch.object(teleagent,'get_json',side_effect=responses) as request:
            teleagent.collect({'id':'main','auth_file':'unused','proxy':'http://127.0.0.1:8888'}, {})
        self.assertEqual([c.kwargs['proxy'] for c in request.call_args_list],['http://127.0.0.1:8888']*2)

    def test_broken_secrets_retries_and_keeps_previous_snapshot(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            config={'_state':root,'_secrets':root/'secrets.json'}
            config['_secrets'].write_text('{')
            atomic_json(root/'latest.json',{'marker':'last-good'})
            with patch('collect.time.sleep',side_effect=KeyboardInterrupt),self.assertRaises(KeyboardInterrupt):
                collect.run(config)
            self.assertEqual(json.loads((root/'latest.json').read_text()),{'marker':'last-good'})
            self.assertEqual(json.loads((root/'status.json').read_text())['error_code'],'secrets_unreadable')
            self.assertEqual(collect.run(config,once=True),1)

    def test_failed_publication_keeps_local_collection(self):
        with tempfile.TemporaryDirectory() as folder:
            config={"_state":Path(folder),"_secrets":Path(folder)/"secrets.json"}
            atomic_json(config["_secrets"],{})
            report={"schema_version":2,"collector_id":"pc-a","accounts":[]}
            with patch("collect.collect_report",return_value=(report,report)), patch("collect.publish",side_effect=RuntimeError("private")):
                self.assertEqual(collect.run(config,once=True),1)
            self.assertEqual(json.loads((Path(folder)/"latest.json").read_text()),report)
            self.assertEqual(json.loads((Path(folder)/"status.json").read_text())["upload"],"error")

    def test_interactive_configuration_keeps_secrets_hidden(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/"config.json"
            config={"schema_version":2,"interval_seconds":300,"secrets_file":"secrets.json",
                    "cos":{"bucket":"example-1250000000","region":"ap-shanghai","prefix":"usage",
                           "secret_id_ref":"COS_ID","secret_key_ref":"COS_KEY"},
                    "accounts":[{"id":"main","provider":"deepseek","enabled":True,"api_key_ref":"API"}]}
            atomic_json(path,config)
            atomic_json(Path(folder)/"secrets.json",{"API":"do-not-echo","COS_ID":"do-not-echo-id","COS_KEY":"do-not-echo-key"})
            with patch("builtins.input",side_effect=["n","","",""]), patch("getpass.getpass",return_value=""), patch("builtins.print") as output:
                collect.configure(path)
            self.assertFalse(json.loads(path.read_text())["accounts"][0]["enabled"])
            self.assertNotIn("do-not-echo",str(output.call_args_list))


if __name__ == "__main__":
    unittest.main()
