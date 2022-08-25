// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { replaceCurlyBraces } from '@microsoft/vcpkg-ce/dist/util/curly-replacements';
import { strict } from 'assert';

describe('replaceCurlyBraces', () => {
  const replacements = new Map<string, string>();
  replacements.set('exists', 'exists-replacement');
  replacements.set('another', 'some other replacement text');

  it('DoesNotTouchLiterals', () => {
    strict.equal(replaceCurlyBraces('some literal text', replacements), 'some literal text');
  });

  it('DoesVariableReplacements', () => {
    strict.equal(replaceCurlyBraces('some {exists} text', replacements), 'some exists-replacement text');
  });

  it('DoesMultipleVariableReplacements', () => {
    strict.equal(replaceCurlyBraces('some {exists} {another} text', replacements), 'some exists-replacement some other replacement text text');
  });

  it('HandlesLeadingEscapes', () => {
    strict.equal(replaceCurlyBraces('some {{exists} text', replacements), 'some {exists} text');
  });

  it('ConsidersTerminalCurlyAsPartOfVariable', () => {
    strict.equal(replaceCurlyBraces('some {exists}} text', replacements), 'some exists-replacement} text');
  });

  it('AllowsDoubleEscapes', () => {
    strict.equal(replaceCurlyBraces('some {{exists}} text', replacements), 'some {exists} text');
  });

  it('PassesThroughUnmatchedCurlies', () => {
    strict.equal(replaceCurlyBraces('these are }{ not matched', replacements), 'these are }{ not matched');
  });

  it('ThrowsForBadValues', () => {
    strict.throws(() => {
      replaceCurlyBraces('some {nonexistent} text', replacements);
    }, new Error('Could not find a value for {nonexistent} in \'some {nonexistent} text\'. To write the literal value, use \'{{nonexistent}}\' instead.'));
  });
});
