// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import * as assert from 'assert';
import { linq } from '../../util/linq';

const anArray = ['A', 'B', 'C', 'D', 'E'];

describe('Linq', () => {
  it('distinct', async () => {

    const items = ['one', 'two', 'two', 'three'];
    const distinctArray = linq.values(items).distinct().toArray();
    assert.deepStrictEqual(distinctArray, ['one', 'two', 'three']);

    const dic = {
      happy: 'hello',
      sad: 'hello',
      more: 'name',
      maybe: 'foo',
    };

    const distinctDictionaryValues = linq.values(dic).distinct().toArray();
    assert.deepStrictEqual(distinctDictionaryValues, ['hello', 'name', 'foo']);
  });

  it('iterating through collections', async () => {
    // items are items.
    assert.strictEqual([...linq.values(anArray)].join(','), anArray.join(','));
    assert.strictEqual(linq.values(anArray).count(), 5);
  });
});
