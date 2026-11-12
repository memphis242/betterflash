# Local diagnostic logging

**Settings > Logging** controls log level and diagnostic verbosity independently.
The defaults are **Warning** and **High**. Level filters events by severity;
verbosity controls how much context each retained event contains. Changes apply
immediately and persist across launches.

Logs are private files in the collection data directory's `logs` subdirectory.
Use **Open log directory** to find them. An isolated `--data-dir` has its own
logs and settings. Logs are not collection records and are not included in sync,
collection exports, or deck checkpoints.

Files rotate on UTC date changes and when a segment reaches 4 MiB. The logger
keeps at most 16 segments and seven days of records, for a maximum of 64 MiB.
The oldest segments are removed automatically. Individual records have a 1 MiB
limit; any truncation is identified explicitly rather than silently omitted.
The log directory is restricted to its owner, as are log files. Multiple app
instances serialize writes and rotation with a directory lock.

High verbosity includes card content, prompts, model replies, retrieved search
content, and tool exchanges associated with model errors or misuse, including
misuse that the app corrects successfully. These logs may contain private
collection information. Credentials and authorization fields are redacted;
sealed model reasoning and signatures are omitted. An unreadable non-JSON provider
HTTP body is omitted because its sensitive fields cannot be separated reliably;
the byte count, parser error, and outbound conversation remain available.
Inspect files before sharing
them. Normal verbosity retains metadata; Low retains the event without its
full diagnostic payload. Lowering verbosity does not erase existing logs.

A failed model operation records a bounded conversation journal with an
operation identifier, model request/response sequence, failure code, and cause.
The application's error details identify the diagnostic file when available.
Provider transport failures also retain their specific status and bounded
response evidence. Ordinary Qt warnings and application storage errors use the
same logging system. Debug level can record additional successful model activity.

Correctness and completeness checks, and Ask AI, can tell a model why its tool
request was rejected and let it correct the request within fixed limits. Unknown
tools are never executed. Error details show the rejected call, turn, allowed
tools, recovery attempts, and reason the app could not proceed. Search and other
retrieval quotas remain independent of correction attempts; invalid requests do
not consume a Tavily search credit.

A logging failure is itself displayed with the path and available filesystem
cause. Fix permissions or storage before relying on diagnostic capture. A
failure that occurred before logging was available cannot be reconstructed from
these files.

Automated tests are paused by project policy. Compilation and review do not
establish behavior across filesystem failures, clock changes, or live model
providers; those remain validation limits until exercised.
