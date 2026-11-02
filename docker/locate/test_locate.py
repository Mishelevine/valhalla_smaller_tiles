#!/usr/bin/env python3
"""Compare a running locate service with the standard Valhalla CLI on the same tiles."""

import argparse
import concurrent.futures
import copy
import json
from pathlib import Path
import subprocess
import unittest
import urllib.error
import urllib.request


class LocateServiceTest(unittest.TestCase):
    base_url = ""
    reference_binary = None
    reference_config = None
    request_template = json.loads(Path(__file__).with_name("request.json").read_text())

    def request(self, payload, action="locate"):
        body = payload.encode() if isinstance(payload, str) else json.dumps(payload).encode()
        request = urllib.request.Request(
            f"{self.base_url}/{action}", body, {"Content-Type": "application/json"}
        )
        try:
            response = urllib.request.urlopen(request, timeout=30)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            return response.status, json.load(response)

    def reference(self, payload):
        result = subprocess.run(
            [self.reference_binary, self.reference_config, "locate", json.dumps(payload)],
            capture_output=True,
            text=True,
            timeout=30,
            check=True,
        )
        return json.loads(result.stdout)

    def test_request_and_repeated_searches(self):
        expected = None
        for cutoff in (50, 1, 100, 50):
            payload = copy.deepcopy(self.request_template)
            payload["locations"][0]["search_cutoff"] = cutoff
            status, result = self.request(payload)
            self.assertEqual(status, 200)
            self.assertEqual(len(result), 1)
            self.assertEqual(result[0]["input_lat"], 55.751244)
            self.assertEqual(result[0]["input_lon"], 37.618423)
            if cutoff == 50:
                self.assertTrue(result[0]["edges"])
                if expected is None:
                    expected = result
                else:
                    self.assertEqual(result, expected)
            if self.reference_binary:
                self.assertEqual(result, self.reference(payload))

    def test_config_defaults(self):
        payload = copy.deepcopy(self.request_template)
        for key in ("radius", "minimum_reachability", "search_cutoff"):
            del payload["locations"][0][key]
        status, result = self.request(payload)
        self.assertEqual(status, 200)
        explicit = copy.deepcopy(self.request_template)
        explicit["locations"][0]["search_cutoff"] = 100
        explicit["locations"][0]["node_snap_tolerance"] = 0
        self.assertEqual(result, self.request(explicit)[1])
        if self.reference_binary:
            self.assertEqual(result, self.reference(payload))

    def test_locations_and_cutoffs(self):
        coordinates = (
            (55.751244, 37.618423),
            (55.75001, 37.62501),
            (50.4501, 30.5234),
            (56.8389, 60.6057),
            (43.1155, 131.8855),
            (0, 0),
        )
        for lat, lon in coordinates:
            for cutoff in (1, 10, 50, 100, 500):
                with self.subTest(lat=lat, lon=lon, cutoff=cutoff):
                    payload = copy.deepcopy(self.request_template)
                    payload["locations"][0].update(lat=lat, lon=lon, search_cutoff=cutoff)
                    status, result = self.request(payload)
                    self.assertEqual(status, 200)
                    if self.reference_binary:
                        self.assertEqual(result, self.reference(payload))
                    if lat == 0:
                        self.assertEqual(result[0]["edges"], [])

    def test_batch(self):
        payload = copy.deepcopy(self.request_template)
        second = dict(payload["locations"][0], lat=56.8389, lon=60.6057)
        payload["locations"].append(second)
        status, result = self.request(payload)
        self.assertEqual(status, 200)
        self.assertEqual(len(result), 2)
        if self.reference_binary:
            self.assertEqual(result, self.reference(payload))

    def test_concurrent_requests(self):
        expected = self.request(self.request_template)
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
            results = list(executor.map(self.request, [self.request_template] * 32))
        for result in results:
            self.assertEqual(result, expected)

    def test_other_endpoints_are_disabled(self):
        for action in (
            "route", "trace_route", "trace_attributes", "height", "sources_to_targets",
            "isochrone", "optimized_route", "expansion", "status", "tile", "unknown",
            "transit_available", "centroid",
        ):
            with self.subTest(action=action):
                payload = dict(self.request_template)
                if action == "expansion":
                    payload["action"] = "route"
                if action == "tile":
                    payload["tile"] = {"z": 0, "x": 0, "y": 0}
                status, result = self.request(payload, action)
                self.assertGreaterEqual(status, 400)
                self.assertEqual(result["error_code"], 106)

    def test_invalid_requests(self):
        for payload in ("{", {}, {"locations": [{"lat": 91, "lon": 37}], "costing": "auto"}):
            with self.subTest(payload=payload):
                status, result = self.request(payload)
                self.assertGreaterEqual(status, 400)
                self.assertIn("error_code", result)
        self.assertEqual(self.request(self.request_template)[0], 200)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url")
    parser.add_argument("--reference-binary")
    parser.add_argument("--reference-config")
    args = parser.parse_args()
    if bool(args.reference_binary) != bool(args.reference_config):
        parser.error("Provide both --reference-binary and --reference-config")
    LocateServiceTest.base_url = args.url.rstrip("/")
    LocateServiceTest.reference_binary = args.reference_binary
    LocateServiceTest.reference_config = args.reference_config
    unittest.main(argv=[__file__], verbosity=2)
