// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { isScalar, isSeq, Scalar, YAMLSeq } from 'yaml';
import { Primitive, Yaml, YAMLScalar, YAMLSequence } from './yaml-types';

/**
 * ScalarSequence is expressed as either a single scalar value or a sequence of scalar values.
 */

export /** @internal */ class ScalarSequence<TElement extends Primitive> extends Yaml<YAMLSeq<TElement> | Scalar<TElement>> {
  static override create(): YAMLScalar {
    return new YAMLScalar('');
  }

  get length(): number {
    if (this.node) {
      if (isSeq(this.node)) {
        return this.node.items.length;
      }
      if (isScalar(this.node)) {
        return 1;
      }
    }
    return 0;
  }

  has(value: TElement) {
    for (const each of this) {
      if (value === each) {
        return true;
      }
    }
    return false;
  }

  add(value: TElement) {
    if (value === undefined || value === null) {
      throw new Error('Cannot add undefined or null to a sequence');
    }

    // check if the value is already in the set
    if (this.has(value)) {
      return;
    }

    if (!this.node) {
      // if we don't have a node at the moment, we need to create one.
      this.assert(true);
      (<YAMLScalar><any>this.node).value = value;
      return;
    }

    if (isScalar(this.node)) {
      // this is currently a single item.
      // we need to convert it to a sequence
      const n = this.node;
      const seq = new YAMLSequence();
      seq.add(n);
      this.dispose(true);
      this.assert(true, seq);
      // fall thru to the sequnce add
    }

    if (isSeq(this.node)) {
      this.node.add(<any>(new Scalar(value)));
    }
  }

  delete(value: TElement) {
    if (isSeq(this.node)) {
      for (let i = 0; i < this.node.items.length; i++) {
        if (value === this.asPrimitive(this.node.items[i])) {
          this.node.items.splice(i, 1);
          return true;
        }
      }
    }
    if (isScalar(this.node) && this.node.value === value) {
      this.dispose(true);
      return true;
    }
    return false;
  }

  get(index: number): TElement | undefined {
    if (isSeq(this.node)) {
      return <TElement>this.node.items[index];
    }

    if (isScalar(this.node) && index === 0) {
      return <TElement>this.node.value;
    }

    return undefined;
  }

  *[Symbol.iterator](): Iterator<TElement> {
    if (isScalar(this.node)) {
      return yield <any>this.asPrimitive(this.node.value);
    }
    if (isSeq(this.node)) {
      for (const each of this.node.items.values()) {
        const v = this.asPrimitive(each);
        if (v) {
          yield <any>v;
        }
      }
    }
  }

  clear() {
    if (isSeq(this.node)) {
      // just make sure the collection is emptied first
      this.node.items.length = 0;
    }
    this.dispose(true);
  }
}
