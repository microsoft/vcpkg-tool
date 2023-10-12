// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { replaceCurlyBraces } from '../../../util/curly-replacements';
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

  it('ThrowsForLeadingOnlyEscapes', () => {
    strict.throws(() => {
      replaceCurlyBraces('some {{exists} text', replacements);
    }, new Error('Found a mismatched } in \'some {{exists} text\'. For a literal }, use }} instead.'));
  });

  it('ConsidersTerminalCurlyAsPartOfVariable', () => {
    strict.throws(() => {
      replaceCurlyBraces('some {exists}} text', replacements);
    }, new Error('Found a mismatched } in \'some {exists}} text\'. For a literal }, use }} instead.'));
  });

  it('AllowsDoubleEscapes', () => {
    strict.equal(replaceCurlyBraces('some {{{exists} text', replacements), 'some {exists-replacement text');
    strict.equal(replaceCurlyBraces('some {exists}}} text', replacements), 'some exists-replacement} text');
    strict.equal(replaceCurlyBraces('some {{exists}} text', replacements), 'some {exists} text');
    strict.equal(replaceCurlyBraces('some {{{exists}}} text', replacements), 'some {exists-replacement} text');
    strict.equal(replaceCurlyBraces('some {{{{{exists}}} text', replacements), 'some {{exists-replacement} text');
  });

  it('ThrowsForUnmatchedCurlies', () => {
    strict.throws(() => {
      replaceCurlyBraces('these are }{ not matched', replacements);
    }, new Error('Found a mismatched } in \'these are }{ not matched\'. For a literal }, use }} instead.'));
  });

  it('ThrowsForBadValues', () => {
    strict.throws(() => {
      replaceCurlyBraces('some {nonexistent} text', replacements);
    }, new Error('Could not find a value for {nonexistent} in \'some {nonexistent} text\'. To write the literal value, use \'{{nonexistent}}\' instead.'));
  });

  it('ThrowsForMismatchedBeginCurlies', () => {
    strict.throws(() => {
      replaceCurlyBraces('some {nonexistent', replacements);
    }, new Error('Found a mismatched { in \'some {nonexistent\'. For a literal {, use {{ instead.'));
  });

  it('ThrowsForMismatchedEndCurlies', () => {
    strict.throws(() => {
      replaceCurlyBraces('some }nonexistent', replacements);
    }, new Error('Found a mismatched } in \'some }nonexistent\'. For a literal }, use }} instead.'));
  });
});
