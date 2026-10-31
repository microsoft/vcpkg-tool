// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

// vcpkg writes one request to stdin, with paths relative to directory:
//   {"operation": "restore" | "save", "directory": "<absolute path>", "entries": [{"key": "...", "path": "..."}]}
// restore fails only if a lookup or download failed, not on a miss; vcpkg picks up whatever arrived.

import * as cache from '@actions/cache';
import { rmSync } from 'node:fs';
import { text } from 'node:stream/consumers';

const RESTORE_CONCURRENCY = 8;
const RESTORE_FAILURE_LIMIT = 3;
const LOOKUP_ATTEMPTS = 12;
const LOOKUP_INTERVAL_MS = 5000;

async function forEachLimited(items, limit, stop, work) {
  let next = 0;
  const workers = Array.from({ length: Math.min(limit, items.length) }, async () => {
    while (next < items.length && !stop()) {
      await work(items[next++]);
    }
  });
  await Promise.all(workers);
}

const request = JSON.parse(await text(process.stdin));
const { operation, directory, entries } = request;
if (typeof directory !== 'string' || !Array.isArray(entries)) {
  console.error('error: the request needs a directory and an array of entries');
  process.exit(2);
}

// The runner sets ACTIONS_CACHE_SERVICE_V2 only for JavaScript actions, and github.com no longer serves v1.
// @actions/cache still uses v1 on GitHub Enterprise Server.
if (process.env.ACTIONS_RESULTS_URL && !process.env.ACTIONS_CACHE_SERVICE_V2) {
  process.env.ACTIONS_CACHE_SERVICE_V2 = 'true';
}

// @actions/cache stores and extracts paths relative to GITHUB_WORKSPACE. It resolves them through symbolic links
// and passes the directory to tar unquoted.
process.chdir(directory);
process.env.GITHUB_WORKSPACE = process.cwd();
if (/\s/.test(process.cwd())) {
  console.error(`error: @actions/cache cannot work in a directory whose path contains spaces: ${process.cwd()}`);
  process.exit(2);
}

// @actions/cache reports some outcomes only in its log.
let log = '';
const writeStdout = process.stdout.write.bind(process.stdout);
process.stdout.write = (chunk, ...rest) => {
  log += String(chunk);
  return writeStdout(chunk, ...rest);
};
const loggedFailure = (since = 0) => /^::(warning|error)::/m.test(log.slice(since));
const loggedFailureCount = () => log.match(/^::(warning|error)::/gm)?.length ?? 0;

async function waitForEntry(path, key, since) {
  for (let attempt = 0; attempt < LOOKUP_ATTEMPTS && !loggedFailure(since); ++attempt) {
    if ((await cache.restoreCache([path], key, [], { lookupOnly: true })) === key) {
      return true;
    }
    await new Promise((resolve) => setTimeout(resolve, LOOKUP_INTERVAL_MS));
  }
  return false;
}

const canRead = !['none', 'write-only'].includes((process.env.ACTIONS_CACHE_MODE ?? '').trim().toLowerCase());

let failures = 0;
let completed = 0;
// Every failed restore logs a failure or throws, so this means that at least one lookup got an answer.
const serviceAnswered = () => completed > failures + loggedFailureCount();
// A failed request takes several seconds of retries. Until the service has answered a lookup, the first failure
// suggests that it is down.
const tooManyFailures = () => failures + loggedFailureCount() >= (serviceAnswered() ? RESTORE_FAILURE_LIMIT : 1);
switch (operation) {
  case 'restore':
    await forEachLimited(entries, RESTORE_CONCURRENCY, tooManyFailures, async ({ key, path }) => {
      try {
        if ((await cache.restoreCache([path], key)) === key) {
          return;
        }
      } catch (error) {
        console.error(`warning: failed to restore ${key}: ${error.message}`);
        ++failures;
      } finally {
        ++completed;
      }

      // A failed extraction can leave part of the archive behind, which vcpkg would take for a hit.
      rmSync(path, { force: true });
    });

    // restoreCache() returns as for a miss when a request fails, and only logs the failure.
    if (loggedFailure()) {
      ++failures;
    }
    break;
  case 'save':
    for (const { key, path } of entries) {
      const logStart = log.length;
      try {
        // On GitHub Enterprise Server, saveCache() returns the reserved ID even if the upload failed.
        if ((await cache.saveCache([path], key)) !== -1 && !loggedFailure(logStart)) {
          continue;
        }

        // Jobs that build the same package at once race to save it, and the loser waits for the winner's upload.
        // Without read access, the 409 Conflict that the v2 service returns is the only sign that the entry exists.
        if (
          log.includes('Unable to reserve cache', logStart) &&
          (canRead ? await waitForEntry(path, key, log.length) : log.includes('Failed request: (409)', logStart))
        ) {
          console.log(`${key} is already stored`);
          continue;
        }
      } catch (error) {
        console.error(`warning: failed to save ${key}: ${error.message}`);
      }

      ++failures;
    }
    break;
  default:
    console.error(`error: unknown operation '${operation}'`);
    process.exit(2);
}

process.exit(failures === 0 ? 0 : 1);
