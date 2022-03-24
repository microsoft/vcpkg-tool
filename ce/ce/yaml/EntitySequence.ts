// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { isMap, isScalar, isSeq } from 'yaml';
import { EntityFactory, Yaml, YAMLDictionary, YAMLSequence } from './yaml-types';

/**
 * EntitySequence is expressed as either a single entity or a sequence of entities.
 */

export /** @internal */ class EntitySequence<TElement extends Yaml<YAMLDictionary>> extends Yaml<YAMLSequence | YAMLDictionary> {
  protected constructor(protected factory: EntityFactory<YAMLDictionary, TElement>, node?: YAMLDictionary, parent?: Yaml, key?: string) {
    super(node, parent, key);
  }

  static override create(): YAMLDictionary {
    return new YAMLDictionary();
  }

  get length(): number {
    if (this.node) {
      if (isSeq(this.node)) {
        return this.node.items.length;
      }
      if (isMap(this.node)) {
        return 1;
      }
    }
    return 0;
  }

  add(value: TElement) {
    if (value === undefined || value === null) {
      throw new Error('Cannot add undefined or null to a sequence');
    }

    if (value.empty) {
      throw new Error('Cannot add an empty entity to a sequence');
    }

    if (!this.node) {
      // if we don't have a node at the moment, we need to create one.
      this.assert(true, value.node);
      return;
    }

    if (isMap(this.node)) {
      // this is currently a single item.
      // we need to convert it to a sequence
      const n = this.node;
      const seq = new YAMLSequence();
      seq.add(n);
      this.node = seq;

      // fall thru to the sequnce add
    }

    if (isSeq(this.node)) {
      this.node.add(value.node);
      return;
    }
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
      return yield new this.factory(this.node);
    }
    yield* EntitySequence.generator(this);
  }

  clear() {
    if (isSeq(this.node)) {
      // just make sure the collection is emptied first
      this.node.items.length = 0;
    }
    this.dispose(true);
  }

  protected static *generator<T extends Yaml<YAMLDictionary>>(sequence: EntitySequence<T>) {
    if (isSeq(sequence.node)) {
      for (const item of sequence.node.items) {
        yield new sequence.factory(item);
      }
    }
  }
}
