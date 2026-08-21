"""AWS Lambda entry point for the mcd demo API. Routed behind an API Gateway HTTP API
(Lambda proxy integration, payload format 2.0) -- see docs/design/07-aws-demo.md sec.3.2
and infra/lib/mcd-stack.ts for the routes this maps to.

CORS is handled entirely by the API Gateway HTTP API's own CORS configuration (CDK), not
here -- one source of truth for allowed origins/methods, not duplicated in Python too.
"""

import json

from cfa_invariants import cfa_invariant_table
from request import RequestError, handle_request

_HEADERS = {"Content-Type": "application/json"}


def _response(status_code, body):
    return {"statusCode": status_code, "headers": _HEADERS, "body": json.dumps(body)}


def lambda_handler(event, _context):
    route = event.get("routeKey", "")

    try:
        if route == "GET /cfa-invariants":
            return _response(200, {"invariants": cfa_invariant_table()})

        if route == "POST /price":
            raw_body = event.get("body") or "{}"
            try:
                req = json.loads(raw_body)
            except json.JSONDecodeError as e:
                return _response(400, {"error": f"malformed JSON body: {e}"})
            try:
                result = handle_request(req)
            except RequestError as e:
                return _response(400, {"error": str(e)})
            return _response(200, result)

        return _response(404, {"error": f"no route for '{route}'"})
    except Exception as e:  # noqa: BLE001 -- last-resort guard: never let an unhandled
        # exception surface as an opaque 502 from API Gateway. Every *expected* failure
        # mode (bad JSON, bad request fields) is already caught above with a clean 400;
        # this is only a safety net for something genuinely unanticipated, still reported
        # as a clean JSON error rather than a stack trace or a silent 500.
        return _response(500, {"error": f"internal error: {e}"})
