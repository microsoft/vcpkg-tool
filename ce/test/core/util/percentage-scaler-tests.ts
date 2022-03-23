// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { PercentageScaler } from '@microsoft/vcpkg-ce/dist/util/percentage-scaler';
import { strict, throws } from 'assert';

describe('PercentageScaler', () => {
  it('ScalesPercentagesTo100', () => {
    const uut = new PercentageScaler(0, 1000);
    for (let idx = 0; idx < 1000; ++idx) {
      strict.equal(uut.scalePosition(idx), idx / 10);
    }
  });

  it('ScalesPercentagesToDifferentRanges', () => {
    const uut = new PercentageScaler(0, 1000, 10, 20);
    for (let idx = 0; idx < 10; ++idx) {
      strict.equal(uut.scalePosition(idx * 100), 10 + idx);
    }
  });

  it('ScalesZeroRangesAsMax', () => {
    const uut = new PercentageScaler(0, 0, 0, 200);
    strict.equal(uut.scalePosition(0), 200);
  });

  it('ScalesUniquePercentageRangesAsThatPercent', () => {
    const uut = new PercentageScaler(0, 100, 200, 200);
    for (let idx = -1; idx < 102; ++idx) {
      strict.equal(uut.scalePosition(idx), 200);
    }
  });

  it('ClampsDomain', () => {
    const uut = new PercentageScaler(0, 10);
    strict.equal(uut.scalePosition(Number.MIN_VALUE), 0);
    strict.equal(uut.scalePosition(-100), 0);
    strict.equal(uut.scalePosition(0), 0);
    strict.equal(uut.scalePosition(1), 10);
    strict.equal(uut.scalePosition(10), 100);
    strict.equal(uut.scalePosition(11), 100);
    strict.equal(uut.scalePosition(Number.MAX_VALUE), 100);
  });

  it('ValidatesParameters', () => {
    throws(() => new PercentageScaler(0, -1)); // transposed domain range
    new PercentageScaler(0, 0); // OK
    new PercentageScaler(0, 0, 0, 0); // OK
    throws(() => new PercentageScaler(0, 0, 0, -1)); // percentage range is reversed
  });
});
