// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { sanitizePath } from '../../artifacts/artifact';
import { strict } from 'assert';
import { describe, it } from 'mocha';

describe('sanitization of paths', () => {
  it('makes nice clean paths', () => {
    strict.equal(sanitizePath(''), '');
    strict.equal(sanitizePath('.'), '');
    strict.equal(sanitizePath('..'), '');
    strict.equal(sanitizePath('..../....'), '');
    strict.equal(sanitizePath('..../foo/....'), 'foo');
    strict.equal(sanitizePath('..../..foo/....'), '..foo');
    strict.equal(sanitizePath('.config'), '.config');
    strict.equal(sanitizePath('\\.config'), '.config');
    strict.equal(sanitizePath('..\\.config'), '.config');
    strict.equal(sanitizePath('/bar'), 'bar');
    strict.equal(sanitizePath('\\this\\is\\a//test/of//a\\path//..'), 'this/is/a/test/of/a/path');
  });
});
