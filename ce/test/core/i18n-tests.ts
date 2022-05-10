// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { i } from '@microsoft/vcpkg-ce/dist/i18n';
import { strict } from 'assert';

// sample test using decorators.
describe('i18n', () => {
  it('make sure tagged templates work like templates', () => {
    strict.equal(`this is ${100} a test `, i`this is ${100} a test `, 'strings should be the same');
    strict.equal(`${true}${false}this is ${100} a test ${undefined}`, i`${true}${false}this is ${100} a test ${undefined}`, 'strings should be the same');
  });

  /*
  it('try translations', () => {
    setLocale('de');
    const uri = 'hello://world';

    strict.equal(i`uri ${uri} has no scheme`, `uri ${uri} hat kein Schema`, 'Translation did not work correctly');
  });
  */
});
