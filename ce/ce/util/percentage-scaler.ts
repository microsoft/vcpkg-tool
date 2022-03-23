// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { strict } from 'assert';

export class PercentageScaler {
  private readonly scaledDomainMax : number;
  private readonly scaledPercentMax : number;

  private static clamp(test: number, min: number, max:number) : number {
    if (test < min) { return min; }
    if (test > max) { return max; }
    return test;
  }

  constructor(public readonly lowestDomain: number, public readonly highestDomain: number,
    public readonly lowestPercentage = 0, public readonly highestPercentage = 100) {
    strict.ok(lowestDomain <= highestDomain);
    strict.ok(lowestPercentage <= highestPercentage);
    this.scaledDomainMax = highestDomain - lowestDomain;
    this.scaledPercentMax = highestPercentage - lowestPercentage;
  }

  scalePosition(domain: number) : number {
    if (this.scaledDomainMax === 0 || this.scaledPercentMax === 0) {
      return this.highestPercentage;
    }
    const domainClamped = PercentageScaler.clamp(domain, this.lowestDomain, this.highestDomain);
    const domainScaled = domainClamped - this.lowestDomain;
    const domainProportion = domainScaled / this.scaledDomainMax;
    const partialPercent = this.scaledPercentMax * domainProportion;
    const percentage = this.lowestPercentage + partialPercent;
    return Math.round(percentage * 10) / 10;
  }
}
