[简体中文](tokenhub-research.zh_CN.md) · **English**

# Zhiyun TokenHub integration investigation

Initial investigation on 2026-10-15 used the signed-in
[cost center](https://token.telecomjs.com/finance/cost) in Chrome. Subsequent
HTTP-only work in `D:/aipassport` successfully computed the dynamic protection,
issued the signed query and decrypted business JSON without launching Chrome.
The user has now accepted a computer/server collector and COS delivery to the
device, with the existing ClinePass, DeepSeek and Codex features retained.

## Verified interface

The public frontend uses Axios with base URL `/api` and a `Satoken` session
header. Its cost-card function issues a read query:

```http
POST https://token.telecomjs.com/api/heimdall-product/cost-center/customer/card-with-time-range
Content-Type: application/json
Satoken: <existing login session>
```

The application-level request before browser protection is:

```json
{"adminView": false, "startDate": "YYYY-MM-DD", "endDate": "YYYY-MM-DD"}
```

The cost page defaults to today's date range. Its decoded card data contains
these numeric fields; they are not interchangeable:

| Field | Meaning observed in the page/client |
| --- | --- |
| `allocateAmount` | Assigned amount in CNY; the page renders null as unlimited |
| `totalConsumption` | Cumulative consumption |
| `monthConsumption` | Current month's consumption in CNY |
| `monthRecharge` | Current month's recharge |
| `balance`, `platformGiftBalance`, `totalBalance` | Wallet balance fields; they can be zero for an allocation-based account |
| `timeRangeConsumption` | Consumption in the selected date range |

For this account role, the UI displays assigned amount and monthly consumption.
Do not present a zero wallet field as its remaining assigned quota. Nor should
`allocateAmount - monthConsumption` be labelled as an authoritative balance:
the allocation period/reset semantics have not been established.

The public client has a `refreshToken` function with an empty implementation;
there is no verified unattended session-refresh flow. No session lifetime is
assumed. The inference API key in the official model documentation is different
from the `Satoken` web session; its ability to query billing is unverified.

## Direct-client result

Browser network observation showed protected request/response bodies instead
of the frontend's ordinary JSON. The page itself successfully decoded and
displayed the card. Two normal HTTPS POST requests using the valid `Satoken`,
JSON body and the application's user agent received:

```text
HTTP 412
Content-Type: text/html; charset=utf-8
Browser-verification page, not JSON
```

The second request also supplied the observed conventional Referer/Origin and
gray-routing header. Neither request used dynamic protection headers or copied
challenge cookies. These results establish a failure of this tested direct
client path; they do not prove that every official integration path is unavailable.

The [official documentation](https://token.telecomjs.com/document) lists model
inference endpoints on `aigw.telecomjs.com`. Searching it for balance returned
model-error explanations, without a documented balance-query API. No model
inference, payment, key creation or account-setting change was performed.
Credentials and actual account amounts are excluded from this record; the
Satoken is not embedded in firmware. The existing private build retains its
previous COS credentials and must not be shared publicly with its debug files.

## Implemented deployment

Python/curl_cffi handles every network request. A local Node.js environment
executes the downloaded protection code, computes cookies and request wrappers,
then decodes the response within the same JavaScript session. It has returned
`returncode: 0` from the real business endpoint. Node does not perform networking,
and Chrome is not a runtime dependency. This environment is not embedded in the
ESP32-C3.

The collector uploads a sanitized report to the existing private COS prefix's
`tokenhub.json`; the device signs a GET using its existing COS configuration.
The report contains no web-session token or raw API response. The firmware
validates the schema, currency, decimal amounts and original source timestamp,
then displays estimated remaining allocation, monthly spending and today's
spending. Errors preserve the previous valid values; stale source timestamps
remain stale after a successful download. Details and controls are in
[the application guide](subscription-viewer.md#telecom-tokenhub-through-cos).

The earlier HTTP 412 results above describe ordinary unwrapped requests; they
are superseded by the working protected HTTP client. Satoken renewal and the
provider's allocation reset semantics remain unverified.
