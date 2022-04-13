// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { isScalar } from 'yaml';
import { BaseMap } from './BaseMap';
import { Primitive } from './yaml-types';


export /** @internal */ class ScalarMap<TElement extends Primitive = Primitive> extends BaseMap {
  get(key: string): TElement | undefined {
    return <TElement>this.getValue(key);
  }

  set(key: string, value: TElement) {
    this.assert(true);
    this.node.set(key, value);
  }


  add(key: string): TElement {
    this.assert(true);
    this.node.set(key, '');
    return <TElement>this.getValue(key);
  }

  *[Symbol.iterator](): Iterator<[string, TElement]> {
    if (this.node) {
      for (const { key, value } of this.node.items) {
        const v = isScalar(value) ? this.asPrimitive(value) : undefined;
        if (v) {
          yield [key, value];
        }
      }
    }
  }
}
