// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { fail, strict } from 'assert';

// I like my collections to be easier to compare.
declare module 'assert' {
  namespace assert {
    function sequenceEqual(actual: Iterable<any> | undefined, expected: Iterable<any>, message?: string | Error): void;
    function throws(block: () => any, message?: string | Error): void;
  }
}

(<any>strict).sequenceEqual = (a: Iterable<any>, e: Iterable<any>, message: string) => {
  a && e ? strict.deepEqual([...a], [...e], message) : fail(message);
};
