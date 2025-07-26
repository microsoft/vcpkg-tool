// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { fail, strict } from 'assert';

export function strictSequenceEqual(a: Iterable<any>|undefined, e: Iterable<any>|undefined, message?: string) {
  if (a && e) {
    strict.deepEqual([...a], [...e], message);
  } else if (!a || !e) {
    fail(message);
  }
}
