// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { Range, SemVer } from 'semver';
import { VersionReference as IVersionReference } from '../interfaces/metadata/version-reference';
import { Yaml, YAMLScalar } from '../yaml/yaml-types';


// nuget-semver parser doesn't have a ts typings package
// eslint-disable-next-line @typescript-eslint/no-var-requires
const parseRange: any = require('@snyk/nuget-semver/lib/range-parser');

export class VersionReference extends Yaml<YAMLScalar> implements IVersionReference {
  get raw(): string | undefined {
    return this.node?.value || undefined;
  }

  set raw(value: string | undefined) {
    if (value === undefined) {
      this.dispose(true);
    } else {
      this.node = new YAMLScalar(value);
    }
  }

  static override create(): YAMLScalar {
    return new YAMLScalar('');
  }

  private split(): [Range, SemVer | undefined] {

    const v = this.raw;
    if (v) {

      const [, a, b] = /(.+)\s+([\d\\.]+)/.exec(v) || [];

      if (/\[|\]|\(|\)/.exec(v)) {
        // looks like a nuget version range.
        try {
          const range = parseRange(a || v);
          let str = '';
          if (range._components[0].minOperator) {
            str = `${range._components[0].minOperator} ${range._components[0].minOperand}`;
          }
          if (range._components[0].maxOperator) {
            str = `${str} ${range._components[0].maxOperator} ${range._components[0].maxOperand}`;
          }
          const newRange = new Range(str);
          newRange.raw = a || v;

          if (b) {
            const ver = new SemVer(b, true);
            return [newRange, ver];
          }

          return [newRange, undefined];

        } catch (E) {
          // ignore and fall thru
        }
      }

      if (a) {
        // we have at least a range going on here.
        try {
          const range = new Range(a, true);
          const ver = new SemVer(b, true);
          return [range, ver];
        } catch (E) {
          // ignore and fall thru
        }
      }
      // the range or version didn't resolve correctly.
      // must be a range alone.
      return [new Range(v, true), undefined];
    }
    return [new Range('*', true), undefined];
  }
  get range() {
    return this.split()[0];
  }
  set range(ver: Range) {
    this.raw = `${ver.raw} ${this.resolved?.raw || ''}`.trim();
  }

  get resolved() {
    return this.split()[1];
  }
  set resolved(ver: SemVer | undefined) {
    this.raw = `${this.range.raw} ${ver?.raw || ''}`.trim();
  }
}